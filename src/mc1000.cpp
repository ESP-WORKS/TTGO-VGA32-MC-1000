/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#include "mc1000.h"
#include "rom_sd.h"
#include "cpu_pacing.h"
#include "cas_format.h"
#include "cas_filename.h"
#include "cas_recovery.h"
#include "cassette_dispatch.h"
#include "cassette_policy.h"
#include "cas_memory_transfer.h"
#include "cas_save_verify.h"
#include "mc1000_io_decode.h"
#include "mc1000_keyboard_matrix.h"
#include "keyboard_abnt2.h"
// FabGL 1.0.9 exposes KeyboardLayout/USLayout through fabgl.h
// (already included by mc1000.h). Its devdrivers/kbdlayouts.h is not
// directly on Arduino's library include path.
#include "osd.h"
#include "basic_inject.h"
#include "bin_game_format.h"
#include "tsave_format.h"
#include "tsave_storage.h"
#include "sysdep.h"
#include "bootl_rc.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <new>
#include <stdlib.h>
#include "esp_timer.h"
#include "soc/rtc_wdt.h"
#include "esp_partition.h"
#include "esp_system.h"

#ifdef __GNUC__
#pragma GCC optimize ("O2")
#endif

#define MC1000_CPU_HZ 3579545
#define CPU_TICK_CYCLES 3580
#define KBD_PERIOD 2
#define MC1000_DEBUG 0 // Keep serial diagnostics opt-in; frequent VGA/CPU logging can perturb scheduling.
/* Enable the MC-1000 64 KiB RAM expansion by default so SD-loaded programs
   that occupy $4000-$BFFF are actually executable. */
#define MC1000_RAM_SIZE_KB 64

// Fast CPU-only page aliases for the Z80's repeated instruction/data accesses.
// All pointers are refreshed whenever ROM, VDG or CRTC bank selection changes.
// The $C000-$FFFF high ROM region MUST still use ReadMem() for its one-shot
// low-ROM-unmap side effect. No VGA task touches these pointers.
static const uint8_t *z80ReadPage[24] = {};
static uint8_t *z80WritePage[24] = {};

extern "C" {
byte RdZ80(register uint16 addr) {
  if (addr < 0xc000) {
    const uint8_t *page = z80ReadPage[addr >> 11];
    if (page) return page[addr & 0x07ff];
  }
  extern MC1000 mc1000;
  return mc1000.ReadMem(addr);
}
void WrZ80(register uint16 addr, register byte value) {
  if (addr < 0xc000) {
    uint8_t *page = z80WritePage[addr >> 11];
    if (page) { page[addr & 0x07ff] = value; return; }
  }
  extern MC1000 mc1000;
  mc1000.WriteMem(addr, value);
}
void OutZ80(register uint16 Port, register byte Value) { extern MC1000 mc1000; mc1000.WriteIO(Port, Value); }
byte InZ80(register uint16 Port) { extern MC1000 mc1000; return mc1000.ReadIO(Port); }
void CheckHook(register Z80 *R) {}
void PatchZ80(register Z80 *R) {}
uint16 LoopZ80(register Z80 *R) { return INT_NONE; }
MC6847 *vdg() { extern MC1000 mc1000; return mc1000.vdg(); }
}

extern MC1000 mc1000;

static void mc1000_dir_callback(char *filename, const char *status) {
  if (filename && filename[0] && status && strcmp(status, "SELECTED") == 0) {
    mc1000.SelectBrowserFile(filename);
  } else if (status && strcmp(status, "CANCELED") == 0) {
    mc1000.BrowserSelectionCanceled();
  } else if (status && strcmp(status, "NEW FILE") == 0) {
    mc1000.BrowserNewFile();
  } else if (status && strcmp(status, "NO FILES") == 0) {
    mc1000.BrowserNoFiles();
    osd_toast((char*)"NENHUM ARQUIVO CAS", 0, 0);
  } else if (status && strcmp(status, "NO SD DIR") == 0) {
    mc1000.BrowserSelectionCanceled();
    osd_toast((char*)"ERRO NA PASTA CAS", 0, 0);
  } else if (status && strcmp(status, "OUT OF MEMORY") == 0) {
    mc1000.BrowserSelectionCanceled();
    osd_toast((char*)"SEM MEMORIA PARA LISTA", 0, 0);
  }
  mc1000.BrowserDialogDone();
}

static void mc1000_txt_callback(char *filename, const char *status) {
  if (filename && filename[0] && status && strcmp(status, "SELECTED") == 0) {
    mc1000.InjectBasicTXT(filename);
  } else if (status && strcmp(status, "NO FILES") == 0) {
    osd_toast((char*)"NENHUM ARQUIVO TXT/BAS", 0, 0);
  } else if (status && strcmp(status, "NO SD DIR") == 0) {
    osd_toast((char*)"ERRO NA PASTA TXT", 0, 0);
  } else if (status && strcmp(status, "OUT OF MEMORY") == 0) {
    osd_toast((char*)"SEM MEMORIA PARA LISTA", 0, 0);
  }
  // TXT is never a retained cassette selection. Each F4 opens afresh.
}

static void mc1000_bin_callback(char *filename, const char *status) {
  if (filename && *filename && status && strcmp(status, "SELECTED") == 0) {
    mc1000.SelectBrowserBIN(filename);
  } else if (status && strcmp(status, "CANCELED") == 0) {
    mc1000.BrowserBINCanceled();
  } else if (status && (strcmp(status, "NO FILES") == 0 ||
                         strcmp(status, "NO SD DIR") == 0 ||
                         strcmp(status, "OUT OF MEMORY") == 0)) {
    mc1000.BrowserBINCanceled();
    osd_toast((char*)(strcmp(status, "NO FILES") == 0 ? "NENHUM ARQUIVO BIN" :
                        strcmp(status, "NO SD DIR") == 0 ? "ERRO NA PASTA BIN" :
                        "SEM MEMORIA PARA LISTA"), 0, 0);
  }
}

static void mc1000_open_bin_browser() {
  if (bin_directory_ready()) {
    osd_open_dialog((char*)"CARREGAR BIN", (char*)"*.BIN",
                    mc1000_bin_callback, false, false, true);
  } else {
    mc1000.BrowserBINCanceled();
    osd_toast((char*)"ERRO NA PASTA BIN", 0, 0);
  }
}

static void mc1000_open_txt_browser() {
  if (txt_directory_ready()) {
    // .BAS = BASIC listing in plain text, same as .TXT (not tokenized).
    osd_open_dialog((char*)"INJETAR PROGRAMA", (char*)"*.TXT;*.BAS",
                    mc1000_txt_callback, false, true);
  } else {
    osd_toast((char*)"ERRO NA PASTA TXT", 0, 0);
  }
}

static void mc1000_tsave_editor_callback(const char *command) {
  extern MC1000 mc1000;
  if (command) mc1000.HostSaveBINCommand(command);
}
static void mc1000_tsave_overwrite_callback(bool overwrite) {
  extern MC1000 mc1000;
  mc1000.SaveBINOverwriteDecision(overwrite);
}

static void mc1000_overwrite_callback(bool overwrite) {
  extern MC1000 mc1000;
  mc1000.SaveOverwriteDecision(overwrite);
}

