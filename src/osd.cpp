/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#include "osd.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include "mc6847.h"
#include "sysdep.h"
#include "cas_filename.h"
#include "cas_browser_label.h"
#include "cas_browser_navigation.h"
#include "osd_f1_layout.h"
#include "osd_f1_scroll.h"
#include "osd_f9_footer.h"
#include "glob.h"



int osd_visible_;
int64_t osd_toast_begin_ms_;
int osd_dialog_;

emu_glob_t globbuf_;
static size_t osd_dlg_scroll_ = 0;
int osd_dlg_cnt_;
int osd_dlg_sel_;
int osd_dlg_top_;
int osd_dlg_max_ = 8; /* max visible lines */
static char osd_dlg_sel_str_[256] = {0};
osd_dlg_callback osd_dlg_cb_;
static bool osd_browser_allow_new_cas_ = false;
static bool osd_browser_txt_ = false;
static bool osd_browser_bin_ = false;
// CAS browser with subfolder navigation. The folder only becomes the active
// CAS folder when a file is chosen (ENTER) or N creates a new CAS; ESC
// restores the folder that was active when the browser opened.
static bool osd_browser_cas_ = false;
static bool osd_cas_commit_ = false;
static char osd_cas_sub_at_open_[128] = {0};
static char osd_dlg_pattern_[32] = {0};
static char selected_cas_sub_[128] = {0};
static int osd_menu_active_ = 0;
static int osd_menu_selection_ = 0;
static int osd_menu_fps_enabled_ = 0;
static bool osd_menu_blocks_ = false;
static bool osd_menu_alternative_borders_ = false;
static uint8_t osd_menu_color_artifacts_ = 0;
static int osd_menu_top_ = 0;
static bool osd_menu_keyboard_br_ = false;
static bool osd_menu_joystick_a_ = false;
static bool osd_menu_joystick_b_ = false;
static int osd_overwrite_active_ = 0;
static osd_confirm_callback osd_overwrite_callback_ = nullptr;
static bool osd_overwrite_bin_ = false;
static bool osd_tsave_active_ = false;
static osd_tsave_callback osd_tsave_callback_ = nullptr;
static char osd_tsave_fields_[4][32] = {{0}};
static int osd_tsave_field_ = 0;
static constexpr int OSD_MENU_VISIBLE_ITEMS = MC1000F1Scroll::kVisibleItems;
static constexpr int OSD_MENU_FUNCTION_ITEMS = MC1000F1Scroll::kFunctionalItems;
static constexpr int OSD_MENU_CHOICES = MC1000F1Scroll::kTotalChoices;
static char selected_cas_[256] = {0};
static char selected_bin_[256] = {0};
void osd_set_selected_bin(const char *filename) {
  if (!filename) filename = "";
  strncpy(selected_bin_, filename, sizeof(selected_bin_) - 1);
  selected_bin_[sizeof(selected_bin_) - 1] = 0;
}
void osd_set_selected_cas(const char *filename) {
  if (!filename) filename = "";
  strncpy(selected_cas_, filename, sizeof(selected_cas_) - 1);
  selected_cas_[sizeof(selected_cas_) - 1] = 0;
  // The '*' marker belongs to the folder where the CAS was selected.
  strncpy(selected_cas_sub_, cas_subdirectory(), sizeof(selected_cas_sub_) - 1);
  selected_cas_sub_[sizeof(selected_cas_sub_) - 1] = 0;
}

// F1 needs two additional columns to display BORDAS ALTERNATIVAS + OFF
// without clipping. Keep F2/F3/SAVE browsers at the original 24 columns.
static void osd_menu_border(int y, bool top, bool wide = false) {
  const int columns = wide ? 26 : 24;
  char row[27];
  row[0] = (char)(top ? 0x87 : 0x88);
  for (int i = 1; i < columns - 1; ++i) row[i] = (char)0x8b;
  row[columns - 1] = (char)(top ? 0x85 : 0x86);
  row[columns] = '\0';
  osd_print(wide ? 3 : 4, y, row, 0);
}

static void osd_menu_rule(int y, bool wide = false) {
  const int columns = wide ? 26 : 24;
  char row[27];
  row[0] = (char)0x8f;
  for (int i = 1; i < columns - 1; ++i) row[i] = (char)0x8b;
  row[columns - 1] = (char)0x8e;
  row[columns] = '\0';
  osd_print(wide ? 3 : 4, y, row, 0);
}

