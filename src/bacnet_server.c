#include <stdio.h>

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

#define BACNET_INSTANCE_NO	    120
#define BACNET_PORT		    0xBAC0
#define BACNET_INTERFACE	    "lo"
#define BACNET_DATALINK_TYPE	    "bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1	    /* ms */

#define BACNET_BBMD_PORT	    0xBAC0
#define BACNET_BBMD_ADDRESS	    "127.0.0.1"
#define BACNET_BBMD_TTL		    90

#define RUN_AS_BBMD_CLIENT	    0

#define INC_TIMER(reset, func)	\
    do {			\
	if (!--timer) {		\
	    timer = reset;	\
	    func();		\
	}			\
    } while (0)

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
            bacnet_Analog_Input_Read_Property,
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

static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
    bacnet_bvlc_register_with_bbmd(
	    bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS), 
	    htons(BACNET_BBMD_PORT),
	    BACNET_BBMD_TTL);
#endif
}

static void minute_tick(void) {
    /* Expire addresses once the TTL has expired */
    bacnet_address_cache_timer(60);

    /* Re-register with BBMD once BBMD TTL has expired */
    register_with_bbmd();

    /* Update addresses for notification class recipient list 
     * Requred for INTRINSIC_REPORTING
     * bacnet_Notification_Class_find_recipient(); */
}

#define S_PER_MIN 60
static void second_tick(void) {
    static int timer = S_PER_MIN;

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

    INC_TIMER(S_PER_MIN, minute_tick);
}

#define MS_PER_SECOND 1000
static void ms_tick(void) {
    static int timer = MS_PER_SECOND;

    /* Updates change of value COV subscribers.
     * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
     * bacnet_handler_cov_task(); */
	
    INC_TIMER(MS_PER_SECOND, second_tick);
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

    bacnet_Send_I_Am(&bacnet_Handler_Transmit_Buffer[0]);

    bacnet_Analog_Input_Present_Value_Set(0, 1.26);

    while (1) {
	pdu_len = bacnet_datalink_receive(
		    &src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);

	if (pdu_len) bacnet_npdu_handler(&src, rx_buf, pdu_len);

	ms_tick();
    }

    return 0;
}