static String mc1000_cass_name(const uint8_t *p) {
  String n;
  for (int i=0;i<14;i++) {
    uint8_t c=p[i];
    if(c==0 || c==13) break;
    if(c>=32 && c<=126) n += (char)c;
  }
  n.trim();
  n.toUpperCase();
  if(n.length()==0) n="NONAME";
  String safe;
  for(size_t i=0;i<n.length();i++) {
    char c=n[i];
    if(c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|') c='_';
    safe += c;
  }
  return safe;
}

static bool mc1000_guest_has_name(const uint8_t *header) {
  return MC1000CAS::guestHeaderHasName(header);
}

static uint8_t mc1000_peek_ram(MC1000 *m, int addr) {
  /* Host-side memory read without the C000 ROM banking side effect. */
  if(addr<0x2000) return m->ReadMem(addr); /* normally RAM after BASIC starts */
  return m->ReadMem(addr);
}

MC1000::MC1000() :
  ram_(nullptr), video_ram_(nullptr), crtc_ram_(nullptr), key_table_(nullptr),
  modifiers_(0xc0), host_shift_down_(false), key_latch_(0xff), rom0000_(1), mc6847_bank_(0),
  mc6845_bank_(0), mc6847_attr_(0), printer_busy_(0), cpu_cycle_accum_(0),
  keyboard_timer_(KBD_PERIOD), irq_cycle_accum_(0), tick_ms_(0) {}

MC1000::~MC1000() {
  free(tsave_stage_); free(rom_); free(ram_); free(video_ram_); free(crtc_ram_); delete[] key_table_;
}

bool MC1000::InitMem() {
  // All-or-nothing allocation. Never allow InitKeyboard, ResetMachine,
  // MC6847, or the CPU to dereference partially constructed guest memory.
  ram_ = (uint8_t*)calloc(1, 0xc000);       // 48 KiB expansion backing
  video_ram_ = (uint8_t*)calloc(1, 0x2000); // 8 KiB mapped MC6847 VRAM
  crtc_ram_ = (uint8_t*)calloc(1, 0x0800);  // 2 KiB optional 80-col card
  key_table_ = new (std::nothrow) KeyMat[(size_t)fabgl::VK_LAST];
  if (!ram_ || !video_ram_ || !crtc_ram_ || !key_table_) {
    Serial.printf("[MC1000][MEM] Allocation failed (RAM=%d VRAM=%d CRTC=%d KEY=%d); freeHeap=%u\n",
                  ram_ != nullptr, video_ram_ != nullptr,
                  crtc_ram_ != nullptr, key_table_ != nullptr,
                  (unsigned)ESP.getFreeHeap());
    free(ram_); ram_ = nullptr;
    free(video_ram_); video_ram_ = nullptr;
    free(crtc_ram_); crtc_ram_ = nullptr;
    delete[] key_table_; key_table_ = nullptr;
    return false;
  }
  for (int i=0;i<fabgl::VK_LAST;i++) key_table_[i] = {-1,0,-1};
  memset(key_rows_,0xff,sizeof(key_rows_));
  modifiers_=0xc0;
  host_shift_down_=false;
  joysticks_.reset();
  return true;
}

// FabGL 1.0.9 has no predefined Brazilian ABNT2 keyboard layout. The
// keyboard converter suppresses scancodes without a virtual-key definition,
// so inherit US and expose the additional ABNT2 slash contact (0x51). All
// remaining punctuation is translated using the actual physical scancode.
static const fabgl::KeyboardLayout MC1000_ABNT2_LAYOUT = {
  "BR", "Brazilian ABNT2", &fabgl::USLayout,
  { { 0x51, fabgl::VK_SLASH } }, {}, {}, {}, {}
};

void MC1000::SetKeyboardBR(bool enable) {
  keyboard_br_ = enable;
  // A layout switch must never leave a guest matrix contact pressed. FabGL
  // works with a read-only static KeyboardLayout; no library patch needed.
  fabgl::PS2Controller::keyboard()->setLayout(
      keyboard_br_ ? &MC1000_ABNT2_LAYOUT : &fabgl::USLayout);
  for (int vk = 0; vk < (int)fabgl::VK_LAST; ++vk)
    key_table_[vk].down = false;
  for (int i = 0; i < 128; ++i) br_scan_vk_[i] = -1;
  reset_key_chord_latched_ = false;
  joysticks_.releaseAll();
  RebuildGuestKeyboard();
}

void MC1000::InitKeyboard() {
  /* EN-US host keyboard mapped to the MC-1000 logical matrix.
     Both FabGL letter cases map to one contact. For shifted punctuation,
     follow jsMC1000's PC_FRIENDLY_MAPPING: translate the host symbol to the
     guest contact and explicitly set/clear guest SHIFT when required. */
  key_table_[fabgl::VK_LSHIFT]={0,0x40}; key_table_[fabgl::VK_RSHIFT]={0,0x40};
  key_table_[fabgl::VK_LCTRL]={0,0x80};  key_table_[fabgl::VK_RCTRL]={0,0x80};

  /* EN-US PC-friendly translation copied from jsMC1000Keyboard.PC_FRIENDLY_MAPPING.
     Shifted host symbols are translated to the corresponding MC-1000 matrix
     contact and, where required, override the guest SHIFT bit. */
  key_table_[fabgl::VK_H]={0,0x02}; key_table_[fabgl::VK_h]={0,0x02};
  key_table_[fabgl::VK_P]={0,0x04}; key_table_[fabgl::VK_p]={0,0x04};
  key_table_[fabgl::VK_X]={0,0x08}; key_table_[fabgl::VK_x]={0,0x08};
  key_table_[fabgl::VK_0]={0,0x10}; key_table_[fabgl::VK_RIGHTPAREN]={1,0x20,1};
  key_table_[fabgl::VK_8]={0,0x20}; key_table_[fabgl::VK_ASTERISK]={2,0x20,1};

  /* Y1: A I Q Y 1 9 */
  key_table_[fabgl::VK_A]={1,0x01}; key_table_[fabgl::VK_a]={1,0x01};
  key_table_[fabgl::VK_I]={1,0x02}; key_table_[fabgl::VK_i]={1,0x02};
  key_table_[fabgl::VK_Q]={1,0x04}; key_table_[fabgl::VK_q]={1,0x04};
  key_table_[fabgl::VK_Y]={1,0x08}; key_table_[fabgl::VK_y]={1,0x08};
  key_table_[fabgl::VK_1]={1,0x10}; key_table_[fabgl::VK_EXCLAIM]={1,0x10,1};
  key_table_[fabgl::VK_9]={1,0x20}; key_table_[fabgl::VK_LEFTPAREN]={0,0x20,1};

  /* Y2: B J R Z 2 : */
  key_table_[fabgl::VK_B]={2,0x01}; key_table_[fabgl::VK_b]={2,0x01};
  key_table_[fabgl::VK_J]={2,0x02}; key_table_[fabgl::VK_j]={2,0x02};
  key_table_[fabgl::VK_R]={2,0x04}; key_table_[fabgl::VK_r]={2,0x04};
  key_table_[fabgl::VK_Z]={2,0x08}; key_table_[fabgl::VK_z]={2,0x08};
  key_table_[fabgl::VK_2]={2,0x10};
  key_table_[fabgl::VK_AT]={0,0x01,0};
  key_table_[fabgl::VK_QUOTEDBL]={2,0x10,1};
  key_table_[fabgl::VK_COLON]={2,0x20,0};

  /* Y3: C K S ENTER 3 ; */
  key_table_[fabgl::VK_C]={3,0x01}; key_table_[fabgl::VK_c]={3,0x01};
  key_table_[fabgl::VK_K]={3,0x02}; key_table_[fabgl::VK_k]={3,0x02};
  key_table_[fabgl::VK_S]={3,0x04}; key_table_[fabgl::VK_s]={3,0x04};
  key_table_[fabgl::VK_RETURN]={3,0x08};
  key_table_[fabgl::VK_3]={3,0x10}; key_table_[fabgl::VK_HASH]={3,0x10,1};
  key_table_[fabgl::VK_SEMICOLON]={3,0x20,0};
  key_table_[fabgl::VK_PLUS]={3,0x20,1};

  /* Y4: D L T SPACE 4 , */
  key_table_[fabgl::VK_D]={4,0x01}; key_table_[fabgl::VK_d]={4,0x01};
  key_table_[fabgl::VK_L]={4,0x02}; key_table_[fabgl::VK_l]={4,0x02};
  key_table_[fabgl::VK_T]={4,0x04}; key_table_[fabgl::VK_t]={4,0x04};
  key_table_[fabgl::VK_SPACE]={4,0x08};
  key_table_[fabgl::VK_4]={4,0x10}; key_table_[fabgl::VK_DOLLAR]={4,0x10,1};
  key_table_[fabgl::VK_COMMA]={4,0x20};

  /* Y5: E M U BACKSPACE 5 - */
  key_table_[fabgl::VK_E]={5,0x01}; key_table_[fabgl::VK_e]={5,0x01};
  key_table_[fabgl::VK_M]={5,0x02}; key_table_[fabgl::VK_m]={5,0x02};
  key_table_[fabgl::VK_U]={5,0x04}; key_table_[fabgl::VK_u]={5,0x04};
  key_table_[fabgl::VK_BACKSPACE]={5,0x08};
  key_table_[fabgl::VK_5]={5,0x10}; key_table_[fabgl::VK_PERCENT]={5,0x10,1};
  key_table_[fabgl::VK_MINUS]={5,0x20};
  /* jsMC1000 PC_FRIENDLY_MAPPING intentionally has no Shift+- mapping.
     Do not alias VK_UNDERSCORE to the minus contact: on FabGL keyboards
     that turns Shift+- into '='.  '=' remains on its own mapped key. */
  key_table_[fabgl::VK_EQUALS]={5,0x20,1};

  /* Y6: F N V ^ 6 . */
  key_table_[fabgl::VK_F]={6,0x01}; key_table_[fabgl::VK_f]={6,0x01};
  key_table_[fabgl::VK_N]={6,0x02}; key_table_[fabgl::VK_n]={6,0x02};
  key_table_[fabgl::VK_V]={6,0x04}; key_table_[fabgl::VK_v]={6,0x04};
  key_table_[fabgl::VK_CARET]={6,0x08,0};
  key_table_[fabgl::VK_6]={6,0x10}; key_table_[fabgl::VK_AMPERSAND]={6,0x10,1};
  key_table_[fabgl::VK_PERIOD]={6,0x20};

  /* Y7: G O W RESET 7 / (hardware key contact bit 3). */
  key_table_[fabgl::VK_G]={7,0x01}; key_table_[fabgl::VK_g]={7,0x01};
  key_table_[fabgl::VK_O]={7,0x02}; key_table_[fabgl::VK_o]={7,0x02};
  key_table_[fabgl::VK_W]={7,0x04}; key_table_[fabgl::VK_w]={7,0x04};
  key_table_[fabgl::VK_F12]={7,0x08}; // the MC-1000's physical RESET key
  key_table_[fabgl::VK_7]={7,0x10};
  key_table_[fabgl::VK_QUOTE]={7,0x10,1};
  key_table_[fabgl::VK_SLASH]={7,0x20}; key_table_[fabgl::VK_QUESTION]={7,0x20};
  key_table_[fabgl::VK_LESS]={4,0x20};
  key_table_[fabgl::VK_GREATER]={6,0x20};
  /* Cursor arrows are not MC-1000 matrix contacts; leave them for controls. */
}

// Separate PS/2 controls, deliberately not aliases for the native MC-1000
// alphanumeric keys. Enabled players consume their respective host keys.
// P1: cursor arrows + RIGHT ALT (fire).
// P2: W A S D + SPACE (fire), both FabGL letter cases accepted.
// Each key-up is processed even while a browser/F1 menu is open, avoiding
// stuck directions if a menu is opened while a control is being held.
bool MC1000::ProcessJoystickKey(int vk, bool down) {
  MC1000Joystick::Control control = MC1000Joystick::Invalid;
  int player = -1;
  switch (vk) {
    case fabgl::VK_UP:    player = 0; control = MC1000Joystick::Up; break;
    case fabgl::VK_DOWN:  player = 0; control = MC1000Joystick::Down; break;
    case fabgl::VK_LEFT:  player = 0; control = MC1000Joystick::Left; break;
    case fabgl::VK_RIGHT: player = 0; control = MC1000Joystick::Right; break;
    case fabgl::VK_RALT:  player = 0; control = MC1000Joystick::Fire; break;
    case fabgl::VK_W: case fabgl::VK_w: player = 1; control = MC1000Joystick::Up; break;
    case fabgl::VK_S: case fabgl::VK_s: player = 1; control = MC1000Joystick::Down; break;
    case fabgl::VK_A: case fabgl::VK_a: player = 1; control = MC1000Joystick::Left; break;
    case fabgl::VK_D: case fabgl::VK_d: player = 1; control = MC1000Joystick::Right; break;
    case fabgl::VK_SPACE: player = 1; control = MC1000Joystick::Fire; break;
    default: break;
  }
  return player >= 0 && joysticks_.setPressed(player, control, down);
}

void MC1000::RebuildGuestKeyboard() {
  MC1000Keyboard::Rebuild(key_table_, (int)fabgl::VK_LAST,
                          (int)fabgl::VK_LSHIFT, (int)fabgl::VK_RSHIFT,
                          (int)fabgl::VK_LCTRL, (int)fabgl::VK_RCTRL,
                          key_rows_, modifiers_, host_shift_down_);
}

void MC1000::UpdateGuestKeyState(int vk, bool down) {
  if (vk < 0 || vk >= (int)fabgl::VK_LAST) return;
  KeyMat &key = key_table_[vk];
  if (key.row < 0 || key.down == down) return;
  key.down = down;
  RebuildGuestKeyboard();
}

void MC1000::ResetMachine() {
  tsave_queued_kind_ = 0;
  if (osd_tsave_on()) osd_cancel_tsave();
  ClearPendingBIN();
  memset(ram_,0,0xc000);
  memset(video_ram_,0,0x2000);
  memset(crtc_ram_,0,0x800);
  memset(key_rows_,0xff,sizeof(key_rows_));
  modifiers_=0xc0; host_shift_down_=false; key_latch_=0xff;
  joysticks_.reset();
  reset_key_chord_latched_ = false;
  for (int i = 0; i < 128; ++i) br_scan_vk_[i] = -1;
  for (int vk=0; vk<(int)fabgl::VK_LAST; ++vk) key_table_[vk].down = false;
  cassette_header_ready_ = false;
  selected_host_file_[0] = 0;
  osd_set_selected_cas("");
  selected_bin_file_[0] = 0;
  osd_set_selected_bin("");
  bin_auto_dialog_ = false;
  bin_prompt_canceled_ = false;
  cassette_prompt_canceled_ = false;
  save_empty_browser_ = false;
  matrix_new_file_requested_ = false;
  save_overwrite_approved_ = false;
  save_overwrite_denied_ = false;
  cassette_auto_dialog_ = false;
  matrix_prompt_done_ = false;
  matrix_load_active_ = false;
  cassette_file_[0] = 0;
  mc6847_.SetOverlayText("");
  rom0000_=1; mc6847_bank_=0; mc6845_bank_=0; mc6847_attr_=0;
  RefreshCpuMemoryPages();
  /* F1 menu full emulator reset also restores external VDG/PSG state. */
  mc6847_.SetModeAttr(0x00);
  mc6847_.SetColorBlocks(false); // reset defaults to the original MC-1000 hardware
  ay38910_.Reset();
  last_psg_loop_ms_ = millis();
  key_latch_=0xff;
  ResetZ80(&cpu_);
  cpu_.ICount = CPU_TICK_CYCLES;
  cpu_cycle_accum_=0; irq_cycle_accum_=0; tick_ms_=millis();
  if (fps_overlay_) RefreshFPSOverlay();
}

// Emulate the original keyboard's hardware RESET line. Unlike F10's full
// emulator reinitialization, the hardware signal resets the Z80 and re-enables
// the low ROM overlay without clearing physical RAM/VRAM or the VDG/PSG state.
// The ROM is responsible for reinitializing BASIC memory pointers afterwards.
void MC1000::ResetFromKeyboard() {
  tsave_queued_kind_ = 0;
  if (osd_tsave_on()) osd_cancel_tsave();
  ClearPendingBIN();
  // A TLOAD game may leave the AY tone/noise/mixer/envelope registers set.
  // Restart their boot state before the ROM starts its keyboard beep again.
  // Do NOT clear guest RAM/VRAM or reset the VDG: this is not F1 hard reset.
  ay38910_.Reset();
  key_latch_ = 0xff; // no keyboard row is initially selected by PSG port A
  last_psg_loop_ms_ = millis();
  rom0000_ = 1;
  RefreshCpuMemoryPages();
  ResetZ80(&cpu_);
  cpu_.ICount = CPU_TICK_CYCLES;
  cpu_cycle_accum_ = 0;
  irq_cycle_accum_ = 0;
  tick_ms_ = millis();
  cassette_header_ready_ = false;
  cassette_prompt_canceled_ = false;
  cassette_auto_dialog_ = false;
  matrix_prompt_done_ = false;
  matrix_load_active_ = false;
  save_empty_browser_ = false;
  matrix_new_file_requested_ = false;
  save_overwrite_approved_ = false;
  save_overwrite_denied_ = false;
  cassette_file_[0] = 0;
  // A reset discards the host cassette selection, matching F10 and the OSD.
  selected_host_file_[0] = 0;
  osd_set_selected_cas("");
  selected_bin_file_[0] = 0;
  osd_set_selected_bin("");
  bin_auto_dialog_ = false;
  bin_prompt_canceled_ = false;
}

bool MC1000::Init() {
  if (!InitMem()) return false;
  InitKeyboard();
  for (int i = 0; i < 128; ++i) br_scan_vk_[i] = -1;

  mc6847_.Init(video_ram_);
  video_resources_ready_ = true;
  mc6847_.SetCRTEffect(0); /* explicitly no scanlines */
  osd_init();
  ay38910_.Init(&sound_generator_);
  // PS/2 pins come from bootl.rc (kbdclk/kbddat), default 33/32 as FabGL.
  (new fabgl::Keyboard)->begin((gpio_num_t)g_bootrc.kbdclk, (gpio_num_t)g_bootrc.kbddat,
                               true, true);
  fabgl::PS2Controller::keyboard()->setLayout(&fabgl::USLayout);
  if (g_bootrc.magicb >= 0) pinMode(g_bootrc.magicb, INPUT_PULLUP);

  ResetMachine();
  cpu_cycles_total_.store(0, std::memory_order_relaxed);
  fps_.store(0, std::memory_order_relaxed);
  eps_.store(0, std::memory_order_relaxed);
  // The video task owns FPS_EPS_Meter; it resets on first enabled frame.
  // Allocate RAM for the user-provided ROM only at startup. Neither this source
  // nor the firmware binary contains any original CCE ROM bytes.
  rom_ = (uint8_t*)malloc(MC1000ROM::ROM_BYTES);
  if (!rom_) {
    Serial.println("[MC1000][ROM] ERROR: not enough memory for 16 KiB ROM");
    return false;
  }
  char actualPath[192];
  const MC1000ROM::Status result = MC1000ROM::load(SD_MOUNT_PATH, rom_,
                                        MC1000ROM::ROM_BYTES, actualPath, sizeof(actualPath));
  if (result != MC1000ROM::Status::OK) {
    Serial.printf("[MC1000][ROM] ERROR: %s. Required on SD: /MC-1000/ROMS/MC-1000.ROM\n",
                  MC1000ROM::statusText(result));
    free(rom_);
    rom_ = nullptr;
    return false;
  }
  RefreshCpuMemoryPages();
  Serial.printf("[MC1000][ROM] Loaded 16384 bytes from %s\n", actualPath);
  return true;
}

void MC1000::RefreshCpuMemoryPages() {
  memory_pages_.Configure(ram_, video_ram_, crtc_ram_, rom_, rom0000_ != 0,
                          mc6847_bank_ != 0, mc6845_bank_ != 0,
                          MC1000_RAM_SIZE_KB > 16);
  for (unsigned page = 0; page < 24; ++page) {
    z80ReadPage[page] = memory_pages_.ReadPage(page);
    z80WritePage[page] = memory_pages_.WritePage(page);
  }
}

void MC1000::BankSwitch() {
  /* The selected bank is represented in the address decoder below. */
}

void MC1000::WriteMem(int addr, int value) {
  addr &= 0xffff; value &= 0xff;
  // Fast page access for ordinary RAM/VRAM/CRTC writes. The high ROM
  // bank remains on the original decoder (write-protected).
  if (memory_pages_.Write((uint16_t)addr, (uint8_t)value)) return;
  if (addr < 0x2000) {
    if (!rom0000_) ram_[addr]=value;
  } else if (addr < 0x2800) {
    if (mc6845_bank_) crtc_ram_[addr-0x2000]=value;
    else ram_[0x2000 + (addr-0x2000)] = value;
  } else if (addr < 0x4000) {
    ram_[addr]=value;
  } else if (addr < 0x8000) {
#if MC1000_RAM_SIZE_KB > 16
    ram_[addr]=value;
#else
#if MC1000_DEBUG
    static bool logged_ext_write_4000 = false;
    if (!logged_ext_write_4000) {
      logged_ext_write_4000 = true;
      Serial.printf("[MC1000] UNMAPPED WRITE PC=%04X ADDR=%04X DATA=%02X (16K mode: no $4000-$7FFF RAM)\n", cpu_.PC.W, addr, value);
    }
#endif
#endif
  } else if (addr < 0xa000) {
    if (mc6847_bank_) {
#if MC1000_RAM_SIZE_KB > 16
      ram_[addr] = value;
#else
#if MC1000_DEBUG
      static bool logged_ext_write_8000 = false;
      if (!logged_ext_write_8000) {
        logged_ext_write_8000 = true;
        Serial.printf("[MC1000] UNMAPPED WRITE PC=%04X ADDR=%04X DATA=%02X (16K mode: bank 1 at $8000-$9FFF unavailable)\n", cpu_.PC.W, addr, value);
      }
#endif
#endif
    } else {
      const uint16_t off = (uint16_t)(addr - 0x8000);
      video_ram_[off] = (uint8_t)value;
    }
  } else if (addr < 0xc000) {
#if MC1000_RAM_SIZE_KB > 16
    ram_[addr]=value;
#else
#if MC1000_DEBUG
    static bool logged_ext_write_a000 = false;
    if (!logged_ext_write_a000) {
      logged_ext_write_a000 = true;
      Serial.printf("[MC1000] UNMAPPED WRITE PC=%04X ADDR=%04X DATA=%02X (16K mode: no $A000-$BFFF RAM)\n", cpu_.PC.W, addr, value);
    }
#endif
#endif
  }
}

int MC1000::ReadMem(int addr) {
  addr &= 0xffff;
  // The high ROM read MUST still turn off the low ROM mapping. All
  // remaining stable banks can be read without a chain of address tests.
  if (addr >= 0xc000) {
    if (rom0000_) { rom0000_ = 0; RefreshCpuMemoryPages(); }
    return rom_ ? rom_[addr - 0xc000] : 0xff;
  }
  uint8_t directValue;
  if (memory_pages_.Read((uint16_t)addr, directValue)) return directValue;
  if (addr < 0x2000) {
    return rom0000_ ? (rom_ ? rom_[addr] : 0xff) : ram_[addr];
  }
  if (addr < 0x2800) {
    return mc6845_bank_ ? crtc_ram_[addr-0x2000] : ram_[addr];
  }
  if (addr < 0x4000) return ram_[addr];
  if (addr < 0x8000) {
#if MC1000_RAM_SIZE_KB > 16
    return ram_[addr];
#else
#if MC1000_DEBUG
    static bool logged_ext_read_4000 = false;
    if (!logged_ext_read_4000) {
      logged_ext_read_4000 = true;
      Serial.printf("[MC1000] UNMAPPED READ PC=%04X ADDR=%04X (16K mode: no $4000-$7FFF RAM)\n", cpu_.PC.W, addr);
    }
#endif
    return 0xff;
#endif
  }
  if (addr < 0xa000) {
    if (!mc6847_bank_) return video_ram_[addr-0x8000];
#if MC1000_RAM_SIZE_KB > 16
    return ram_[addr];
#else
#if MC1000_DEBUG
    static bool logged_ext_read_8000 = false;
    if (!logged_ext_read_8000) {
      logged_ext_read_8000 = true;
      Serial.printf("[MC1000] UNMAPPED READ PC=%04X ADDR=%04X (16K mode: bank 1 at $8000-$9FFF unavailable)\n", cpu_.PC.W, addr);
    }
#endif
    return 0xff;
#endif
  }
  if (addr < 0xc000) {
#if MC1000_RAM_SIZE_KB > 16
    return ram_[addr];
#else
#if MC1000_DEBUG
    static bool logged_ext_read_a000 = false;
    if (!logged_ext_read_a000) {
      logged_ext_read_a000 = true;
      Serial.printf("[MC1000] UNMAPPED READ PC=%04X ADDR=%04X (16K mode: no $A000-$BFFF RAM)\n", cpu_.PC.W, addr);
    }
#endif
    return 0xff;
#endif
  }
  /* Reading the ROM banking window disables ROM at 0000-1FFF, matching
     the MAME banking behavior. */
  return 0xff; // unreachable (high ROM handled before page dispatch)
}

uint8_t MC1000::ReadKeyboardMatrix() {
  // Same AY-3-8910 port-A row latch / port-B scan as MAME's mc1000.
  // JOY0 is ANDed onto Y1, JOY1 onto Y0 (both active-low).
  return joysticks_.readMatrix(key_latch_, key_rows_, modifiers_);
}

void MC1000::WriteIO(int addr, int data) {
  addr &= 0xff; data &= 0xff;
  switch(addr) {
    case 0x04: printer_busy_ = data & 1; break;
    case 0x05: break;
    case 0x12:
      if (mc6845_bank_ != (data & 1)) {
        mc6845_bank_ = data & 1;
        RefreshCpuMemoryPages();
      }
      break;
    default: break;
  }
  switch (MC1000IO::deviceGroup(addr)) {
    case MC1000IO::kPsgControl:
      ay38910_.WrCtrl(data);
      break;
    case MC1000IO::kPsgData: {
      uint8_t reg = ay38910_.reg();
      if(reg==14) {
        key_latch_=data;
      }
      ay38910_.Write(data);
      break;
    }
    case MC1000IO::kVdg: {
      const bool paletteNeedsUpdate = mc6847_attr_ != data;
      mc6847_attr_=data;
      const uint8_t oldVideoBank = mc6847_bank_;
      mc6847_bank_=data & 1;
      if (oldVideoBank != mc6847_bank_) RefreshCpuMemoryPages();
      if (paletteNeedsUpdate) mc6847_.SetModeAttr(data);
#if MC1000_DEBUG
      Serial.printf("[MC1000] VDG PC=%04X ATTR=%02X BANK=%d VRAM=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    cpu_.PC.W, data, mc6847_bank_,
                    video_ram_[0], video_ram_[1], video_ram_[2], video_ram_[3],
                    video_ram_[4], video_ram_[5], video_ram_[6], video_ram_[7]);
#endif
      break;
    }
    default:
      break;
  }
}

int MC1000::ReadIO(int addr) {
  addr &= 0xff;
  if(addr==0x04) return printer_busy_;
  if(MC1000IO::deviceGroup(addr) == MC1000IO::kPsgRead) {
    if(ay38910_.reg()==15) return ReadKeyboardMatrix();
    return ay38910_.RdData();
  }
  return 0xff;
}

void MC1000::PollKeyboard() {
  if(--keyboard_timer_ > 0) return;
  keyboard_timer_ += KBD_PERIOD;
  PollMagicButton();
  auto kbd=fabgl::PS2Controller::keyboard();
  while(kbd->virtualKeyAvailable()) {
    VirtualKeyItem item;
    if(!kbd->getNextVirtualKey(&item)) break;
    int vk=(int)item.vk;
    // The MC-1000 has a RESET key distinct from the emulator's F10 shortcut.
    // F12 alone is readable in the real keyboard matrix at Y7/bit 3.
    // SHIFT+F12 is the documented physical reset; CTRL+F12 is an optional
    // PC-friendly shortcut requested for this emulator. Both sides work.
    // With the F1 menu open, F12 = "RESET EMULADOR (F12)".
    if (item.vk == fabgl::VK_F12 && osd_menu_on()) {
      if (item.down) {
        osd_close_menu();
        RefreshFPSOverlay();
        ResetMachine();
        osd_toast((char*)"MC-1000 REINICIADO", 0, 0);
      }
      continue;
    }
    const bool isGuestReset = item.vk == fabgl::VK_F12;
    const bool isModifier = item.vk == fabgl::VK_LSHIFT ||
                            item.vk == fabgl::VK_RSHIFT ||
                            item.vk == fabgl::VK_LCTRL ||
                            item.vk == fabgl::VK_RCTRL;
    if (isGuestReset || isModifier) {
      UpdateGuestKeyState(vk, item.down);
      const auto held = [this](int index) -> bool {
        return index >= 0 && index < (int)fabgl::VK_LAST && key_table_[index].down;
      };
      const bool chord = MC1000Keyboard::ResetChord(
          held(fabgl::VK_F12), held(fabgl::VK_LSHIFT),
          held(fabgl::VK_RSHIFT), held(fabgl::VK_LCTRL),
          held(fabgl::VK_RCTRL));
      if (MC1000Keyboard::ResetEdge(held(fabgl::VK_F12), chord,
                                     reset_key_chord_latched_)) {
        if (osd_menu_on()) { osd_close_menu(); RefreshFPSOverlay(); }
        else if (osd_overwrite_on()) osd_cancel_overwrite();
        else if (osd_dialog_on()) osd_close_dialog();
        ResetFromKeyboard();
        osd_toast((char*)"RESET PELO TECLADO", 0, 0);
        continue;
      }
      if (isGuestReset) continue; // never invoke an F-key OSD action
    }
    // Hardware keys released while F1/F2/F3 is visible must never stick.
    if (!item.down) ProcessJoystickKey(vk, false);
    if (osd_tsave_on()) {
      if (item.down) osd_process_tsave_key(item.vk);
      else UpdateGuestKeyState(vk, false);
      continue;
    }
    // A modal dialog blocks guest key presses, but releases must still clear
    // contacts held before it opened (including either Ctrl/Shift side).
    if (osd_overwrite_on()) {
      if (item.down) osd_process_overwrite_key(item.vk);
      else UpdateGuestKeyState(vk, false);
      continue;
    }
    // Keep the emulator reset shortcut independent of matrix mappings and
    // usable even when the file-browser dialog is open.
    if (item.vk == fabgl::VK_F1) {
      if (!item.down) { f1_key_latched_ = false; continue; }
      if (f1_key_latched_) continue;
      f1_key_latched_ = true;
      if (osd_menu_on()) {
        osd_close_menu();
        RefreshFPSOverlay();
      } else if (!osd_dialog_on()) {
        mc6847_.SetOverlayText("");
        osd_open_menu(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_, mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(), joysticks_.enabled(0), joysticks_.enabled(1));
      }
      continue;
    }
    if (osd_menu_on()) {
      if (!item.down) { UpdateGuestKeyState(vk, false); continue; }
      int action = osd_menu_process_key(item.vk);
      if (action == OSD_MENU_TOGGLE_FPS) {
        fps_overlay_.store(!fps_overlay_.load(std::memory_order_relaxed),
                           std::memory_order_release);
        mc6847_.SetOverlayText(""); // keep the menu unobstructed
        osd_menu_refresh(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_, mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(), joysticks_.enabled(0), joysticks_.enabled(1));
      } else if (action == OSD_MENU_TOGGLE_COLOR_ARTIFACTS) {
        mc6847_.CycleColorArtifactMode();
        osd_menu_refresh(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_,
                         mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(), joysticks_.enabled(0), joysticks_.enabled(1));
      } else if (action == OSD_MENU_TOGGLE_ALTERNATIVE_BORDERS) {
        mc6847_.SetAlternativeBorders(!mc6847_.AlternativeBorders());
        osd_menu_refresh(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_, mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(), joysticks_.enabled(0), joysticks_.enabled(1));
      } else if (action == OSD_MENU_TOGGLE_BLOCKS) {
        mc6847_.SetColorBlocks(!mc6847_.ColorBlocks());
        osd_menu_refresh(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_, mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(), joysticks_.enabled(0), joysticks_.enabled(1));
      } else if (action == OSD_MENU_TOGGLE_KEYBOARD) {
        SetKeyboardBR(!keyboard_br_);
        osd_menu_refresh(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_, mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(), joysticks_.enabled(0), joysticks_.enabled(1));
      } else if (action == OSD_MENU_TOGGLE_JOYSTICK_A) {
        joysticks_.setEnabled(0, !joysticks_.enabled(0));
        osd_menu_refresh(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_,
                         mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(),
                         joysticks_.enabled(0), joysticks_.enabled(1));
      } else if (action == OSD_MENU_TOGGLE_JOYSTICK_B) {
        // A WASD/SPACE key may already be held in BASIC when enabled. Drop
        // its old guest contact before routing further events to joystick B.
        if (!joysticks_.enabled(1)) {
          const int keys[] = {fabgl::VK_W, fabgl::VK_w, fabgl::VK_A, fabgl::VK_a,
                              fabgl::VK_S, fabgl::VK_s, fabgl::VK_D, fabgl::VK_d,
                              fabgl::VK_SPACE};
          for (unsigned i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)
            key_table_[keys[i]].down = false;
          RebuildGuestKeyboard();
        }
        joysticks_.setEnabled(1, !joysticks_.enabled(1));
        osd_menu_refresh(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_,
                         mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(),
                         joysticks_.enabled(0), joysticks_.enabled(1));
      } else if (action == OSD_MENU_OPEN_SD) {
        osd_close_menu();
        RefreshFPSOverlay();
        if (cas_directory_ready()) osd_open_dialog((char*)"CARREGAR CAS", (char*)"*.CAS", mc1000_dir_callback);
        else osd_toast((char*)"ERRO NA PASTA CAS", 0, 0);
      } else if (action == OSD_MENU_OPEN_BIN) {
        osd_close_menu();
        RefreshFPSOverlay();
        mc1000_open_bin_browser();
      } else if (action == OSD_MENU_INJECT_TXT) {
        osd_close_menu();
        RefreshFPSOverlay();
        mc1000_open_txt_browser();
      } else if (action == OSD_MENU_RESET) {
        osd_close_menu();
        ResetMachine();
        osd_toast((char*)"MC-1000 REINICIADO", 0, 0);
      } else if (action == OSD_MENU_RESTART_ESP32) {
        osd_close_menu();
        // Match ESPectrum's hard-reset path: force a full ESP32 reboot via RTC WDT.
        rtc_wdt_protect_off();
        rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
        rtc_wdt_set_time(RTC_WDT_STAGE0, 100);
        rtc_wdt_enable();
        rtc_wdt_protect_on();
        while (true) { }
      } else if (action == OSD_MENU_CLOSE) {
        osd_close_menu();
        RefreshFPSOverlay();
      }
      continue;
    }
    if (osd_dialog_on()) {
      // The browser owns presses; previously held keys still get released.
      if (item.down) osd_process_key(item.vk);
      else UpdateGuestKeyState(vk, false);
      continue;
    }
    // ABNT2 is a physical PS/2 keyboard. Translate set-2 punctuation from
    // jsmc1000's ABNT2 mapping, independently of FabGL's US symbols. Pin
    // the translated VK to the *physical* key until key-up, even if Shift
    // is released first; otherwise stuck matrix contacts can occur.
    if (keyboard_br_) {
      const uint8_t sc = MC1000Keyboard::ABNT2SimpleScancode(item.scancode);
      if (sc && sc < 128) {
        if (item.down) {
          const MC1000Keyboard::ABNT2Action action =
              MC1000Keyboard::TranslateABNT2(sc, item.SHIFT != 0);
          int target = vk;
          switch (action) {
            case MC1000Keyboard::ABNT2_IGNORE: target = -2; break;
            case MC1000Keyboard::ABNT2_APOSTROPHE: target = fabgl::VK_QUOTE; break;
            case MC1000Keyboard::ABNT2_DOUBLEQUOTE: target = fabgl::VK_QUOTEDBL; break;
            case MC1000Keyboard::ABNT2_SEMICOLON: target = fabgl::VK_SEMICOLON; break;
            case MC1000Keyboard::ABNT2_COLON: target = fabgl::VK_COLON; break;
            case MC1000Keyboard::ABNT2_SLASH: target = fabgl::VK_SLASH; break;
            case MC1000Keyboard::ABNT2_QUESTION: target = fabgl::VK_QUESTION; break;
            case MC1000Keyboard::ABNT2_CARET: target = fabgl::VK_CARET; break;
            default: break;
          }
          // Don't override an existing pressed assignment during repeats.
          if (br_scan_vk_[sc] == -1) br_scan_vk_[sc] = (int16_t)target;
          target = br_scan_vk_[sc];
          if (target == -2) continue;
          vk = target;
        } else if (br_scan_vk_[sc] != -1) {
          const int target = br_scan_vk_[sc];
          br_scan_vk_[sc] = -1;
          if (target == -2) continue;
          vk = target;
        }
      }
    }
    if (!keyboard_br_ && item.down && host_shift_down_ && item.vk == fabgl::VK_MINUS) {
      // Shift+- is intentionally unmapped in the US JavaScript reference.
      continue;
    }
    // The ROM's BASIC direct input buffer is $0200. Recognize the additional
    // host TSAVE command at Return and turn it into a valid REM before BASIC
    // tokenization. The Return itself still goes to the original ROM.
    if (item.down && item.vk == fabgl::VK_RETURN && !osd_dialog_on() &&
        ram_ && ram_[0x0106] == 0xff && QueueTSAVEFromGuest()) {
      // Do NOT consume the guest Return; GETL must finish normally.
    }
    // Do not also inject virtual joystick keys into the guest BASIC matrix.
    if (ProcessJoystickKey((int)item.vk, item.down)) {
      if (!item.down) UpdateGuestKeyState(vk, false); // clear any prior contact
      continue;
    }
    if (vk >= 0 && vk < (int)fabgl::VK_LAST && key_table_[vk].row >= 0)
      UpdateGuestKeyState(vk, item.down);
    else
      ProcessEmulatorKey(&item);
  }
}

// bootl.rc "magicb": active-low button that toggles the F1 menu, like F1.
void MC1000::PollMagicButton() {
  if (g_bootrc.magicb < 0) return;
  const bool pressed = digitalRead(g_bootrc.magicb) == LOW;
  const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  if (pressed == magic_raw_) {
    if (pressed != magic_stable_ && now - magic_change_ms_ >= 40) {
      magic_stable_ = pressed;
      if (pressed) {
        if (osd_menu_on()) {
          osd_close_menu();
          RefreshFPSOverlay();
        } else if (!osd_dialog_on()) {
          mc6847_.SetOverlayText("");
          osd_open_menu(fps_overlay_, mc6847_.ColorBlocks(), keyboard_br_, mc6847_.AlternativeBorders(), mc6847_.ColorArtifactMode(), joysticks_.enabled(0), joysticks_.enabled(1));
        }
      }
    }
  } else {
    magic_raw_ = pressed;
    magic_change_ms_ = now;
  }
}

void MC1000::RefreshFPSOverlay() {
  // Also invoked by the CPU task when the F1 menu changes state.
  // The VGA driver snapshots the text atomically; no guest VRAM writes.
  if (!fps_overlay_.load(std::memory_order_acquire) || osd_menu_on()) {
    mc6847_.SetOverlayText("");
    return;
  }
  // Only display the Z80-equivalent throughput. The first digit occupies
  // the original left edge of "FPS" in the top overscan (x=24); never draw
  // FPS, "EPS" or an extra blank prefix.
  const unsigned eps = eps_.load(std::memory_order_relaxed);
  char msg[8];
  snprintf(msg, sizeof(msg), "%u", eps);
  mc6847_.SetOverlayText(msg);
}

void MC1000::UpdateFPS() {
  // Called ONLY by the VGA task after a completed video service. Even if the
  // displayed MC6847 VRAM did not change, a complete output frame was serviced.
  // This matches the MSX counter's use of microseconds, independent of frame
  // skips or elapsed milliseconds rounded to the FreeRTOS tick period.
  const uint64_t nowUs = (uint64_t)esp_timer_get_time();
  const uint32_t cycles = cpu_cycles_total_.load(std::memory_order_relaxed);
  if (!fps_overlay_.load(std::memory_order_acquire)) {
    fps_was_enabled_on_video_ = false;
    return;
  }
  if (!fps_was_enabled_on_video_) {
    fps_was_enabled_on_video_ = true;
    fps_meter_.Reset(nowUs, cycles);
    fps_.store(0, std::memory_order_relaxed);
    eps_.store(0, std::memory_order_relaxed);
    RefreshFPSOverlay();
    return;
  }
  FPS_EPS_Meter::Result current;
  if (fps_meter_.Sample(nowUs, cycles, &current)) {
    fps_.store(current.fps, std::memory_order_relaxed);
    eps_.store(current.eps, std::memory_order_relaxed);
    RefreshFPSOverlay();
  }
}

void MC1000::ProcessEmulatorKey(VirtualKeyItem *item) {
  if (!item->down) return;
  /* F2 opens the cassette browser; F12 is the physical RESET key. */
  if (item->vk == fabgl::VK_F2) {
    if (cas_directory_ready()) osd_open_dialog((char*)"CARREGAR CAS", (char*)"*.CAS", mc1000_dir_callback);
    else osd_toast((char*)"ERRO NA PASTA CAS", 0, 0);
    return;
  }
  if (item->vk == fabgl::VK_F3) {
    mc1000_open_bin_browser();
    return;
  }
  if (item->vk == fabgl::VK_F4) {
    mc1000_open_txt_browser();
    return;
  }
}


// ROM cassette entry points (CCE MC-1000 ROM):
//   $C162 reads only the 14-byte tape header and returns early in BASIC mode
//   (HEAD=$FF). The BASIC caller then compares the requested name before
//   calling $C188 to read the two address words and the program body.
//   $C4B7 writes header + address words + body for SAVE.
// Intercepting $C162 as a *complete* LOAD made the ROM call $C188 afterwards
// and wait forever for cassette bits. This two-phase protocol is intentional.
void MC1000::ReturnFromTapeRoutine() {
  const uint16_t sp = cpu_.SP.W;
  const uint16_t ret = (uint16_t)ReadMem(sp) | ((uint16_t)ReadMem((sp + 1) & 0xffff) << 8);
  cpu_.SP.W = (uint16_t)(sp + 2);
  cpu_.PC.W = ret;
}

void MC1000::FinishHostSave(MC1000CAS::SaveOutcome outcome, const char *message) {
  const bool success = MC1000CAS::saveSucceeded(outcome);
  // A successful ROM tape-write returns A=1 and Z set. Failure/cancel are
  // distinct: never report the SAVE as successful to the BASIC caller.
  cpu_.AF.B.h = success ? 1 : 0;
  if (success) cpu_.AF.B.l |= Z_FLAG;
  else cpu_.AF.B.l &= (uint8_t)~Z_FLAG;
  if (message) {
    osd_toast((char*)message, 0, 0);
    Serial.printf("[MC1000][SAVE] %s (%s)\n", MC1000CAS::saveOutcomeName(outcome), message);
  }
  ReturnFromTapeRoutine();
}

void MC1000::SaveOverwriteDecision(bool approved) {
  save_overwrite_approved_ = approved;
  save_overwrite_denied_ = !approved;
}

void MC1000::HostSaveCassette() {
  const uint16_t start = (uint16_t)ram_[0x00fb] | ((uint16_t)ram_[0x00fc] << 8);
  const uint16_t end = (uint16_t)ram_[0x00fd] | ((uint16_t)ram_[0x00fe] << 8);
  if (end <= start || start < 0x0200 || end > 0xc000) {
    Serial.printf("[MC1000][SAVE] invalid range %04X-%04X\n", start, end);
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: FAIXA INVALIDA");
    return;
  }
  if (!cas_directory_ready()) {
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "CARTAO SD INDISPONIVEL");
    return;
  }

  // The cassette's *internal header* must remain the ROM's exact 14 bytes.
  // A normal named SAVE uses its BASIC filename even after a previous F9
  // selection. SAVE* uses the host selection if present; if no CAS exists,
  // it can create one named for the matrix in the ROM header.
  const uint16_t sp = cpu_.SP.W;
  const uint16_t ret = (uint16_t)ReadMem(sp) |
                       ((uint16_t)ReadMem((uint16_t)(sp+1)) << 8);
  const bool isMatrixSave = MC1000CAS::classifyTapeCall(0xC4B7, ret) == MC1000CAS::TAPE_MATRIX_SAVE;
  const String guestName = mc1000_cass_name(&ram_[0x018d]);
  const String name = isMatrixSave
      ? (MC1000CAS::useSelectedMatrixDestination(selected_host_file_[0] != 0,
             save_empty_browser_, matrix_new_file_requested_) ? String(selected_host_file_) : guestName)
      : (mc1000_guest_has_name(&ram_[0x018d]) ? guestName :
         ((selected_host_file_[0] && !save_empty_browser_) ? String(selected_host_file_) : guestName));
  // SAVE ARQUIVO -> ARQUIVO.CAS; SAVE ARQUIVO.TXT -> ARQUIVO.TXT.CAS;
  // SAVE ARQUIVO.CAS -> ARQUIVO.CAS (no duplicate suffix).
  // This affects only the SD filename, never the ROM's 14-byte tape header.
  char saveFilename[sizeof(selected_host_file_) + 8];
  if (!MC1000CAS::makeSaveFilename(name.c_str(), saveFilename, sizeof(saveFilename))) {
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: NOME INVALIDO");
    return;
  }
  String filename = saveFilename;
  // Resolve the real spelling on the SD card, including lowercase .cas.
  // Probe BEFORE opening TMP or replacing any data. FAT is case-insensitive.
  char actual[sizeof(saveFilename)];
  if (!cas_recover_named(filename.c_str())) {
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: ERRO RECUPERACAO");
    return;
  }
  const bool alreadyExists = cas_find_existing_filename(filename.c_str(), actual, sizeof(actual));
  if (alreadyExists) filename = actual;
  // osd_dialog_on() freezes the Z80 at the same ROM entrypoint until S or N.
  if (alreadyExists && !save_overwrite_approved_) {
    osd_open_overwrite_confirm(filename.c_str(), mc1000_overwrite_callback);
    return;
  }
  // The S decision belongs to this single save attempt only.
  save_overwrite_approved_ = false;
  // Only now execute the ROM's tape-side banking operation. On N no video/
  // RAM bank changes are left behind by a canceled SAVE.
  WriteIO(0x80, 0x01);
  // Write to a temporary file first, so an interrupted save will not replace
  // a previously valid cassette with a partially written one.
  // Reconcile any interrupted transaction before starting this SAVE. An
  // uncommitted, complete TMP may be the only good copy of a new cassette.
  if (!cas_recover_named(filename.c_str()) ||
      !cas_protect_old_temp(filename.c_str())) {
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: ERRO RECUPERACAO");
    return;
  }
  char tempName[sizeof(saveFilename)];
  if (!MC1000CAS::casSibling(filename.c_str(), ".TMP", tempName, sizeof(tempName))) {
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: NOME INVALIDO");
    return;
  }
  const String temporary = tempName;
  // Do not remove the previous TMP. It was parked as .REC0..REC9 above.
  reload_dir();
  FILE *f = file_browser()->openFile(temporary.c_str(), "wb");
  if (!f) {
    
    Serial.printf("[MC1000][SAVE] failed to open %s\n", temporary.c_str());
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: ERRO NO SD");
    return;
  }
  uint8_t hdr[MC1000CAS::HEADER_SIZE];
  MC1000CAS::encode(hdr, &ram_[0x018d], start, end);
  bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
  uint8_t block[256];
  for (uint32_t pos = start; ok && pos < end; ) {
    size_t chunk = end - pos;
    if (chunk > sizeof(block)) chunk = sizeof(block);
    for (size_t j = 0; j < chunk; ++j) {
      const uint16_t a = (uint16_t)(pos + j);
      block[j] = MC1000CAS::savedMemoryByte(a, ram_, video_ram_, crtc_ram_,
                                            mc6847_bank_ != 0, mc6845_bank_ != 0);
    }
    ok = fwrite(block, 1, chunk, f) == chunk;
    pos += chunk;
  }
  if (fflush(f) != 0) ok = false;
  if (fclose(f) != 0) ok = false;
  reload_dir();
  const size_t expected = sizeof(hdr) + (size_t)(end - start);
  // Size alone is not sufficient: read back the exact header and the body
  // bytes from the temporary CAS before any existing tape is replaced.
  // The ROM is stopped at its SAVE hook, so guest RAM/VRAM is stable here.
  if (ok && file_browser()->fileSize(temporary.c_str()) == expected) {
    FILE *check = file_browser()->openFile(temporary.c_str(), "rb");
    if (!check) ok = false;
    else {
      ok = MC1000CAS::verifySavedCassette(check, hdr, start, end,
                           ram_, video_ram_, crtc_ram_,
                           mc6847_bank_ != 0, mc6845_bank_ != 0);
      if (fclose(check) != 0) ok = false;
    }
  } else ok = false;
  if (!ok) {
    file_browser()->remove(temporary.c_str());
    
    Serial.printf("[MC1000][SAVE] write failed: %s expected=%u\n", temporary.c_str(), (unsigned)expected);
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: ERRO DE GRAVACAO");
    return;
  }
  // After flushing and validating a complete TMP, rename within the same
  // FAT filesystem. This avoids a second, interruptible copy of the body.
  // If an old file exists, TMP remains recoverable if renaming fails.
  char oldPath[512], newPath[512];
  snprintf(oldPath, sizeof(oldPath), "%s/%s", cas_directory_path(), temporary.c_str());
  snprintf(newPath, sizeof(newPath), "%s/%s", cas_directory_path(), filename.c_str());
  // FAT rename often refuses to replace an existing destination. Move the
  // original to an unused backup first; restore it if the TMP rename fails.
  bool hadOldFile = alreadyExists;
  String backup;
  char bakPath[512] = {0};
  if (hadOldFile) {
    for (unsigned i = 0; i < 10; ++i) {
      char candidateText[sizeof(saveFilename)];
      char suffix[8];
      snprintf(suffix, sizeof(suffix), ".BA%u", i);
      if (!MC1000CAS::casSibling(filename.c_str(), suffix,
                                candidateText, sizeof(candidateText))) break;
      const String candidate = candidateText;
      char actualBackup[sizeof(saveFilename)];
      if (!cas_find_existing_filename(candidate.c_str(), actualBackup, sizeof(actualBackup))) {
        backup = candidate;
        break;
      }
    }
    if (backup.length()) {
      snprintf(bakPath, sizeof bakPath, "%s/%s", cas_directory_path(), backup.c_str());
      if (rename(newPath, bakPath) != 0) ok = false;
    } else {
      ok = false;
    }
  }
  if (ok) ok = rename(oldPath, newPath) == 0;
  if (!ok && hadOldFile && backup.length() &&
      !file_browser()->exists(filename.c_str(), false)) {
    rename(bakPath, newPath); // recover previous intact cassette
  }
  reload_dir();
  ok = ok && file_browser()->fileSize(filename.c_str()) == expected;
  if (!ok && hadOldFile && backup.length() &&
      file_browser()->exists(backup.c_str(), false)) {
    // Even a valid rename can be followed by a failed size verification.
    // Preserve the original cassette instead of discarding the backup.
    if (file_browser()->exists(filename.c_str(), false)) file_browser()->remove(filename.c_str());
    rename(bakPath, newPath);
  } else if (ok && hadOldFile && backup.length()) {
    file_browser()->remove(backup.c_str());
  }
  reload_dir();
  if (!ok) {
    FinishHostSave(MC1000CAS::SaveOutcome::Error, "SAVE: ERRO VERIFICACAO");
    return;
  }
  if (isMatrixSave && matrix_new_file_requested_) {
    // A newly created matrix cassette becomes the selected file only after
    // a successful, validated commit. On error, preserve the old F9 marker.
    SelectBrowserFile(filename.c_str());
  }
  matrix_new_file_requested_ = false;
  Serial.printf("[MC1000][SAVE] OK %s start=%04X end=%04X bytes=%u\n",
                filename.c_str(), start, end, (unsigned)expected);
  // Preserve the original successful tape-call register values.
  cpu_.HL.W = end;
  cpu_.DE.W = end;
  FinishHostSave(MC1000CAS::SaveOutcome::Success, "CAS SALVO");
}