static void osd_menu_line(int y, const char *text, int inverse, bool wide = false) {
  const int columns = wide ? 26 : 24;
  char row[27];
  memset(row, ' ', columns);
  row[0] = (char)0x8a;
  row[columns - 1] = (char)0x8a;
  row[columns] = '\0';
  if (text) {
    size_t n = strlen(text);
    if (n > (size_t)columns - 2) n = columns - 2;
    memcpy(row + 1, text, n);
  }
  osd_print(wide ? 3 : 4, y, row, inverse);
}

static void osd_menu_draw() {
  osd_menu_border(0, true, true);
  // Unchanged panel dimensions. The title uses 23 of the 24 columns, so the
  // "more options" indicators (^ / v) moved to the RETORNAR (ESC) footer.
  char title[MC1000OSD::kF1InteriorColumns + 1] = "MC-1000 BY ANDRE CHAVES ";
  osd_menu_line(1, title, 1, true);
  osd_menu_rule(2, true);
  char row[MC1000OSD::kF1InteriorColumns + 1];
  for (int visible = 0; visible < OSD_MENU_VISIBLE_ITEMS; ++visible) {
    const int item = osd_menu_top_ + visible;
    const bool selected = osd_menu_selection_ == item;
    const char *label = "";
    const char *state = nullptr;
    switch (item) {
      case 0: label = "CARREGAR CAS"; state = "(F2)"; break;
      case 1: label = "CARREGAR BIN"; state = "(F3)"; break;
      case 2: label = "INJETAR PROGRAMA"; state = "(F4)"; break;
      case 3: label = "EXIBIR FPS"; state = osd_menu_fps_enabled_ ? "ON" : "OFF"; break;
      case 4: label = "ARTEFATOS COLORIDOS"; state = osd_menu_color_artifacts_ == 1 ? "STD" :
          (osd_menu_color_artifacts_ == 2 ? "REV" : "OFF"); break;
      case 5: label = "BORDAS ALTERNATIVAS"; state = osd_menu_alternative_borders_ ? "ON" : "OFF"; break;
      case 6: label = "BLOCOS COLORIDOS"; state = osd_menu_blocks_ ? "ON" : "OFF"; break;
      case 7: label = "LAYOUT TECLADO"; state = osd_menu_keyboard_br_ ? "BR" : "US"; break;
      case 8: label = "JOYSTICK A (P1)"; state = osd_menu_joystick_a_ ? "ON" : "OFF"; break;
      case 9: label = "RESET EMULADOR"; state = "(F12)"; break;
      case 10: label = "JOYSTICK B (P2)"; state = osd_menu_joystick_b_ ? "ON" : "OFF"; break;
      case 11: label = "REINICIAR ESP32"; break;
    }
    MC1000OSD::f1MenuItem(row, label, selected, state);
    osd_menu_line(3 + visible, row, selected, true);
  }
  osd_menu_rule(12, true);
  MC1000OSD::f1MenuCentered(row, "RETORNAR (ESC)",
                            osd_menu_selection_ == OSD_MENU_FUNCTION_ITEMS);
  row[MC1000OSD::kF1InteriorColumns - 2] = osd_menu_top_ > 0 ? '^' : ' ';
  row[MC1000OSD::kF1InteriorColumns - 1] =
      osd_menu_top_ + OSD_MENU_VISIBLE_ITEMS < OSD_MENU_FUNCTION_ITEMS ? 'v' : ' ';
  osd_menu_line(13, row, osd_menu_selection_ == OSD_MENU_FUNCTION_ITEMS, true);
  osd_menu_border(14, false, true);
}

static void osd_menu_keep_selection_visible() {
  osd_menu_top_ = MC1000F1Scroll::keepVisible(osd_menu_selection_, osd_menu_top_);
}

static void osd_overwrite_draw(const char *filename) {
  osd_menu_border(5, true);
  osd_menu_line(6, "  ARQUIVO JA EXISTE", 1);
  osd_menu_rule(7);
  // Uppercase name is for display only. Actual filesystem path stays intact.
  char nameRow[23];
  snprintf(nameRow, sizeof(nameRow), " %.19s", filename ? filename : "");
  osd_menu_line(8, nameRow, 0);
  osd_menu_line(9, osd_overwrite_bin_ ? "  SOBRESCREVER BIN?" : "  SOBRESCREVER CAS?", 0);
  osd_menu_line(10, " S=SIM     N=NAO", 1);
  osd_menu_border(11, false);
}

