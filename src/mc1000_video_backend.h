/* SPDX-License-Identifier: MIT
 * Palette backend selection.  VGA16 permits all MC6847 SG4 foreground inks
 * (plus black and the two text backgrounds) without checkerboard mixing.
 * Set MC1000_USE_VGA16=0 to restore the stable original VGA8 path for an
 * A/B performance or Aitek synchronization test. No runtime mode switch.
 */
#pragma once
#ifndef MC1000_USE_VGA16
#define MC1000_USE_VGA16 1
#endif
#if MC1000_USE_VGA16
#include "dispdrivers/vga16controller.h"
namespace MC1000VideoBackend { using Controller = fabgl::VGA16Controller; }
#else
#include "dispdrivers/vga8controller.h"
namespace MC1000VideoBackend { using Controller = fabgl::VGA8Controller; }
#endif
