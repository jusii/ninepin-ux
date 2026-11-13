/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * firmware updates on rev7: a .uf2 uploaded over the wi-fi page goes into the
 * partition slot that is not running (partition_table_rev7.json), and the
 * board then reboots into it as a "flash update" boot. every rev7 image is
 * built try-before-you-buy (PICO_CRT0_IMAGE_TYPE_TBYB): the bootrom runs a new
 * image under a 16.7 s watchdog, and it only becomes the current image once it
 * calls explicit_buy - which ota_task() does after ten seconds of clean main
 * loop. an image that hangs or crashes before that (or any firmware that is
 * not this one, which never buys itself) is not bought, and the old image
 * boots again. a uf2 dropped on the BOOTSEL drive takes the same path.
 *
 * buying erases the first sector of the other slot, which makes the old image
 * unbootable. to keep "boot previous firmware" possible, the running firmware
 * saves the first sector of its own slot before it reboots into the new one;
 * going back puts it back.
 *
 * all flash work happens in the main loop. the upload arrives in network
 * callbacks and only goes into a ring buffer there.
 */

#include "config.h"
#include "ota.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "boot/bootrom_constants.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/flash.h"

#include "apply.h"
#ifdef ENABLE_BLUETOOTH_HID
#include "bt_hid.h"
#endif
#include "settings.h"
#include "util/debug_cons.h"
#include "util/output.h"

#define UF2_MAGIC_START0        0x0a324655u
#define UF2_MAGIC_START1        0x9e5d5157u
#define UF2_MAGIC_END           0x0ab16f30u
#define UF2_FLAG_FAMILY         0x00002000u
#define UF2_FAMILY_RP2350_ARM_S 0xe48bff59u
#define UF2_BLOCK               512u
#define UF2_PAYLOAD             256u

#define OTA_RING                32768u  // more than a tcp window (http_server.c)
#define OTA_MAX_BLOCKS          (4096u * 1024u / UF2_PAYLOAD)
#define OTA_CONFIRM_MS          30000u
#define OTA_INSTALL_DELAY_MS    1500u   // the page's answer goes out first
#define OTA_STALL_MS            30000u  // a dropped upload
#ifndef OTA_BUY_AFTER_MS                // a test build may never accept itself
#  define OTA_BUY_AFTER_MS      10000u
#endif
#define OTA_TASK_BUDGET_US      15000u

// the saved first sector of the previous firmware's slot, in the spare space
// above the slots (unpartitioned, so no image ever lands there): the sector,
// then a header in the next one
#define OTA_BACKUP_OFFSET       (8u * 1024u * 1024u)
#define OTA_BACKUP_MAGIC        0x4f544142u     // "BATO"
#define OTA_BACKUP_CHECK        (60u * 1024u)   // of the slot after the first sector

typedef struct {
    uint32_t magic;
    uint32_t slot;              // partition number whose first sector this is
    uint32_t sector_crc;        // of the saved first sector
    uint32_t rest_crc;          // of the slot's next 60K, as it was: still the same image?
    uint32_t crc;               // of the fields above
} ota_backup_t;

typedef enum {
    OTA_IDLE = 0,
    OTA_RECEIVING,
    OTA_CONFIRM,                // waiting for a BOOT press
    OTA_INSTALL,                // restarting into the new image shortly
    OTA_FAILED,
} ota_state_t;

static volatile ota_state_t state;
static char status_text[48] = "idle";

// upload ring: written in network callbacks, read in the main loop
static uint8_t ring[OTA_RING];
static volatile uint32_t ring_head, ring_tail;
static volatile bool upload_done;
static char const *volatile refusal;   // why ota_receive() said no, for ota_finish()
static volatile uint32_t last_data_ms;

