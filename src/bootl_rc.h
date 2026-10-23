/* SPDX-License-Identifier: MIT
 * bootl.rc: optional SD configuration shared with the ESP-WORKS bootloader.
 *
 * File: /bootl.rc in the SD root (name is case-insensitive).
 * One setting per line; "key=value", "key value" or "key: value".
 * Lines starting with '#', ';' or "//" are comments.
 *
 *   kbddat=32   PS/2 keyboard DATA GPIO   (default 32)
 *   kbdclk=33   PS/2 keyboard CLOCK GPIO  (default 33)
 *   magicb=36   "magic button" GPIO: opens/closes the F1 menu
 *               (default: none). Active low (button to GND).
 *
 * Missing file, unknown keys or invalid pins keep the defaults.
 */
#pragma once

struct BootRC {
  int kbddat = 32;
  int kbdclk = 33;
  int magicb = -1;   // -1 = no magic button
  bool loaded = false;
};

extern BootRC g_bootrc;

// Read /bootl.rc from the mounted SD. Safe to call when the SD is missing.
void bootrc_load(const char *sdMountPath);