extern "C" {
MC6847 *vdg();
}

/*
 * OSD superimposed on MC6847.
 */

void osd_init() {
  osd_dlg_sel_str_[0] = 0;
  osd_toast_begin_ms_ = -1;
  osd_visible_ = 0;
  osd_dialog_ = 0;
  osd_overwrite_active_ = 0;
  osd_tsave_active_ = false;
  osd_menu_blocks_ = false;
  osd_menu_color_artifacts_ = 0;
  osd_menu_joystick_a_ = false;
  osd_menu_joystick_b_ = false;
  osd_menu_top_ = 0;
}

void osd_print(int x, int y, char *str, int inverse) {
  vdg()->PutOSDText(x, y, str, inverse != 0);
}

int osd_is_toast_on() {
  return osd_toast_begin_ms_ > 0;
}

int osd_should_close_toast() {
  if (!osd_is_toast_on()) return 0;

  int64_t t = get_timestamp_ms();
  return t - osd_toast_begin_ms_ > 1400; /* 1.4 s, shorter reset/SD notices */
}

/**
 * Display a modeless toast message on OSD.
 *
 * @param str Message string. Null-terminated.
 * @param vloc 0 - center
 *             1 - bottom of the screen
 * @param inverse Inverse attr
 */
void osd_toast(char *str, int vloc, int inverse) {
  (void)vloc;
  (void)inverse;
  if (!str || osd_dialog_on()) return;
  char message[32];
  strncpy(message, str, sizeof(message) - 1);
  message[sizeof(message) - 1] = '\0';
  // A toast is a framebuffer overlay, not guest text memory. Replacing an
  // active toast is safe and ensures LOAD status is not hidden by the prior
  // "FILE SELECTED" message.
  vdg()->SetToastText(message);
  osd_toast_begin_ms_ = get_timestamp_ms();
}

void osd_close_toast() {
  vdg()->SetToastText("");
  osd_toast_begin_ms_ = -1;
}

void osd_exit() {
  osd_close_dialog();
}

int osd_dialog_on() {
  return osd_dialog_ || osd_menu_active_ || osd_overwrite_active_ || osd_tsave_active_;
}

int osd_menu_on() { return osd_menu_active_; }
int osd_browser_on() { return osd_dialog_ != 0; }
bool osd_overwrite_on() { return osd_overwrite_active_ != 0; }

void osd_open_overwrite_confirm_bin(const char *filename, osd_confirm_callback cb) {
  if (osd_dialog_on() || !cb) return;
  osd_overwrite_bin_ = true;
  osd_close_toast();
  osd_overwrite_callback_ = cb;
  osd_overwrite_active_ = 1;
  osd_show(1);
  osd_overwrite_draw(filename);
}

void osd_open_overwrite_confirm(const char *filename, osd_confirm_callback cb) {
  if (osd_dialog_on() || !cb) return;
  osd_overwrite_bin_ = false;
  osd_open_overwrite_confirm_bin(filename, cb);
  osd_overwrite_bin_ = false;
}

void osd_process_overwrite_key(KeyCode key) {
  if (!osd_overwrite_active_) return;
  bool answer;
  if (key == fabgl::VK_S || key == fabgl::VK_s) answer = true;
  else if (key == fabgl::VK_N || key == fabgl::VK_n) answer = false;
  else return;  // no timeout, Esc, Enter, F1, or other dismissal
  const auto cb = osd_overwrite_callback_;
  osd_overwrite_callback_ = nullptr;
  osd_overwrite_active_ = 0;
  osd_show(0);
  if (cb) cb(answer);
}

// RESET can interrupt a blocking SAVE confirmation. Always report "No"
// to the caller before resetting the guest, so the file remains untouched.
void osd_cancel_overwrite() {
  if (!osd_overwrite_active_) return;
  const auto cb = osd_overwrite_callback_;
  osd_overwrite_callback_ = nullptr;
  osd_overwrite_active_ = 0;
  osd_show(0);
  if (cb) cb(false);
}