void MC1000::SelectBrowserFile(const char *filename) {
  // Preserve the actual FAT spelling when accepting a browser selection;
  // never uppercase a physical filename or strip only uppercase ".CAS".
  char proposedBase[sizeof(selected_host_file_)];
  if (!MC1000CAS::selectedCASBaseName(filename, proposedBase, sizeof(proposedBase))) {
    osd_toast((char*)"NOME CAS MUITO LONGO", 0, 0);
    return;
  }
  char proposedFile[sizeof(cassette_file_)];
  char actualFile[sizeof(cassette_file_)];
  char actualBase[sizeof(selected_host_file_)];
  if (!MC1000CAS::makeSaveFilename(proposedBase, proposedFile, sizeof(proposedFile)) ||
      !cas_find_existing_filename(proposedFile, actualFile, sizeof(actualFile)) ||
      !MC1000CAS::selectedCASBaseName(actualFile, actualBase, sizeof(actualBase))) {
    osd_toast((char*)"CAS NAO ENCONTRADO", 0, 0);
    return;
  }
  memcpy(selected_host_file_, actualBase, strlen(actualBase) + 1);
  // F9 selects a *host filename*, not a cassette header. Do not overwrite
  // $018D: SAVE* uses the matrix name prepared by BASIC here. On LOAD/LOAD*,
  // HostPrepareCassetteHeader() supplies the ROM's name comparison buffer.
  cassette_header_ready_ = false;
  cassette_prompt_canceled_ = false;
  save_empty_browser_ = false;
  matrix_new_file_requested_ = false;
  // Selection marker refers to the physical filename, not its uppercase
  // and possibly truncated presentation in the F9 rows.
  osd_set_selected_cas(actualFile);
  Serial.printf("[MC1000][CAS] selected %s\n", selected_host_file_);
  osd_toast((char*)"CAS SELECIONADO", 0, 0);
}

