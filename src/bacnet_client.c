#include <stdio.h>

#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/bactext.h>
#include "bacnet_namespace.h"

#define BACNET_PORT		    0xBAC0
#define BACNET_INTERFACE	    "lo"
#define BACNET_DATALINK_TYPE	    "bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1	    /* ms */

#define RUN_AS_BBMD_CLIENT	    0

#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT	    0xBAC0
#define BACNET_BBMD_ADDRESS	    "127.0.0.1"
#define BACNET_BBMD_TTL		    90
#endif

#define MAX_OBJECT_INSTANCES	    4

static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

struct server_details {
    int			found;
    uint32_t		device_id;
    BACNET_OBJECT_TYPE	object_type;
    uint32_t		object_instances;
    BACNET_PROPERTY_ID	object_property;
    uint32_t		array_index;
    BACNET_ADDRESS	bacnet_address;
    int			request_invoke_id[MAX_OBJECT_INSTANCES];
};

static struct server_details servers[] = {
    {
	.found = 0,
	.device_id = 120,
	.object_type = bacnet_OBJECT_ANALOG_INPUT,
	.object_instances = 1,
	.object_property = bacnet_PROP_PRESENT_VALUE,
	.array_index = BACNET_ARRAY_ALL
    }, {
	.found = 0,
	.device_id = 121,
	.object_type = bacnet_OBJECT_ANALOG_INPUT,
	.object_instances = 3,
	.object_property = bacnet_PROP_PRESENT_VALUE,
	.array_index = BACNET_ARRAY_ALL
    }
};
#define NUM_SERVERS (sizeof(servers)/sizeof(struct server_details))

static bacnet_object_functions_t client_objects[] = {
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
    {MAX_BACNET_OBJECT_TYPE}
};

static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
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

static void ping_servers(void) {
    int i;
    for (i = 0; i < NUM_SERVERS; i++) {
	if (!servers[i].found)
	    bacnet_Send_WhoIs(servers[i].device_id, servers[i].device_id);
    }
}

static void *second_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Keep searching for server */
	ping_servers();

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

static void ack_servers(void) {
    int i;
    unsigned max_apdu;

    for (i = 0; i < NUM_SERVERS; i++) {

	if (!servers[i].found)
	    servers[i].found =
		bacnet_address_bind_request(
		    servers[i].device_id,
		    &max_apdu,
		    &servers[i].bacnet_address);
    }

}

static void ms_tick(void) {
    /* Updates change of value COV subscribers.
     * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
     * bacnet_handler_cov_task(); */

    ack_servers();
}

#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(		\
		    SERVICE_UNCONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON_ACK(service, handler) \
    bacnet_apdu_set_confirmed_ack_handler(		\
		    SERVICE_CONFIRMED_##service,	\
		    handler)
#define BN_ERR(service, handler) \
    bacnet_apdu_set_error_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    handler)

static int match_server(BACNET_ADDRESS *src,
			uint8_t invoke_id,
			uint32_t *object_instance) {
    int i, j;
    for (i = 0; i < NUM_SERVERS; i++) {
	if (bacnet_address_match(&servers[i].bacnet_address, src)) {
	    for (j = 0; j < MAX_OBJECT_INSTANCES; j++) {
		if (servers[i].request_invoke_id[j] == invoke_id) {
		    *object_instance = j;
		    return i;
		}
	    }
	}
    }
    return -1;
}

static void abort_handler(
		BACNET_ADDRESS *src,
		uint8_t invoke_id,
		uint8_t abort_reason,
		bool server) {

    int index;
    uint32_t object_instance;
    
    if ((index = match_server(src, invoke_id, &object_instance)) < 0) return;

    fprintf(stderr, "BACnet Abort from server %i: %s\n",
	    servers[index].device_id,
	    bactext_abort_reason_name(abort_reason));
    servers[index].found = 0;
}

static void reject_handler(
		BACNET_ADDRESS *src,
		uint8_t invoke_id,
		uint8_t reject_reason) {

    int index;
    uint32_t object_instance;
    
    if ((index = match_server(src, invoke_id, &object_instance)) < 0) return;

    fprintf(stderr, "BACnet Reject from server %i: %s\n",
	    servers[index].device_id,
	    bactext_reject_reason_name(reject_reason));
    servers[index].found = 0;
}

