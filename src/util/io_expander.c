/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * tca9554 i2c i/o expander: rotary encoder, encoder switch and usb-c vbus sense.
 */

#include "config.h"
#include "util/io_expander.h"

#ifdef HAS_IO_EXPANDER

#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "display/disp_ssd.h"
#include "util/output.h"

// tca9554 registers
#define TCA9554_REG_INPUT       0x00
#define TCA9554_REG_OUTPUT      0x01
#define TCA9554_REG_POLARITY    0x02
#define TCA9554_REG_CONFIG      0x03    // 1 = input

// P0-P4 in (encoder a/b/switch, spare, vbus sense), P5-P7 out and low
#define IOX_INPUT_MASK  ((1u << IO_EXPANDER_P_ENC_A) | (1u << IO_EXPANDER_P_ENC_B) | \
                         (1u << IO_EXPANDER_P_ENC_SW) | (1u << IO_EXPANDER_P_ENC_SP) | \
                         (1u << IO_EXPANDER_P_VBUS))

// one i2c transfer is a few bytes at 400 kHz, well under 100us; anything longer
// is a wedged bus, and the display driver has its own recovery for that
#define IOX_I2C_TIMEOUT_US      2000

// switch contacts settle within a few ms; vbus gets longer for a cable going in
// at an angle
#define IOX_SW_DEBOUNCE_MS      10
#define IOX_VBUS_DEBOUNCE_MS    50

// re-read this often even without an interrupt: covers an edge lost whilst
// the bus was busy, and a vbus change racing the first read after init
#define IOX_SAFETY_POLL_MS      100

// quarter steps per detent; an ec11 with a full quadrature cycle per detent
// (the common kind) is 4, half-cycle variants are 2
#ifndef IO_EXPANDER_ENC_STEPS_PER_DETENT
#  define IO_EXPANDER_ENC_STEPS_PER_DETENT 4
#endif

#define IOX_EVENT_QUEUE_DEPTH   16

// how long init waits for the display to finish with the bus
#define IOX_INIT_BUS_WAIT_MS    250

typedef struct
{
    bool stable;            // debounced level
    bool candidate;         // most recent raw level
    uint32_t since;         // when candidate was first seen
    uint32_t debounce_ms;
} iox_debounce_t;

static bool iox_present = false;
static volatile bool iox_irq_pending = false;

static uint8_t enc_state;   // (a << 1) | b
static int8_t enc_accum;

static iox_debounce_t sw_db = { .debounce_ms = IOX_SW_DEBOUNCE_MS };
static iox_debounce_t vbus_db = { .debounce_ms = IOX_VBUS_DEBOUNCE_MS };

static uint32_t last_read_ms;

static io_expander_event_t event_queue[IOX_EVENT_QUEUE_DEPTH];
static uint8_t event_head, event_tail;

/**
 * gray-code transition table, indexed by (previous state << 2) | new state.
 * +1 / -1 for a valid quarter step, 0 for no change or an impossible double
 * step (a missed state, or contact bounce flipping both lines at once).
 */
static const int8_t enc_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

static void iox_push_event(io_expander_event_t event)
{
    uint8_t next = (uint8_t)((event_head + 1) % IOX_EVENT_QUEUE_DEPTH);

    // a full queue means nobody is listening; drop the newest
    if (next == event_tail)
        return;

    event_queue[event_head] = event;
    event_head = next;
}

/**
 * /INT went low. gpio raw handlers see every bank interrupt, so check it is ours.
 */
static void iox_gpio_irq(void)
{
    if (gpio_get_irq_event_mask(IO_EXPANDER_INT) & GPIO_IRQ_EDGE_FALL) {
        gpio_acknowledge_irq(IO_EXPANDER_INT, GPIO_IRQ_EDGE_FALL);
        iox_irq_pending = true;
    }
}

