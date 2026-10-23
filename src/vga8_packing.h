/* SPDX-License-Identifier: MIT
 * Packing helpers for the fixed 320x240 VGA8 viewport.
 * The MC6847 is 256 pixels wide; 32-pixel border at each side.
 * No FabGL or ESP32 dependency so byte order can be regression-tested.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace MC1000VGA8 {
static constexpr int kWidth = 320;
static constexpr int kBorderX = 32;
static constexpr int kActiveWidth = 256;
static constexpr int kRowBytes = kWidth * 3 / 8;

inline uint32_t packedSolid(uint8_t color) {
  return (uint32_t(color & 7) * 0x00249249u);
}
inline void storeTriple(uint8_t *dst, uint32_t packed) {
  dst[0] = (uint8_t)packed;
  dst[1] = (uint8_t)(packed >> 8);
  dst[2] = (uint8_t)(packed >> 16);
}
inline void fillBorderRow(uint8_t *dst, uint8_t color) {
  if (!dst) return;
  const uint32_t bits = packedSolid(color);
  for (int i=0; i<kRowBytes; i+=3) storeTriple(dst+i, bits);
}
inline void packRow(uint8_t *dst, const uint8_t *src, uint8_t border) {
  if (!dst || !src) return;
  const uint32_t b = packedSolid(border);
  // Four eight-pixel groups on each side, since the 256-pixel image is 1:1.
  for (int group=0; group<4; ++group) storeTriple(dst+group*3, b);
  for (int group=0; group<kActiveWidth/8; ++group) {
    const uint8_t *p=src+group*8;
    // Equivalent to original FabGL VGA8 three-byte packing without sampleX
    // lookups, border branches, or per-pixel loops.
    const uint32_t bits = (uint32_t(p[0]&7)<<21) | (uint32_t(p[1]&7)<<18) |
                          (uint32_t(p[2]&7)<<15) | (uint32_t(p[3]&7)<<12) |
                          (uint32_t(p[4]&7)<< 9) | (uint32_t(p[5]&7)<< 6) |
                          (uint32_t(p[6]&7)<< 3) |  uint32_t(p[7]&7);
    storeTriple(dst+(group+4)*3, bits);
  }
  for (int group=36; group<40; ++group) storeTriple(dst+group*3, b);
}
} // namespace MC1000VGA8
