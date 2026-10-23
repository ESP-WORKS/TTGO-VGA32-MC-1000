/*
 * SPDX-License-Identifier: MIT
 * Logical MC6847 picture geometry inside the FabGL VGA framebuffer.
 * Fixed 320x240 (4:3) framebuffer. The native 256x192 MC6847 image
 * stays centered 1:1; overscan uses the active VDG border color.
 */
#pragma once

namespace MC1000Video {

struct Layout {
  int framebuffer_width;
  int framebuffer_height;
  int content_x;
  int content_y;
  int content_width;
  int content_height;

  // Logical column source index; 256 is the border sentinel.
  int sampleX(int x) const {
    return x >= content_x && x < content_x + content_width
      ? ((x - content_x) * 256) / content_width : 256;
  }
};

// Keep the FPS/EPS glyphs inside the top overscan (safe for VGA monitor cropping).
static constexpr int FPS_OSD_X = 24;
static constexpr int FPS_OSD_Y = 12;
static constexpr int FPS_OSD_HEIGHT = 8;

inline Layout makeLayout() {
  const int width = 320;
  const int height = 240;
  const int picture_width = 256;
  const int picture_height = 192;
  return { width, height,
           (width - picture_width) / 2, (height - picture_height) / 2,
           picture_width, picture_height };
}

}  // namespace MC1000Video
