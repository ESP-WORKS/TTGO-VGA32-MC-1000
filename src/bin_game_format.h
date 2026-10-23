/* SPDX-License-Identifier: MIT
 * MC-1000 TLOAD images (headerless memory dump from $0100), plus the
 * 18-byte legacy tape envelope used by early MC1000CasTools .BIN exports.
 * Only identifies the range; the caller must stage the whole file before
 * overwriting guest memory.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MC1000BIN {
struct Image { size_t offset; size_t length; };
static constexpr size_t kGameRAMStart = 0x0100;
static constexpr size_t kCodeOffset = 0x0100; // $0200 - $0100
static constexpr size_t kMaxLength = 0xC000 - kGameRAMStart;
inline bool validPayload(const uint8_t *p, size_t size) {
  // $0104 PLAY, $0105 PLAYMX, $0106 HEAD in the game header.
  // Valid games use HEAD=0 and start execution at $0200.
  return p && size > kCodeOffset && size <= kMaxLength &&
         p[4] >= 1 && p[5] >= p[4] && p[6] == 0 &&
         p[kCodeOffset] != 0xff; // erased space is not a plausible entry
}
inline bool identify(const uint8_t *file, size_t length, Image *image) {
  if (!file || !image) return false;
  // Older tools called a cassette record BIN. Its 18-byte envelope contains
  // a 14-byte tape header and two little-endian address words. In the sample
  // game the address range uses an INCLUSIVE end (4000h..490Fh). Some exports
  // use the usual exclusive end. Either must equal the exact body length.
  if (length > 18) {
    const uint16_t start = (uint16_t)file[14] | ((uint16_t)file[15] << 8);
    const uint16_t end = (uint16_t)file[16] | ((uint16_t)file[17] << 8);
    const size_t body = length - 18;
    if (end >= start && ((size_t)(end-start) == body ||
                         (size_t)(end-start) + 1 == body) &&
        validPayload(file + 18, body)) {
      image->offset=18; image->length=body; return true;
    }
  }
  if (validPayload(file, length)) {
    image->offset=0; image->length=length; return true;
  }
  return false;
}
} // namespace MC1000BIN