static void read_property_err(
		BACNET_ADDRESS *src,
		uint8_t invoke_id,
		BACNET_ERROR_CLASS error_class,
		BACNET_ERROR_CODE error_code) {

    int index;
    uint32_t object_instance;
    
    if ((index = match_server(src, invoke_id, &object_instance)) < 0) return;

    fprintf(stderr, "BACnet Error from server %i: %s: %s\n",
	    servers[index].device_id,
	    bactext_error_class_name(error_class),
	    bactext_error_code_name(error_code));
    servers[index].found = 0;
}

static void read_property_ack(
		uint8_t *service_request,
		uint16_t service_len,
		BACNET_ADDRESS *src,
		BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data) {
    int len, index;
    BACNET_READ_PROPERTY_DATA data;
    uint32_t object_instance;
    
    if ((index = match_server(src,
		    service_data->invoke_id, &object_instance)) < 0) return;

    len = bacnet_rp_ack_decode_service_request(
			service_request, service_len, &data);
    if (len < 0) {
	fprintf(stderr, "Read Property ACK service request decode failed\n");
    } else {
	fprintf(stderr, "Data received from server %i, instance %i: ", 
	    servers[index].device_id, object_instance);
	bacnet_rp_ack_print_data(&data);
    }
}

static void send_rp_request(struct server_details *server, 
			    uint32_t object_instance) {
    if (!server->found) return;

    if (!server->request_invoke_id[object_instance])
	server->request_invoke_id[object_instance] = 
		bacnet_Send_Read_Property_Request(
				    server->device_id,
				    server->object_type,
				    object_instance,
				    server->object_property,
				    server->array_index);

    else if (bacnet_tsm_invoke_id_free(
			    server->request_invoke_id[object_instance])) {

	/* Transaction is finished */
	server->request_invoke_id[object_instance] = 0;

    } else if (bacnet_tsm_invoke_id_failed(
			    server->request_invoke_id[object_instance])) {

	fprintf(stderr, "Error: TSM Timeout for device %i\n",
			server->device_id);

	bacnet_tsm_free_invoke_id(server->request_invoke_id[object_instance]);
	server->request_invoke_id[object_instance] = 0;
	server->found = 0;
    }
}

void *read_prop_thread(void *arg) {
    int i, j;
    while (1) {

	usleep(100000);

	pthread_mutex_lock(&timer_lock);

	for (i = 0; i < NUM_SERVERS; i++)
	    for (j = 0; j < servers[i].object_instances; j++)
		send_rp_request(&servers[i], j);
	    
	pthread_mutex_unlock(&timer_lock);
    }

    return arg;
}

int main(int argc, char **argv) {
    uint8_t rx_buf[bacnet_MAX_MPDU];
    uint16_t pdu_len;
    BACNET_ADDRESS src;
    pthread_t read_prop_thread_id, minute_tick_id, second_tick_id;

    bacnet_Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    bacnet_address_init();

    /* Setup device objects */
    bacnet_Device_Init(client_objects);
    BN_UNC(I_AM, i_am_bind);
    BN_CON_ACK(READ_PROPERTY, read_property_ack);
    BN_ERR(READ_PROPERTY, read_property_err);
    bacnet_apdu_set_abort_handler(abort_handler);
    bacnet_apdu_set_reject_handler(reject_handler);

    bacnet_BIP_Debug = true;
    bacnet_bip_set_port(htons(BACNET_PORT));
    bacnet_datalink_set(BACNET_DATALINK_TYPE);
    bacnet_datalink_init(BACNET_INTERFACE);
    atexit(bacnet_datalink_cleanup);
    memset(&src, 0, sizeof(src));

    register_with_bbmd();

    pthread_create(&read_prop_thread_id, 0, read_prop_thread, NULL);
    pthread_create(&minute_tick_id, 0, minute_tick, NULL);
    pthread_create(&second_tick_id, 0, second_tick, NULL);
    
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