// ---- TSAVE editor. It opens after the BASIC ROM has accepted the Return
// character, avoiding any change to the F1 menu or guest keyboard matrix.
static void osd_tsave_draw() {
  osd_menu_border(0, true);
  osd_menu_line(1, "   SALVAR BIN (TSAVE)", 1);
  osd_menu_rule(2);
  const char *labels[4] = {"NOME", "INICIO", "FIM", "EXEC"};
  const int rows[4] = {4,6,8,10};
  for (int i=0;i<4;++i) {
    char text[23];
    snprintf(text, sizeof(text), " %-6s %.13s", labels[i], osd_tsave_fields_[i]);
    osd_menu_line(rows[i], text, i==osd_tsave_field_);
  }
  osd_menu_rule(12);
  osd_menu_line(13, "ENTER:PROX/SALVAR ESC", 1);
  osd_menu_border(14, false);
}
static char osd_tsave_char(KeyCode key) {
  switch(key) {
    // FabGL exposes both capital and lowercase VK variants.
#define KEY_LETTER(x,y) case fabgl::VK_##x: case fabgl::VK_##y: return #x[0];
    KEY_LETTER(A,a) KEY_LETTER(B,b) KEY_LETTER(C,c)
    KEY_LETTER(D,d) KEY_LETTER(E,e) KEY_LETTER(F,f)
    KEY_LETTER(G,g) KEY_LETTER(H,h) KEY_LETTER(I,i)
    KEY_LETTER(J,j) KEY_LETTER(K,k) KEY_LETTER(L,l)
    KEY_LETTER(M,m) KEY_LETTER(N,n) KEY_LETTER(O,o)
    KEY_LETTER(P,p) KEY_LETTER(Q,q) KEY_LETTER(R,r)
    KEY_LETTER(S,s) KEY_LETTER(T,t) KEY_LETTER(U,u)
    KEY_LETTER(V,v) KEY_LETTER(W,w) KEY_LETTER(X,x)
    KEY_LETTER(Y,y) KEY_LETTER(Z,z)
#undef KEY_LETTER
    case fabgl::VK_0: return '0'; case fabgl::VK_1: return '1';
    case fabgl::VK_2: return '2'; case fabgl::VK_3: return '3';
    case fabgl::VK_4: return '4'; case fabgl::VK_5: return '5';
    case fabgl::VK_6: return '6'; case fabgl::VK_7: return '7';
    case fabgl::VK_8: return '8'; case fabgl::VK_9: return '9';
    case fabgl::VK_MINUS: return '-';
    default: return 0;
  }
}
bool osd_tsave_on() { return osd_tsave_active_; }
void osd_open_tsave_editor(osd_tsave_callback cb) {
  if (osd_dialog_on() || !cb) return;
  osd_close_toast();
  osd_tsave_callback_ = cb;
  osd_tsave_active_ = true;
  osd_tsave_field_ = 0;
  strcpy(osd_tsave_fields_[0], "JOGO");
  strcpy(osd_tsave_fields_[1], "16384");
  strcpy(osd_tsave_fields_[2], "20479");
  strcpy(osd_tsave_fields_[3], "16384");
  osd_show(1);
  osd_tsave_draw();
}
void osd_cancel_tsave() {
  if (!osd_tsave_active_) return;
  const auto cb = osd_tsave_callback_;
  osd_tsave_callback_ = nullptr;
  osd_tsave_active_ = false;
  osd_show(0);
  if (cb) cb(nullptr);
}
void osd_process_tsave_key(KeyCode key) {
  if (!osd_tsave_active_) return;
  if (key == fabgl::VK_ESCAPE) { osd_cancel_tsave(); return; }
  if (key == fabgl::VK_UP) {
    osd_tsave_field_ = (osd_tsave_field_ + 3) % 4;
  } else if (key == fabgl::VK_DOWN || key == fabgl::VK_TAB) {
    osd_tsave_field_ = (osd_tsave_field_ + 1) % 4;
  } else if (key == fabgl::VK_RETURN) {
    if (osd_tsave_field_ < 3) ++osd_tsave_field_;
    else {
      char command[160];
      snprintf(command,sizeof(command), "TSAVE \"%s\",%s,%s,%s",
               osd_tsave_fields_[0],osd_tsave_fields_[1],
               osd_tsave_fields_[2],osd_tsave_fields_[3]);
      const auto cb=osd_tsave_callback_;
      osd_tsave_callback_=nullptr;
      osd_tsave_active_=false;
      osd_show(0);
      if (cb) cb(command);
      return;
    }
  } else if (key == fabgl::VK_BACKSPACE) {
    size_t n = strlen(osd_tsave_fields_[osd_tsave_field_]);
    if (n) osd_tsave_fields_[osd_tsave_field_][n-1] = 0;
  } else {
    const char ch=osd_tsave_char(key);
    if (ch && (osd_tsave_field_ == 0 || (ch >= '0' && ch <= '9'))) {
      char *field=osd_tsave_fields_[osd_tsave_field_];
      const size_t n=strlen(field);
      if (n < (osd_tsave_field_ == 0 ? 12u : 5u)) {
        field[n]=ch; field[n+1]=0;
      }
    }
  }
  osd_tsave_draw();
}

