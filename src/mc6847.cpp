/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#include "mc6847.h"
#include "osd.h"
#include "video_layout.h"
#include "mc6847_ensjo_mod.h"
#include "osd_frame_geometry.h"
#include "vga8_packing.h"
#include "vga8_graphics_fast.h"
#include "vga16_packing.h"
#include "mc6847_artifacts.h"
#include <string.h>
#include <stdlib.h>

#pragma GCC optimize ("Ofast")

/* Fixed 320x240 @ ~60 Hz VGA signal (4:3). The FabGL DMA controller
 * performs physical scan doubling, while the CPU task stays on core 1.
 * The VGA task starts the I2S/DMA hardware on core 0. No dynamic mode
 * switching or 16:9 timings are compiled into this release.
 */
static const char *const MC1000_MODELINE_4_3 =
    "\"320x240@60Hz MC1000\" 12.6 320 328 376 400 240 245 246 262 -HSync -VSync DoubleScan";

// CCE MC-1000 colour reference: the site's YUV-derived color names and
// Emerson Costa's jsMC1000 MC6847.COLOR table (MC6847.js, MIT license).
// The previous MAME-style palette approximated a different TV/video path.
// Use a single explicit RGB table so CSS=0/1, text, GR and HGR agree.
// https://sites.google.com/site/ccemc1000/v%C3%ADdeo/cores
// https://github.com/ensjo/jsmc1000/blob/master/MC6847.js
// Array order matches jsMC1000's eleven defined color entries.
enum MC1000Color : uint8_t {
  C_GREEN, C_YELLOW, C_BLUE, C_RED,
  C_BUFF, C_CYAN, C_MAGENTA, C_ORANGE,
  C_BLACK, C_DARK_GREEN, C_DARK_RED
};
static constexpr uint8_t MC1000_RGB[][3] = {
  {0x07, 0xff, 0x00}, // Green
  {0xff, 0xff, 0x00}, // Yellow
  {0x3b, 0x08, 0xff}, // Blue
  {0xcc, 0x00, 0x3b}, // Red
  {0xff, 0xff, 0xff}, // Buff (white)
  {0x07, 0xe3, 0x99}, // Cyan
  {0xff, 0x1c, 0xff}, // Magenta
  {0xff, 0x81, 0x00}, // Orange
  {0x00, 0x00, 0x00}, // Black
  {0x00, 0x7c, 0x00}, // Dark green
  {0x91, 0x00, 0x00}  // Dark red
};
static RGB888 mc1000RGB(MC1000Color c) {
  const uint8_t *p = MC1000_RGB[(unsigned)c];
  return RGB888(p[0], p[1], p[2]);
}

// VGA8Controller has exactly 8 palette entries.  The MC6847 has a 16-entry
// analog palette, so we remap the 8 hardware-visible colors required by the
// current VDG mode into the eight FabGL slots instead of writing palette
// indices 8..15 (which are invalid for VGA8Controller).
enum {
  PAL0 = 0, PAL1, PAL2, PAL3, PAL4, PAL5, PAL6, PAL7
};

/* MC6847 internal 6x12 character generator: MIT-licensed jsMC1000 source. */
/*
 * SPDX-License-Identifier: MIT
 * MC6847 character bitmap table from jsMC1000/MC6847.js
 * Copyright (c) 2013 Emerson Jose Silveira da Costa (Ensjo)
 * Source: https://github.com/ensjo/jsMC1000 (MIT License)
 * Derived C/C++ presentation for the ESP32 emulator, 2026.
 */
static const uint8_t font6847[64 * 12] = {
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x02, 0x1a, 0x2a, 0x2a, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x14, 0x22, 0x22, 0x3e, 0x22, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3c, 0x12, 0x12, 0x1c, 0x12, 0x12, 0x3c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x20, 0x20, 0x20, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3c, 0x12, 0x12, 0x12, 0x12, 0x12, 0x3c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x20, 0x20, 0x3c, 0x20, 0x20, 0x3e, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x20, 0x20, 0x3c, 0x20, 0x20, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1e, 0x20, 0x20, 0x26, 0x22, 0x22, 0x1e, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x3e, 0x22, 0x22, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3e, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x36, 0x2a, 0x2a, 0x22, 0x22, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x32, 0x2a, 0x26, 0x22, 0x22, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x22, 0x22, 0x22, 0x22, 0x22, 0x3e, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3c, 0x22, 0x22, 0x3c, 0x20, 0x20, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x22, 0x2a, 0x24, 0x18, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3c, 0x22, 0x22, 0x3c, 0x28, 0x24, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x10, 0x08, 0x04, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x2a, 0x2a, 0x36, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3e, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x38, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x20, 0x10, 0x08, 0x04, 0x02, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x1c, 0x2a, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x08, 0x10, 0x3e, 0x10, 0x08, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x14, 0x14, 0x36, 0x00, 0x36, 0x14, 0x14, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x1e, 0x20, 0x1c, 0x02, 0x3c, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x32, 0x32, 0x04, 0x08, 0x10, 0x26, 0x26, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x10, 0x28, 0x28, 0x10, 0x2a, 0x24, 0x1a, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x10, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x08, 0x1c, 0x3e, 0x1c, 0x08, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3e, 0x08, 0x08, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x10, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x02, 0x02, 0x04, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0x24, 0x24, 0x24, 0x18, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x02, 0x1c, 0x20, 0x20, 0x3e, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x02, 0x1c, 0x02, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x04, 0x0c, 0x14, 0x3e, 0x04, 0x04, 0x04, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x20, 0x3c, 0x02, 0x02, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x20, 0x20, 0x3c, 0x22, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x02, 0x04, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x1c, 0x22, 0x22, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x1e, 0x02, 0x02, 0x1c, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x08, 0x10, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x18, 0x24, 0x04, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00,
};


MC6847::MC6847() : MC1000VideoBackend::Controller(), frame_canvas_(this),
  vram_(nullptr), font_(nullptr),
  attr_(0), mode_(0), page_(0), css_(0), gm_(0),
  intext_(0), as_(0), ag_(0) {
  memset(overlay_, 0, sizeof overlay_);
  memset(toast_overlay_, 0, sizeof toast_overlay_);
}

void MC6847::Init(byte *vram) {
  // Do not start FabGL on the Arduino setup()/CPU core. The VGA8 ISR is
  // allocated on the core invoking setResolution(), so StartVideo() must run
  // on the dedicated VDG task pinned to core 0. Z80 busy loops stay on core 1.
  vram_ = vram;
  memset(vram_, 0, 0x2000); // 8 KiB CPU-visible VRAM, 6 KiB used by RG6
  font_ = (byte *)font6847;
  ClearOSD();
  SetModeAttr(0x00);
}

