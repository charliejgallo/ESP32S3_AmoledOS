/* D6: the USB network interface (CDC-NCM). Started and stopped by device
 * mode in aos_usb.c; nothing else calls this. docs/USB.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool aos_usb_net_start(void);
void aos_usb_net_stop(void);
bool aos_usb_net_up(void);
void aos_usb_net_relink(bool up);          /* the bus came back (true) or went away (false) */
void aos_usb_net_mac(uint8_t mac[6]);      /* the address the descriptor and the netif share */