void osd_open_menu(int fps_enabled, bool color_blocks, bool keyboard_br, bool alternative_borders, uint8_t color_artifact_mode, bool joystick_a, bool joystick_b) {
  if (osd_dialog_ || osd_menu_active_) return;
  osd_menu_active_ = 1;
  osd_menu_selection_ = 0;
  osd_menu_top_ = 0;
  osd_menu_fps_enabled_ = fps_enabled;
  osd_menu_blocks_ = color_blocks;
  osd_menu_keyboard_br_ = keyboard_br;
  osd_menu_alternative_borders_ = alternative_borders;
  osd_menu_color_artifacts_ = color_artifact_mode;
  osd_menu_joystick_a_ = joystick_a;
  osd_menu_joystick_b_ = joystick_b;
  osd_close_toast();
  osd_show(1);
  osd_menu_draw();
}

void osd_close_menu() {
  if (!osd_menu_active_) return;
  osd_menu_active_ = 0;
  osd_show(0);
}

void osd_menu_refresh(int fps_enabled, bool color_blocks, bool keyboard_br, bool alternative_borders, uint8_t color_artifact_mode, bool joystick_a, bool joystick_b) {
  osd_menu_fps_enabled_ = fps_enabled;
  osd_menu_blocks_ = color_blocks;
  osd_menu_keyboard_br_ = keyboard_br;
  osd_menu_alternative_borders_ = alternative_borders;
  osd_menu_color_artifacts_ = color_artifact_mode;
  osd_menu_joystick_a_ = joystick_a;
  osd_menu_joystick_b_ = joystick_b;
  if (osd_menu_active_) osd_menu_draw();
}

int osd_menu_process_key(KeyCode key) {
  if (!osd_menu_active_) return OSD_MENU_NONE;
  if (key == fabgl::VK_UP) {
    osd_menu_selection_ = (osd_menu_selection_ + OSD_MENU_CHOICES - 1) % OSD_MENU_CHOICES;
    osd_menu_keep_selection_visible();
    osd_menu_draw();
    return OSD_MENU_NONE;
  }
  if (key == fabgl::VK_DOWN) {
    osd_menu_selection_ = (osd_menu_selection_ + 1) % OSD_MENU_CHOICES;
    osd_menu_keep_selection_visible();
    osd_menu_draw();
    return OSD_MENU_NONE;
  }
  if (key == fabgl::VK_ESCAPE) return OSD_MENU_CLOSE;
  if (key == fabgl::VK_F2) return OSD_MENU_OPEN_SD;
  if (key == fabgl::VK_F3) return OSD_MENU_OPEN_BIN;
  if (key == fabgl::VK_F4) return OSD_MENU_INJECT_TXT;
  if (key == fabgl::VK_F12) return OSD_MENU_RESET;
  if (key != fabgl::VK_RETURN) return OSD_MENU_NONE;
  switch (osd_menu_selection_) {
    case 0: return OSD_MENU_OPEN_SD;
    case 1: return OSD_MENU_OPEN_BIN;
    case 2: return OSD_MENU_INJECT_TXT;
    case 3: return OSD_MENU_TOGGLE_FPS;
    case 4: return OSD_MENU_TOGGLE_COLOR_ARTIFACTS;
    case 5: return OSD_MENU_TOGGLE_ALTERNATIVE_BORDERS;
    case 6: return OSD_MENU_TOGGLE_BLOCKS;
    case 7: return OSD_MENU_TOGGLE_KEYBOARD;
    case 8: return OSD_MENU_TOGGLE_JOYSTICK_A;
    case 9: return OSD_MENU_RESET;
    case 10: return OSD_MENU_TOGGLE_JOYSTICK_B;
    case 11: return OSD_MENU_RESTART_ESP32;
    default: return OSD_MENU_CLOSE;
  }
}

