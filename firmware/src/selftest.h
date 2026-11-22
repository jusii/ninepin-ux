/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * the board's test of itself: a first check of a new board, and one to run
 * again when something seems wrong. asked for with a confirmation every time
 * (console: selftest go; the page: a tick box), because it drives the ports.
 *
 * with nothing attached it checks what is on the board. two cables let it
 * check the ports against each other:
 *   - a straight de9 cable from port 1 to port 2;
 *   - a 6-pin mini-din cable from KBD OUT to KBD IN, which also feeds the
 *     amiga side its 5 V (from KBD IN's supply), without which the de9 lines
 *     do not move.
 * a loop that is not there is "not tested", never a pass.
 *
 * the automatic part takes some seconds and asks nothing. then BOOT pressed
 * within 10 s passes the button and starts the checks that need a hand or an
 * eye (the encoder, the led's colours, the display), 10 s each, BOOT for yes.
 *
 * not covered: the a500 headers' joints, 5 V in on de9 pin 7, and whatever
 * only a real machine shows.
 */

#ifndef _SELFTEST_H
#define _SELFTEST_H

#include <stdbool.h>

#include "util/io_expander.h"
#include "util/user_button.h"

/**
 * @brief Ask for a run. Any context; it starts on the main loop's next pass.
 *
 * @return false if one is running
 */
bool selftest_start(void);

/**
 * @return true from selftest_start() until the report is complete
 */
bool selftest_running(void);

/**
 * @return the report of the run in progress or of the last one, "" before any
 */
char const *selftest_report(void);

/**
 * @brief Main loop.
 */
void selftest_task(void);

/**
 * @brief The BOOT button and the encoder whilst a run is on.
 *
 * @return true if the event was the self-test's (nothing else acts on it)
 */
bool selftest_button(user_button_event_t event);
bool selftest_encoder(io_expander_event_t event);

#endif // _SELFTEST_H