void MC6847::StartVideo() {
  // Called exactly once from VDG core-0 task, before any RefreshScreen().
  if (video_ready_.load(std::memory_order_acquire)) return;
  begin();
  ConfigureVideo4x3();
  InitColor();
#if MC1000_USE_VGA16
  // VGA16 uses a FIXED 16-entry palette for all MC6847 modes, including
  // text/SG4/GR/HGR and OSD.  Do not reprogram it at arbitrary scanlines:
  // even a short palette update during DMA scanout can affect a full line.
  applied_palette_key_ = 0;
  palette_dirty_.store(false, std::memory_order_release);
#endif
  video_ready_.store(true, std::memory_order_release);
}

void MC6847::ConfigureVideo4x3() {
  // Called once by the dedicated VGA task during StartVideo(). Keep the
  // working FabGL modeline unchanged: this is the configuration confirmed
  // stable on the Aitek monitor, with 32px/24px visible MC6847 borders.
  // FabGL performs the actual DMA page flip at vertical retrace. Drawing into
  // the other 28.8 KiB viewport prevents the monitor scanning a half-updated
  // image during large text/GR/HGR/cursor/OSD changes.
  setResolution(MC1000_MODELINE_4_3, 320, 240, true);
  double_buffered_ = isDoubleBuffered() &&
      getViewPortWidth() == 320 && getViewPortHeight() == 240;
  if (!double_buffered_) {
    // Not all WROVER heap configurations can reserve TWO DMA-capable screens.
    // Never run a 240-line renderer on a truncated viewport. Fall back to
    // the proven original framebuffer rather than corrupting memory.
    setResolution(MC1000_MODELINE_4_3, 320, 240, false);
    Serial.printf("[MC1000][VGA] double buffer unavailable; single buffer %dx%d\n",
                  getViewPortWidth(), getViewPortHeight());
  } else {
    Serial.println("[MC1000][VGA] vsync double buffering enabled (320x240)");
  }
  if (getViewPortWidth() != 320 || getViewPortHeight() != 240) {
    Serial.printf("[MC1000][VGA] FATAL: viewport truncated to %dx%d\n",
                  getViewPortWidth(), getViewPortHeight());
    abort(); // never write 240 lines into a shorter DMA viewport
  }
  warmup_frames_ = double_buffered_ ? 2 : 0;
  const MC1000Video::Layout layout = MC1000Video::makeLayout();
  output_width_ = layout.framebuffer_width;
  output_height_ = layout.framebuffer_height;
  content_width_ = layout.content_width;
  content_height_ = layout.content_height;
  content_x_ = layout.content_x;
  content_y_ = layout.content_y;
  // The resize recreates the DMA framebuffer; force a full redraw and palette.
  palette_dirty_.store(true, std::memory_order_release);
  last_draw_generation_ = UINT32_MAX;
  last_vram_hash_ = UINT32_MAX;
}

void MC6847::SetColorBlocks(bool enabled) {
  // Persist while the MC-1000 resets; only a cold ESP32 start restores OFF.
  if (requested_color_blocks_.exchange(enabled, std::memory_order_acq_rel) == enabled)
    return;
#if !MC1000_USE_VGA16
  palette_dirty_.store(true, std::memory_order_release);
#endif
  draw_generation_.fetch_add(1, std::memory_order_release);
}

void MC6847::SetColorArtifacts(bool enabled) {
  SetColorArtifactMode(enabled ? 1 : 0);
}

void MC6847::SetColorArtifactMode(uint8_t mode) {
  if (mode > 2) mode = 0;
  if (requested_color_artifact_mode_.exchange(mode, std::memory_order_acq_rel) == mode)
    return;
  // The video core applies the palette atomically with the next picture.
  palette_dirty_.store(true, std::memory_order_release);
  draw_generation_.fetch_add(1, std::memory_order_release);
}

void MC6847::CycleColorArtifactMode() {
  const uint8_t old = ColorArtifactMode();
  SetColorArtifactMode(static_cast<uint8_t>((old + 1) % 3));
}

void MC6847::SetAlternativeBorders(bool enabled) {
  // UI runs on CPU core 1; the video core samples the option once per frame.
  // Preserve across guest reset, but restore OFF on ESP32 cold boot.
  if (requested_alternative_borders_.exchange(enabled, std::memory_order_acq_rel) == enabled)
    return;
  // Trigger a redraw even if the guest RAM and video mode did not change.
  draw_generation_.fetch_add(1, std::memory_order_release);
}

void MC6847::SetFontFace(byte *font) {
  font_ = font ? font : (byte *)font6847;
}

void MC6847::SetCRTEffect(int enabled) {
  /* MC-1000 target deliberately has no scanline/vintage CRT effect. */
  (void)enabled;
}

void MC6847::SetMode(int text, byte param) {
  (void)text;
  SetModeAttr(param);
}

void MC6847::SetOSDActive(bool active) {
  if (osd_active_.exchange(active, std::memory_order_acq_rel) == active)
    return;
  draw_generation_.fetch_add(1, std::memory_order_release);
#if !MC1000_USE_VGA16
  palette_dirty_.store(true, std::memory_order_release);
#endif
}

void MC6847::SetModeAttr(byte attr) {
  // WriteIO/reset runs on CPU core. VGA16 has a fixed palette; only the
  // content/colour-index decoder changes with the MC6847 mode.
  if (requested_attr_.exchange(attr, std::memory_order_acq_rel) == attr)
    return;
#if !MC1000_USE_VGA16
  palette_dirty_.store(true, std::memory_order_release);
#endif
}

// Dedicated indices shared by text, semigraphics and overlays.  No VGA8
// palette-slot collisions or spatial dithering are needed in VGA16 mode.
static constexpr uint8_t UI_NAVY = 11;
static constexpr uint8_t UI_WHITE = 12;
static constexpr uint8_t UI_CYAN = 13;

