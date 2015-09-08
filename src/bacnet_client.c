#include <stdio.h>

#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/apdu.h>
#include <libbacnet/handlers.h>
#include <libbacnet/dlenv.h>
#include <libbacnet/bip.h>
#include <libbacnet/datalink.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include "bacnet_namespace.h"

int main(int argc, char **argv) {
    bacnet_address_init();
    bacnet_Device_Init(NULL);
    bacnet_apdu_set_unconfirmed_handler(
                    SERVICE_UNCONFIRMED_I_AM, handler_i_am_bind);

#define BACNET_PORT 0xBAC0
    bacnet_BIP_Debug = true;
    bacnet_bip_set_port(htons(BACNET_PORT));
    bacnet_datalink_set("bip");
    bacnet_datalink_init("lo");
    atexit(bacnet_datalink_cleanup);

    bacnet_Send_I_Am(&bacnet_Handler_Transmit_Buffer[0]);

    return 0;
}
