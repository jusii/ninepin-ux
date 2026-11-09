// host-test fake of the bits of lwip raw tcp that http_server.c uses
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int8_t err_t;
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_VAL -6
#define ERR_ABRT -13
#define TCP_SND_QUEUELEN 32
#define TCP_WRITE_FLAG_COPY 1
#define TCP_PRIO_MIN 1
#define IPADDR_TYPE_ANY 46
typedef struct { uint32_t addr; } ip_addr_t;
extern ip_addr_t ip_addr_any_fake;
#define IP_ANY_TYPE (&ip_addr_any_fake)
struct pbuf { struct pbuf *next; void *payload; u16_t tot_len; u16_t len; };
struct tcp_pcb;
typedef err_t (*tcp_recv_fn)(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
typedef err_t (*tcp_sent_fn)(void *arg, struct tcp_pcb *tpcb, u16_t len);
typedef err_t (*tcp_poll_fn)(void *arg, struct tcp_pcb *tpcb);
typedef void (*tcp_err_fn)(void *arg, err_t err);
typedef err_t (*tcp_accept_fn)(void *arg, struct tcp_pcb *newpcb, err_t err);
struct tcp_pcb {
    ip_addr_t remote_ip;
    void *arg; tcp_recv_fn recv; tcp_sent_fn sent; tcp_poll_fn poll; tcp_err_fn errf; tcp_accept_fn accept;
    char out[1 << 16]; size_t out_len; int closed, aborted; size_t recved; u16_t sndbuf; int queued; u32_t rcv_ann_wnd;
};
struct tcp_pcb *tcp_new_ip_type(u8_t type);
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *ip, u16_t port);
struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb, u8_t backlog);
void tcp_accept(struct tcp_pcb *pcb, tcp_accept_fn fn);
void tcp_arg(struct tcp_pcb *pcb, void *arg);
void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn fn);
void tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn fn);
void tcp_err(struct tcp_pcb *pcb, tcp_err_fn fn);
void tcp_poll(struct tcp_pcb *pcb, tcp_poll_fn fn, u8_t interval);
void tcp_setprio(struct tcp_pcb *pcb, u8_t prio);
err_t tcp_write(struct tcp_pcb *pcb, const void *dataptr, u16_t len, u8_t apiflags);
err_t tcp_output(struct tcp_pcb *pcb);
err_t tcp_close(struct tcp_pcb *pcb);
void tcp_abort(struct tcp_pcb *pcb);
void tcp_recved(struct tcp_pcb *pcb, u16_t len);
u8_t pbuf_free(struct pbuf *p);
char *ipaddr_ntoa(const ip_addr_t *addr);
#define tcp_sndbuf(pcb) ((pcb)->sndbuf)
#define tcp_sndqueuelen(pcb) ((pcb)->queued)