#if MC1000_USE_VGA16
void MC6847::InitColor() { ApplyPalette(); }
void MC6847::ApplyPalette() {
  // Only the video task changes the VGA16 palette. The RG6 artifact
  // palette occupies 16 slots: 14 MAME-derived intermediate mixtures,
  // plus the MC-1000's actual dark/bright solid background endpoints.
  // All other modes, and modal overlays, use the normal VGA16 palette.
  if (applied_palette_key_ != 0) {
    const uint8_t (*mameRGB)[3] = MC1000Artifacts::kRGB[css_ != 0]
                                          [active_color_artifact_mode_ - 1];
    for (unsigned i = 0; i < 16; ++i)
      setPaletteItem(i, RGB888(mameRGB[i][0], mameRGB[i][1], mameRGB[i][2]));
    // Index 0 is the uniform pixel-zero background; index 15 is the
    // uniform pixel-one RG6 image and outer border.  The composite MAME
    // palette tints these two solids green, including CSS=1 black/buff.
    // Use MC-1000's original VGA color for those neutral endpoints while
    // retaining MAME's 14 intermediate artifact mixtures unchanged.
    setPaletteItem(0, mc1000RGB(css_ ? C_BLACK : C_DARK_GREEN));
    setPaletteItem(15, mc1000RGB(css_ ? C_BUFF : C_GREEN));
    return;
  }
  // Normal palette retains all 11 MC6847 shades and dedicated OSD colors.
  for (unsigned i = 0; i <= C_DARK_RED; ++i)
    setPaletteItem(i, mc1000RGB(static_cast<MC1000Color>(i)));
  setPaletteItem(UI_NAVY, RGB888(0x08, 0x16, 0x2e));
  setPaletteItem(UI_WHITE, RGB888(0xf2, 0xf6, 0xff));
  setPaletteItem(UI_CYAN, RGB888(0x38, 0xd9, 0xff));
  // Inactive-mode placeholders; MAME RG6 uses its own full 16-color palette.
  setPaletteItem(14, RGB888(0x00, 0x80, 0xff));
  setPaletteItem(15, RGB888(0xff, 0x80, 0x00));
}
#else
void MC6847::InitColor() {
  // Default text CSS=0: dark-green background and green foreground.
  // Slots 2-4 remain reserved for menu/toast OSD when active.
  setPaletteItem(0, mc1000RGB(C_DARK_GREEN));
  setPaletteItem(1, mc1000RGB(C_GREEN));
  setPaletteItem(2, mc1000RGB(C_BLACK));
  setPaletteItem(3, mc1000RGB(C_BUFF));
  setPaletteItem(4, mc1000RGB(C_YELLOW));
  setPaletteItem(5, mc1000RGB(C_BLUE));
  setPaletteItem(6, mc1000RGB(C_RED));
  setPaletteItem(7, mc1000RGB(C_ORANGE));
}

void MC6847::ApplyPalette() {
  const bool sg4 = !ag_ && as_ && !intext_;
  const bool sg6 = !ag_ && as_ && intext_;
  const bool resolutionGraphics = ag_ && ((gm_ & 1) != 0);
  if (ag_ && !resolutionGraphics) {
    // CG1/CG2/CG3/CG6, CSS controls the four-colour set.
    static const MC1000Color cg0[4] = {C_GREEN, C_YELLOW, C_BLUE, C_RED};
    static const MC1000Color cg1[4] = {C_BUFF, C_CYAN, C_MAGENTA, C_ORANGE};
    const MC1000Color *cg = css_ ? cg1 : cg0;
    for (int i=0; i<4; ++i) setPaletteItem(i, mc1000RGB(cg[i]));
    setPaletteItem(4, mc1000RGB(C_BLACK));
    setPaletteItem(5, mc1000RGB(C_DARK_GREEN));
    setPaletteItem(6, mc1000RGB(C_DARK_RED));
    setPaletteItem(7, mc1000RGB(C_BUFF));
  } else if (resolutionGraphics) {
    // RG1/RG2/RG3/RG6: CSS=0 dark-green/green; CSS=1 black/white.
    // HGR is RG6; do not simulate analog PAL-M artifact colours on VGA.
    setPaletteItem(0, mc1000RGB(css_ ? C_BLACK : C_DARK_GREEN));
    setPaletteItem(1, mc1000RGB(css_ ? C_BUFF : C_GREEN));
    setPaletteItem(2, mc1000RGB(C_YELLOW));
    setPaletteItem(3, mc1000RGB(C_BUFF));
    setPaletteItem(4, mc1000RGB(C_BLUE));
    setPaletteItem(5, (active_color_artifact_mode_ && gm_ == 7) ?
                         RGB888(0xff, 0x80, 0x00) : mc1000RGB(C_RED));
    setPaletteItem(6, (active_color_artifact_mode_ && gm_ == 7) ?
                         RGB888(0x00, 0x80, 0xff) : mc1000RGB(C_CYAN));
    setPaletteItem(7, mc1000RGB(C_ORANGE));
  } else if (sg4 || sg6) {
    // SG6 uses BLACK + 4 selected colours and fits in VGA8.
    // SG4 has BLACK + all 8 foreground colours, 9 in total; VGA8 has
    // only 8 slots. Preserve the first 7 EXACT RGB colours and render the
    // last, orange, as a spatial RED/YELLOW blend at the pixel level.
    // Do not switch the VGA controller or change sync timings to fix this.
    setPaletteItem(0, mc1000RGB(C_BLACK));
    if (sg6 && css_) {
      setPaletteItem(1, mc1000RGB(C_BUFF));
      setPaletteItem(2, mc1000RGB(C_CYAN));
      setPaletteItem(3, mc1000RGB(C_MAGENTA));
      setPaletteItem(4, mc1000RGB(C_ORANGE));
    } else {
      setPaletteItem(1, mc1000RGB(C_GREEN));
      setPaletteItem(2, mc1000RGB(C_YELLOW));
      setPaletteItem(3, mc1000RGB(C_BLUE));
      setPaletteItem(4, mc1000RGB(C_RED));
    }
    setPaletteItem(5, mc1000RGB(C_BUFF));
    setPaletteItem(6, mc1000RGB(C_CYAN));
    setPaletteItem(7, mc1000RGB(C_MAGENTA));
  } else {
    // Alphanumeric: CSS=0 dark-green/green, CSS=1 dark-red/orange.
    // The previous dark-brown / yellow-orange CSS=1 pair was not correct
    // for the MC-1000 colour reference.
    setPaletteItem(0, mc1000RGB(css_ ? C_DARK_RED : C_DARK_GREEN));
    setPaletteItem(1, mc1000RGB(css_ ? C_ORANGE : C_GREEN));
    setPaletteItem(2, mc1000RGB(C_BLACK));
    setPaletteItem(3, mc1000RGB(C_BUFF));
    setPaletteItem(4, mc1000RGB(C_YELLOW));
    setPaletteItem(5, mc1000RGB(C_BLUE));
    setPaletteItem(6, mc1000RGB(C_RED));
    setPaletteItem(7, mc1000RGB(C_CYAN));
    if (active_color_blocks_) {
      // The hardware mod dynamically selects SG4 on a character byte.
      // VGA8 cannot show the eleven possible SG4/text RGB colours at
      // once, so the uncommon MAGENTA/ORANGE SG4 inks are dithered below.
      setPaletteItem(2, mc1000RGB(C_BLACK));
      if (css_) {
        // Preserve dark-red/orange alphanumerics AND SG4's green and
        // white; approximate cyan/magenta by combinations below.
        setPaletteItem(3, mc1000RGB(C_GREEN));
        setPaletteItem(4, mc1000RGB(C_YELLOW));
        setPaletteItem(5, mc1000RGB(C_BLUE));
        setPaletteItem(6, mc1000RGB(C_RED));
        setPaletteItem(7, mc1000RGB(C_BUFF));
      } else {
        setPaletteItem(3, mc1000RGB(C_YELLOW));
        setPaletteItem(4, mc1000RGB(C_BLUE));
        setPaletteItem(5, mc1000RGB(C_RED));
        setPaletteItem(6, mc1000RGB(C_BUFF));
        setPaletteItem(7, mc1000RGB(C_CYAN));
      }
    }
  }
  // OSD-specific colours are an overlay compromise: VGA8 cannot display
  // the full guest palette and three additional menu colours together.
  if (osd_active_.load(std::memory_order_acquire) || toast_active_.load(std::memory_order_acquire)) {
    setPaletteItem(2, RGB888(0x08, 0x16, 0x2e)); // navy menu background
    setPaletteItem(3, RGB888(0xf2, 0xf6, 0xff)); // menu text
    setPaletteItem(4, RGB888(0x38, 0xd9, 0xff)); // menu dividers
  }
}

