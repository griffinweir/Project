#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <pthread.h>
#include <string.h>
#include <modbus.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"
#define BACNET_INSTANCE_NO    40
#define BACNET_PORT    0xBAC1
#define BACNET_INTERFACE    "lo"
#define BACNET_DATALINK_TYPE    "bvlc"
#define BACNET_SELECT_TIMEOUT_MS 1    /* ms */
#define RUN_AS_BBMD_CLIENT    1
#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT    0xBAC0
#define BACNET_BBMD_ADDRESS    "140.159.160.7"
#define BACNET_BBMD_TTL    90
#define SERVER_ADDR "140.159.153.159"
#define SERVER_PORT 502
#endif
#define listnum 2

typedef struct s_word_object word_object;
struct s_word_object {
char *word;
word_object *next;
};

static word_object *list_heads[NUM_LISTS];

static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

static word_object *list_get_first(word_object **list_head) { //grabs the current header object and assigns the next object in the structure and the new heade
word_object *first_object;
first_object = *list_head; // grab the current list header
*list_head = (*list_head)->next; //set the next element as the list header
return first_object; //return the list header obtained above
}

static void add_to_list(word_object **list_head, char *word) {
word_object *last_object, *tmp_object;
char *tmp_string=strdup(word);
tmp_object = malloc(sizeof(word_object)); //create a new tmp_object
tmp_object->word = tmp_string;
tmp_object->next = NULL;
pthread_mutex_lock(&list_lock);
if (*list_head == NULL) { //initialisation of the first object
*list_head =tmp_object; //assigns the head of the list pointer to the tetmp object
} else {
last_object = *list_head; //set Last object pointer to the start of the structure
while (last_object->next) { //while the current objects next does not point to NULL
last_object = last_object->next; //change the last object pointer to the next object
}
last_object->next = tmp_object; //set the next pointer to the temp object
last_object=last_object->next;
}
pthread_mutex_unlock(&list_lock);
pthread_cond_signal(&list_data_ready);
}

/*Initialise modbus structure*/
static void *actmodbus(void *arg)
{
modbus_t *ctx;
int rc;
uint16_t tab_reg[4];
int i;
modstart:
ctx = modbus_new_tcp(SERVER_ADDR, SERVER_PORT);
if (ctx == NULL) {
fprintf(stderr, "Unable to allocate lobmodbus context\n");
sleep(1);
goto modstart;

}
if (modbus_connect(ctx) == -1) {
fprintf(stderr, "connection failed: %s\n", modbus_strerror(errno));
sleep(1);
modbus_free(ctx);
goto modstart;

} else {
fprintf(stderr, "connection Successful\n");
}

/*Read registers*/
while (1) {
rc = modbus_read_registers(ctx, BACNET_INSTANCE_NO, NUM_INSTANCES,
tab_reg);
if (rc == -1) {
fprintf(stderr, "%s\n", modbus_strerror(errno));
//return -1;
}

	for (i = 0; i < rc; i++) {
printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
add_to_list(&list_heads[i], tab_reg[i]);
}
usleep(100000);
}


modbus_close(ctx);
modbus_free(ctx);


}





#if 1
/*This section is for testing procedure but still needs to be included*/

static uint16_t test_data[] = {
0xA4EC, 0x6E39, 0x8740, 0x1065, 0x9134, 0xFC8C
};

#define NUM_TEST_DATA (sizeof(test_data)/sizeof(test_data[0]))
#endif

static int Update_Analog_Input_Read_Property(
BACNET_READ_PROPERTY_DATA *rpdata) {
word_object *current_package; // variable
uint16_t stack[3]; //second variable
static int index;
int instance_no = bacnet_Analog_Input_Instance_To_Index(
rpdata->object_instance);
if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE) goto not_pv;
pthread_mutex_lock(&list_lock); // lock list 

if (list_heads[instance_no] != NULL) { current_object = list_get_first(&list_heads[instance_no]);

bacnet_Analog_Input_Present_Value_Set(instance_no,
current_object->number);
printf("-------------------- %i:04X\n", instance_no,
current_object->number);

free(current_object);
}
}

not_pv:
pthread_mutex_unlock(&list_lock);
return bacnet_Analog_Input_Read_Property(rpdata);
}



