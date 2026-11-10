/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * a minimal dhcp server for the access point; see dhcp_server.h. written for
 * this tree from rfc 2131 / rfc 2132; the shape (one udp pcb on port 67, a
 * small lease table keyed by mac, every reply broadcast) is the usual one for
 * an embedded access point, as in micropython's shared/netutils/dhcpserver.c,
 * but no code is taken from it.
 *
 * DISCOVER gets an OFFER, REQUEST for the address on offer (or held) an ACK,
 * any other REQUEST a NAK so the client starts over, RELEASE frees the lease.
 */

#include "dhcp_server.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "util/output.h"

#define DHCP_SERVER_PORT        67
#define DHCP_CLIENT_PORT        68

#define DHCP_LEASES             8
#define DHCP_FIRST_HOST         16          // .16 .. .23
#define DHCP_LEASE_S            (2u * 60u * 60u)

#define DHCP_OP_REQUEST         1
#define DHCP_OP_REPLY           2
#define DHCP_HTYPE_ETHERNET     1

#define DHCP_DISCOVER           1
#define DHCP_OFFER              2
#define DHCP_REQUEST            3
#define DHCP_DECLINE            4
#define DHCP_ACK                5
#define DHCP_NAK                6
#define DHCP_RELEASE            7

#define OPT_PAD                 0
#define OPT_SUBNET_MASK         1
#define OPT_ROUTER              3
#define OPT_REQUESTED_IP        50
#define OPT_LEASE_TIME          51
#define OPT_MESSAGE_TYPE        53
#define OPT_SERVER_ID           54
#define OPT_END                 255

// the fixed bootp part, then the magic cookie and the options
typedef struct __attribute__((packed)) {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint8_t ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint8_t cookie[4];
    uint8_t options[312];
} dhcp_msg_t;

#define DHCP_FIXED_LEN      (offsetof(dhcp_msg_t, options))
#define DHCP_MIN_REPLY_LEN  300     // the bootp minimum some clients insist on

static uint8_t const magic_cookie[4] = { 99, 130, 83, 99 };

typedef struct {
    uint8_t mac[6];
    bool used;
    uint32_t expires_ms;
} dhcp_lease_t;

static struct udp_pcb *server_pcb;
static struct netif *server_netif;
static dhcp_lease_t leases[DHCP_LEASES];
static dhcp_msg_t message;      // in and out; lwip callbacks do not nest

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static bool lease_live(dhcp_lease_t const *lease)
{
    return lease->used && ((int32_t)(lease->expires_ms - now_ms()) > 0);
}

static uint8_t const *find_option(uint8_t const *options, size_t length, uint8_t code, uint8_t *option_length)
{
    size_t i = 0;

    while (i < length) {
        uint8_t type = options[i];

        if (type == OPT_END)
            break;
        if (type == OPT_PAD) {
            i++;
            continue;
        }
        if (i + 1 >= length || i + 2 + options[i + 1] > length)
            break;
        if (type == code) {
            *option_length = options[i + 1];
            return &options[i + 2];
        }
        i += 2u + options[i + 1];
    }
    return NULL;
}

/**
 * @return the lease for mac: the one it holds, else a free or expired one
 *         (-1: the table is full of live leases)
 */
static int lease_for(uint8_t const *mac)
{
    int spare = -1;

    for (int i = 0; i < DHCP_LEASES; i++) {
        if (leases[i].used && memcmp(leases[i].mac, mac, 6) == 0)
            return i;
        if ((spare < 0) && !lease_live(&leases[i]))
            spare = i;
    }
    return spare;
}

static void lease_address(int index, uint8_t out[4])
{
    uint32_t base = ip4_addr_get_u32(netif_ip4_addr(server_netif)) & ip4_addr_get_u32(netif_ip4_netmask(server_netif));
    ip4_addr_t address;

    ip4_addr_set_u32(&address, base | PP_HTONL((uint32_t)(DHCP_FIRST_HOST + index)));
    memcpy(out, &address.addr, 4);
}

static uint8_t *put_option(uint8_t *at, uint8_t code, uint8_t length, void const *value)
{
    *at++ = code;
    *at++ = length;
    memcpy(at, value, length);
    return at + length;
}

