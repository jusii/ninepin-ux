/**
 * ssd1306 display handling
 *
 * please see disp_ssd.c for a more comprehensive readme.
 */

#ifndef _DISPLAY_DISP_SSD_H
#define _DISPLAY_DISP_SSD_H

#include <stdbool.h>
#include <stdint.h>

void disp_ssd_init(void);

/**
 * @brief Borrow the display's i2c controller for a blocking transfer to another
 *        device on the same bus (the rev7 i/o expander).
 *
 * The display streams frames by dma and completes them in its i2c interrupt.
 * This returns false whilst a display transfer is in flight - try again on a
 * later pass, never spin on it. On true the controller is idle and the display
 * interrupt is masked; display updates queued meanwhile wait for the matching
 * disp_i2c_bus_release(). The controller stays configured as disp_ssd_init()
 * left it (I2C_PORT, pins, SSD_BAUD), whether or not a display answered, so the
 * borrower must not re-initialise it. Core0 main loop only.
 */
bool disp_i2c_bus_acquire(void);

/**
 * @brief Hand the i2c controller back and start any display transfer queued
 *        whilst it was borrowed.
 */
void disp_i2c_bus_release(void);

extern void (*disp_write)(uint8_t x, uint8_t y, char *message);

// where disp_write points whilst there is no display (or it is borrowed)
void _noop_disp_write(uint8_t x, uint8_t y, char *message);

// the panel's framebuffer: 128x64, one bit per pixel
#define DISP_FRAMEBUFFER_BYTES ((128 * 64) / 8)

/**
 * @return true if a display answered at init and disp_write reaches it
 */
bool disp_present(void);

/**
 * @brief Copy what the panel shows, to put back later with disp_restore().
 */
void disp_save(uint8_t copy[DISP_FRAMEBUFFER_BYTES]);

/**
 * @brief Show a copy taken with disp_save() again.
 */
void disp_restore(uint8_t const copy[DISP_FRAMEBUFFER_BYTES]);

/**
 * @brief Blank the panel.
 */
void disp_clear(void);

#endif // _DISPLAY_DISP_SSD_H
