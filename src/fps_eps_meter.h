/* SPDX-License-Identifier: MIT
 * Independent monotonic video / Z80-equivalent throughput counter.
 * No Arduino, DMA or FreeRTOS dependency: portable host testing.
 */
#pragma once
#include <stdint.h>

class FPS_EPS_Meter {
public:
  struct Result { uint16_t fps; uint16_t eps; };
  static constexpr uint64_t kSampleWindowUs = 1000000ull;
  static constexpr uint32_t kGuestHz = 3579545;
  static constexpr uint32_t kFramesPerGuestSecond = 60;

  void Reset(uint64_t nowUs, uint32_t totalCycles) {
    beginUs_ = nowUs;
    beginCycles_ = totalCycles;
    frames_ = 0;
  }

  // One call after each *completed* VGA service, not each guest VRAM write.
  // The elapsed interval is measured in microseconds (not RTOS ticks).
  // Unsigned cycle subtraction correctly handles the uint32_t wraparound.
  bool Sample(uint64_t nowUs, uint32_t totalCycles, Result *out) {
    if (!out) return false;
    if (frames_ != UINT32_MAX) ++frames_;
    if (nowUs < beginUs_) { Reset(nowUs, totalCycles); return false; }
    const uint64_t elapsedUs = nowUs - beginUs_;
    if (elapsedUs < kSampleWindowUs) return false;
    const uint32_t elapsedCycles = totalCycles - beginCycles_;
    // Divide once, rounding half up from the original counts: (n + d/2) / d.
    // Do not first round to tenths: double rounding can turn 59.45 into 60.
    const uint64_t fps = (uint64_t(frames_) * 1000000ull + elapsedUs / 2) / elapsedUs;
    const uint64_t denominator = uint64_t(kGuestHz) * elapsedUs;
    const uint64_t eps =
        (uint64_t(elapsedCycles) * kFramesPerGuestSecond * 1000000ull + denominator / 2) / denominator;
    // Report real measured throughput, rounded to integer and capped at 60.
    out->fps = uint16_t(fps > 60 ? 60 : fps);
    out->eps = uint16_t(eps > 60 ? 60 : eps);
    Reset(nowUs, totalCycles);
    return true;
  }
private:
  uint64_t beginUs_ = 0;
  uint32_t beginCycles_ = 0;
  uint32_t frames_ = 0;
};
