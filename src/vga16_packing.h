/* SPDX-License-Identifier: MIT
 * Native FabGL VGA16 packing, two logical pixels per byte (high nibble first).
 * Physical timing remains the existing 320x240 DoubleScan modeline.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace MC1000VGA16 {
static constexpr int kWidth = 320;
static constexpr int kBorderX = 32;
static constexpr int kActiveWidth = 256;
static constexpr int kRowBytes = 160;
inline uint8_t pair(uint8_t a, uint8_t b) {
  return uint8_t(((a & 15) << 4) | (b & 15));
}
inline void fillBorderRow(uint8_t *dst, uint8_t color) {
  if (!dst) return;
  memset(dst, pair(color, color), kRowBytes);
}
inline void packRow(uint8_t *dst, const uint8_t *src, uint8_t border) {
  if (!dst || !src) return;
  memset(dst, pair(border, border), kBorderX / 2);
  for (int i = 0; i < kActiveWidth / 2; ++i)
    dst[kBorderX / 2 + i] = pair(src[2 * i], src[2 * i + 1]);
  memset(dst + (kBorderX + kActiveWidth) / 2,
         pair(border, border), kBorderX / 2);
}
// Destination is the 320px raw VGA16 line. Row is drawn from raw MC6847
// packed (1 or 2 bpp) bytes using the mode-specific 4-entry colour map.
inline void packGraphicsRow(uint8_t *dst, const uint8_t *src,
                            int width, int bpp, uint8_t border,
                            const uint8_t *palette) {
  if (!dst || !src) return;
  fillBorderRow(dst, border);
  const int scale = kActiveWidth / width;
  for (int x = 0; x < width; ++x) {
    const unsigned px = bpp == 2
        ? unsigned((src[x / 4] >> (6 - (x % 4) * 2)) & 3)
        : unsigned((src[x / 8] >> (7 - (x % 8))) & 1);
    const uint8_t color = palette[px];
    const int first = kBorderX + x * scale;
    for (int n = 0; n < scale; ++n) {
      const int targetX = first + n;
      uint8_t &b = dst[targetX >> 1];
      if ((targetX & 1) == 0) b = uint8_t((b & 0x0f) | ((color & 15) << 4));
      else b = uint8_t((b & 0xf0) | (color & 15));
    }
  }
}
inline void packTopOverlayRow(uint8_t *dst, const uint8_t *src,
                              int xOffset, uint8_t border) {
  fillBorderRow(dst, border);
  for (int x = 0; x < kWidth; x += 2) {
    int a = x - xOffset, b = x + 1 - xOffset;
    const uint8_t c0 = a >= 0 && a < kActiveWidth ? src[a] : border;
    const uint8_t c1 = b >= 0 && b < kActiveWidth ? src[b] : border;
    dst[x / 2] = pair(c0, c1);
  }
}
} // namespace MC1000VGA16