void MC1000::InjectBasicTXT(const char *filename) {
  // Invoked on the CPU task, after the F4 dialog has closed. No Z80
  // instruction can run between validation and the atomic RAM commit.
  if (!filename || !*filename || !txt_directory_ready()) {
    osd_toast((char*)"ERRO NA PASTA TXT", 0, 0);
    return;
  }
  // FileBrowser names retain physical FAT capitalization. Reject paths
  // and extensions outside the TXT browser even if a callback is misused.
  const char *base = strrchr(filename, '/');
  base = base ? base + 1 : filename;
  if (!*base || strchr(base, '\\')) {
    osd_toast((char*)"NOME TXT INVALIDO", 0, 0);
    return;
  }
  const size_t n = strlen(base);
  if (n < 5 || (strcasecmp(base + n - 4, ".TXT") != 0 &&
                strcasecmp(base + n - 4, ".BAS") != 0)) {
    osd_toast((char*)"NOME TXT INVALIDO", 0, 0);
    return;
  }
  fabgl::FileBrowser *browser = txt_file_browser();
  FILE *fp = browser ? browser->openFile(base, "rb") : nullptr;
  if (!fp) {
    osd_toast((char*)"ERRO AO LER TXT", 0, 0);
    return;
  }
  const MC1000Basic::Result result = MC1000Basic::inject(fp, ram_, 0xC000);
  fclose(fp);
  switch (result.status) {
    case MC1000Basic::Status::OK:
      osd_toast((char*)"PROGRAMA INJETADO", 0, 0);
      Serial.printf("[MC1000][TXT] Injected %u lines, %u bytes from %s\n",
                    result.lines, (unsigned)result.bytes, base);
      break;
    case MC1000Basic::Status::InvalidFormat:
      osd_toast((char*)"FORMATO BASIC INCORRETO", 0, 0);
      break;
    case MC1000Basic::Status::TooLarge:
      osd_toast((char*)"PROGRAMA TXT MUITO GRANDE", 0, 0);
      break;
    case MC1000Basic::Status::NotReady:
      osd_toast((char*)"BASIC NAO INICIADO", 0, 0);
      break;
    case MC1000Basic::Status::NoMemory:
      osd_toast((char*)"SEM MEMORIA PARA INJETAR", 0, 0);
      break;
    default:
      osd_toast((char*)"ERRO AO LER TXT", 0, 0);
      break;
  }
}