static void reply(uint8_t type, uint8_t const yiaddr[4])
{
    uint8_t *opt = message.options;
    uint32_t server = ip4_addr_get_u32(netif_ip4_addr(server_netif));
    uint32_t mask = ip4_addr_get_u32(netif_ip4_netmask(server_netif));
    uint32_t lease_s = PP_HTONL(DHCP_LEASE_S);
    size_t length;
    struct pbuf *p;

    // xid, flags, giaddr and chaddr stay as the client sent them
    message.op = DHCP_OP_REPLY;
    message.hops = 0;
    message.secs = 0;
    memset(message.ciaddr, 0, 4);
    if (yiaddr)
        memcpy(message.yiaddr, yiaddr, 4);
    else
        memset(message.yiaddr, 0, 4);
    memcpy(message.siaddr, &server, 4);
    memset(message.sname, 0, sizeof(message.sname));
    memset(message.file, 0, sizeof(message.file));
    memcpy(message.cookie, magic_cookie, 4);
    memset(message.options, 0, sizeof(message.options));

    opt = put_option(opt, OPT_MESSAGE_TYPE, 1, &type);
    opt = put_option(opt, OPT_SERVER_ID, 4, &server);
    if (type != DHCP_NAK) {
        opt = put_option(opt, OPT_LEASE_TIME, 4, &lease_s);
        opt = put_option(opt, OPT_SUBNET_MASK, 4, &mask);
        opt = put_option(opt, OPT_ROUTER, 4, &server);
    }
    *opt++ = OPT_END;

    length = (size_t)(opt - (uint8_t *)&message);
    if (length < DHCP_MIN_REPLY_LEN)
        length = DHCP_MIN_REPLY_LEN;

    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)length, PBUF_RAM);
    if (!p)
        return;
    pbuf_take(p, &message, (u16_t)length);
    // the client has no address yet: always broadcast
    udp_sendto_if(server_pcb, p, IP_ADDR_BROADCAST, DHCP_CLIENT_PORT, server_netif);
    pbuf_free(p);
}

static void on_receive(void *arg, struct udp_pcb *pcb, struct pbuf *p, ip_addr_t const *addr, u16_t port)
{
    size_t length, options_length;
    uint8_t const *option;
    uint8_t option_length = 0;
    uint8_t type;
    uint8_t wanted[4];
    int index;

    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;

    length = pbuf_copy_partial(p, &message, sizeof(message), 0);
    pbuf_free(p);

    if ((length < DHCP_FIXED_LEN + 4) || (message.op != DHCP_OP_REQUEST) || (message.htype != DHCP_HTYPE_ETHERNET)
        || (message.hlen != 6) || memcmp(message.cookie, magic_cookie, 4) != 0 || !server_netif)
        return;

    options_length = length - DHCP_FIXED_LEN;
    option = find_option(message.options, options_length, OPT_MESSAGE_TYPE, &option_length);
    if (!option || option_length != 1)
        return;
    type = option[0];

    index = lease_for(message.chaddr);

    switch (type) {
        case DHCP_DISCOVER:
            if (index < 0)
                return;     // full: stay quiet, the client tries again later
            lease_address(index, wanted);
            reply(DHCP_OFFER, wanted);
            break;

        case DHCP_REQUEST: {
            uint32_t server = ip4_addr_get_u32(netif_ip4_addr(server_netif));
            uint8_t lease_ip[4];

            // a request naming another server is that server's business
            option = find_option(message.options, options_length, OPT_SERVER_ID, &option_length);
            if (option && ((option_length != 4) || memcmp(option, &server, 4) != 0))
                return;

            option = find_option(message.options, options_length, OPT_REQUESTED_IP, &option_length);
            if (option && option_length == 4)
                memcpy(wanted, option, 4);
            else
                memcpy(wanted, message.ciaddr, 4);

            if (index < 0) {
                reply(DHCP_NAK, NULL);
                return;
            }
            lease_address(index, lease_ip);
            if (memcmp(wanted, lease_ip, 4) != 0) {
                reply(DHCP_NAK, NULL);
                return;
            }
            memcpy(leases[index].mac, message.chaddr, 6);
            leases[index].used = true;
            leases[index].expires_ms = now_ms() + DHCP_LEASE_S * 1000u;
            reply(DHCP_ACK, lease_ip);
            ahprintf("[dhcp] %u.%u.%u.%u to %02x:%02x:%02x:%02x:%02x:%02x\n", lease_ip[0], lease_ip[1], lease_ip[2],
                lease_ip[3], message.chaddr[0], message.chaddr[1], message.chaddr[2], message.chaddr[3],
                message.chaddr[4], message.chaddr[5]);
            break;
        }

        case DHCP_RELEASE:
            if ((index >= 0) && leases[index].used && memcmp(leases[index].mac, message.chaddr, 6) == 0)
                leases[index].used = false;
            break;

        default:
            // DECLINE and INFORM: nothing to do for a handful of clients
            break;
    }
}

bool dhcp_server_start(struct netif *netif)
{
    dhcp_server_stop();

    server_pcb = udp_new();
    if (!server_pcb)
        return false;

    if (udp_bind(server_pcb, IP_ANY_TYPE, DHCP_SERVER_PORT) != ERR_OK) {
        udp_remove(server_pcb);
        server_pcb = NULL;
        return false;
    }
    udp_bind_netif(server_pcb, netif);
    udp_recv(server_pcb, on_receive, NULL);
    server_netif = netif;
    memset(leases, 0, sizeof(leases));
    return true;
}

void dhcp_server_stop(void)
{
    if (server_pcb) {
        udp_remove(server_pcb);
        server_pcb = NULL;
    }
    server_netif = NULL;
}

unsigned dhcp_server_leases(void)
{
    unsigned count = 0;

    for (int i = 0; i < DHCP_LEASES; i++)
        if (server_pcb && lease_live(&leases[i]))
            count++;
    return count;
}
