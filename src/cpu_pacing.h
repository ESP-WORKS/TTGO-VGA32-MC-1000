/* SPDX-License-Identifier: MIT
 * Z80 soft real-time microsecond pacing, independent of FreeRTOS tick size.
 */
#pragma once
#include <stdint.h>
namespace MC1000Timing {
// Real FreeRTOS blocking still runs IDLE1 and feeds the watchdog.
// At a 100 Hz tick, sleeping 1 tick every 100 ms burns up to 10% of
// guest execution. 250 ms lowers that to <=4%, while IDLE1 still runs
// four times per second. Keep the 12 ms limited catch-up unchanged.
static constexpr int64_t kIdleIntervalUs = 250000;
static constexpr int64_t kMaxCatchupUs = 12000;

inline bool shouldBlockForIdle(int64_t nowUs, int64_t lastBlockedUs) {
  return nowUs - lastBlockedUs >= kIdleIntervalUs;
}

// Do not discard a *small* backlog after sleeping an RTOS tick; it contains
// guest cycles owed to the actual wall clock. Cap large stalls (e.g. SD IO).
inline int64_t capCatchup(int64_t nowUs, int64_t deadlineUs) {
  return (nowUs - deadlineUs > kMaxCatchupUs)
       ? nowUs - kMaxCatchupUs : deadlineUs;
}
}
