/* SPDX-License-Identifier: MIT
 * MC-1000 I/O mirror groups based on MC1000Core.js (jsMC1000, MIT).
 * The PSG/VDG decode ignores A0..A4 and sees only A5..A7.
 * Printer/expansion I/O decoding is intentionally handled separately.
 */
#pragma once
#include <stdint.h>
namespace MC1000IO {
  enum : uint8_t { kPsgControl=0x20, kPsgRead=0x40,
                   kPsgData=0x60, kVdg=0x80 };
  static inline uint8_t deviceGroup(uint16_t port) {
    return (uint8_t)(port & 0xE0u);
  }
}
