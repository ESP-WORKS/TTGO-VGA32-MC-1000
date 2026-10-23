/* SPDX-License-Identifier: MIT
 * TTGO-VGA32 MC-1000 port; derived structure from Jindor (c) 2024.
 * Modifications (c) 2026 MC-1000 contributors.
 */
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "mc1000.h"
#include "sysdep.h"
#include "esp_timer.h"
#include "esp_system.h"

// Files created by pre-SD-ROM releases are not valid for this build. They
// may still be compiled by Arduino despite .gitignore; stop with a clear
// message instead of accidentally embedding the original ROM into a BIN.
#if defined(__has_include)
# if __has_include("mc1000_rom.c") || __has_include("mc1000_rom.h")
#  error "Delete legacy src/mc1000_rom.c and src/mc1000_rom.h before compiling (ROM now loads from SD)"
# endif
#endif

MC1000 mc1000;

// Periodic Serial.printf can cause short bursts of work/interrupt traffic.
// Keep the long-term memory/stack diagnostic opt-in during VGA stability
// testing; boot and fatal error diagnostics are always printed.
#ifndef MC1000_PERIODIC_HEALTH_LOG
#define MC1000_PERIODIC_HEALTH_LOG 0
#endif

TaskHandle_t vdgTaskHandle;
TaskHandle_t cpuTaskHandle;

void vdgTask(void *params) {
  MC6847 *vdg = (MC6847 *)params;
  // FabGL's I2S IRQ is allocated on the calling core. Pin the VGA ISR to
  // core 0, away from the Z80 interpreter running on core 1.
  vdg->StartVideo();
  Serial.printf("[MC1000][VGA] initialized on core %d, mode %dx%d\n",
                xPortGetCoreID(), vdg->OutputWidth(), vdg->OutputHeight());
  // Pace the VIDEO render rate from the ESP32 microsecond clock, rather than
  // relying on pdMS_TO_TICKS(16/17): Arduino FreeRTOS can run at 100 Hz and
  // those intervals may both round to one tick, allowing ~100 renders/s.
  const int64_t frameUs = 16667;  // 60 Hz nominal; never render catch-up bursts
  int64_t nextFrameUs = esp_timer_get_time();
  int64_t lastBlockingYieldUs = nextFrameUs;
  while (true) {
    // taskYIELD() alone does NOT run the lower-priority IDLE0 task. With
    // 100 Hz FreeRTOS ticks, any remaining frame time < 10ms is busy-spun;
    // continuous GR/HGR frames can therefore starve IDLE0 and trigger the
    // task watchdog after seconds of simultaneous keyboard/VDG activity.
    // IDLE0 still needs CPU time, but a 10 ms delay every 30 ms can
    // drop a VSync slot and halve the effective redraw rate. Yield
    // periodically (250 ms); the VGA DMA remains independent of this task.
    const int64_t frameBeginUs = esp_timer_get_time();
    if (frameBeginUs - lastBlockingYieldUs >= 250000) {
      vTaskDelay(1);
      lastBlockingYieldUs = esp_timer_get_time();
      if (nextFrameUs < lastBlockingYieldUs) nextFrameUs = lastBlockingYieldUs;
    }
    vdg->RefreshScreen();
    mc1000.FrameRendered();
    // One fixed 320x240 @ ~60Hz VGA mode. FPS measures real completed
    // service intervals, not an assumed constant value.
    nextFrameUs += frameUs;
    int64_t now = esp_timer_get_time();
    if (now >= nextFrameUs) {
      // A late frame must NOT incur an additional full RTOS tick: with
      // double-buffered VSync this turned a slight overrun into ~30 FPS.
      // Drop stale deadlines and attempt the next complete backbuffer now.
      nextFrameUs = now;
      continue;
    }
    while ((now = esp_timer_get_time()) < nextFrameUs) {
      const int64_t remaining = nextFrameUs - now;
      if (remaining > 2000) {
        TickType_t ticks = pdMS_TO_TICKS((remaining - 1000) / 1000);
        if (ticks > 0) {
          vTaskDelay(ticks);
          lastBlockingYieldUs = esp_timer_get_time();
          continue;
        }
      }
      taskYIELD();
    }
  }
}

void cpuTask(void *params) {
  MC1000 *m = (MC1000 *)params;
  m->Run();
}

void setup() {
  // ESP-WORKS bootloader compatibility: its emulator image is installed in
  // ota_0 and it expects otadata to be cleared so the factory bootloader runs
  // again on the next power cycle.  This is harmless when flashing directly
  // from Arduino CLI.
  const esp_partition_t *otadata = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (otadata) {
    esp_partition_erase_range(otadata, 0, otadata->size);
  }

  Serial.begin(115200);
  Serial.printf("[MC1000] ESP32 reset reason: %d; free heap: %u\n",
                (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap());
  init_fs();
  const bool romReady = mc1000.Init();
  if (!mc1000.VideoResourcesReady()) {
    Serial.println("[MC1000][MEM] FATAL: guest buffers unavailable; no VGA/CPU tasks started");
    return;
  }
  if (!romReady) {
    // ROM errors can be displayed only after the guest video buffers exist.
    mc1000.vdg()->SetOverlayText("ROM ERROR: CHECK SD");
    mc1000.vdg()->SetToastText("/MC-1000/ROMS/MC-1000.ROM");
  }

  xTaskCreatePinnedToCore(vdgTask, "VDG", 8 * 1024, mc1000.vdg(), 2, &vdgTaskHandle, 0);
  configASSERT(vdgTaskHandle);
  // Start the guest only once DMA/framebuffer/ISR are stable. setup() runs
  // on core 1, yielding while the VDG task starts on core 0.
  while (!mc1000.vdg()->VideoReady()) vTaskDelay(1);

  if (!romReady) {
    Serial.println("[MC1000][ROM] CPU halted. Fix SD ROM and restart ESP32.");
    return; // Arduino loop() continues diagnostics; Z80 is not launched.
  }

  xTaskCreatePinnedToCore(cpuTask, "CPU", 8 * 1024, &mc1000, 2, &cpuTaskHandle, 1);
  configASSERT(cpuTaskHandle);
}

void loop() {
  // Low-frequency diagnostic for the intermittent GR+typing reboot.
  // Report task high-water marks (FreeRTOS stack words), not just free heap:
  // a watchdog, exhausted stack, brownout and panic need different remedies.
  // Keep UART diagnostics well away from the reported ~5-second monitor
  // reacquisition cadence; prolonged UART writes must not coincide with
  // every high-load Z80/LIST burst on the old VGA IRQ core.
  vTaskDelay(pdMS_TO_TICKS(15000));
#if MC1000_PERIODIC_HEALTH_LOG
  const unsigned cpuMin = cpuTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(cpuTaskHandle) : 0;
  const unsigned vdgMin = vdgTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(vdgTaskHandle) : 0;
  const unsigned attr = (unsigned)mc1000.vdg()->mode_attr();
  const unsigned frameCount = (unsigned)mc1000.vdg()->FrameCounter();
  Serial.printf("[MC1000][HEALTH] freeHeap=%u minHeap=%u cpuStackWords=%u vdgStackWords=%u vdgAttr=%02X vdgFrames=%u video=%dx%d\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                cpuMin, vdgMin, attr, frameCount,
                mc1000.vdg()->OutputWidth(), mc1000.vdg()->OutputHeight());
#endif
}
