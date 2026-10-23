/* SPDX-License-Identifier: MIT
 * ABNT2 PS/2 set-2 physical scancode translation for the MC-1000 guest.
 * Rules follow jsmc1000-master/MC1000Keyboard.js SPECIFIC_MAPPING[2].
 * The MC-1000 ROM has no native accented / cedilla contacts.
 */
#pragma once
#include <stdint.h>

namespace MC1000Keyboard {
  enum ABNT2Action : uint8_t {
    ABNT2_KEEP, ABNT2_IGNORE, ABNT2_APOSTROPHE, ABNT2_DOUBLEQUOTE,
    ABNT2_SEMICOLON, ABNT2_COLON, ABNT2_SLASH, ABNT2_QUESTION,
    ABNT2_CARET
  };

  // Layout physical positions, not US-localized virtual-key names. The
  // physical slash key next to the right Shift is scan code 0x51 on ABNT2.
  inline ABNT2Action TranslateABNT2(uint8_t code, bool shift) {
    switch (code) {
      case 0x0e: return shift ? ABNT2_DOUBLEQUOTE : ABNT2_APOSTROPHE;
      case 0x4a: return shift ? ABNT2_COLON : ABNT2_SEMICOLON;
      case 0x51: return shift ? ABNT2_QUESTION : ABNT2_SLASH;
      case 0x52: return shift ? ABNT2_CARET : ABNT2_IGNORE;
      case 0x36: return shift ? ABNT2_IGNORE : ABNT2_KEEP;
      // Dead acute and cedilla do not exist in the MC-1000 matrix. Do not
      // let the US key interpretations inject unrelated punctuation.
      case 0x54: case 0x4c: return ABNT2_IGNORE;
      default: return ABNT2_KEEP;
    }
  }

  // FabGL scancode[] encodes PS/2 set 2 as [make,0], [F0,break,0],
  // [E0,make,0] or [E0,F0,break,0]. Extended keys remain outside this
  // unextended punctuation translation to protect reset and menu controls.
  inline uint8_t ABNT2SimpleScancode(const uint8_t *s) {
    if (!s || !s[0] || s[0] == 0xe0 || s[0] == 0xe1) return 0;
    if (s[0] == 0xf0) return s[1];
    return s[0];
  }
}
