/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * lwip options for the rev7 wi-fi (wifi.c): no os, the raw api only, called
 * from pico_cyw43_arch_lwip_threadsafe_background's async context. laid out
 * after the pico-examples lwipopts_examples_common.h conventions, trimmed to
 * what the settings page needs: ipv4, dhcp client (join a network), one small
 * http server, the mdns responder, and a dhcp server of our own on the access
 * point (dhcp_server.c, plain udp).
 */

#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// no os: everything runs in the cyw43 background context
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

// lwip's own heap; malloc would be called from an interrupt (the threadsafe
// background context forbids MEM_LIBC_MALLOC)
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
// the page goes out with TCP_WRITE_FLAG_COPY, a send buffer at a time
#define MEM_SIZE                    24000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
// receive buffers for the radio driver: a full tcp window plus out-of-order
// segments after a loss, with room to spare (when they run out, the driver
// drops every frame, retransmissions too)
#define PBUF_POOL_SIZE              32
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_UDP_PCB            6
// lwip's own, plus room for the mdns responder's probe / announce timers
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 6)

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    0

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_LISTEN_BACKLOG          1
#define LWIP_TCP_KEEPALIVE          1

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3

// dhcp client for "join network"; no address conflict probe (it holds the
// address back for seconds and is of little use on a home network)
#define LWIP_DHCP                   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

// mdns: amigahid-xxxx.local in join mode
#define LWIP_IGMP                   1
#define LWIP_MDNS_RESPONDER         1
#define LWIP_NUM_NETIF_CLIENT_DATA  1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1
#define MDNS_MAX_SERVICES           1

// no statistics, no debug output
#define LWIP_STATS                  0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
#define LWIP_DEBUG                  0

#endif // _LWIPOPTS_H