// Text shown for one browser entry: folders as [NAME], ".." as [..].
static const char *osd_dlg_entry_text(int ind, char *buf, size_t size) {
  const char *physical = globbuf_.gl_pathv[ind];
  const bool isDir = globbuf_.gl_isdir && globbuf_.gl_isdir[ind];
  if (!isDir) {
    const char *slash = strrchr(physical, '/');
    return slash ? slash + 1 : physical;
  }
  snprintf(buf, size, "[%s]", physical);
  return buf;
}

static bool osd_dlg_entry_is_dir(int ind) {
  return ind >= 0 && ind < osd_dlg_cnt_ && globbuf_.gl_isdir && globbuf_.gl_isdir[ind];
}

static bool osd_dlg_no_cas_files() {
  for (int i = 0; i < osd_dlg_cnt_; ++i)
    if (!osd_dlg_entry_is_dir(i)) return false;
  return true;
}

// (Re)load the listing of the current CAS folder: subfolders, then *.CAS.
static bool osd_cas_build_list() {
  emu_globfree(&globbuf_);
  if (!cas_directory_ready()) return false;
  cas_recover_directory();
  reload_dir();
  fabgl::FileBrowser *browser = file_browser();
  if (!browser) return false;
  return emu_glob_browser_ex(osd_dlg_pattern_, &globbuf_, browser,
                             true, cas_subdirectory()[0] != 0) == 0;
}

void _osd_dlg_update() {
  // Always redraw the eight visible rows after vertical/horizontal movement.
  for (int i = 0; i < osd_dlg_max_; ++i) {
    const int ind = osd_dlg_top_ + i;
    const bool valid = ind < osd_dlg_cnt_;
    const bool selected = valid && osd_dlg_sel_ == ind;
    char row[25];
    memset(row, ' ', 24);
    row[24] = 0;
    row[0] = (char)0x8a;
    row[23] = (char)0x8a;
    if (valid) {
      char entry[260];
      const char *physical = osd_dlg_entry_text(ind, entry, sizeof(entry));
      const bool marked = !osd_browser_txt_ && !osd_dlg_entry_is_dir(ind) &&
          (osd_browser_bin_ || strcmp(selected_cas_sub_, cas_subdirectory()) == 0) &&
          MC1000CAS::sameCASFileName(physical,
            osd_browser_bin_ ? selected_bin_ : selected_cas_);
      row[1] = marked ? '*' : ' ';
      row[2] = selected ? '>' : ' ';
      char label[20];
      MC1000CAS::browserDisplayLabel(physical, selected ? osd_dlg_scroll_ : 0, label);
      memcpy(row + 4, label, 19);
    } else if (osd_browser_allow_new_cas_ && ind == osd_dlg_cnt_ && osd_dlg_no_cas_files()) {
      // Folder with no CAS yet (maybe only [..] / subfolders): show the N hint.
      memcpy(row + 2, "N: CRIAR NOVO CAS", 17);
    }
    osd_print(4, 4 + i, row, selected ? 1 : 0);
  }
  // Show the absolute position at the left. Keep RETORNAR (ESC) at the
  // right edge in both LOAD and SAVE* dialogs, without losing selection.
  char footer[MC1000OSD::kF9FooterColumns + 1];
  const int position = osd_dlg_cnt_ ? osd_dlg_sel_ + 1 : 0;
  MC1000OSD::f9BrowserFooter(footer, position, osd_dlg_cnt_);
  osd_menu_line(13, footer, 0);
}

/**
 * Open modal dialog.
 */
