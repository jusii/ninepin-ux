/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * who owns which part of the flash. the firmware slots are in the partition
 * table (partition_table_rev7.json): A and B, 8K up to 8180K of the 16M.
 * everything above them is the data area, outside the partition table and
 * handed out here from the top down, a 4K sector at a time:
 *
 *   end -  4K   the RP2350-E10 workaround block every uf2 carries
 *   end - 12K   btstack's pairing bank (two sectors; the pico-sdk places it)
 *   end - 20K   settings (settings.c; two sectors, written in turn)
 *   end - 28K   pad profiles (profiles.c; two sectors, written in turn)
 *   below       free, down to the end of slot B
 *
 * on the rp2040 there is no E10 block: everything sits one sector higher.
 */

#ifndef _FLASH_MAP_H
#define _FLASH_MAP_H

#include "hardware/flash.h"

#if PICO_RP2040
#  define FLASH_MAP_TOP_SECTORS 2u      // the pairing bank
#  define FLASH_MAP_READ_BASE   XIP_BASE
#else
#  define FLASH_MAP_TOP_SECTORS 3u      // the E10 block and the pairing bank
#  define FLASH_MAP_READ_BASE   XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE
#endif

#define FLASH_MAP_SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - (FLASH_MAP_TOP_SECTORS + 2u) * FLASH_SECTOR_SIZE)
#define FLASH_MAP_PROFILES_OFFSET (PICO_FLASH_SIZE_BYTES - (FLASH_MAP_TOP_SECTORS + 4u) * FLASH_SECTOR_SIZE)

#endif // _FLASH_MAP_H
