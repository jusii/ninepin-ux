/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * network keyboard and mouse: a subset of the PiKVM kvmd HTTP API
 * (https://docs.pikvm.org/api/) under /api/, so PiKVM / GL.iNet KVM clients
 * and their MCP servers can drive the machine behind this board. input joins
 * the other sources (input bridge slot INPUT_BRIDGE_NET_SLOT) and follows the
 * mode. off unless the "Network control" setting is on, never in access point
 * mode.
 */

#ifndef _KVMD_API_H
#define _KVMD_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char const *method, *path, *query;      // query may be NULL
    char const *body;                       // nul-terminated
    size_t body_len;
    bool basic_ok;                          // http basic auth matched the page password
    char const *(*header)(void *ctx, char const *name, size_t *length);
    void *ctx;
    uint32_t remote_ip;                     // ipv4, network order
    uint32_t wait_id;                       // ticket for a deferred answer
} kvmd_request_t;

typedef struct {
    int status;
    char *json;                             // the server's buffer
    size_t size, len;
    char extra_head[128];                   // extra response header lines
    bool deferred;                          // answered later, http_server_api_complete()
} kvmd_response_t;

/**
 * @brief Handle one /api/ request. Network context, lwip lock held: validates
 *        and queues, never touches the outputs.
 */
void kvmd_api_handle(kvmd_request_t const *req, kvmd_response_t *resp);

/**
 * @brief Feed queued input to the outputs at their pace; release held input
 *        after 60 s without a request. Main loop.
 */
void kvmd_api_task(void);

/**
 * @brief Write a kvmd error object into out.
 *
 * @return its length
 */
size_t kvmd_api_error(char *out, size_t size, char const *error, char const *message);

/**
 * @brief Answer a deferred request (http_server.c). Main loop.
 */
void http_server_api_complete(uint32_t wait_id, int status, char const *json);

#endif // _KVMD_API_H
