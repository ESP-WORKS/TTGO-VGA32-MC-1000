/* SPDX-License-Identifier: MIT
 * MC-1000 emulator ROM loader. The ROM image is supplied by the user on SD;
 * this module contains no copyrighted MC-1000 ROM data.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MC1000ROM {

static const size_t ROM_BYTES = 16384;

enum class Status {
  OK,
  INVALID_ARGUMENT,
  SD_MISSING,
  MC1000_DIR_MISSING,
  ROMS_DIR_MISSING,
  ROM_FILE_MISSING,
  ROM_FILE_SIZE,
  ROM_READ_FAILED,
  PATH_TOO_LONG
};

// Filesystem path is case-insensitive at each component, independent of FAT
// filename capitalization (including combinations like Mc-1000/rOmS/mC-1000.rOm).
// Does not create directories: boot requires an existing user-provided ROM.
// Never returns OK unless exactly ROM_BYTES bytes were read.
Status load(const char *sdMount, uint8_t *destination, size_t capacity,
            char *resolvedPath, size_t resolvedPathSize);
const char *statusText(Status status);

} // namespace MC1000ROM