#endif // MC1000_USE_VGA16

// Update a compact 3-bit-per-pixel VGA8 line, then replicate it for the
// fractionally scaled logical row. Physical scan doubling is DMA-driven.
// This routine NEVER modifies sync timing or performs any per-pixel I/O.
// Native VGA8 3-bit packed border; unlike memset(0), works for RG1/2/3/6
// and remains stable while scrolling or while a menu is being redrawn.
void MC6847::FillBorderLine(uint8_t *scanline) const {
  #if MC1000_USE_VGA16
  MC1000VGA16::fillBorderRow(scanline, border_index_);
#else
  MC1000VGA8::fillBorderRow(scanline, border_index_);
#endif
}

void MC6847::BlitLogicalRow(const uint8_t *colors, int y) {
  if (y < 0 || y >= 192) return;
  // The MC-1000's fixed 4:3 layout is EXACTLY 256x192 centered at 32,24.
  // Pack the middle 256 pixels directly without per-pixel sampleX mapping,
  // branches, or a second scanline buffer. The FabGL backbuffer (when
  // available) is the only destination for each completed row.
  const int physicalY = content_y_ + y;
  #if MC1000_USE_VGA16
  MC1000VGA16::packRow(getScanline(physicalY), colors, border_index_);
#else
  MC1000VGA8::packRow(getScanline(physicalY), colors, border_index_);
#endif
  if (y == 0) {
    for (int sy = 0; sy < content_y_; ++sy) {
      FillBorderLine(getScanline(sy));
      FillBorderLine(getScanline(output_height_ - sy - 1));
    }
  }
}

// Hash a snapshot of guest VRAM to avoid redundant full-frame composition. This is especially helpful when the BASIC prompt is idle.
// The 32-bit hash is not a correctness/security checksum; force a regular
// repaint as a fallback for collisions and transient cross-core writes.
uint32_t MC6847::VideoRAMHash() const {
  const size_t n = ag_ ? 0x1800 : 512;
  uint32_t h=2166136261u;
  for (size_t i=0;i<n;++i) h=(h ^ frame_vram_[i]) * 16777619u;
  return h;
}

void MC6847::RefreshScreen() {
  if (!vram_ || !VideoReady()) return;
  // Apply pending changes ONLY from the VDG task, never concurrently with
  // render or FabGL's VGA palette. Changing the mode here also guarantees
  // a single consistent GR/HGR mode for every scanline of the frame.
  const uint8_t nextAttr = requested_attr_.load(std::memory_order_acquire);
  const bool modeChanged = nextAttr != attr_;
  if (modeChanged) {
    attr_ = nextAttr;
    css_ = (nextAttr >> 1) & 1;
    gm_ = (nextAttr >> 2) & 7;
    intext_ = (nextAttr >> 5) & 1;
    as_ = (nextAttr >> 6) & 1;
    ag_ = (nextAttr >> 7) & 1;
    mode_ = ag_ ? (gm_ + 1) : 0;
    page_ = 0;
  }
  const bool nextColorBlocks = requested_color_blocks_.load(std::memory_order_acquire);
#if !MC1000_USE_VGA16
  const bool colorBlocksChanged = active_color_blocks_ != nextColorBlocks;
#endif
  active_color_blocks_ = nextColorBlocks;
  active_color_artifact_mode_ = requested_color_artifact_mode_.load(std::memory_order_acquire);
  // The MC6847 changes the border colour when AG/GM/CSS changes; the
  // luminance-zero pixel is *not* a suitable border for RG modes.
  #if MC1000_USE_VGA16
  border_index_ = !ag_ ? C_BLACK :
      ((gm_ & 1) ? (css_ ? C_BUFF : C_GREEN) :
                   (css_ ? C_BUFF : C_GREEN));
#else
  border_index_ = ag_ ? ((gm_ & 1) ? 1 : 0) : (as_ ? 0 : 2);
#endif
#if MC1000_USE_VGA16
  if (ag_ && gm_ == 7 && active_color_artifact_mode_ != 0 &&
      !osd_active_.load(std::memory_order_acquire) &&
      !toast_active_.load(std::memory_order_acquire))
    border_index_ = 15; // special green/buff RG6 border in the artifact palette
#endif
  // MC-1000 TEXT $01 and $03: CSS selects green/dark-green or
  // orange/dark-red. Ignore bit 0 (MC6847 input not used for colour),
  // but do not change SG4/SG6, GR, HGR or any other video mode.
  // Sample once on the VGA task, then reuse for every row of this frame.
  const bool alternativeBorders = requested_alternative_borders_.load(std::memory_order_acquire);
  if (alternativeBorders && (attr_ & 0xfc) == 0) {
#if MC1000_USE_VGA16
    border_index_ = css_ ? C_DARK_RED : C_DARK_GREEN;
#else
    // Both VGA8 text palettes reserve slot zero for their background.
    border_index_ = 0;
#endif
  }
#if !MC1000_USE_VGA16
  if (colorBlocksChanged) palette_dirty_.store(true, std::memory_order_release);
#endif
  const bool paletteChanged = palette_dirty_.exchange(false, std::memory_order_acq_rel);
  bool paletteSwitched = false; // available in both VGA16 and VGA8 builds
#if MC1000_USE_VGA16
  // Preserve the normal fixed palette when switching ordinary text/GR modes.
  // Reprogram the 16 entries ONLY on a transition into/out of artifacted
  // RG6, when CSS changes, or when the MAME artifact phase changes.
  // A single VGA16 framebuffer cannot carry 16 MAME RGB values AND three
  // dedicated navy/white/cyan OSD colors.  For the duration of a menu/toast
  // use the normal fixed palette and a precomputed RG6 approximation;
  // restore MAME colors on the first frame without overlays.
  const bool uiPaletteActive = osd_active_.load(std::memory_order_acquire) ||
                               toast_active_.load(std::memory_order_acquire);
  const uint8_t paletteKey = (ag_ && gm_ == 7 && active_color_artifact_mode_ && !uiPaletteActive)
      ? static_cast<uint8_t>(1 + (css_ != 0) * 2 + active_color_artifact_mode_ - 1)
      : 0;
  paletteSwitched = paletteKey != applied_palette_key_;
  if (paletteSwitched) {
    applied_palette_key_ = paletteKey;
    ApplyPalette();
  }
#else
  // VGA8 preserves its existing dynamic eight-color mapping.
  if (modeChanged || paletteChanged) ApplyPalette();
#endif
  frame_counter_.fetch_add(1, std::memory_order_relaxed);
  // Snapshot guest VRAM before composing the next picture. CPU core 1 is
  // allowed to write while core 0 renders; the snapshot keeps every line
  // within this particular presentation independent of subsequent writes.
  memcpy(frame_vram_, vram_, ag_ ? 0x1800 : 512);
  const uint32_t hash = VideoRAMHash();
  const uint32_t generation = draw_generation_.load(std::memory_order_acquire);
  // Once both FabGL DMA buffers have a complete valid picture, leave them
  // completely untouched until VRAM, mode, or overlay ACTUALLY changes.
  // The old 60-frame forced repaint/swap caused needless DMA contention on
  // an idle BASIC screen.  Keep the old periodic repair only for the rare
  // single-buffer allocation fallback, where direct writers may need repair.
  if (!modeChanged && !paletteChanged && !paletteSwitched && hash == last_vram_hash_ &&
      generation == last_draw_generation_ && warmup_frames_ == 0 &&
      (double_buffered_ || ++unchanged_frames_ < 60)) {
    // The already-visible front framebuffer remains intact. Do NOT repaint
    // the backbuffer or swap it unless a complete new picture was rendered.
    // FPS/EPS changes post draw_generation_ via SetOverlayText().
    return;
  }
  unchanged_frames_ = 0;
  last_vram_hash_ = hash;
  last_draw_generation_ = generation;
  // Snapshot all overlay state ONCE per presented frame. F1/F9 and SAVE S/N
  // may change from the CPU task during scanout; do not mix rows belonging
  // to different menu screens within the new backbuffer.
  frame_osd_active_ = osd_active_.load(std::memory_order_acquire);
  frame_osd_confirmation_ = osd_overwrite_on();
  frame_osd_browsing_ = osd_browser_on();
  // Snapshot both modeless overlays once per frame. This avoids races with
  // the CPU task and removes hundreds of per-pixel framebuffer writes.
  char overlayCopy[32];
  char toastCopy[32];
  portENTER_CRITICAL(&overlay_mux_);
  memcpy(overlayCopy, overlay_, sizeof(overlayCopy));
  memcpy(toastCopy, toast_overlay_, sizeof(toastCopy));
  if (frame_osd_active_) memcpy(frame_osd_page_, osd_page_, sizeof frame_osd_page_);
  portEXIT_CRITICAL(&overlay_mux_);
  overlayCopy[sizeof(overlayCopy)-1] = 0;
  toastCopy[sizeof(toastCopy)-1] = 0;
  if (ag_) RefreshGraphics(overlayCopy, toastCopy);
  else RefreshText(overlayCopy, toastCopy);
  DrawTopBorderOverlay(overlayCopy);
  if (double_buffered_) frame_canvas_.swapBuffers();
  if (warmup_frames_) --warmup_frames_;
}