void osd_open_dialog(char *title, char *globp, osd_dlg_callback cb,
                     bool allowNewCAS, bool txtBrowser, bool binBrowser) {
  // Refresh the mounted directory before taking a snapshot of its entries.
  if (binBrowser ? !bin_directory_ready() :
      (txtBrowser ? !txt_directory_ready() : !cas_directory_ready())) {
    if (cb) cb(NULL, "NO SD DIR");
    return;
  }
  const bool casBrowser = !binBrowser && !txtBrowser;
  if (casBrowser) {
    strncpy(osd_dlg_pattern_, globp ? globp : "*.CAS", sizeof(osd_dlg_pattern_) - 1);
    osd_dlg_pattern_[sizeof(osd_dlg_pattern_) - 1] = 0;
    strncpy(osd_cas_sub_at_open_, cas_subdirectory(), sizeof(osd_cas_sub_at_open_) - 1);
    osd_cas_sub_at_open_[sizeof(osd_cas_sub_at_open_) - 1] = 0;
    if (!osd_cas_build_list()) {
      emu_globfree(&globbuf_);
      if (cb) cb(NULL, "OUT OF MEMORY");
      return;
    }
  } else {
    fabgl::FileBrowser *browser = nullptr;
    if (binBrowser) {
      browser = bin_file_browser();
      if (browser) browser->reload();
    } else {
      browser = txt_file_browser();
      if (browser) browser->reload();
    }
    if (!browser || emu_glob_browser(globp, &globbuf_, browser) != 0) {
      if (cb) cb(NULL, "OUT OF MEMORY");
      return;
    }
  }
  int nfiles = globbuf_.gl_pathc;
  if (nfiles == 0 && !allowNewCAS) {
    emu_globfree(&globbuf_);
    if (cb) cb(NULL, "NO FILES");
    return;
  }
  // If a toast was visible, restore the screen before creating the modal page.
  if (osd_visible_ && !osd_dialog_) osd_show(0);
  osd_dlg_cb_ = cb;
  osd_browser_allow_new_cas_ = allowNewCAS && !txtBrowser;
  osd_browser_txt_ = txtBrowser;
  osd_browser_bin_ = binBrowser;
  osd_browser_cas_ = casBrowser;
  osd_cas_commit_ = false;
  osd_dlg_cnt_ = nfiles;
  osd_dlg_top_ = 0;
  osd_dlg_sel_ = 0;
  osd_dlg_scroll_ = 0;
  osd_dialog_ = 1;
  osd_show(1);
  // Use the same pixel-drawn grid frame and OSD palette as the F1 menu.
  char row[25];
  osd_menu_border(1, true);
  memset(row, ' ', 24); row[24] = '\0'; row[0] = (char)0x8a; row[23] = (char)0x8a;
  const char *caption = title ? title : "SD - CARREGAR";
  size_t titleLen = strlen(caption); if (titleLen > 22) titleLen = 22;
  size_t titlePos = 1 + (22 - titleLen) / 2;
  memcpy(row + titlePos, caption, titleLen);
  osd_print(4, 2, row, 1);
  // Match F1: a full blue divider separates the inverted title from the
  // first CAS entry. Keep all eight filename rows visible (4 through 11).
  osd_menu_rule(3);
  for (int y = 4; y <= 11; ++y) osd_menu_line(y, "", 0);
  osd_menu_rule(12);
  // Position and RETORNAR (ESC) are drawn by _osd_dlg_update().
  osd_menu_line(13, "", 0);
  osd_menu_border(14, false);

  _osd_dlg_update();
}

void osd_close_dialog() {
  if (!osd_dialog_) return;
  osd_show(0);
  osd_dialog_ = 0;
  osd_browser_allow_new_cas_ = false;
  osd_browser_txt_ = false;
  osd_browser_bin_ = false;
  emu_globfree(&globbuf_);
  osd_dlg_scroll_ = 0;
  // Leaving without choosing a file (ESC, reset...) keeps the previous folder.
  if (osd_browser_cas_ && !osd_cas_commit_ &&
      strcmp(cas_subdirectory(), osd_cas_sub_at_open_) != 0)
    cas_set_subdirectory(osd_cas_sub_at_open_);
  osd_browser_cas_ = false;
  osd_cas_commit_ = false;
}