// the slot being written
static uint32_t target_slot, target_start, target_size;
static uint32_t blocks_expected, blocks_seen;
static uint8_t block_seen[OTA_MAX_BLOCKS / 8];
static uint8_t sector_written[OTA_MAX_BLOCKS * UF2_PAYLOAD / FLASH_SECTOR_SIZE / 8];
// the sector being filled: written out whole, one erase and program at a time
static uint8_t __aligned(4) sector_buf[FLASH_SECTOR_SIZE];
static int32_t buf_sector = -1;
// the image's whole range is erased before the first sector is written, a 64K
// block per main loop pass, while the upload waits in the ring and the tcp
// window: an erase keeps interrupts off for tens of milliseconds a sector, and
// a burst arriving at the radio meanwhile is lost (and resent after a back-off
// that grows with every repeat). programming a pre-erased sector takes ~7 ms
static uint32_t erase_next, erase_end;     // slot offsets

// and even programming a sector (~7 ms with interrupts off) loses a burst now
// and then. so flash is only touched whilst nothing can be on its way: once
// half the ring is waiting, the window stays shut (ota_hold_window()) until
// the sender has filled it, the ring is written out, and the window reopens.
// one round trip per 16K, and no flash pause ever overlaps incoming data
#define OTA_QUIET_WAIT_MS       300u    // the sender sent less than a window: go anyway
static volatile bool flash_wanted;
static uint32_t wanted_since;

/**
 * the http server's view (http_server.c): nothing more can arrive for the
 * upload until its window is reopened; true when there is no upload
 */
__attribute__((weak)) bool http_server_upload_quiet(void)
{
    return true;
}

bool ota_hold_window(void)
{
    return flash_wanted;
}
static uint32_t taken_bytes, started_ms, next_report;
static uint32_t since_ms;       // OTA_CONFIRM, OTA_INSTALL: when it began
static uint32_t shown_seconds;

static int trial = -1;          // -1 not looked at yet, 0 no, 1 on trial
static bool buy_done;
static uint8_t __aligned(4) work[FLASH_SECTOR_SIZE];

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/**
 * an update waits for a BOOT press if the settings say so, and always when
 * the page takes requests from the network without its password
 */
static bool needs_boot(void)
{
    settings_t const *s = settings_get();

    return s->ota_needs_boot || (s->page_open && (s->wifi_mode == SETTINGS_WIFI_JOIN));
}

static uint32_t ring_used(void)
{
    return (ring_head - ring_tail) % OTA_RING;
}

//--------------------------------------------------------------------+
// slots
//--------------------------------------------------------------------+

