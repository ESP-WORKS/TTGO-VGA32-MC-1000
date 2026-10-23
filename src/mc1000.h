/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "fabgl.h"
#include "mc6847.h"
#include "fps_eps_meter.h"
#include "cassette_dispatch.h"
#include "z80_memory_pages.h"
#include "ay38910.h"
#include "mc1000_joystick.h"
#include "tsave_format.h"
#include "z80/Z80.h"

struct KeyMat {
  int row;
  byte mask;
  int8_t shift_override;
  bool down; // actual physical key state, tracked independently for L/R modifiers
  KeyMat(int r = -1, byte m = 0, int8_t so = -1) : row(r), mask(m), shift_override(so), down(false) {}
};

class MC1000 {
#ifdef MC1000_HOST_TEST
  friend int main();  // test-only access, compiled out on ESP32
#endif
public:
  MC1000();
  ~MC1000();
  bool Init();  // false: memory or SD ROM unavailable/invalid; never start guest CPU
  bool VideoResourcesReady() const { return video_resources_ready_; }
  void Run();
  MC6847 *vdg() { return &mc6847_; }
  void FrameRendered() { UpdateFPS(); }
  void SelectBrowserFile(const char *filename);
  void BrowserSelectionCanceled();
  void BrowserNoFiles();
  void BrowserNewFile();
  void BrowserDialogDone();
  void InjectBasicTXT(const char *filename);
  void SelectBrowserBIN(const char *filename);
  void BrowserBINCanceled();
  void SaveOverwriteDecision(bool approved);
  void SaveBINOverwriteDecision(bool approved);
  void HostSaveBINCommand(const char *command);

  void WriteMem(int addr, int value);
  int ReadMem(int addr);
  void WriteIO(int addr, int value);
  int ReadIO(int addr);

  uint32_t cas_start_time() { return tick_ms_; }

private:
  bool InitMem();
  void InitKeyboard();
  void UpdateGuestKeyState(int vk, bool down);
  void RebuildGuestKeyboard();
  bool ProcessJoystickKey(int vk, bool down);
  void PollKeyboard();
  void SetKeyboardBR(bool enable);
  void ProcessEmulatorKey(VirtualKeyItem *item);
  uint8_t ReadKeyboardMatrix();
  void BankSwitch();
  void RefreshCpuMemoryPages();
  void ResetMachine();
  void ResetFromKeyboard(); // physical Z80 RESET line; preserves RAM/VRAM and peripherals
  void UpdateFPS();
  void RefreshFPSOverlay();
  void PollMagicButton();   // bootl.rc magicb -> F1 menu
  bool magic_raw_ = false;
  bool magic_stable_ = false;
  uint32_t magic_change_ms_ = 0;
  void HostSaveCassette();
  bool HostLoadBIN();
  bool QueueTSAVEFromGuest();
  void CommitPendingBIN(bool replaceExisting);
  void ClearPendingBIN();
  bool HostPrepareCassetteHeader();
  bool HostLoadCassetteData();
  void CompleteCassetteLoad(bool success);
  void ReturnFromTapeRoutine();
  void FinishHostSave(MC1000CAS::SaveOutcome outcome, const char *message);

  Z80MemoryPages memory_pages_;
  uint8_t *rom_ = nullptr; // user-provided 16 KiB ROM, loaded once from SD at boot
  bool video_resources_ready_ = false;
  uint8_t *ram_;          // backing store; availability is selected by MC1000_RAM_SIZE_KB
  uint8_t *video_ram_;    // 8 KiB MC6847 mapped VRAM (6 KiB visible in RG6)
  uint8_t *crtc_ram_;     // 2 KiB 80-column card RAM area
  KeyMat *key_table_;
  uint8_t key_rows_[8];
  uint8_t modifiers_;
  MC1000Joystick::Ports joysticks_;
  bool host_shift_down_;
  bool keyboard_br_ = false;
  int16_t br_scan_vk_[128] = {};  // pinned press->release VK per physical key
  uint8_t key_latch_;
  uint8_t rom0000_;
  uint8_t mc6847_bank_;
  uint8_t mc6845_bank_;
  uint8_t mc6847_attr_;
  uint8_t printer_busy_;

  fabgl::PS2Controller keyboard_;
  fabgl::SoundGenerator sound_generator_{32900};
  Z80 cpu_;
  MC6847 mc6847_;
  AY38910 ay38910_;

  int32_t cpu_cycle_accum_;
  int32_t irq_cycle_accum_;
  int32_t keyboard_timer_;
  uint32_t tick_ms_;
  uint32_t last_psg_loop_ms_ = 0;
  // MSX-style independent counters: VGA task owns the sampler and rendered
  // values; CPU core only increments Z80 cycles and requests visibility.
  FPS_EPS_Meter fps_meter_;
  std::atomic<uint32_t> cpu_cycles_total_{0};
  std::atomic<uint16_t> fps_{0};
  std::atomic<uint16_t> eps_{0};
  std::atomic<bool> fps_overlay_{false};
  bool fps_was_enabled_on_video_ = false; // VGA-task only
  bool cassette_header_ready_ = false;
  bool cassette_prompt_canceled_ = false;
  bool save_empty_browser_ = false;
  bool matrix_new_file_requested_ = false;
  bool save_overwrite_approved_ = false;
  bool save_overwrite_denied_ = false;
  bool cassette_auto_dialog_ = false;
  bool matrix_prompt_done_ = false;
  bool matrix_load_active_ = false;
  uint16_t cassette_file_start_ = 0;
  uint16_t cassette_file_end_ = 0;
  char cassette_file_[224] = {0};
  bool f1_key_latched_ = false;
  bool reset_key_chord_latched_ = false; // re-arm only on release of F11
  uint16_t last_cas_end_ = 0; // ROM LOAD leaves HL and DE at the end of the block
  char selected_host_file_[224] = {0};
  char selected_bin_file_[224] = {0};
  bool bin_auto_dialog_ = false;
  bool bin_prompt_canceled_ = false;
  uint8_t tsave_queued_kind_ = 0; // 1: command, 2: open editor
  uint32_t tsave_queued_at_ms_ = 0;
  char tsave_queued_command_[160] = {0};
  uint8_t *tsave_stage_ = nullptr;
  size_t tsave_stage_len_ = 0;
  char tsave_filename_[64] = {0};
  bool tsave_overwrite_exists_ = false;
};