void MC1000::BrowserNewFile() {
  if (!cassette_auto_dialog_ || cpu_.PC.W != 0xC4B7 ||
      !MC1000CAS::guestHeaderHasName(&ram_[0x018d])) {
    cassette_prompt_canceled_ = true;
    return;
  }
  // N in SAVE* F9 means create a new CAS using the ROM's matrix name.
  // Keep the old F9 selection until the new file has been saved correctly.
  matrix_new_file_requested_ = true;
  save_empty_browser_ = false;
  cassette_prompt_canceled_ = false;
}

void MC1000::BrowserSelectionCanceled() {
  // ESC in a manually opened F9 must not cancel a later BASIC tape command.
  if (!cassette_auto_dialog_) return;
  cassette_prompt_canceled_ = true;
  save_empty_browser_ = false;
  matrix_new_file_requested_ = false;
}

void MC1000::SelectBrowserBIN(const char *filename) {
  if (!filename || !*filename || !bin_directory_ready()) {
    BrowserBINCanceled(); return;
  }
  const char *base = strrchr(filename, '/');
  base = base ? base + 1 : filename;
  const size_t n = strlen(base);
  if (n <= 4 || strcasecmp(base + n - 4, ".BIN") != 0 ||
      n >= sizeof(selected_bin_file_)) {
    BrowserBINCanceled();
    osd_toast((char*)"NOME BIN INVALIDO", 0, 0);
    return;
  }
  char actual[sizeof(selected_bin_file_)];
  if (!bin_find_existing_filename(base, actual, sizeof(actual))) {
    BrowserBINCanceled();
    osd_toast((char*)"BIN NAO ENCONTRADO", 0, 0);
    return;
  }
  strcpy(selected_bin_file_, actual);
  osd_set_selected_bin(actual);
  bin_auto_dialog_ = false;
  bin_prompt_canceled_ = false;
  Serial.printf("[MC1000][BIN] selected %s\n", selected_bin_file_);
  osd_toast((char*)"BIN SELECIONADO", 0, 0);
}

