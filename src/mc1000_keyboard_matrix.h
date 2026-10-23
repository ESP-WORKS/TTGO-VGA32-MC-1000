/*
 * SPDX-License-Identifier: MIT
 * MC-1000 keyboard matrix state derived from FabGL virtual key events.
 * The scanner calls this only when a host key changes state, never per Z80 opcode.
 */
#pragma once
#include <stdint.h>

namespace MC1000Keyboard {

// The real MC-1000 connects its RESET key to the matrix (Y7, bit 3).
// Pressing RESET alone does not reset the CPU. SHIFT+RESET asserts the
// physical Z80 RESET signal (CCE manual and jsMC1000 implementation).
// Optionally accept CTRL+RESET as a host convenience alias.
inline bool ResetChord(bool guestResetHeld, bool leftShiftHeld,
                       bool rightShiftHeld, bool leftCtrlHeld,
                       bool rightCtrlHeld, bool ctrlAliasEnabled = true) {
  return guestResetHeld &&
         (leftShiftHeld || rightShiftHeld ||
          (ctrlAliasEnabled && (leftCtrlHeld || rightCtrlHeld)));
}


// Edge-triggered emulation of the electrical RESET line: once per F11 press,
// even with PS/2 repeat events or multiple modifiers. Re-arm on F11 release.
inline bool ResetEdge(bool resetHeld, bool chordActive, bool &latched) {
  if (!resetHeld) { latched = false; return false; }
  if (!chordActive || latched) return false;
  latched = true;
  return true;
}

// A matrix key is active low. Both PC sides of Shift and Ctrl share one
// MC-1000 contact, so the contact must remain active until BOTH sides are up.
// "shift_override" is used by translated symbols (for example @ or ").
template<class Key>
inline void Rebuild(const Key *keys, int count,
                    int leftShift, int rightShift, int leftCtrl, int rightCtrl,
                    uint8_t (&rows)[8], uint8_t &modifiers, bool &hostShiftDown) {
  for (int row = 0; row < 8; ++row) rows[row] = 0xff;

  const auto held = [keys, count](int vk) -> bool {
    return vk >= 0 && vk < count && keys[vk].down;
  };
  hostShiftDown = held(leftShift) || held(rightShift);
  const bool hostCtrlDown = held(leftCtrl) || held(rightCtrl);

  // When one or more translated punctuation keys are held, their requested
  // Shift state overrides the host modifier.  A real shift is restored as
  // soon as the translated key is released.
  int shiftOverride = -1;
  for (int vk = 0; vk < count; ++vk) {
    const Key &key = keys[vk];
    if (!key.down || key.row < 0) continue;
    if (key.mask != 0x40 && key.mask != 0x80 && key.row < 8) {
      rows[key.row] &= (uint8_t)~key.mask;
      if (key.shift_override >= 0) shiftOverride = key.shift_override;
    }
  }

  const bool guestShiftDown = (shiftOverride >= 0) ? (shiftOverride != 0) : hostShiftDown;
  modifiers = (guestShiftDown ? 0 : 0x40) | (hostCtrlDown ? 0 : 0x80);
}

} // namespace MC1000Keyboard
