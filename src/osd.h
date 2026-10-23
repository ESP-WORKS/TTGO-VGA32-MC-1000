/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#pragma once

#include "common.h"
#include "key.h"

typedef void (*osd_dlg_callback)(char *sel, const char *errmsg);

extern byte *osd_;
extern int osd_visible_;

void osd_init();
int osd_should_close_toast();
void osd_close_toast();
int osd_dialog_on();
byte osd_data(int addr);
void osd_exit();
void osd_show(int show);
void osd_print(int x, int y, char *str, int inverse);
void osd_toast(char *msg, int vloc, int inverse);
void osd_open_dialog(char *title, char *globp, osd_dlg_callback cb, bool allowNewCAS = false, bool txtBrowser = false, bool binBrowser = false);
void osd_close_dialog();
void osd_process_key(KeyCode key);
void osd_set_selected_cas(const char *basenameWithExtension);
void osd_set_selected_bin(const char *basenameWithExtension);
// Blocking confirmation: independent opaque panel; only S or N can dismiss it.
typedef void (*osd_confirm_callback)(bool overwrite);
void osd_open_overwrite_confirm(const char *filename, osd_confirm_callback cb);
void osd_open_overwrite_confirm_bin(const char *filename, osd_confirm_callback cb);
bool osd_overwrite_on();
void osd_process_overwrite_key(KeyCode key);
void osd_cancel_overwrite(); // Cancel on hardware-reset, never commit existing CAS

// TSAVE with no parameters: four-field editor, independent of the F1 menu.
// Callback receives a complete ASCII BASIC TSAVE command or NULL on ESC.
typedef void (*osd_tsave_callback)(const char *command);
void osd_open_tsave_editor(osd_tsave_callback cb);
bool osd_tsave_on();
void osd_process_tsave_key(KeyCode key);
void osd_cancel_tsave();

// Main configuration menu styled after ESPectrum's F1 OSD.
enum OSDMenuAction {
  OSD_MENU_NONE = 0,
  OSD_MENU_OPEN_SD = 1,
  OSD_MENU_TOGGLE_FPS = 2,
  OSD_MENU_RESET = 3,
  OSD_MENU_RESTART_ESP32 = 4,
  OSD_MENU_CLOSE = 5,
  OSD_MENU_TOGGLE_BLOCKS = 6,
  OSD_MENU_INJECT_TXT = 7,
  OSD_MENU_TOGGLE_KEYBOARD = 8,
  OSD_MENU_OPEN_BIN = 9,
  OSD_MENU_TOGGLE_ALTERNATIVE_BORDERS = 10,
  OSD_MENU_TOGGLE_COLOR_ARTIFACTS = 11,
  OSD_MENU_TOGGLE_JOYSTICK_A = 12,
  OSD_MENU_TOGGLE_JOYSTICK_B = 13
};
int osd_menu_on();
int osd_browser_on();
void osd_open_menu(int fps_enabled, bool color_blocks, bool keyboard_br, bool alternative_borders, uint8_t color_artifact_mode, bool joystick_a, bool joystick_b);
void osd_close_menu();
void osd_menu_refresh(int fps_enabled, bool color_blocks, bool keyboard_br, bool alternative_borders, uint8_t color_artifact_mode, bool joystick_a, bool joystick_b);
int osd_menu_process_key(KeyCode key);