void MC1000::BrowserBINCanceled() {
  if (bin_auto_dialog_) bin_prompt_canceled_ = true;
  bin_auto_dialog_ = false;
  // An explicit F3 cancellation must not erase a prior valid selection.
}

// Detect TSAVE only on an actual Return press while the MC-1000 is in BASIC
// mode. Do not inspect arbitrary user program lines or execute from a timer.
bool MC1000::QueueTSAVEFromGuest() {
  if (tsave_queued_kind_ || tsave_stage_) return false;
  char line[256];
  memcpy(line, ram_ + 0x0200, 255);
  line[255] = 0;
  MC1000TSAVE::Spec spec={};
  const auto kind=MC1000TSAVE::parse(line,&spec);
  if (kind == MC1000TSAVE::ParseResult::NotCommand) return false;
  // Preserve the remainder as a comment so BASIC accepts the line. The ROM's
  // line editor writes the terminating NUL when it receives the real Return.
  size_t pos=0;
  while (pos < 249 && (ram_[0x200+pos]==' ' || ram_[0x200+pos]=='\t')) ++pos;
  memcpy(ram_ + 0x0200 + pos,"REM  ",5);
  tsave_queued_at_ms_ = millis();
  tsave_queued_kind_ = kind == MC1000TSAVE::ParseResult::Help ? 2 : 1;
  strncpy(tsave_queued_command_, line, sizeof(tsave_queued_command_)-1);
  tsave_queued_command_[sizeof(tsave_queued_command_)-1]=0;
  Serial.printf("[MC1000][TSAVE] accepted BASIC command (kind=%u)\n",
                (unsigned)tsave_queued_kind_);
  return true;
}

void MC1000::ClearPendingBIN() {
  free(tsave_stage_);
  tsave_stage_ = nullptr;
  tsave_stage_len_ = 0;
  tsave_filename_[0] = 0;
  tsave_overwrite_exists_ = false;
}

void MC1000::CommitPendingBIN(bool replaceExisting) {
  if (!tsave_stage_ || !tsave_filename_[0]) return;
  const bool ok = bin_directory_ready() &&
       MC1000TSAVE::commit(bin_directory_path(), tsave_filename_,
                            tsave_stage_, tsave_stage_len_, replaceExisting);
  char filename[sizeof(tsave_filename_)];
  strcpy(filename, tsave_filename_);
  const size_t savedLength = tsave_stage_len_;
  ClearPendingBIN();
  if (!ok) {
    osd_toast((char*)"TSAVE: ERRO AO SALVAR",0,0);
    Serial.printf("[MC1000][TSAVE] commit failed for %s\n",filename);
    return;
  }
  strncpy(selected_bin_file_, filename, sizeof(selected_bin_file_)-1);
  selected_bin_file_[sizeof(selected_bin_file_)-1] = 0;
  osd_set_selected_bin(selected_bin_file_);
  if (fabgl::FileBrowser *browser = bin_file_browser()) browser->reload();
  osd_toast((char*)"BIN SALVO COM SUCESSO",0,0);
  Serial.printf("[MC1000][TSAVE] saved %s bytes=%u\n", filename,
                (unsigned)savedLength);
}

void MC1000::SaveBINOverwriteDecision(bool approved) {
  if (!approved) {
    ClearPendingBIN();
    osd_toast((char*)"TSAVE CANCELADO",0,0);
    return;
  }
  CommitPendingBIN(true);
}

void MC1000::HostSaveBINCommand(const char *command) {
  MC1000TSAVE::Spec spec={};
  if (!command || MC1000TSAVE::parse(command,&spec) != MC1000TSAVE::ParseResult::Valid) {
    osd_toast((char*)"TSAVE: PARAMETROS INVALIDOS",0,0);
    Serial.println("[MC1000][TSAVE] invalid arguments");
    return;
  }
  if (!bin_directory_ready()) {
    osd_toast((char*)"TSAVE: ERRO NA PASTA BIN",0,0);
    return;
  }
  ClearPendingBIN();
  const size_t len=MC1000TSAVE::payloadLength(spec);
  uint8_t *data=(uint8_t*)malloc(len);
  if (!data) {
    osd_toast((char*)"TSAVE: SEM MEMORIA",0,0);
    return;
  }
  if (!MC1000TSAVE::build(spec,ram_,data,len)) {
    free(data);
    osd_toast((char*)"TSAVE: BIN INVALIDO",0,0);
    return;
  }
  char actual[sizeof(tsave_filename_)];
  const bool exists=bin_find_existing_filename(spec.filename,actual,sizeof(actual));
  tsave_stage_=data;
  tsave_stage_len_=len;
  tsave_overwrite_exists_=exists;
  strcpy(tsave_filename_, exists ? actual : spec.filename);
  if (exists) {
    osd_open_overwrite_confirm_bin(tsave_filename_, mc1000_tsave_overwrite_callback);
    return;
  }
  CommitPendingBIN(false);
}

// Atomic validation and load of the original TLOAD image into guest RAM.
// The 18-byte legacy envelope is not copied. $0100..$01FF carries system
// variables (HEAD/PLAY/PLAYMX and game hooks); $0200 begins Z80 code.
bool MC1000::HostLoadBIN() {
  if (!bin_directory_ready() || !selected_bin_file_[0]) {
    osd_toast((char*)"BIN NAO SELECIONADO", 0, 0);
    return false;
  }
  char actual[sizeof(selected_bin_file_)];
  if (!bin_find_existing_filename(selected_bin_file_, actual, sizeof(actual))) {
    osd_toast((char*)"BIN NAO ENCONTRADO", 0, 0);
    return false;
  }
  fabgl::FileBrowser *browser = bin_file_browser();
  FILE *f = browser ? browser->openFile(actual, "rb") : nullptr;
  if (!f) {
    osd_toast((char*)"BIN: ERRO NO SD", 0, 0);
    return false;
  }
  bool ok = fseek(f, 0, SEEK_END) == 0;
  const long fileLen = ok ? ftell(f) : -1;
  if (fileLen <= 0 || fileLen > (long)(MC1000BIN::kMaxLength + 18) ||
      fseek(f, 0, SEEK_SET) != 0) ok = false;
  uint8_t *staging = ok ? (uint8_t*)malloc((size_t)fileLen) : nullptr;
  if (!staging) ok = false;
  if (ok) ok = fread(staging, 1, (size_t)fileLen, f) == (size_t)fileLen;
  if (fclose(f) != 0) ok = false;
  MC1000BIN::Image image = {};
  if (ok) ok = MC1000BIN::identify(staging, (size_t)fileLen, &image);
  if (!ok) {
    free(staging);
    osd_toast((char*)"BIN INVALIDO OU SEM MEMORIA", 0, 0);
    Serial.printf("[MC1000][BIN] rejected %s bytes=%ld\n", actual, fileLen);
    return false;
  }
  // Stage was completely read and validated: no partial RAM corruption.
  memcpy(ram_ + MC1000BIN::kGameRAMStart, staging + image.offset, image.length);
  free(staging);
  rom0000_ = 0; // execute program at $0200, not low ROM overlay
  RefreshCpuMemoryPages();
  WriteIO(0x80, 0x01);
  // Authentic ROM TLOAD enters START with SP=PC=$0200.
  cpu_.SP.W = 0x0200;
  cpu_.HL.W = 0x0200;
  cpu_.AF.B.h = 0;
  cpu_.PC.W = 0x0200;
  Serial.printf("[MC1000][BIN] TLOAD %s offset=%u len=%u entry=0200\n",
                actual, (unsigned)image.offset, (unsigned)image.length);
  return true;
}

