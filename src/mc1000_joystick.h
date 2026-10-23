/* SPDX-License-Identifier: MIT
 * MC-1000 joysticks A/B: MAME cce/mc1000.cpp JOY0/JOY1 and keydata_r.
 * The original controllers are wired to Y1 and Y0 of the keyboard scanner,
 * respectively. Contacts are active LOW and are ORed electrically (AND in
 * logical active-low representation) with the keyboard matrix.
 * No MAME source code is copied; only the public hardware wiring is modeled.
 */
#pragma once
#include <stdint.h>

namespace MC1000Joystick {

enum Control : uint8_t { Up, Down, Left, Right, Fire, Invalid };

class Ports {
public:
  Ports() { reset(); }
  void reset() {
    for (int i = 0; i < 2; ++i) { enabled_[i] = false; state_[i] = 0xff; }
  }
  bool enabled(int player) const { return player >= 0 && player < 2 && enabled_[player]; }
  void setEnabled(int player, bool on) {
    if (player < 0 || player >= 2) return;
    if (enabled_[player] != on) state_[player] = 0xff;
    enabled_[player] = on;
  }
  void releaseAll() { state_[0] = state_[1] = 0xff; }
  // Store the physical state only when this joystick is enabled. The host
  // keyboard dispatcher swallows active control keys so BASIC doesn't also
  // receive (e.g.) WASD/SPACE matrix presses.
  bool setPressed(int player, Control control, bool down) {
    if (!enabled(player) || control == Invalid) return false;
    const uint8_t bit = mask(player, control);
    if (down) state_[player] &= static_cast<uint8_t>(~bit);
    else state_[player] |= bit;
    return true;
  }
  uint8_t port(int player) const {
    return enabled(player) ? state_[player] : 0xff;
  }
  uint8_t readMatrix(uint8_t latch, const uint8_t (&rows)[8], uint8_t modifiers) const {
    uint8_t data = 0xff;
    for (unsigned row = 0; row < 8; ++row) {
      if ((latch & (1u << row)) != 0) continue;
      data &= rows[row];
      if (row == 0) data &= port(1); // JOY1 / joystick B
      if (row == 1) data &= port(0); // JOY0 / joystick A
    }
    // Only SHIFT and CTRL use bits 6/7; joysticks never drive these bits.
    return static_cast<uint8_t>((data & 0x3f) | (modifiers & 0xc0));
  }
private:
  static uint8_t mask(int player, Control control) {
    switch (control) {
      case Up:    return 0x02;
      case Down:  return 0x04;
      case Left:  return 0x08;
      case Right: return 0x10;
      case Fire:  return player == 0 ? 0x20 : 0x01;
      default:    return 0x00;
    }
  }
  uint8_t state_[2];
  bool enabled_[2];
};
} // namespace MC1000Joystick