static bacnet_object_functions_t server_objects[] = {
    {bacnet_OBJECT_DEVICE,
	    NULL,
	    bacnet_Device_Count,
	    bacnet_Device_Index_To_Instance,
	    bacnet_Device_Valid_Object_Instance_Number,
	    bacnet_Device_Object_Name,
	    bacnet_Device_Read_Property_Local,
	    bacnet_Device_Write_Property_Local,
	    bacnet_Device_Property_Lists,
	    bacnet_DeviceGetRRInfo,
	    NULL, /* Iterator */
	    NULL, /* Value_Lists */
	    NULL, /* COV */
	    NULL, /* COV Clear */
	    NULL  /* Intrinsic Reporting */
    },
    {bacnet_OBJECT_ANALOG_INPUT,
            bacnet_Analog_Input_Init,
            bacnet_Analog_Input_Count,
            bacnet_Analog_Input_Index_To_Instance,
            bacnet_Analog_Input_Valid_Instance,
            bacnet_Analog_Input_Object_Name,
            Update_Analog_Input_Read_Property,
            bacnet_Analog_Input_Write_Property,
            bacnet_Analog_Input_Property_Lists,
            NULL /* ReadRangeInfo */ ,
            NULL /* Iterator */ ,
            bacnet_Analog_Input_Encode_Value_List,
            bacnet_Analog_Input_Change_Of_Value,
            bacnet_Analog_Input_Change_Of_Value_Clear,
            bacnet_Analog_Input_Intrinsic_Reporting},
    {MAX_BACNET_OBJECT_TYPE}
};

static void modbus(void *arg) {
#if RUN_AS_BBMD_CLIENT
    /* Thread safety: Shares data with datalink_send_pdu */
    bacnet_bvlc_register_with_bbmd(
	    bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS), 
	    htons(BACNET_BBMD_PORT),
	    BACNET_BBMD_TTL);
#endif
}

static void *minute_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Expire addresses once the TTL has expired */
	bacnet_address_cache_timer(60);

	/* Re-register with BBMD once BBMD TTL has expired */
	register_with_bbmd();

	/* Update addresses for notification class recipient list 
	 * Requred for INTRINSIC_REPORTING
	 * bacnet_Notification_Class_find_recipient(); */
	
	/* Sleep for 1 minute */
	pthread_mutex_unlock(&timer_lock);
	sleep(60);
    }
    return arg;
}

static void *second_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Invalidates stale BBMD foreign device table entries */
	bacnet_bvlc_maintenance_timer(1);

	/* Transaction state machine: Responsible for retransmissions and ack
	 * checking for confirmed services */
	bacnet_tsm_timer_milliseconds(1000);

	/* Re-enables communications after DCC_Time_Duration_Seconds
	 * Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
	 * bacnet_dcc_timer_seconds(1); */

	/* State machine for load control object
	 * Required for OBJECT_LOAD_CONTROL
	 * bacnet_Load_Control_State_Machine_Handler(); */

	/* Expires any COV subscribers that have finite lifetimes
	 * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
	 * bacnet_handler_cov_timer_seconds(1); */

	/* Monitor Trend Log uLogIntervals and fetch properties
	 * Required for OBJECT_TRENDLOG
	 * bacnet_trend_log_timer(1); */
	
	/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
	 * Required for INTRINSIC_REPORTING
	 * bacnet_Device_local_reporting(); */
	
	/* Sleep for 1 second */
	pthread_mutex_unlock(&timer_lock);
	sleep(1);
    }
    return arg;
}

static void ms_tick(void) {
    /* Updates change of value COV subscribers.
     * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
     * bacnet_handler_cov_task(); */
}

#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(		\
		    SERVICE_UNCONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    bacnet_handler_##handler)

int main(int argc, char **argv) {
uint8_t rx_buf[bacnet_MAX_MPDU];
uint16_t pdu_len;
BACNET_ADDRESS src;
pthread_t minute_tick_id, second_tick_id, modbus_new_thread_id;

    bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
    bacnet_address_init();

    /* Setup device objects */
    bacnet_Device_Init(server_objects);
    BN_UNC(WHO_IS, who_is);
    BN_CON(READ_PROPERTY, read_property);

    bacnet_BIP_Debug = true;
    bacnet_bip_set_port(htons(BACNET_PORT));
    bacnet_datalink_set(BACNET_DATALINK_TYPE);
    bacnet_datalink_init(BACNET_INTERFACE);
    atexit(bacnet_datalink_cleanup);
    memset(&src, 0, sizeof(src));

    register_with_bbmd();

    bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);

    pthread_create(&minute_tick_id, 0, minute_tick, NULL);
    pthread_create(&second_tick_id, 0, second_tick, NULL);
    pthread_create(&modbus_new_thread_id, NULL, modbus, NULL);
    
    /* Start another thread here to retrieve your allocated registers from the
     * modbus server. This thread should have the following structure (in a
     * separate function):
     *
     * Initialise:
     *	    Connect to the modbus server
     *
     * Loop:
     *	    Read the required number of registers from the modbus server
     *	    Store the register data into the tail of a linked list 
     */


    while (1) {
	pdu_len = bacnet_datalink_receive(
		    &src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);

	if (pdu_len) {
	    /* May call any registered handler.
	     * Thread safety: May block, however we still need to guarantee
	     * atomicity with the timers, so hold the lock anyway */
	    pthread_mutex_lock(&timer_lock);
	    bacnet_npdu_handler(&src, rx_buf, pdu_len);
	    pthread_mutex_unlock(&timer_lock);
	}

	ms_tick();
    }

    return 0;
}