void MC1000::BrowserDialogDone() {
  cassette_auto_dialog_ = false;
}

void MC1000::BrowserNoFiles() {
  if (!cassette_auto_dialog_) return;
  // SAVE must still be able to create the very first CAS on a blank card.
  // We attempted F9 as requested, but the browser has no entry to select;
  // in this one case retain the cassette name already provided by BASIC.
  save_empty_browser_ = cpu_.PC.W == 0xC4B7;
  cassette_prompt_canceled_ = !save_empty_browser_;
}

bool MC1000::HostPrepareCassetteHeader() {
  cassette_header_ready_ = false;
  cassette_file_[0] = 0;
  if (!cas_directory_ready()) {
    osd_toast((char*)"CARTAO SD INDISPONIVEL", 0, 0);
    Serial.println("[MC1000][CAS] card not mounted");
    return false;
  }
  // For LOAD "NAME", the explicit BASIC filename has priority over an
  // earlier F9 selection. LOAD* always uses the current F9 choice.
  const bool selected = selected_host_file_[0] != 0 &&
                        (!MC1000CAS::guestHeaderHasName(&ram_[0x018d]) ||
                         matrix_load_active_);
  String wanted = selected ? String(selected_host_file_)
                           : mc1000_cass_name(&ram_[0x018d]);
  char proposed[sizeof(cassette_file_)];
  char actual[sizeof(cassette_file_)];
  FILE *f = nullptr;
  String opened;
  if (MC1000CAS::makeSaveFilename(wanted.c_str(), proposed, sizeof(proposed)) &&
      cas_recover_named(proposed) &&
      cas_find_existing_filename(proposed, actual, sizeof(actual))) {
    opened = actual; // use exact on-disk spelling, including lowercase .cas
    f = file_browser()->openFile(opened.c_str(), "rb");
  }
  // Never implicitly load the first file. An F9 choice must be explicit.
  if (!f) {
    osd_toast((char*)"CAS NAO ENCONTRADO", 0, 0);
    Serial.printf("[MC1000][CAS] not found: %s\n", wanted.c_str());
    return false;
  }
  uint8_t bytes[MC1000CAS::HEADER_SIZE];
  bool ok = fseek(f, 0, SEEK_END) == 0;
  const long fileLen = ok ? ftell(f) : -1;
  if (fileLen < 0 || fseek(f, 0, SEEK_SET) != 0) ok = false;
  if (ok) ok = fread(bytes, 1, sizeof(bytes), f) == sizeof(bytes);
  fclose(f);
  MC1000CAS::Header header;
  if (!ok || !MC1000CAS::decode(bytes, (uint32_t)(fileLen < 0 ? 0 : fileLen), &header)) {
    osd_toast((char*)"CAS INVALIDO", 0, 0);
    Serial.printf("[MC1000][CAS] invalid image: %s bytes=%ld\n", opened.c_str(), fileLen);
    return false;
  }
  for (int i = 0; i < 14; ++i) ram_[0x017f + i] = header.name[i];
  // With F9, the selected disk filename need not equal the 14-byte tape
  // header. The BASIC loader compares the tape header to $018D (first 5
  // chars), so make this explicit browser choice match the original name.
  if (selected) {
    for (int i = 0; i < 14; ++i) ram_[0x018d + i] = header.name[i];
  }
  strncpy(cassette_file_, opened.c_str(), sizeof(cassette_file_) - 1);
  cassette_file_[sizeof(cassette_file_) - 1] = 0;
  cassette_file_start_ = header.start;
  cassette_file_end_ = header.end;
  cassette_header_ready_ = true;
  // Keep F9 selection sticky (and its asterisk) until another selection or reset.
  Serial.printf("[MC1000][CAS] header %s start=%04X end=%04X body=%u BASIC=%u\n",
                cassette_file_, header.start, header.end, (unsigned)header.length,
                (unsigned)(ram_[0x0106] == 0xff));
  return true;
}

bool MC1000::HostLoadCassetteData() {
  if (!cassette_header_ready_) {
    osd_toast((char*)"LOAD: SEM CABECALHO", 0, 0);
    return false;
  }
  // A file can be renamed or re-cased on the SD card between the header
  // and body phases. Re-resolve its physical spelling before reopening.
  char actual[sizeof(cassette_file_)];
  FILE *f = nullptr;
  if (cas_find_existing_filename(cassette_file_, actual, sizeof(actual)))
    f = file_browser()->openFile(actual, "rb");
  if (!f) {
    cassette_header_ready_ = false;
    osd_toast((char*)"LOAD: ERRO NO SD", 0, 0);
    return false;
  }
  // C07: check the real, current file size in *both* ROM LOAD phases.
  // Never pass a fabricated expected size to the CAS validator.
  bool ok = fseek(f, 0, SEEK_END) == 0;
  const long fileLen = ok ? ftell(f) : -1;
  if (fileLen < 0 || fseek(f, 0, SEEK_SET) != 0) ok = false;
  uint8_t h[MC1000CAS::HEADER_SIZE];
  if (ok) ok = fread(h, 1, sizeof(h), f) == sizeof(h);
  const uint16_t start = cassette_file_start_;
  const uint16_t end = cassette_file_end_;
  MC1000CAS::Header header;
  if (!ok || !MC1000CAS::decode(h, (uint32_t)(fileLen < 0 ? 0 : fileLen), &header) ||
      header.start != start || header.end != end) {
    fclose(f);
    cassette_header_ready_ = false;
    osd_toast((char*)"LOAD: CABECALHO INVALIDO", 0, 0);
    return false;
  }
  const size_t length = (size_t)(end - start);
  // $FB is the current *destination* chosen by BASIC. The CAS header's
  // $start/$end identify the recorded range and its LENGTH; unlike cassette
  // emulation, we must explicitly update the ROM's $FD end pointer.
  const uint16_t dest = (uint16_t)ram_[0xfb] | ((uint16_t)ram_[0xfc] << 8);
  const uint32_t destEnd = (uint32_t)dest + length;
  // LOAD* must not overwrite neighbouring BASIC variables or arrays if the
  // user selects a program CAS or a matrix with different dimensions.
  // BASIC already established the exact target array byte range at $FB-$FE.
  if (matrix_load_active_ && !MC1000CAS::matrixLengthFits(dest,
                           (uint16_t)ram_[0xfd] | ((uint16_t)ram_[0xfe] << 8), length)) {
    fclose(f);
    cassette_header_ready_ = false;
    osd_toast((char*)"TAMANHO DA MATRIZ DIFERENTE", 0, 0);
    Serial.printf("[MC1000][LOAD*] incompatible array length=%u dest=%04X targetEnd=%04X\n",
                  (unsigned)length, dest,
                  (unsigned)((uint16_t)ram_[0xfd] | ((uint16_t)ram_[0xfe] << 8)));
    return false;
  }
  if (dest < 0x0200 || dest >= 0xc000 || destEnd > 0xc000) {
    fclose(f);
    cassette_header_ready_ = false;
    osd_toast((char*)"LOAD: FAIXA RAM INVALIDA", 0, 0);
    Serial.printf("[MC1000][CAS] RAM range start=%04X len=%u\n", dest, (unsigned)length);
    return false;
  }
  uint8_t *data = (uint8_t*)malloc(length);
  if (!data) {
    fclose(f);
    cassette_header_ready_ = false;
    osd_toast((char*)"LOAD: SEM MEMORIA", 0, 0);
    return false;
  }
  ok = fread(data, 1, length, f) == length;
  if (fclose(f) != 0) ok = false;
  if (!ok) {
    free(data);
    cassette_header_ready_ = false;
    osd_toast((char*)"LOAD: ERRO DE LEITURA", 0, 0);
    return false;
  }
  // Check every mapped page BEFORE modifying any guest memory. A fully
  // staged CAS can then be copied through the active RAM/VRAM/CRTC banks.
  // Low RAM retains the existing BASIC loader behavior: disable the low
  // ROM overlay only after a successful copy.
  if (!MC1000CAS::copyCassetteToMappedMemory(memory_pages_, ram_, dest, data, length)) {
    free(data);
    cassette_header_ready_ = false;
    osd_toast((char*)"LOAD: BANCO INVALIDO", 0, 0);
    return false;
  }
  free(data);
  if (dest < 0x2000) { rom0000_ = 0; RefreshCpuMemoryPages(); }
  ram_[0xfd] = (uint8_t)destEnd;
  ram_[0xfe] = (uint8_t)(destEnd >> 8);
  last_cas_end_ = (uint16_t)destEnd;
  cassette_header_ready_ = false;
  // The skipped ROM path finishes with OUT ($80),$01.
  WriteIO(0x80, 0x01);
  Serial.printf("[MC1000][CAS] load OK %s range=%04X-%04X -> %04X-%04X len=%u\n",
                cassette_file_, start, end, dest, (unsigned)destEnd, (unsigned)length);
  return true;
}

// Duplicate the ROM's final tape-read register/flag state for its BASIC
// callers. The ROM uses RET Z in BASIC mode and JR NZ to retry after $C188.
void MC1000::CompleteCassetteLoad(bool success) {
  cpu_.AF.B.h = 1;
  cpu_.AF.B.l = (uint8_t)((cpu_.AF.B.l & ~(Z_FLAG | C_FLAG)) |
                         (ram_[0x0106] == 0xff ? Z_FLAG : 0));
  if (success) {
    cpu_.HL.W = last_cas_end_;
    cpu_.DE.W = last_cas_end_;
  } else {
    // Prevent the BASIC caller from replacing its program-end pointer with
    // the cassette-header buffer ($018D) after a missing/unreadable file.
    // There is no physical tape stream to retry here, so return with the
    // existing end-of-program marker and keep the message visible.
    uint16_t oldEnd = (uint16_t)ram_[0x03b7] | ((uint16_t)ram_[0x03b8] << 8);
    if (oldEnd < 0x0200 || oldEnd >= 0xc000)
      oldEnd = (uint16_t)ram_[0xfb] | ((uint16_t)ram_[0xfc] << 8);
    cpu_.HL.W = oldEnd;
    cpu_.DE.W = oldEnd;
    cpu_.AF.B.l |= Z_FLAG;
  }
  ReturnFromTapeRoutine();
}