void MC6847::SetOverlayText(const char *text) {
  char next[sizeof overlay_] = {};
  if (text) { strncpy(next, text, sizeof next - 1); }
  portENTER_CRITICAL(&overlay_mux_);
  const bool changed = memcmp(overlay_, next, sizeof next) != 0;
  if (changed) memcpy(overlay_, next, sizeof next);
  portEXIT_CRITICAL(&overlay_mux_);
  if (changed) draw_generation_.fetch_add(1, std::memory_order_release);
}

void MC6847::RefreshText(const char *overlay, const char *toast) {
  (void)overlay; // top-border OSD is composited after the active picture
  // Render one logical 256-pixel row into a compact line buffer, then pack
  // eight native-resolution pixels into each three-byte VGA8 group.
  const bool sg4 = as_ && !intext_;
  const bool sg6 = as_ && intext_;
  // The original Ensjo mod changes the per-cell A/S input only for the
  // four specific high-bit groups; it does NOT select all bytes >= 0x80.
  // IN/EXT is unchanged: in external-text mode the same gate selects SG6.

#if !MC1000_USE_VGA16
  const int bg = 0;
#endif
  uint8_t line[256];

  for (int y = 0; y < 192; ++y) {
    const int row = y / 12;
    const int fy = y % 12;
    for (int col = 0; col < 32; ++col) {
      const uint8_t ch = frame_vram_[row * 32 + col];
      const int x0 = col * 8;
#if MC1000_USE_VGA16
      const int cellBg = css_ ? C_DARK_RED : C_DARK_GREEN;
      const int cellFg = css_ ? C_ORANGE : C_GREEN;
#else
      const int cellBg = 0;
      const int cellFg = 1;
#endif
      const bool modSemigraphics = MC1000EnsjoMod::selectsSemigraphics(
          ch, ag_ != 0, as_ != 0, active_color_blocks_);
      const bool mixedSG4 = modSemigraphics && !intext_;
      const bool mixedSG6 = modSemigraphics && intext_;
      const uint8_t sgData = modSemigraphics ?
          MC1000EnsjoMod::dataForSemigraphics(ch) : ch;
      if (sg4 || mixedSG4) {
        const uint8_t color = (sgData >> 4) & 7;
        const bool top = fy < 6;
        const bool left = top ? (sgData & 0x08) : (sgData & 0x02);
        const bool right = top ? (sgData & 0x04) : (sgData & 0x01);
#if !MC1000_USE_VGA16
        static const uint8_t sg4Map[8] = {1,2,3,4,5,6,7,2};
        static const uint8_t mixedGreen[8] = {1,3,4,5,6,7,4,3};
        static const uint8_t mixedOrange[8] = {3,4,5,6,7,3,5,1};
#endif
#if MC1000_USE_VGA16
        static const uint8_t inks[8] = {
          C_GREEN, C_YELLOW, C_BLUE, C_RED,
          C_BUFF, C_CYAN, C_MAGENTA, C_ORANGE
        };
        const uint8_t ci = inks[color];
#else
        const uint8_t ci = mixedSG4 ?
            (css_ ? mixedOrange[color] : mixedGreen[color]) : sg4Map[color];
#endif
        // SG4 colour 7 is ORANGE, the ninth colour including background.
        // Blend RED(4) and YELLOW(2) at 1:1 without changing the VGA8
        // palette; all other SG4 colours are represented exactly.
        for (int dx=0; dx<8; ++dx) {
          const bool lit = (dx < 4) ? left : right;
          if (!lit) {
#if MC1000_USE_VGA16
            line[x0+dx] = C_BLACK;
#else
            line[x0+dx] = mixedSG4 ? 2 : bg;
#endif
#if MC1000_USE_VGA16
          } else {
            line[x0+dx] = ci;
#else
          } else if (mixedSG4) {
            // Violet = BLUE/RED and ORANGE = RED/YELLOW in VGA8.
            if (css_) {
              line[x0+dx] = color == 5 ? (((dx ^ fy) & 1) ? 3 : 5) :
                            color == 6 ? (((dx ^ fy) & 1) ? 5 : 6) : ci;
            } else {
              line[x0+dx] = color == 6 ? (((dx ^ fy) & 1) ? 4 : 5) :
                            color == 7 ? (((dx ^ fy) & 1) ? 3 : 5) : ci;
            }
          } else {
            line[x0+dx] = (color == 7 && ((dx ^ fy) & 1)) ? 4 : ci;
#endif
          }
        }
      } else if (sg6 || mixedSG6) {
        const uint8_t color = (sgData >> 6) & 3;
#if !MC1000_USE_VGA16
        static const uint8_t sg6Map[4] = {1,2,3,4};
        static const uint8_t mixedSG6Green[4] = {1,3,4,5};
        static const uint8_t mixedSG6Orange[4] = {7,3,5,1};
#endif
#if MC1000_USE_VGA16
        static const uint8_t inks0[4] = {C_GREEN, C_YELLOW, C_BLUE, C_RED};
        static const uint8_t inks1[4] = {C_BUFF, C_CYAN, C_MAGENTA, C_ORANGE};
        const uint8_t ci = (css_ ? inks1 : inks0)[color];
#else
        const uint8_t ci = mixedSG6 ?
            (css_ ? mixedSG6Orange[color] : mixedSG6Green[color]) : sg6Map[color];
#endif
        const int slice = 2 - (fy / 4); // top/middle/bottom: bits 5..4, 3..2, 1..0
        const bool left = (sgData >> (slice * 2 + 1)) & 1;
        const bool right = (sgData >> (slice * 2)) & 1;
        for (int dx = 0; dx < 8; ++dx) {
          const bool lit = dx < 4 ? left : right;
#if MC1000_USE_VGA16
          line[x0 + dx] = lit ? ci : static_cast<uint8_t>(C_BLACK);
#else
          if (!lit) line[x0 + dx] = mixedSG6 ? 2 : bg;
          else if (mixedSG6 && css_ && color == 1) // cyan = green/blue
            line[x0 + dx] = ((dx ^ fy) & 1) ? 3 : 5;
          else if (mixedSG6 && css_ && color == 2) // magenta = blue/red
            line[x0 + dx] = ((dx ^ fy) & 1) ? 5 : 6;
          else line[x0 + dx] = ci;
#endif
        }
      } else {
        const bool inv = (ch & 0x80) != 0;
        const uint8_t code = ch & 0x3f;
        const uint8_t bits = font_[(code * 12) + fy];
        const uint8_t on = inv ? cellBg : cellFg;
        const uint8_t off = inv ? cellFg : cellBg;
        line[x0] = off;
        for (int dx = 0; dx < 6; ++dx)
          line[x0 + 1 + dx] = (bits & (0x20 >> dx)) ? on : off;
        line[x0 + 7] = off;
      }
    }
    ApplyToastToLine(line, y, toast);
    CompositeOSDLine(line, y);
    BlitLogicalRow(line, y);
  }
}