void osd_process_key(KeyCode key) {
  if (!osd_dialog_) return;
  if (osd_browser_allow_new_cas_ && (key == fabgl::VK_N || key == fabgl::VK_n)) {
    const auto cb = osd_dlg_cb_;
    osd_cas_commit_ = true;  // new CAS is created in the folder being shown
    osd_close_dialog();
    if (cb) cb(NULL, "NEW FILE");
    return;
  }
  if (key == VKEY_RETURN && osd_dlg_cnt_ == 0) return;
  if (key == VKEY_RETURN && osd_browser_cas_ && osd_dlg_entry_is_dir(osd_dlg_sel_)) {
    // Enter a subfolder (or go up with [..]) without leaving the browser.
    char name[128];
    const char *dirName = globbuf_.gl_pathv[osd_dlg_sel_];
    if (strlen(dirName) >= sizeof(name)) {
      osd_menu_line(13, " PASTA: NOME LONGO", 1);
      return;
    }
    strcpy(name, dirName);
    const bool goingUp = strcmp(name, "..") == 0;
    char cameFrom[128] = {0};
    if (goingUp) {
      const char *sub = cas_subdirectory();
      const char *slash = strrchr(sub, '/');
      strncpy(cameFrom, slash ? slash + 1 : sub, sizeof(cameFrom) - 1);
    }
    emu_globfree(&globbuf_);  // names belong to the FileBrowser being replaced
    const bool entered = cas_enter_subdirectory(name);
    if (!osd_cas_build_list()) {
      const auto cb = osd_dlg_cb_;
      osd_close_dialog();
      if (cb) cb(NULL, "OUT OF MEMORY");
      return;
    }
    osd_dlg_cnt_ = globbuf_.gl_pathc;
    osd_dlg_sel_ = 0;
    osd_dlg_top_ = 0;
    osd_dlg_scroll_ = 0;
    // Going up: put the cursor back on the folder we just left.
    if (goingUp && cameFrom[0]) {
      for (int i = 0; i < osd_dlg_cnt_; ++i) {
        if (osd_dlg_entry_is_dir(i) && strcmp(globbuf_.gl_pathv[i], cameFrom) == 0) {
          osd_dlg_sel_ = i;
          if (osd_dlg_sel_ >= osd_dlg_max_) osd_dlg_top_ = osd_dlg_sel_ - osd_dlg_max_ + 1;
          break;
        }
      }
    }
    _osd_dlg_update();
    if (!entered) osd_menu_line(13, " ERRO AO ABRIR PASTA", 1);
    return;
  }
  if (key == VKEY_ESCAPE || key == VKEY_RETURN) {
    if (key == VKEY_RETURN) {
      // No silent truncation of physical paths. Selection storage is 256.
      const char *selected = globbuf_.gl_pathv[osd_dlg_sel_];
      if (strlen(selected) >= sizeof(osd_dlg_sel_str_)) {
        osd_menu_line(13, osd_browser_bin_ ? " NOME BIN MUITO LONGO" :
                          (osd_browser_txt_ ? " NOME TXT MUITO LONGO" : " CAS NAME TOO LONG"), 1);
        return;
      }
      strcpy(osd_dlg_sel_str_, selected);
      osd_cas_commit_ = true;  // the folder shown becomes the active CAS folder
    } else {
      *osd_dlg_sel_str_ = '\0';
    }
    osd_close_dialog();

    if (osd_dlg_cb_) osd_dlg_cb_(osd_dlg_sel_str_, key == VKEY_RETURN ? "SELECTED" : "CANCELED");

  } else if (key == fabgl::VK_LEFT || key == fabgl::VK_RIGHT) {
    if (osd_dlg_cnt_ == 0) return;
    char entry[260];
    const char *physical = osd_dlg_entry_text(osd_dlg_sel_, entry, sizeof(entry));
    osd_dlg_scroll_ = MC1000CAS::browserScroll(physical, osd_dlg_scroll_,
                                               key == fabgl::VK_RIGHT);
    _osd_dlg_update();
  } else if (key == VKEY_UP || key == VKEY_DOWN) {
    if (osd_dlg_cnt_ == 0) return;
    const MC1000CAS::BrowserPosition next = MC1000CAS::browserMoveVertical(
        osd_dlg_cnt_, osd_dlg_max_, osd_dlg_sel_, osd_dlg_top_,
        key == VKEY_UP ? -1 : 1);
    if (next.selected == osd_dlg_sel_) return;  // top/bottom reached
    osd_dlg_sel_ = next.selected;
    osd_dlg_top_ = next.top;
    osd_dlg_scroll_ = 0;  // horizontal position belongs to selected file
    _osd_dlg_update();
  }
}

void osd_show(int show) {
  if (!!show == !!osd_visible_) return;
  if (show) {
    vdg()->ClearOSD();
    vdg()->SetOSDActive(true);
  } else {
    vdg()->SetOSDActive(false);
    osd_close_toast();
  }
  osd_visible_ = !!show;
}