void MC1000::Run() {
  int64_t nextCpuUs = esp_timer_get_time() + 1000;
  int64_t lastIdleUs = esp_timer_get_time();
#if MC1000_DEBUG
  uint32_t dbg_last = millis();
#endif
  // Count Z80 T-states locally and publish once per ~1ms emulated slice.
  // Publishing an atomic on every *instruction* needlessly slows the guest.
  uint32_t cyclesPending = 0;
  while(true) {
    if (tsave_queued_kind_ &&
        (uint32_t)(millis() - tsave_queued_at_ms_) >= 250 &&
        !osd_dialog_on()) {
      const uint8_t kind = tsave_queued_kind_;
      tsave_queued_kind_ = 0;
      if (kind == 2) {
        osd_open_tsave_editor(mc1000_tsave_editor_callback);
      } else {
        HostSaveBINCommand(tsave_queued_command_);
      }
    }
    // All guest execution is stopped when an F1/F9 or SAVE confirmation is open.
    if (osd_dialog_on()) {
      if (cyclesPending) {
        cpu_cycles_total_.fetch_add(cyclesPending, std::memory_order_relaxed);
        cyclesPending = 0;
      }
      const uint32_t psgNowMs = millis();
      const uint32_t psgElapsedMs = psgNowMs - last_psg_loop_ms_;
      if (psgElapsedMs) {
        ay38910_.Loop((int)psgElapsedMs);
        last_psg_loop_ms_ = psgNowMs;
      }
      PollKeyboard();
      vTaskDelay(1);
      nextCpuUs = esp_timer_get_time() + 1000;
      continue;
    }
    // Avoid evaluating ROM cassette state, SD selection and array flags for
    // every ordinary Z80 instruction. Enter this block only at the four ROM
    // entrypoints used by LOAD/SAVE and their matrix counterparts.
    if (MC1000CAS::isTapeInterceptPC(cpu_.PC.W)) {
    // Cassette entrypoints still run before executing the matching opcode.
    // The expensive host clock / AY / toast service is handled per CPU slice.
    // Both normal and star-suffixed BASIC commands reach these ROM tape
    // entrypoints. The ROM's *array* callers are distinguished by the return
    // address pushed on the Z80 stack: DED9 (SAVE*), DECF (LOAD* header),
    // DED7 (LOAD* body). The star command initializes at DE8A exactly once.
    // Re-prompt on every new SAVE*/LOAD*, even if a previous F9 selection
    // exists; do not reopen during LOAD*'s header/body loop.
    if (cpu_.PC.W == 0xDE8A) {
      matrix_new_file_requested_ = false;
      matrix_prompt_done_ = false;
      matrix_load_active_ = false;
      cassette_prompt_canceled_ = false;
      save_empty_browser_ = false;
    }
    MC1000CAS::TapeCall tapeCall = MC1000CAS::TAPE_NONE;
    if (cpu_.PC.W == 0xC4B7 || cpu_.PC.W == 0xC162 || cpu_.PC.W == 0xC188) {
      const uint16_t sp = cpu_.SP.W;
      const uint16_t ret = (uint16_t)ReadMem(sp) |
            ((uint16_t)ReadMem((uint16_t)(sp + 1)) << 8);
      tapeCall = MC1000CAS::classifyTapeCall(cpu_.PC.W, ret);
    }
    // On the real MC-1000, TLOAD sets HEAD=$01 while reading a game tape.
    // BASIC LOAD uses HEAD=$FF. Preserve the original CAS path unchanged.
    if (cpu_.PC.W == 0xC162 && ram_[0x0106] != 0xff &&
        tapeCall != MC1000CAS::TAPE_MATRIX_LOAD_HEADER) {
      if (!selected_bin_file_[0] && !bin_prompt_canceled_) {
        bin_auto_dialog_ = true;
        mc1000_open_bin_browser();
        nextCpuUs = esp_timer_get_time() + 1000;
        continue;
      }
      const bool cancelled = bin_prompt_canceled_ || !selected_bin_file_[0];
      bin_prompt_canceled_ = false;
      const bool loaded = !cancelled && HostLoadBIN();
      if (!loaded) {
        // Never jump into partially loaded game RAM. Hardware-style reset
        // returns to the ROM without clearing RAM/VRAM or ESP32 resources.
        ResetFromKeyboard();
        osd_toast((char*)(cancelled ? "TLOAD CANCELADO" : "TLOAD: ERRO BIN"), 0, 0);
      }
      nextCpuUs = esp_timer_get_time() + 1000;
      continue;
    }
    const bool isMatrixStart = tapeCall == MC1000CAS::TAPE_MATRIX_SAVE ||
                               tapeCall == MC1000CAS::TAPE_MATRIX_LOAD_HEADER;
    if (tapeCall == MC1000CAS::TAPE_MATRIX_LOAD_HEADER)
      matrix_load_active_ = true;
    if (cpu_.PC.W == 0xC4B7 || cpu_.PC.W == 0xC162) {
      // SAVE with an explicit BASIC filename must not be redirected to F9:
      // the requested filename is already in the ROM cassette header.
      // Bare SAVE / LOAD with no host selection still opens the browser.
      const bool namedProgram = !isMatrixStart &&
                         mc1000_guest_has_name(&ram_[0x018d]);
      const bool needsPrompt = (isMatrixStart && !matrix_prompt_done_) ||
                 (!isMatrixStart && !namedProgram && !selected_host_file_[0] && !cassette_prompt_canceled_);
      if (needsPrompt) {
        if (isMatrixStart) matrix_prompt_done_ = true;
        cassette_prompt_canceled_ = false;
        cassette_auto_dialog_ = true;
        if (!cas_directory_ready()) {
          cassette_auto_dialog_ = false;
          cassette_prompt_canceled_ = true;
          osd_toast((char*)"ERRO NA PASTA CAS", 0, 0);
          nextCpuUs = esp_timer_get_time() + 1000;
          continue;
        }
        osd_open_dialog((char*)MC1000CAS::cassetteBrowserTitle(tapeCall),
                        (char*)"*.CAS", mc1000_dir_callback,
                        tapeCall == MC1000CAS::TAPE_MATRIX_SAVE);
        if (osd_browser_on()) { nextCpuUs = esp_timer_get_time() + 1000; continue; }
      }
    }
    if (cpu_.PC.W == 0xC4B7) {
      if (save_overwrite_denied_) {
        save_overwrite_denied_ = false;
        save_overwrite_approved_ = false;
        FinishHostSave(MC1000CAS::SaveOutcome::Canceled, "SAVE CANCELADO");
        nextCpuUs = esp_timer_get_time() + 1000;
        continue;
      }
      // Explicit named SAVE does not require an F9 selection. A prior
      // (possibly canceled) unnamed save must not invalidate this name.
      const bool hasExplicitSaveName = !isMatrixStart &&
          mc1000_guest_has_name(&ram_[0x018d]);
      if (cassette_prompt_canceled_ ||
          (!hasExplicitSaveName && !selected_host_file_[0] && !save_empty_browser_ &&
           !matrix_new_file_requested_)) {
        cassette_prompt_canceled_ = false;
        save_empty_browser_ = false;
        FinishHostSave(MC1000CAS::SaveOutcome::Canceled, "SAVE CANCELADO");
        nextCpuUs = esp_timer_get_time() + 1000;
        continue;
      }
      cassette_prompt_canceled_ = false;
      HostSaveCassette();
      // If the file existed, the Z80 PC is intentionally STILL $C4B7.
      // Do not let the trailing ExecZ80 run while the S/N panel is active.
      if (osd_overwrite_on()) {
        nextCpuUs = esp_timer_get_time() + 1000;
        continue;
      }
      save_empty_browser_ = false;
      nextCpuUs = esp_timer_get_time() + 1000;
    } else if (cpu_.PC.W == 0xC162) {
      const MC1000CAS::LoadSource source = MC1000CAS::chooseLoadSource(
          isMatrixStart, mc1000_guest_has_name(&ram_[0x018d]),
          selected_host_file_[0] != 0);
      if (cassette_prompt_canceled_ || source == MC1000CAS::LoadSource::BrowserPrompt) {
        cassette_prompt_canceled_ = false;
        osd_toast((char*)(isMatrixStart ? "LOAD* CANCELADO" : "LOAD CANCELADO"), 0, 0);
        if (isMatrixStart) {
          // LOAD*'s ROM retries until it finds a matching header. ESC must
          // jump to the existing array-handler epilogue (POP AF, POP HL,
          // RET), not synthesize a matching header and loop forever.
          ReturnFromTapeRoutine();
          cpu_.PC.W = 0xDED9;
          matrix_load_active_ = false;
        } else {
          // Keep the preexisting BASIC LOAD cancellation semantics.
          ram_[0x00f5] = 1;
          ram_[0x0158] = 1;
          memcpy(ram_ + 0x017f, ram_ + 0x018d, 14);
          cpu_.AF.B.l |= Z_FLAG;
          ReturnFromTapeRoutine();
        }
        nextCpuUs = esp_timer_get_time() + 1000;
        continue;
      }
      cassette_prompt_canceled_ = false;
      // The real tape entry starts with OUT ($80),$01.
      WriteIO(0x80, 0x01);
      // Stage 1: ROM tape header. BASIC ($0106=$FF) returns here and
      // compares the name before requesting stage 2 at $C188.
      ram_[0x00f5] = 1;
      ram_[0x0158] = 1;
      const bool ok = HostPrepareCassetteHeader();
      if (ram_[0x0106] == 0xff) {
        if (ok) {
          cpu_.HL.W = 0x018d; // Header was written to $017F..$018C.
          cpu_.AF.B.h = 0;
          cpu_.AF.B.l |= Z_FLAG; // ROM INC A on HEAD=$FF, then RET Z
          ReturnFromTapeRoutine();
        } else {
          // Avoid entering the ROM's cassette bit loop on a missing file.
          // Produce an empty synthetic header, then report failure at $C188.
          memcpy(ram_ + 0x017f, ram_ + 0x018d, 14);
          cpu_.AF.B.l |= Z_FLAG;
          ReturnFromTapeRoutine();
          if (isMatrixStart) {
            // Missing/corrupt matrix: do not reenter the ROM's endless tape
            // search loop. Leave all guest array data untouched.
            cpu_.PC.W = 0xDED9;
            matrix_load_active_ = false;
          }
        }
      } else {
        // HEAD != $FF: ROM normally proceeds directly to program/game
        // payload, and on success transfers control to RAM at $0200.
        const bool loaded = ok && HostLoadCassetteData();
        if (loaded) {
          WriteIO(0x80, 0x01);
          ram_[0x00f5] = 1;
          ram_[0x0130] = 0xc9;
          ram_[0x015e] = 0;
          ram_[0x0106] = 0;
          ram_[0x011b] = 0;
          cpu_.SP.W = 0x0200;
          cpu_.HL.W = 0x0200;
          cpu_.AF.B.h = 0;
          cpu_.PC.W = 0x0200;
        } else {
          CompleteCassetteLoad(false);
        }
      }
      nextCpuUs = esp_timer_get_time() + 1000;
    } else if (cpu_.PC.W == 0xC188) {
      // Stage 2: BASIC has compared its requested cassette name and now
      // invokes the ROM body reader. Skipping only this routine avoids the
      // historical hang caused by intercepting the entire LOAD at $C162.
      const bool ok = HostLoadCassetteData();
      CompleteCassetteLoad(ok);
      if (tapeCall == MC1000CAS::TAPE_MATRIX_LOAD_BODY && !ok) {
        cpu_.PC.W = 0xDED9; // abort matrix ROM retry, preserve existing array
      }
      matrix_load_active_ = false;
      nextCpuUs = esp_timer_get_time() + 1000;
    }
    } // Fast cassette PC filter.
    if(cpu_.ICount <= 0) {
      if (cyclesPending) {
        cpu_cycles_total_.fetch_add(cyclesPending, std::memory_order_relaxed);
        cyclesPending = 0;
      }
      tick_ms_ = millis();
      const uint32_t psgElapsedMs = tick_ms_ - last_psg_loop_ms_;
      if (psgElapsedMs) {
        ay38910_.Loop((int)psgElapsedMs);
        last_psg_loop_ms_ = tick_ms_;
      }
      if (osd_should_close_toast()) osd_close_toast();
#if MC1000_DEBUG
      if (tick_ms_ - dbg_last >= 1000) {
        dbg_last = tick_ms_;
        Serial.printf("[MC1000] STATE PC=%04X ATTR=%02X BANK=%d ROM0=%d\n",
                      cpu_.PC.W, mc6847_attr_, mc6847_bank_, rom0000_);
      }
#endif
      cpu_.ICount += CPU_TICK_CYCLES;
      irq_cycle_accum_ += CPU_TICK_CYCLES;
      if(irq_cycle_accum_ >= 9754) {
        irq_cycle_accum_ -= 9754;
        /* Hardware NE555 is approximately 367 Hz. */
        if(cpu_.IFF & IFF_1) {
          cpu_.IFF |= IFF_IM1;
          IntZ80(&cpu_,0);
        }
      }
      PollKeyboard();
      // A03: absolute 1-ms Z80 budget. Keep the deadline after yielding a
      // FreeRTOS tick; otherwise a 10-ms tick every 40 ms loses ~20% EPS.
      // Catch up to the wall clock after a periodic blocking idle window.
      int64_t nowUs = esp_timer_get_time();
      if (MC1000Timing::shouldBlockForIdle(nowUs, lastIdleUs)) {
        // taskYIELD alone cannot let lower-priority IDLE1 run. A real delay
        // once per ~100ms prevents watchdog starvation on either tick rate.
        vTaskDelay(1);
        lastIdleUs = esp_timer_get_time();
        nowUs = lastIdleUs;
      }
      // Never accumulate unlimited backlog after SD, reset, an OSD, or a
      // large host scheduling stall: at most 12ms of Z80 catch-up.
      nextCpuUs = MC1000Timing::capCatchup(nowUs, nextCpuUs);
      // Fast path when late: run the guest rather than sleep or spin.
      // Otherwise briefly yield until its next 1ms accounting boundary.
      while ((nowUs = esp_timer_get_time()) < nextCpuUs) {
        taskYIELD();
      }
      nextCpuUs += 1000;

    }
    const int32_t before = cpu_.ICount;
    ExecZ80(&cpu_);
    const int32_t used = before - cpu_.ICount;
    if (used > 0) cyclesPending += (uint32_t)used;
  }
}