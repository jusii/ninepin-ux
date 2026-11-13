/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * firmware from the internet: the latest release on github, found and
 * installed when asked for, never by itself. wi-fi join mode only.
 *
 * the release is trusted by our own signature (ecdsa p-256 over the sha-256
 * of ninepin-ux.uf2, in ninepin-ux.uf2.sig beside it), checked against the
 * public keys in update_keys.h before the update side is told the upload is
 * complete. tls is transport only; no certificate is verified.
 *
 * the image goes through ota_receive() like an upload from the page, with
 * the same rules after it (a BOOT press if the settings ask, the trial run).
 */

#ifndef _NET_UPDATE_H
#define _NET_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Ask github which release is the latest. Any context; the answer
 *        shows in net_update_status() and net_update_latest().
 *
 * @return false if something is already going on, or there is no network
 */
bool net_update_check(void);

/**
 * @brief Download the release the last check found, check its signature and
 *        hand it to the update side. Any context.
 *
 * @return false if there is no such release, or something is already going on
 */
bool net_update_install(void);

/**
 * @return true whilst a check or a download runs
 */
bool net_update_busy(void);

/**
 * @return true whilst a check runs (a matter of seconds; the page waits for it)
 */
bool net_update_checking(void);

/**
 * @return one line on what is going on or how the last request went
 */
char const *net_update_status(void);

/**
 * @return the latest release's number ("0.9.1") as the last check found it,
 *         "" before any check
 */
char const *net_update_latest(void);

/**
 * @return how the latest release stands to this firmware: 1 newer, 0 the
 *         same, -1 older or this is no release ("dev"); 0 before any check
 */
int net_update_newer(void);

/**
 * @return true if nothing more can arrive for a download until its window is
 *         reopened (as http_server_upload_quiet(); true when there is none)
 */
bool net_update_quiet(void);

/**
 * @brief Main loop: start what was asked for, give the tcp window back as the
 *        update side takes the image, time a stuck request out.
 */
void net_update_task(void);

#endif // _NET_UPDATE_H
