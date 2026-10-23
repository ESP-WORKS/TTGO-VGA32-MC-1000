/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#pragma once

#include <stdio.h>
#include <stdint.h>
#include "fabutils.h"
#include "common.h"

#define SD_MOUNT_PATH "/SD"

#ifdef __cplusplus
extern "C" {
#endif

void init_fs();
bool cas_directory_ready();
const char *cas_directory_path();
// CAS subfolders: path relative to /MC-1000/CAS ("" = root). Changing it
// moves every CAS operation (LOAD/SAVE/browser) to that folder.
const char *cas_subdirectory();
bool cas_set_subdirectory(const char *sub);
bool cas_enter_subdirectory(const char *name); // ".." goes one level up
// Independent TXT browser: never change the active CAS folder or its FileBrowser.
bool bin_directory_ready();
const char *bin_directory_path();
fabgl::FileBrowser *bin_file_browser();
bool bin_find_existing_filename(const char *wanted, char *actual, size_t actualSize);
bool txt_directory_ready();
const char *txt_directory_path();
fabgl::FileBrowser *txt_file_browser();
// Return the exact on-card spelling of an existing CAS-related file.
// Comparison is case-insensitive, even with a case-sensitive VFS.
bool cas_find_existing_filename(const char *wanted, char *actual, size_t actualSize);
// Recover interrupted CAS commits (exact-size validation); called outside
// the real-time VGA/CPU hot paths, before cassette access or opening F9.
bool cas_recover_named(const char *casFilename);
void cas_recover_directory();
bool cas_protect_old_temp(const char *casFilename);
fabgl::FileBrowser *file_browser();
FILE *ext_fopen(const char *pathname, const char *mode);
void ext_remove(const char *pathname);
void reload_dir();
uint32_t get_timestamp_ms();
#ifdef __cplusplus
}
#endif
