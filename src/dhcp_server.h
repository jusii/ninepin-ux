/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * a minimal dhcp server (rfc 2131) for the wi-fi access point: eight leases
 * from the netif's own /24, the board as the router, no dns. lwip raw udp;
 * call with the lwip lock held (cyw43_arch_lwip_begin / end).
 */

#ifndef _DHCP_SERVER_H
#define _DHCP_SERVER_H

#include <stdbool.h>

struct netif;

/**
 * @brief Answer dhcp requests on netif (its address must be set).
 *
 * @return false if lwip had no udp pcb to spare
 */
bool dhcp_server_start(struct netif *netif);

void dhcp_server_stop(void);

/**
 * @return leases handed out and not released or expired
 */
unsigned dhcp_server_leases(void);

#endif // _DHCP_SERVER_H