static bool iox_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };

    return i2c_write_timeout_us(I2C_PORT, IO_EXPANDER_ADDR, buf, sizeof(buf), false,
        IOX_I2C_TIMEOUT_US) == (int)sizeof(buf);
}

static bool iox_read_reg(uint8_t reg, uint8_t *value)
{
    if (i2c_write_timeout_us(I2C_PORT, IO_EXPANDER_ADDR, &reg, 1, true, IOX_I2C_TIMEOUT_US) != 1)
        return false;

    return i2c_read_timeout_us(I2C_PORT, IO_EXPANDER_ADDR, value, 1, false, IOX_I2C_TIMEOUT_US) == 1;
}

/**
 * read the input register (which also releases /INT).
 *
 * @return false if the bus is busy with a display frame or the read failed
 */
static bool iox_read_inputs(uint8_t *inputs)
{
    bool ok;

    if (!disp_i2c_bus_acquire())
        return false;

    ok = iox_read_reg(TCA9554_REG_INPUT, inputs);
    disp_i2c_bus_release();

    return ok;
}

static void iox_debounce_seed(iox_debounce_t *db, bool level, uint32_t now)
{
    db->stable = level;
    db->candidate = level;
    db->since = now;
}

/**
 * @return true if the debounced level changed
 */
static bool iox_debounce(iox_debounce_t *db, bool raw, uint32_t now)
{
    if (raw != db->candidate) {
        db->candidate = raw;
        db->since = now;
    }

    if ((db->candidate != db->stable) && ((uint32_t)(now - db->since) >= db->debounce_ms)) {
        db->stable = db->candidate;
        return true;
    }

    return false;
}

static inline bool iox_debounce_pending(iox_debounce_t const *db)
{
    return db->candidate != db->stable;
}

static void iox_decode_encoder(uint8_t inputs)
{
    uint8_t a = (inputs >> IO_EXPANDER_P_ENC_A) & 1u;
    uint8_t b = (inputs >> IO_EXPANDER_P_ENC_B) & 1u;
    uint8_t state = (uint8_t)((a << 1) | b);

    enc_accum += enc_table[(enc_state << 2) | state];
    enc_state = state;

    if (enc_accum >= IO_EXPANDER_ENC_STEPS_PER_DETENT) {
        enc_accum -= IO_EXPANDER_ENC_STEPS_PER_DETENT;
        iox_push_event(IO_EXPANDER_EVENT_ENC_CW);
    } else if (enc_accum <= -IO_EXPANDER_ENC_STEPS_PER_DETENT) {
        enc_accum += IO_EXPANDER_ENC_STEPS_PER_DETENT;
        iox_push_event(IO_EXPANDER_EVENT_ENC_CCW);
    }

    // back at the rest position (both contacts open, both lines high) any
    // partial count is bounce or a missed state; start the next detent clean
    if (state == 3)
        enc_accum = 0;
}

static void iox_process(uint8_t inputs, uint32_t now)
{
    // the switch pulls its line to ground when pressed
    bool sw_pressed = !((inputs >> IO_EXPANDER_P_ENC_SW) & 1u);
    bool vbus = (inputs >> IO_EXPANDER_P_VBUS) & 1u;

    iox_decode_encoder(inputs);

    if (iox_debounce(&sw_db, sw_pressed, now))
        iox_push_event(sw_db.stable ? IO_EXPANDER_EVENT_ENC_PRESS : IO_EXPANDER_EVENT_ENC_RELEASE);

    if (iox_debounce(&vbus_db, vbus, now))
        iox_push_event(vbus_db.stable ? IO_EXPANDER_EVENT_VBUS_ON : IO_EXPANDER_EVENT_VBUS_OFF);
}