void MC6847::SetToastText(const char *text) {
  char next[sizeof toast_overlay_] = {};
  if (text) strncpy(next, text, sizeof next - 1);
  portENTER_CRITICAL(&overlay_mux_);
  const bool changed = memcmp(toast_overlay_, next, sizeof next) != 0;
  if (changed) memcpy(toast_overlay_, next, sizeof next);
  portEXIT_CRITICAL(&overlay_mux_);
  if (!changed) return;
  toast_active_.store(next[0] != 0, std::memory_order_release);
  draw_generation_.fetch_add(1, std::memory_order_release);
#if !MC1000_USE_VGA16
  palette_dirty_.store(true, std::memory_order_release);
#endif
}

uint8_t MC6847::OverlayPaletteIndex(uint8_t normal) const {
#if MC1000_USE_VGA16
  if (applied_palette_key_ != 0) {
    // The 16 slots belong to MAME during gameplay, not while an OSD/toast is open.
    // Choose high-contrast MAME shades for temporary OSD elements; NEVER
    // modify any entry in the artifact palette to accommodate the menu.
    if (normal == UI_NAVY) return 0;        // MAME dark background
    if (normal == UI_WHITE) return 15;      // MAME light ink
    if (normal == UI_CYAN) return 12;       // MAME accent
    if (normal == C_YELLOW) return 15;      // FPS / selection indicator
  }
#endif
  return normal;
}

void MC6847::ApplyToastToLine(uint8_t *colors, int y, const char *toast) {
  if (!toast || !toast[0]) return;
  const int len = (int)strlen(toast);
  const int boxW = len * 8 + 16;
  if (boxW > 240) return;
  const int x0 = (256 - boxW) / 2;
  const int y0 = 84;
  const int localY = y - y0;
  if (localY < 0 || localY >= 24) return;
  // Same navy/cyan/white palette as the F1/F9 menus. Text is deliberately
  // short to avoid spilling into the right margin or showing an old green box.
  memset(colors + x0, MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_NAVY) : 2, (size_t)boxW);
  if (localY == 0 || localY == 23) {
    memset(colors + x0, MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_CYAN) : 4, (size_t)boxW);
    return;
  }
  colors[x0] = colors[x0 + boxW - 1] = MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_CYAN) : 4;
  if (localY >= 6 && localY < 18) {
    const int gy=localY - 6;
    for (int i=0;i<len;i++) {
      unsigned char ch=(unsigned char)toast[i];
      if (ch >= 'a' && ch <= 'z') ch=(unsigned char)(ch-'a'+'A');
      int code=32;
      if (ch=='@') code=0;
      else if (ch >= 'A' && ch <= 'Z') code=ch-'A'+1;
      else if (ch >= ' ' && ch <= '?') code=ch;
      else if (ch=='[') code=27;
      else if (ch=='\\') code=28;
      else if (ch==']') code=29;
      else if (ch=='^') code=30;
      else if (ch=='_') code=31;
      const uint8_t bits=font_[(code*12)+gy];
      const int gx=x0+8+i*8;
      for (int fx=0;fx<6;fx++)
        if (bits & (0x20 >> fx)) colors[gx+fx]=MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_WHITE) : 3;
    }
  }
}

void MC6847::ApplyOverlayToLine(uint8_t *colors, int y, const char *overlay) {
  if (!overlay || !overlay[0] || y < 0 || y >= 8) return;
  const int fy = y;
  for (int i = 0; overlay[i] && i < 31; ++i) {
    unsigned char ch = (unsigned char)overlay[i];
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
    int code = 32;
    if (ch == '@') code = 0;
    else if (ch >= 'A' && ch <= 'Z') code = (int)(ch - 'A') + 1;
    else if (ch >= ' ' && ch <= '?') code = (int)ch;
    else if (ch == '[') code = 27;
    else if (ch == '\\') code = 28;
    else if (ch == ']') code = 29;
    else if (ch == '^') code = 30;
    else if (ch == '_') code = 31;
    const uint8_t bits = font_[(code * 12) + fy + 3];
    const int x0 = i * 7;
    // Opaque background prevents stale pixels when the text changes length.
    for (int fx = 0; fx < 6; ++fx) {
      const int x = x0 + fx;
      if (x >= 0 && x < 256) colors[x] = (bits & (0x20 >> fx)) ? (MC1000_USE_VGA16 ?
          (applied_palette_key_ != 0 ? 0 : C_YELLOW) : 3) :
          (MC1000_USE_VGA16 ? border_index_ : 0);
    }
    if (x0 + 6 < 256) colors[x0 + 6] = MC1000_USE_VGA16 ? border_index_ : 0;
  }
}