static bool slot_location(uint32_t slot, uint32_t *start, uint32_t *size)
{
    uint32_t info[3];
    int words = rom_get_partition_table_info(info, 3,
        PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_SINGLE_PARTITION | (slot << 24));
    uint32_t first, last;

    if ((words < 2) || (info[0] != (PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_SINGLE_PARTITION)))
        return false;

    first = (info[1] & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    last = (info[1] & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    *start = first * FLASH_SECTOR_SIZE;
    *size = (last - first + 1u) * FLASH_SECTOR_SIZE;
    return true;
}

static int running_slot(void)
{
    boot_info_t boot;

    if (!rom_get_boot_info(&boot))
        return -1;
    return boot.partition;
}

//--------------------------------------------------------------------+
// flash, always through flash_safe_execute (core1 runs from flash)
//--------------------------------------------------------------------+

typedef struct {
    uint32_t offset;
    uint32_t erase_length;      // erased first (whole sectors), 0: none
    uint8_t const *data;        // then programmed, NULL: nothing
    size_t length;
} flash_job_t;

static void flash_job(void *context)
{
    flash_job_t const *job = context;

    if (job->erase_length)
        flash_range_erase(job->offset, job->erase_length);
    if (job->data)
        flash_range_program(job->offset, job->data, job->length);
}

static bool flash_run(uint32_t offset, uint32_t erase_length, uint8_t const *data, size_t length)
{
    flash_job_t job = { offset, erase_length, data, length };

    return flash_safe_execute(flash_job, &job, 500) == PICO_OK;
}

/**
 * NULL data: erase the sector at offset; else program
 */
static bool flash_do(uint32_t offset, uint8_t const *data, size_t length)
{
    return data ? flash_run(offset, 0, data, length) : flash_run(offset, FLASH_SECTOR_SIZE, NULL, 0);
}

static uint32_t crc32(uint8_t const *data, size_t length)
{
    uint32_t crc = UINT32_MAX;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return ~crc;
}

static uint8_t const *flash_read(uint32_t offset)
{
    // physical offsets: past the address translation that maps the running slot
    return (uint8_t const *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset);
}

/**
 * save the first sector of a slot (and how to recognise the rest of it)
 */
static bool backup_sector(uint32_t slot)
{
    static uint8_t __aligned(4) page[FLASH_PAGE_SIZE];
    uint32_t start, size;
    ota_backup_t header;

    if (!slot_location(slot, &start, &size))
        return false;

    memcpy(work, flash_read(start), FLASH_SECTOR_SIZE);
    header.magic = OTA_BACKUP_MAGIC;
    header.slot = slot;
    header.sector_crc = crc32(work, FLASH_SECTOR_SIZE);
    header.rest_crc = crc32(flash_read(start + FLASH_SECTOR_SIZE), OTA_BACKUP_CHECK);
    header.crc = crc32((uint8_t const *)&header, offsetof(ota_backup_t, crc));

    // the header goes last: a save cut short is no save
    if (!flash_do(OTA_BACKUP_OFFSET + FLASH_SECTOR_SIZE, NULL, 0) || !flash_do(OTA_BACKUP_OFFSET, NULL, 0))
        return false;
    if (!flash_do(OTA_BACKUP_OFFSET, work, FLASH_SECTOR_SIZE))
        return false;

    memset(page, 0xff, sizeof(page));
    memcpy(page, &header, sizeof(header));
    return flash_do(OTA_BACKUP_OFFSET + FLASH_SECTOR_SIZE, page, sizeof(page));
}

static bool backup_valid(uint32_t slot, uint32_t *start)
{
    ota_backup_t header;
    uint32_t size;

    memcpy(&header, flash_read(OTA_BACKUP_OFFSET + FLASH_SECTOR_SIZE), sizeof(header));
    if ((header.magic != OTA_BACKUP_MAGIC) || (header.slot != slot)
        || (header.crc != crc32((uint8_t const *)&header, offsetof(ota_backup_t, crc))))
        return false;
    if (header.sector_crc != crc32(flash_read(OTA_BACKUP_OFFSET), FLASH_SECTOR_SIZE))
        return false;
    if (!slot_location(slot, start, &size))
        return false;
    // the rest of that slot still holds the same image
    return header.rest_crc == crc32(flash_read(*start + FLASH_SECTOR_SIZE), OTA_BACKUP_CHECK);
}

//--------------------------------------------------------------------+
// the upload (network callbacks)
//--------------------------------------------------------------------+

size_t ota_space(void)
{
    if (state != OTA_RECEIVING)
        return OTA_RING - 1u;   // the next upload starts with an empty ring
    return OTA_RING - 1u - ring_used();
}

bool ota_receive(uint32_t offset, uint8_t const *data, size_t len)
{
    if (offset == 0) {
        // a refused upload's finish must leave the one in progress alone
        refusal = NULL;
        switch (state) {
            case OTA_RECEIVING:
                refusal = "busy: an earlier upload is still open; try again in 30 s";
                return false;
            case OTA_CONFIRM:
                refusal = "an update is already waiting for BOOT on the board";
                return false;
            case OTA_INSTALL:
                refusal = "the board is installing an update";
                return false;
            default:
                break;
        }
        ring_head = ring_tail = 0;
        upload_done = false;
        target_size = 0;        // the main loop sets the slot up afresh
        last_data_ms = now_ms();
        state = OTA_RECEIVING;
        snprintf(status_text, sizeof(status_text), "receiving");
    }

    if (refusal)
        return false;
    if ((state != OTA_RECEIVING) || upload_done) {
        refusal = status_text;      // the main loop found a fault in it
        return false;
    }
    if (len > OTA_RING - 1u - ring_used()) {
        refusal = "the board fell behind the upload";
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        ring[ring_head] = data[i];
        ring_head = (ring_head + 1u) % OTA_RING;
    }
    last_data_ms = now_ms();
    return true;
}

bool ota_finish(char *message, size_t size)
{
    if (refusal || (state != OTA_RECEIVING) || upload_done) {
        snprintf(message, size, "Not installed: %s.", refusal ? refusal : status_text);
        refusal = NULL;
        return false;
    }
    upload_done = true;
    snprintf(message, size, needs_boot()
        ? "Received. The board checks it now; when its screen says so, press BOOT within 30 s to install."
        : "Received. The board checks it now, then installs it and restarts; its screen says how it went.");
    return true;
}

//--------------------------------------------------------------------+
// main loop
//--------------------------------------------------------------------+

static void oled(char const *text)
{
    dbgcons_ota(text);
}

/**
 * bluetooth stops searching for new devices whilst an upload comes in: its
 * scanning holds the shared radio most of the time, and wi-fi loses bursts
 * meanwhile (an upload that takes 7 s with it paused stalls for minutes)
 */
static void radio_for_upload(bool upload)
{
#ifdef ENABLE_BLUETOOTH_HID
    bt_hid_pause_scanning(BT_HID_PAUSE_UPLOAD, upload);
#else
    (void)upload;
#endif
}

static void fail(char const *why)
{
    flash_wanted = false;
    radio_for_upload(false);
    state = OTA_FAILED;
    snprintf(status_text, sizeof(status_text), "failed: %s", why);
    ahprintf("[ota] update %s\n", status_text);
    oled("update failed");
    since_ms = now_ms();
}

static void start_receiving(void)
{
    int running = running_slot();

    target_slot = (running == 0) ? 1u : 0u;
    if ((running < 0) || !slot_location(target_slot, &target_start, &target_size)) {
        target_size = 0;
        fail("no partition table");
        return;
    }
    blocks_expected = blocks_seen = 0;
    memset(block_seen, 0, sizeof(block_seen));
    memset(sector_written, 0, sizeof(sector_written));
    buf_sector = -1;
    erase_next = erase_end = 0;
    flash_wanted = false;
    taken_bytes = 0;
    next_report = 128u * 1024u;
    started_ms = now_ms();
    ahprintf("[ota] receiving an update into slot %c\n", target_slot ? 'B' : 'A');
    radio_for_upload(true);
    oled("update: receiving");
}

/**
 * write the buffered sector out
 */
static bool sector_flush(void)
{
    uint32_t sector = (uint32_t)buf_sector;
    uint32_t at;

    bool erased;

    if (buf_sector < 0)
        return true;
    buf_sector = -1;
    at = target_start + sector * FLASH_SECTOR_SIZE;
    // erased ahead and not written since: program only
    erased = (sector * FLASH_SECTOR_SIZE < erase_next) && !(sector_written[sector / 8] & (1u << (sector % 8)));
    if (!flash_run(at, erased ? 0 : FLASH_SECTOR_SIZE, sector_buf, FLASH_SECTOR_SIZE)
        || (memcmp(flash_read(at), sector_buf, FLASH_SECTOR_SIZE) != 0)) {
        fail("flash write");
        return false;
    }
    sector_written[sector / 8] |= (uint8_t)(1u << (sector % 8));
    return true;
}

/**
 * make a sector the buffered one: what it holds so far (blocks may come in
 * any order), or blank
 */
static bool sector_load(uint32_t sector)
{
    if (buf_sector == (int32_t)sector)
        return true;
    if (!sector_flush())
        return false;
    if (sector_written[sector / 8] & (1u << (sector % 8)))
        memcpy(sector_buf, flash_read(target_start + sector * FLASH_SECTOR_SIZE), FLASH_SECTOR_SIZE);
    else
        memset(sector_buf, 0xff, FLASH_SECTOR_SIZE);
    buf_sector = (int32_t)sector;
    return true;
}

static void take_block(uint8_t const *b)
{
    uint32_t w[8], end_magic, offset, sector;

    memcpy(w, b, sizeof(w));
    memcpy(&end_magic, b + UF2_BLOCK - 4, sizeof(end_magic));
    // w: magic0, magic1, flags, target address, payload size, block no, blocks, family
    if ((w[0] != UF2_MAGIC_START0) || (w[1] != UF2_MAGIC_START1) || (end_magic != UF2_MAGIC_END)) {
        fail("not a uf2 file");
        return;
    }

    // other families (the RP2350-E10 absolute block, risc-v) are not ours
    if (!(w[2] & UF2_FLAG_FAMILY) || (w[7] != UF2_FAMILY_RP2350_ARM_S))
        return;

    if (!blocks_expected) {
        if (!w[6] || (w[6] > OTA_MAX_BLOCKS)) {
            fail("image too large");
            return;
        }
        blocks_expected = w[6];
        erase_next = 0;
        erase_end = (blocks_expected * UF2_PAYLOAD + FLASH_SECTOR_SIZE - 1u) & ~(FLASH_SECTOR_SIZE - 1u);
        if (erase_end > target_size)
            erase_end = target_size;
    }

    offset = w[3] - XIP_BASE;
    if ((w[4] != UF2_PAYLOAD) || (w[3] < XIP_BASE) || (offset % UF2_PAYLOAD)
        || (offset + UF2_PAYLOAD > target_size) || (w[6] != blocks_expected) || (w[5] >= blocks_expected)) {
        fail("image does not fit the slot");
        return;
    }

    sector = offset / FLASH_SECTOR_SIZE;
    if (!sector_load(sector))
        return;
    memcpy(sector_buf + (offset % FLASH_SECTOR_SIZE), b + 32, UF2_PAYLOAD);

    if (!(block_seen[w[5] / 8] & (1u << (w[5] % 8)))) {
        block_seen[w[5] / 8] |= (uint8_t)(1u << (w[5] % 8));
        blocks_seen++;
    }
}

static void invalidate_target(void)
{
    // the first sector erased: a half-written or unconfirmed image never boots
    buf_sector = -1;
    if (target_size)
        flash_do(target_start, NULL, 0);
}

static void install(void)
{
    int running = running_slot();

    snprintf(status_text, sizeof(status_text), "restarting into slot %c", target_slot ? 'B' : 'A');
    ahprintf("[ota] saving this slot's first sector, then %s\n", status_text);
    oled("update: restarting");

    if ((running >= 0) && !backup_sector((uint32_t)running))
        ahprintf("[ota] could not save it: no way back after this update\n");

    sleep_ms(50);   // the console line out
    rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 100,
        XIP_BASE + target_start, 0);
    fail("restart refused");
}

static bool on_trial(void)
{
    if (trial < 0) {
        boot_info_t boot;

        trial = (rom_get_boot_info(&boot) && (boot.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING))
            ? 1 : 0;
    }
    return trial && !buy_done;
}

bool ota_trial(void)
{
    return on_trial();
}

static bool sector_blank(uint32_t offset)
{
    uint32_t const *words = (uint32_t const *)flash_read(offset);

    for (unsigned i = 0; i < FLASH_SECTOR_SIZE / 4u; i++)
        if (words[i] != UINT32_MAX)
            return false;
    return true;
}

static void buy_if_on_trial(void)
{
    int running, rc;
    uint32_t other = 0, start = 0, size;
    bool have_other;

    if (!on_trial() || (now_ms() < OTA_BUY_AFTER_MS))
        return;
    buy_done = true;

    // a firmware dropped on the BOOTSEL drive comes here with no save of the
    // one it replaces: make it now, so "boot previous" works for that too
    running = running_slot();
    have_other = (running >= 0) && slot_location(other = running ? 0u : 1u, &start, &size);
    if (have_other && !sector_blank(start) && !backup_valid(other, &start))
        backup_sector(other);

    // clears the try flag in flash and erases the other slot's first sector;
    // the sdk wrapper runs it through flash_safe_execute (core1 parked)
    rc = rom_explicit_buy(work, sizeof(work));
    if (rc) {
        ahprintf("[ota] could not accept this firmware (%d): the previous one boots next time\n", rc);
    } else {
        ahprintf("[ota] this firmware is accepted: %u s of clean running\n", OTA_BUY_AFTER_MS / 1000u);
        // the old image must not win a later normal boot on an equal version
        if (have_other && !sector_blank(start)) {
            ahprintf("[ota] retiring the previous firmware in slot %c\n", other ? 'B' : 'A');
            flash_do(start, NULL, 0);
        }
    }

    // the bootrom's trial watchdog is still counting: the settings take it over
    apply_watchdog();
}

/**
 * the next piece of the image's range erased: up to the next 64K boundary of
 * the flash, where the bootrom erases a whole block at once
 *
 * @return false if it failed
 */
static bool erase_step(void)
{
    uint32_t at = target_start + erase_next;
    uint32_t length = 0x10000u - (at & 0xffffu);

    if (length > erase_end - erase_next)
        length = erase_end - erase_next;
    if (!flash_run(at, length, NULL, 0)) {
        fail("flash erase");
        return false;
    }
    erase_next += length;
    return true;
}

static uint32_t ring_peek_word(uint32_t at)
{
    uint32_t word = 0;

    for (unsigned i = 0; i < 4; i++)
        word |= (uint32_t)ring[(ring_tail + at + i) % OTA_RING] << (8 * i);
    return word;
}

/**
 * @return true if taking the next block in the ring writes flash: the range
 *         still to erase, or a block for another sector than the buffered one
 */
static bool next_needs_flash(void)
{
    uint32_t address;

    if (erase_next < erase_end)
        return true;
    if (buf_sector < 0)
        return false;
    if ((ring_peek_word(0) != UF2_MAGIC_START0) || !(ring_peek_word(8) & UF2_FLAG_FAMILY)
        || (ring_peek_word(28) != UF2_FAMILY_RP2350_ARM_S))
        return false;   // take_block() turns it down or passes it over
    address = ring_peek_word(12);
    return (address < XIP_BASE) || ((address - XIP_BASE) / FLASH_SECTOR_SIZE != (uint32_t)buf_sector);
}

/**
 * @return true if flash may be written now
 */
static bool flash_allowed(void)
{
    if (!flash_wanted) {
        if (!upload_done && (ring_used() < OTA_RING / 2))
            return false;
        flash_wanted = true;
        wanted_since = now_ms();
    }
    return upload_done || http_server_upload_quiet()
        || ((uint32_t)(now_ms() - wanted_since) >= OTA_QUIET_WAIT_MS);
}

static void receive_task(void)
{
    absolute_time_t until = make_timeout_time_us(OTA_TASK_BUDGET_US);

    if (!target_size)
        start_receiving();

    while ((state == OTA_RECEIVING) && (ring_used() >= UF2_BLOCK) && !time_reached(until)) {
        uint8_t block[UF2_BLOCK];

        if (next_needs_flash() && !flash_allowed())
            break;

        // the rest waits in the ring until the range is erased
        if (erase_next < erase_end) {
            erase_step();
            break;
        }

        for (unsigned i = 0; i < UF2_BLOCK; i++) {
            block[i] = ring[ring_tail];
            ring_tail = (ring_tail + 1u) % OTA_RING;
        }
        take_block(block);
        taken_bytes += UF2_BLOCK;
        if (taken_bytes >= next_report) {
            uint32_t ms = now_ms() - started_ms;

            next_report += 128u * 1024u;
            ahprintf("[ota] %lu KB in, %lu KB/s\n", (unsigned long)(taken_bytes / 1024u),
                (unsigned long)(ms ? taken_bytes / ms : 0));
        }
    }
    if (state != OTA_RECEIVING) {
        invalidate_target();
        return;
    }
    // written out: the window reopens
    if (flash_wanted && (ring_used() < UF2_BLOCK))
        flash_wanted = false;

    if (!upload_done) {
        // signed: a chunk may arrive (and stamp a newer time) between the
        // clock read and the compare, and that is no stall
        if ((int32_t)(now_ms() - last_data_ms) >= (int32_t)OTA_STALL_MS) {
            invalidate_target();
            fail("the upload stopped");
        }
        return;
    }
    if (ring_used() >= UF2_BLOCK)
        return;
    if (erase_next < erase_end) {
        if (erase_step() && (erase_next >= erase_end))
            sector_flush();
        return;
    }
    if (!sector_flush())
        return;

    if (!blocks_seen) {
        fail("no rp2350 firmware in that file");
    } else if (blocks_seen != blocks_expected) {
        invalidate_target();
        fail("the image is incomplete");
    } else if (needs_boot()) {
        radio_for_upload(false);
        state = OTA_CONFIRM;
        since_ms = now_ms();
        shown_seconds = 0;
        snprintf(status_text, sizeof(status_text), "waiting for BOOT");
        ahprintf("[ota] %lu KB checked; press BOOT within %u s to install\n",
            (unsigned long)(blocks_seen * UF2_PAYLOAD / 1024u), OTA_CONFIRM_MS / 1000u);
    } else {
        radio_for_upload(false);
        state = OTA_INSTALL;
        since_ms = now_ms();
        snprintf(status_text, sizeof(status_text), "installing");
        ahprintf("[ota] %lu KB checked; installing\n", (unsigned long)(blocks_seen * UF2_PAYLOAD / 1024u));
        oled("update: installing");
    }
}

void ota_task(void)
{
    uint32_t elapsed;

    buy_if_on_trial();

    switch (state) {
        case OTA_RECEIVING:
            receive_task();
            break;

        case OTA_CONFIRM:
            elapsed = now_ms() - since_ms;
            if (elapsed >= OTA_CONFIRM_MS) {
                invalidate_target();
                fail("BOOT was not pressed");
            } else if (shown_seconds != (OTA_CONFIRM_MS - elapsed + 999u) / 1000u) {
                char line[32];

                shown_seconds = (OTA_CONFIRM_MS - elapsed + 999u) / 1000u;
                snprintf(line, sizeof(line), "BOOT: install (%lus)", (unsigned long)shown_seconds);
                oled(line);
            }
            break;

        case OTA_INSTALL:
            if ((uint32_t)(now_ms() - since_ms) >= OTA_INSTALL_DELAY_MS)
                install();
            break;

        case OTA_FAILED:
            // the screen line goes after a while; the status stays for the console
            if (since_ms && (uint32_t)(now_ms() - since_ms) >= 10000u) {
                since_ms = 0;
                oled(NULL);
            }
            break;

        default:
            break;
    }
}

bool ota_button(void)
{
    if (state != OTA_CONFIRM)
        return false;
    install();
    return true;
}

char const *ota_status(void)
{
    return status_text;
}

bool ota_previous_available(void)
{
    int running = running_slot();
    uint32_t start;

    return (running >= 0) && backup_valid(running ? 0u : 1u, &start);
}

bool ota_boot_previous(char *message, size_t size)
{
    int running = running_slot();
    uint32_t other, start;

    if (running < 0) {
        snprintf(message, size, "no partition table");
        return false;
    }
    if ((state == OTA_RECEIVING) || (state == OTA_CONFIRM) || (state == OTA_INSTALL)) {
        snprintf(message, size, "an update is in progress");
        return false;
    }
    other = running ? 0u : 1u;
    if (!backup_valid(other, &start)) {
        snprintf(message, size, "no previous firmware");
        return false;
    }

    // its first sector back, and ours saved, so the way forward stays open too
    memcpy(work, flash_read(OTA_BACKUP_OFFSET), FLASH_SECTOR_SIZE);
    if (!flash_do(start, NULL, 0) || !flash_do(start, work, FLASH_SECTOR_SIZE)
        || (memcmp(flash_read(start), work, FLASH_SECTOR_SIZE) != 0)) {
        snprintf(message, size, "restoring it failed");
        return false;
    }
    backup_sector((uint32_t)running);

    target_slot = other;
    snprintf(message, size, "restarting into slot %c", other ? 'B' : 'A');
    ahprintf("[ota] previous firmware: %s\n", message);
    oled("restarting");
    sleep_ms(50);
    rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 100, XIP_BASE + start, 0);
    snprintf(message, size, "restart refused");
    return false;
}

