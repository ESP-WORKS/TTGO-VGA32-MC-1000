/* SPDX-License-Identifier: BSD-3-Clause
 * MC6847 RG6 artifact model adapted from MAME's artifacter, by Nathan Woods.
 * MAME project: https://github.com/mamedev/mame/blob/master/src/devices/video/mc6847.cpp
 * The exact 128-entry artifact correction indices and rounded RGB factors
 * are generated OFFLINE by tools/generate_mame_artifact_tables.py.
 * The VGA16 framebuffer uses all 16 MAME colors for the active RG6 mode,
 * with a separate palette for CSS=0/1 and Standard/Reverse phase.
 * No floating-point operations, palette modifications, or allocations occur
 * inside the per-pixel render loop. VGA8 retains an approximate fallback.
 * Artifact processing only applies to RG6 when enabled.
 */
#pragma once
#include <stdint.h>
namespace MC1000Artifacts {
// Output color indices: 0..15 are MAME's 16 RG6 artifact RGB colors.
// MAME uses the same correction mapping for CSS0, CSS1 and both phases.
static const uint8_t kCorrection[128] = {
   0,  0,  0,  0,  0,  6,  0,  2,  5,  7,  5,  7,  1,  3,  1, 11,
   8,  6,  8, 14,  8,  9,  8,  9,  4,  4,  4, 15, 12, 12, 12, 15,
   5, 13,  5, 13, 13,  0, 13,  2, 10, 10, 10, 10, 10, 15, 10, 11,
   3,  1,  3,  1, 15,  9, 15,  9, 11, 11, 11, 11, 15, 15, 15, 15,
  14,  0, 14,  0, 14,  6, 14,  2,  0,  7,  0,  7,  1,  3,  1, 11,
   9,  6,  9, 14,  9,  9,  9,  9, 15,  4, 15, 15, 12, 12, 12, 15,
   2, 13,  2, 13,  2,  0,  2,  2, 10, 10, 10, 10, 10, 15, 10, 11,
  12,  1, 12,  1, 12,  9, 12,  9, 15, 11, 15, 11, 15, 15, 15, 15
};

// Indexed [CSS][phase-1][artifact index][RGB component].
static const uint8_t kRGB[2][2][16][3] = {
  { // CSS=0
    { // Standard
      {0x26, 0x30, 0x16}, // color 0
      {0x26, 0x49, 0x16}, // color 1
      {0x28, 0x30, 0x13}, // color 2
      {0x2e, 0xd2, 0x04}, // color 3
      {0x30, 0xb5, 0x00}, // color 4
      {0x26, 0x50, 0x0c}, // color 5
      {0x2d, 0x56, 0x13}, // color 6
      {0x29, 0xaf, 0x00}, // color 7
      {0x30, 0x89, 0x0d}, // color 8
      {0x30, 0x81, 0x16}, // color 9
      {0x26, 0x81, 0x00}, // color 10
      {0x2a, 0xc9, 0x00}, // color 11
      {0x30, 0xc9, 0x05}, // color 12
      {0x26, 0x30, 0x11}, // color 13
      {0x28, 0x30, 0x16}, // color 14
      {0x30, 0xd2, 0x00} // color 15
    },
    { // Reverse
      {0x26, 0x30, 0x16}, // color 0
      {0x28, 0x30, 0x13}, // color 1
      {0x26, 0x49, 0x16}, // color 2
      {0x30, 0xb5, 0x00}, // color 3
      {0x2e, 0xd2, 0x04}, // color 4
      {0x2d, 0x56, 0x13}, // color 5
      {0x26, 0x50, 0x0c}, // color 6
      {0x30, 0x89, 0x0d}, // color 7
      {0x29, 0xaf, 0x00}, // color 8
      {0x26, 0x81, 0x00}, // color 9
      {0x30, 0x81, 0x16}, // color 10
      {0x30, 0xc9, 0x05}, // color 11
      {0x2a, 0xc9, 0x00}, // color 12
      {0x28, 0x30, 0x16}, // color 13
      {0x26, 0x30, 0x11}, // color 14
      {0x30, 0xd2, 0x00} // color 15
    }
  },
  { // CSS=1
    { // Standard
      {0x26, 0x30, 0x16}, // color 0
      {0x26, 0x48, 0x16}, // color 1
      {0x3e, 0x30, 0x2e}, // color 2
      {0xa4, 0xc8, 0x92}, // color 3
      {0xbf, 0xad, 0xad}, // color 4
      {0x26, 0x4e, 0x5d}, // color 5
      {0x92, 0x54, 0x28}, // color 6
      {0x50, 0xa7, 0xad}, // color 7
      {0xbf, 0x84, 0x51}, // color 8
      {0xbf, 0x7c, 0x16}, // color 9
      {0x26, 0x7c, 0xad}, // color 10
      {0x62, 0xbf, 0xad}, // color 11
      {0xbf, 0xbf, 0x8d}, // color 12
      {0x26, 0x30, 0x3a}, // color 13
      {0x4a, 0x30, 0x16}, // color 14
      {0xbf, 0xc8, 0xad} // color 15
    },
    { // Reverse
      {0x26, 0x30, 0x16}, // color 0
      {0x3e, 0x30, 0x2e}, // color 1
      {0x26, 0x48, 0x16}, // color 2
      {0xbf, 0xad, 0xad}, // color 3
      {0xa4, 0xc8, 0x92}, // color 4
      {0x92, 0x54, 0x28}, // color 5
      {0x26, 0x4e, 0x5d}, // color 6
      {0xbf, 0x84, 0x51}, // color 7
      {0x50, 0xa7, 0xad}, // color 8
      {0x26, 0x7c, 0xad}, // color 9
      {0xbf, 0x7c, 0x16}, // color 10
      {0xbf, 0xbf, 0x8d}, // color 11
      {0x62, 0xbf, 0xad}, // color 12
      {0x4a, 0x30, 0x16}, // color 13
      {0x26, 0x30, 0x3a}, // color 14
      {0xbf, 0xc8, 0xad} // color 15
    }
  }
};

// The reverse phase exchanges neighboring correction colors 1/2,
// 3/4, ..., 13/14 in MAME; colors 0 and 15 remain unchanged. The
// index-table itself stays the same: only the RGB palette changes.
// MAME reads the neighboring horizontal BORDER pixels as its c1 color.
// RG6's green/buff border therefore contributes a 1, not a zero, to
// the first/last six-pixel windows.
inline uint8_t sample(const uint8_t *src, int x) {
  return (x >= 0 && x < 256) ? ((src[x >> 3] >> (7 - (x & 7))) & 1u) : 1u;
}

// For VGA16, the 16 MAME colors are directly represented by indices 0..15.
// RGB fidelity is handled by kRGB and the mode-local FabGL palette.
// For VGA8 (build fallback), approximate 16 colors using its eight slots.
static const uint8_t kVGA8_CSS0[16] = {
  0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1
};
static const uint8_t kVGA8_CSS1[16] = {
  0, 0, 0, 1, 5, 5, 5, 1, 6, 6, 5, 5, 1, 6, 6, 1
};
inline void renderRG6Row(uint8_t *dst, const uint8_t *src,
                         bool css, bool vga16, uint8_t phase = 1) {
  const uint8_t *fallback = css ? kVGA8_CSS1 : kVGA8_CSS0;
  uint8_t pattern = (sample(src, -2) << 5) | (sample(src, -1) << 4) |
                    (sample(src,  0) << 3) | (sample(src,  1) << 2) |
                    (sample(src,  2) << 1) |  sample(src,  3);
  for (int x = 0; x < 256; x += 2) {
    const uint8_t a = kCorrection[pattern * 2];
    const uint8_t b = kCorrection[pattern * 2 + 1];
    if (vga16) {
      dst[x] = a; dst[x + 1] = b;
    } else {
      uint8_t ca = fallback[a], cb = fallback[b];
      // VGA8 has only 8 simultaneous colors. Approximate Reverse phase
      // by swapping its amber/azure artifact palette slots in CSS1.
      if (css && phase == 2) {
        if (ca == 5) ca = 6; else if (ca == 6) ca = 5;
        if (cb == 5) cb = 6; else if (cb == 6) cb = 5;
      }
      dst[x] = ca; dst[x + 1] = cb;
    }
    // Sliding window of six pixels, advanced two at a time.
    pattern = uint8_t(((pattern << 2) & 63u) |
                      (sample(src, x + 4) << 1) | sample(src, x + 5));
  }
}
} // namespace MC1000Artifacts