// Draw FPS/EPS within the 24-line TOP OVERSCAN, not at (0,0).  Some
// VGA monitors crop the edges of 320x240 double-scanned modes.  Do not
// change the known-good FabGL modeline or resize the MC6847 image.
void MC6847::DrawTopBorderOverlay(const char *overlay) {
  const int textY = MC1000Video::FPS_OSD_Y;
  const int textX = MC1000Video::FPS_OSD_X;
  // BlitLogicalRow() already filled both vertical borders into this frame.
  // A disabled overlay therefore needs no extra pixel writes.
  if (!overlay || !overlay[0]) return;

  for (int fy = 0; fy < MC1000Video::FPS_OSD_HEIGHT; ++fy) {
    const int outY = textY + fy;
    if (outY < 0 || outY >= content_y_) continue;
    uint8_t logical[256];
    memset(logical, border_index_, sizeof logical);
    // The character renderer writes 6x8 glyphs at logical x=0. Place
    // the glyphs at a safe physical margin, leaving the content untouched.
    ApplyOverlayToLine(logical, fy, overlay);
    uint8_t *row = getScanline(outY);
    if (!row) continue;
#if MC1000_USE_VGA16
    MC1000VGA16::packTopOverlayRow(row, logical, textX, border_index_);
#else
    for (int x = 0; x < output_width_; x += 8) {
      uint32_t bits = 0;
      for (int i = 0; i < 8; ++i) {
        const int lx = x + i - textX;
        const uint8_t c = (lx >= 0 && lx < 256) ? logical[lx] : border_index_;
        bits |= uint32_t(c & 7) << (21 - 3 * i);
      }
      const int o = (x / 8) * 3;
      row[o] = (uint8_t)bits;
      row[o+1] = (uint8_t)(bits >> 8);
      row[o+2] = (uint8_t)(bits >> 16);
    }
#endif
  }
}

#if MC1000_USE_VGA16
// Computed offline from MC1000Artifacts::kRGB to the original 16 VGA16
// palette colors using weighted (3R,4G,2B) squared RGB distance.
// Only used while a menu/toast is visible; normal gameplay uses MAME RGB.
static const uint8_t kArtifactForOSD[2][2][16] = {
  {
    {11,11,11,0,9,9,9,9,9,9,9,0,0,11,11,0},
    {11,11,11,9,0,9,9,9,9,9,9,0,0,11,11,0}
  },
  {
    {11,11,11,12,12,11,10,13,7,15,14,13,12,11,11,12},
    {11,11,11,12,12,10,11,7,13,14,15,12,13,11,11,12}
  }
};
#endif

void MC6847::RefreshGraphics(const char *overlay, const char *toast) {
  (void)overlay;
  // Same MC6847 GM0..GM2 decoding and bytes/row as jsMC1000 (MC6847.js).
  // The address never exceeds 0x17ff even in RG6/HGR; the CPU can still
  // access the full 8 KiB VRAM window at $8000-$9fff.
  struct ModeInfo { uint16_t w, h, bpr; uint8_t bpp; };
  static const ModeInfo modes[8] = {
    {64,64,16,2}, {128,64,16,1}, {128,64,32,2}, {128,96,16,1},
    {128,96,32,2}, {128,192,16,1}, {128,192,32,2}, {256,192,32,1}
  };
  const ModeInfo &m = modes[gm_];
  const bool artifactRG6 = active_color_artifact_mode_ != 0 && gm_ == 7;
  // Fast normal-game path: compose native MC6847 bytes directly into the
  // FabGL BACK buffer, without decoding and re-packing 49,152 pixels.
  // Keep the compositing path below unchanged for modal menus/toasts.
  if (!frame_osd_active_ && (!toast || !toast[0])) {
    int lastNativeY = -1;
    for (int y = 0; y < 192; ++y) {
      const int nativeY = (y * m.h) / 192;
      uint8_t *dst = getScanline(content_y_ + y);
      if (nativeY == lastNativeY) {
#if MC1000_USE_VGA16
        memcpy(dst, getScanline(content_y_ + y - 1), MC1000VGA16::kRowBytes);
#else
        memcpy(dst, getScanline(content_y_ + y - 1), MC1000VGA8::kRowBytes);
#endif
      } else {
        if (artifactRG6) {
          uint8_t line[256];
          MC1000Artifacts::renderRG6Row(line, frame_vram_ + nativeY * m.bpr,
                                        css_ != 0, MC1000_USE_VGA16 != 0, active_color_artifact_mode_);
          BlitLogicalRow(line, y);
        } else {
#if MC1000_USE_VGA16
        static const uint8_t text0[4] = {C_DARK_GREEN,C_GREEN,0,0};
        static const uint8_t text1[4] = {C_BLACK,C_BUFF,0,0};
        static const uint8_t cg0[4] = {C_GREEN,C_YELLOW,C_BLUE,C_RED};
        static const uint8_t cg1[4] = {C_BUFF,C_CYAN,C_MAGENTA,C_ORANGE};
        const uint8_t *ink = (m.bpp == 1) ? (css_ ? text1 : text0) :
                                                 (css_ ? cg1 : cg0);
        MC1000VGA16::packGraphicsRow(dst, frame_vram_ + nativeY*m.bpr,
                                      m.w, m.bpp, border_index_, ink);
#else
        MC1000VGA8::packGraphicsRow(dst, frame_vram_ + nativeY*m.bpr,
                                    m.w, m.bpp, border_index_);
#endif
        }
        lastNativeY = nativeY;
      }
    }
    // Also refresh the top/bottom border in the SAME backbuffer before the flip.
    for (int y = 0; y < content_y_; ++y) {
      FillBorderLine(getScanline(y));
      FillBorderLine(getScanline(output_height_ - y - 1));
    }
    return;
  }
  const int xscale = 256 / m.w; // exact: CG1 4, other 128-wide 2, HGR 1
  uint8_t decoded[256];
  uint8_t composited[256];
  int lastNativeY = -1;

  for (int y = 0; y < 192; ++y) {
    const int nativeY = (y * m.h) / 192;
    // Avoid decoding the same bitmap row 2 or 3 times in GR modes.
    if (nativeY != lastNativeY) {
      lastNativeY = nativeY;
      const uint8_t *src = frame_vram_ + nativeY * m.bpr;
      if (artifactRG6) {
        MC1000Artifacts::renderRG6Row(decoded, src, css_ != 0,
                                      MC1000_USE_VGA16 != 0, active_color_artifact_mode_);
#if MC1000_USE_VGA16
        if (applied_palette_key_ == 0) {
          const uint8_t *map = kArtifactForOSD[css_ != 0][active_color_artifact_mode_ - 1];
          for (int x = 0; x < 256; ++x) decoded[x] = map[decoded[x]];
        }
#endif
      } else {
      int x = 0;
      for (int b = 0; b < m.bpr; ++b) {
        const uint8_t value = src[b];
        if (m.bpp == 2) {
          for (int p = 0; p < 4; ++p) {
            const uint8_t ci = (value >> (6 - 2 * p)) & 3;
            for (int i = 0; i < xscale; ++i) decoded[x++] = ci;
          }
        } else {
          for (int bit = 7; bit >= 0; --bit) {
            const uint8_t ci = (value >> bit) & 1;
            for (int i = 0; i < xscale; ++i) decoded[x++] = ci;
          }
        }
      }
      } // default bitmap decode
    }
    memcpy(composited, decoded, sizeof decoded);
#if MC1000_USE_VGA16
    static const uint8_t rg0[2]={C_DARK_GREEN,C_GREEN};
    static const uint8_t rg1[2]={C_BLACK,C_BUFF};
    static const uint8_t cg0[4]={C_GREEN,C_YELLOW,C_BLUE,C_RED};
    static const uint8_t cg1[4]={C_BUFF,C_CYAN,C_MAGENTA,C_ORANGE};
    const uint8_t* map = (m.bpp == 1) ? (css_ ? rg1 : rg0) : (css_ ? cg1 : cg0);
    if (!artifactRG6) for (int i=0;i<256;i++) composited[i] = map[composited[i]];
#endif
    ApplyToastToLine(composited, y, toast);
    CompositeOSDLine(composited, y);
    BlitLogicalRow(composited, y);
  }
}

