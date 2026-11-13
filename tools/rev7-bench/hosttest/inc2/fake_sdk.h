// host-test fakes of the pico-sdk pieces ota.c uses
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define __aligned(n) __attribute__((aligned(n)))
#define PICO_OK 0
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#define FAKE_FLASH_SIZE (16u * 1024u * 1024u)
#define PICO_FLASH_SIZE_BYTES FAKE_FLASH_SIZE
extern uint8_t fake_flash[];
#define XIP_BASE 0x10000000u
#define XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE ((uintptr_t)fake_flash)
typedef uint64_t absolute_time_t;
extern uint64_t fake_now_us;
static inline absolute_time_t get_absolute_time(void) { return fake_now_us; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
static inline absolute_time_t make_timeout_time_us(uint64_t us) { return fake_now_us + us; }
static inline bool time_reached(absolute_time_t t) { return fake_now_us >= t; }
static inline void sleep_ms(uint32_t ms) { fake_now_us += ms * 1000ull; }
void flash_range_erase(uint32_t off, size_t len);
void flash_range_program(uint32_t off, uint8_t const *data, size_t len);
int flash_safe_execute(void (*fn)(void *), void *param, uint32_t timeout_ms);
typedef struct { int8_t partition; uint8_t tbyb_and_update_info; uint8_t boot_type; } boot_info_t;
bool rom_get_boot_info(boot_info_t *info);
int rom_get_partition_table_info(uint32_t *out, uint32_t words, uint32_t flags);
int rom_explicit_buy(uint8_t *buffer, uint32_t size);
int rom_reboot(uint32_t flags, uint32_t delay_ms, uint32_t p0, uint32_t p1);
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t s) { (void)s; }
static inline void __dmb(void) {}
