/* SPDX-License-Identifier: MIT
 * Verify a newly written CAS against the exact bytes of the guest memory
 * BEFORE renaming the temporary over a user's existing tape image.
 * Respect the optional CRTC bank at $2000-$27FF and MC6847 VRAM at
 * $8000-$9FFF according to the effective memory mapping.
 * Portable file I/O lets the same routine run in isolated host tests.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cas_format.h"

namespace MC1000CAS {
inline uint8_t savedMemoryByte(uint16_t address,
                               const uint8_t *ram, const uint8_t *vram,
                               const uint8_t *crtc,
                               bool videoBankRam, bool crtcBank) {
  if (crtcBank && address >= 0x2000 && address < 0x2800)
    return crtc[address - 0x2000];
  if (!videoBankRam && address >= 0x8000 && address < 0xa000)
    return vram[address - 0x8000];
  return ram[address];
}
inline bool verifySavedCassette(FILE *f, const uint8_t *expectedHeader,
                                uint16_t start, uint16_t end,
                                const uint8_t *ram, const uint8_t *vram,
                                const uint8_t *crtc,
                                bool videoBankRam, bool crtcBank) {
  if (!f || !expectedHeader || !ram || !vram || !crtc || end <= start ||
      start < 0x0200 || end > 0xC000) return false;
  const size_t expected = HEADER_SIZE + (size_t)(end - start);
  if (fseek(f, 0, SEEK_END) != 0 || ftell(f) != (long)expected ||
      fseek(f, 0, SEEK_SET) != 0) return false;
  uint8_t hdr[HEADER_SIZE];
  if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
      memcmp(hdr, expectedHeader, sizeof(hdr)) != 0) return false;
  uint8_t block[256];
  for (uint32_t pos = start; pos < end;) {
    size_t chunk = end - pos;
    if (chunk > sizeof(block)) chunk = sizeof(block);
    if (fread(block, 1, chunk, f) != chunk) return false;
    for (size_t j = 0; j < chunk; ++j) {
      const uint32_t a = pos + j;
      const uint8_t byte = savedMemoryByte((uint16_t)a, ram, vram, crtc,
                                            videoBankRam, crtcBank);
      if (block[j] != byte) return false;
    }
    pos += chunk;
  }
  return true;
}
} // namespace MC1000CAS