bool io_expander_init(void)
{
    uint8_t inputs = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // /INT is open drain with no board pull-up. a change between the first
    // read below and arming the edge interrupt is not lost: the task also
    // services the expander whenever it finds /INT held low
    gpio_init(IO_EXPANDER_INT);
    gpio_set_dir(IO_EXPANDER_INT, GPIO_IN);
    gpio_pull_up(IO_EXPANDER_INT);

    // disp_ssd_init() has just queued its init sequence and first frame, which
    // drain by interrupt; give them time (a full frame is ~25 ms at 400 kHz)
    absolute_time_t give_up = make_timeout_time_ms(IOX_INIT_BUS_WAIT_MS);
    while (!disp_i2c_bus_acquire()) {
        if (time_reached(give_up)) {
            ahprintf("[iox] i2c still busy at init, expander disabled\n");
            return false;
        }
        sleep_us(500);
    }

    // outputs low before the direction register makes P5-P7 outputs, so they
    // never glitch high; polarity left at its non-inverting default
    iox_present = iox_write_reg(TCA9554_REG_OUTPUT, 0x00)
        && iox_write_reg(TCA9554_REG_POLARITY, 0x00)
        && iox_write_reg(TCA9554_REG_CONFIG, IOX_INPUT_MASK)
        && iox_read_reg(TCA9554_REG_INPUT, &inputs);

    disp_i2c_bus_release();

    if (!iox_present) {
        ahprintf("[iox] no tca9554 at 0x%02x\n", IO_EXPANDER_ADDR);
        return false;
    }

    enc_state = (uint8_t)((((inputs >> IO_EXPANDER_P_ENC_A) & 1u) << 1) | ((inputs >> IO_EXPANDER_P_ENC_B) & 1u));
    enc_accum = 0;
    iox_debounce_seed(&sw_db, !((inputs >> IO_EXPANDER_P_ENC_SW) & 1u), now);
    iox_debounce_seed(&vbus_db, (inputs >> IO_EXPANDER_P_VBUS) & 1u, now);
    last_read_ms = now;

    gpio_add_raw_irq_handler(IO_EXPANDER_INT, iox_gpio_irq);
    gpio_set_irq_enabled(IO_EXPANDER_INT, GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);

    ahprintf("[iox] tca9554 up, inputs %02x, usb-c vbus %s\n", inputs, vbus_db.stable ? "present" : "absent");
    return true;
}

void io_expander_task(void)
{
    uint32_t now;
    uint8_t inputs;
    bool due;

    if (!iox_present)
        return;

    now = to_ms_since_boot(get_absolute_time());

    // /INT held low means an unread change, whether or not we saw the edge
    due = iox_irq_pending || !gpio_get(IO_EXPANDER_INT)
        || ((uint32_t)(now - last_read_ms) >= IOX_SAFETY_POLL_MS);

    // a debounce in progress needs a second look once its window has passed,
    // even if the line has been quiet since
    if (iox_debounce_pending(&sw_db) && ((uint32_t)(now - sw_db.since) >= sw_db.debounce_ms))
        due = true;
    if (iox_debounce_pending(&vbus_db) && ((uint32_t)(now - vbus_db.since) >= vbus_db.debounce_ms))
        due = true;

    if (!due)
        return;

    // clear before the read: an edge during the read re-arms it
    iox_irq_pending = false;

    if (!iox_read_inputs(&inputs)) {
        // display frame in flight (or a failed transfer): try again next pass
        iox_irq_pending = true;
        return;
    }

    last_read_ms = now;
    iox_process(inputs, now);
}

io_expander_event_t io_expander_poll_event(void)
{
    io_expander_event_t event;

    if (event_tail == event_head)
        return IO_EXPANDER_EVENT_NONE;

    event = event_queue[event_tail];
    event_tail = (uint8_t)((event_tail + 1) % IOX_EVENT_QUEUE_DEPTH);
    return event;
}

bool io_expander_present(void)
{
    return iox_present;
}

bool io_expander_vbus_present(void)
{
    return iox_present && vbus_db.stable;
}

#endif // HAS_IO_EXPANDER
