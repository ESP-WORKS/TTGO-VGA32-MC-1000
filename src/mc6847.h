/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#pragma once
#include "mc1000_video_backend.h"
#include "fabgl.h"
#include "common.h"
#include "freertos/FreeRTOS.h"
#include <atomic>

class MC6847 : public MC1000VideoBackend::Controller {
public:
  MC6847();
  ~MC6847() = default;
  MC6847(MC6847 const&) = delete;
  void operator=(MC6847 const&) = delete;

  void Init(byte *vram);
  void StartVideo();  // must run on the pinned VGA task / ESP32 core 0
  bool VideoReady() const { return video_ready_.load(std::memory_order_acquire); }
  void SetFontFace(byte *font);
  void SetCRTEffect(int enabled); // retained for API compatibility; ignored
  void SetMode(int text, byte param);
  void SetModeAttr(byte attr);
  uint8_t mode_attr() const { return requested_attr_.load(std::memory_order_acquire); }
  void RefreshScreen();
  void SetColorBlocks(bool enabled);  // optional per-character SG4-in-text hardware mod
  bool ColorBlocks() const { return requested_color_blocks_.load(std::memory_order_acquire); }
  void SetAlternativeBorders(bool enabled); // only TEXT $01/$03 (CSS=0/1)
  bool AlternativeBorders() const { return requested_alternative_borders_.load(std::memory_order_acquire); }
  // Off (0), MAME NTSC Standard (1), MAME NTSC Reverse (2).
  // Keep the boolean setter/getter for existing callers and tests.
  void SetColorArtifacts(bool enabled);
  void SetColorArtifactMode(uint8_t mode);
  void CycleColorArtifactMode();
  uint8_t ColorArtifactMode() const { return requested_color_artifact_mode_.load(std::memory_order_acquire); }
  bool ColorArtifacts() const { return ColorArtifactMode() != 0; }
  int OutputWidth() const { return output_width_; }
  int OutputHeight() const { return output_height_; }
  // Independent OSD text plane; never touches guest VRAM.
  void ClearOSD();
  void PutOSDText(int x, int y, const char *s, bool inverse);
  bool mode() const { return mode_ == 0; }
  uint8_t mode_value() const { return mode_; }
  void SetOverlayText(const char *text);
  void SetToastText(const char *text);
  void SetOSDActive(bool active);
  uint32_t FrameCounter() const { return frame_counter_.load(std::memory_order_relaxed); }
  void ApplyPalette();
  bool DoubleBuffered() const { return double_buffered_; }

private:
  void InitColor();
  void ConfigureVideo4x3();
  uint32_t VideoRAMHash() const;
  void RefreshText(const char *overlay, const char *toast);
  void RefreshGraphics(const char *overlay, const char *toast);
  void BlitLogicalRow(const uint8_t * colors, int y);
  void ApplyToastToLine(uint8_t * colors, int y, const char *toast);
  void ApplyOverlayToLine(uint8_t * colors, int y, const char *overlay);
  void DrawTopBorderOverlay(const char *overlay);
  void FillBorderLine(uint8_t *scanline) const;
  void CompositeOSDLine(uint8_t *colors, int y);
  uint8_t OverlayPaletteIndex(uint8_t normal) const;
  int ColorIndex(uint8_t c) const;

  fabgl::Canvas frame_canvas_; // FabGL VSYNC page flip; owns no guest VRAM
  bool double_buffered_ = false;
  uint8_t warmup_frames_ = 0;
  uint8_t frame_vram_[0x1800] = {}; // one immutable MC6847 frame snapshot
  uint8_t frame_osd_page_[32 * 24] = {};
  bool frame_osd_active_ = false;
  bool frame_osd_confirmation_ = false;
  bool frame_osd_browsing_ = false;
  byte *vram_;
  uint8_t osd_page_[32 * 24] = {0};
  byte *font_;
  uint8_t attr_;
  int mode_;
  int page_;
  uint8_t css_;
  uint8_t gm_;
  uint8_t intext_;
  uint8_t as_;
  uint8_t ag_;
  portMUX_TYPE overlay_mux_ = portMUX_INITIALIZER_UNLOCKED;
  char overlay_[32];
  char toast_overlay_[32];
  // CPU/UI task only posts requests.  The video task owns active mode and
  // VGA palette updates; this avoids racing DMA palette state between cores.
  std::atomic<uint8_t> requested_attr_{0};
  std::atomic<bool> palette_dirty_{true};
  std::atomic<bool> toast_active_{false};
  std::atomic<bool> osd_active_{false};
  std::atomic<uint32_t> frame_counter_{0};
  std::atomic<bool> requested_color_blocks_{false};
  std::atomic<bool> requested_alternative_borders_{false};
  std::atomic<uint8_t> requested_color_artifact_mode_{0};
  uint8_t active_color_artifact_mode_ = 0;
  uint8_t applied_palette_key_ = 0; // 0=normal, 1..4=RG6 CSS/phase
  bool active_color_blocks_ = false;
  uint8_t border_index_ = 2;
  std::atomic<bool> video_ready_{false};
  std::atomic<uint32_t> draw_generation_{1};
  int output_width_ = 320, output_height_ = 240;
  int content_x_ = 32, content_y_ = 24;
  int content_width_ = 256, content_height_ = 192;
  uint32_t last_vram_hash_ = UINT32_MAX;
  uint32_t last_draw_generation_ = UINT32_MAX;
  uint8_t unchanged_frames_ = 0;
  RGB888 rgb_[12];
};