void MC6847::ClearOSD() {
  // An independent 32x16 text plane avoids writing menu bytes into BASIC's
  // 6KiB framebuffer and stops stale BASIC text from bleeding into the panel.
  portENTER_CRITICAL(&overlay_mux_);
  memset(osd_page_, ' ', sizeof osd_page_);
  portEXIT_CRITICAL(&overlay_mux_);
  draw_generation_.fetch_add(1, std::memory_order_release);
}

void MC6847::PutOSDText(int x, int y, const char *str, bool inverse) {
  if (!str || y < 0 || y >= 24 || x < 0 || x >= 32) return;
  portENTER_CRITICAL(&overlay_mux_);
  for (int col = x; col < 32 && *str; ++col, ++str) {
    const uint8_t ch = (uint8_t)*str;
    osd_page_[y * 32 + col] = inverse ? (uint8_t)(ch | 0x80) : ch;
  }
  portEXIT_CRITICAL(&overlay_mux_);
  draw_generation_.fetch_add(1, std::memory_order_release);
}

void MC6847::CompositeOSDLine(uint8_t *line, int y) {
  if (!frame_osd_active_ || y < 0 || y >= 192) return;
  const bool confirmation = frame_osd_confirmation_;
  const bool browsing = frame_osd_browsing_;
  const int firstRow = MC1000OSDFrame::firstRow(confirmation, browsing);
  const int lastRow = MC1000OSDFrame::lastRow(confirmation, browsing);
  const int cell = 12;
  const int row = y / cell;
  if (row < firstRow || row > lastRow) return;
  const int fy = y % cell; // Restore the original 12-pixel font, no scaling.
  // Paint an opaque panel first, then render OSD characters from dedicated
  // memory. The backing guest image and all graphics modes remain intact.
  const bool f1 = !confirmation && !browsing;
  const int panelFirstCol = f1 ? 3 : 4;
  const int panelColumns = f1 ? 26 : 24;
  uint8_t glyphs[26];
  memcpy(glyphs, frame_osd_page_ + row * 32 + panelFirstCol, panelColumns);
  // F1 states (ON/OFF/(F2)/(F12)...) are a separate right-aligned word of up
  // to 5 columns (panel 20..24). Only colour that word, never label text.
  int f1StateFirstCol = 99;
  if (f1 && row >= 3 && row <= 11) {
    int c = 24;
    while (c >= 20 && (glyphs[c] & 0x7f) != ' ') --c;
    if ((glyphs[c] & 0x7f) == ' ') f1StateFirstCol = c + 1;
  }
  for (int col = 0; col < panelColumns; ++col) {
    const int x0 = (panelFirstCol + col) * 8;
    memset(line + x0, MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_NAVY) : 2, 8); // solid navy: no underlying BASIC pixels
    const uint8_t ch = glyphs[col];
    if (ch >= 0x85 && ch <= 0x8f) continue; // frame drawn below
    const bool inv = (ch & 0x80) != 0;
    const uint8_t code = ch & 0x3f;
    const uint8_t bits = font_[(code * 12) + fy];
    // Browser title uses white background with black text, like F1 header.
    const bool header = (browsing && row == 2);
    const uint8_t fg = header ? (MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_NAVY) : 2) : (MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_WHITE) : 3);
    // F1 ON/OFF states use the existing yellow palette entry, including
    // when the row is selected (inverse video). The fixed VGA16 palette and
    // all menu text/background colours are otherwise unchanged.
    // Every visible F1 item reserves its right four columns for state / Fn.
    // Position-independent coloring survives vertical scrolling.
    const bool f1State = f1 && row >= 3 && row <= 11 && col >= f1StateFirstCol && col <= 24;
    const bool cursor = !confirmation && col == (browsing ? 2 : 1) && (ch & 0x7f) == '>';
    const bool star = browsing && col == 1 && (ch & 0x7f) == '*';
    const bool escapeHint = !confirmation &&
         ((browsing && row == 13 && col >= 19 && col <= 21) ||
          (!browsing && row == 13 && col >= 16 && col <= 18));
    const bool yellow = f1State || cursor || star || escapeHint;
    const uint8_t on = yellow ? (MC1000_USE_VGA16 ? OverlayPaletteIndex(C_YELLOW) : 4) :
                       (inv ? (MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_NAVY) : 2) : fg);
    const uint8_t off = header ? (MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_WHITE) : 3) :
                        (inv ? fg : (MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_NAVY) : 2));
    line[x0] = off;
    for (int px = 0; px < 6; ++px)
      line[x0 + 1 + px] = (bits & (0x20 >> px)) ? on : off;
    line[x0 + 7] = off;
  }
  const int top = firstRow * cell + 5;
  const int bottom = lastRow * cell + 6;
  if (y >= top && y <= bottom) {
    const int left = panelFirstCol * 8 + 3;
    const int right = (panelFirstCol + panelColumns - 1) * 8 + 4;
    line[left] = line[left + 1] = MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_CYAN) : 4;
    line[right] = line[right + 1] = MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_CYAN) : 4;
    if (MC1000OSDFrame::horizontalRuleAt(y, confirmation, browsing))
      for (int xx = left; xx <= right + 1; ++xx) line[xx] = MC1000_USE_VGA16 ? OverlayPaletteIndex(UI_CYAN) : 4;
  }
}

int MC6847::ColorIndex(uint8_t c) const { return c & 0x0f; }
