/*
 * SPDX-License-Identifier: MIT
 * MC6847 per-character semigraphics mod (Ensjo, 2013).
 * Reference: jsMC1000/MC6847EnsjoMod.js, Emerson Costa (MIT).
 * This helper emulates the logic gates; the guest VRAM is never modified.
 */
#pragma once
#include <stdint.h>

namespace MC1000EnsjoMod {

inline bool selectsSemigraphics(uint8_t data, bool ag, bool as, bool enabled) {
  if (!enabled || ag || as) return false;
  switch (data & 0xe0) {
    case 0x00: // 000----- -> 000-----
    case 0x60: // 011----- -> 001-----
    case 0x80: // 100----- -> 110-----
    case 0xe0: // 111----- -> 111-----
      return true;
    default:
      return false;
  }
}

inline uint8_t dataForSemigraphics(uint8_t data) {
  // Only called when selectsSemigraphics() returned true.
  switch (data & 0xe0) {
    case 0x60: return static_cast<uint8_t>(0x20 | (data & 0x1f));
    case 0x80: return static_cast<uint8_t>(0xc0 | (data & 0x1f));
    default: return data;
  }
}

} // namespace MC1000EnsjoMod
