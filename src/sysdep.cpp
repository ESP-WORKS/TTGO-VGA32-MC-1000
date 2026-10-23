/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#include <stdio.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <new>
#include <stdlib.h>
#include "sysdep.h"
#include "cas_filename.h"
#include "cas_recovery.h"
#include "fabutils.h"
#include "driver/spi_common.h"
#include "bootl_rc.h"

static fabgl::FileBrowser *fs_ = nullptr;
static char cas_dir_[192] = {0};
// Active CAS subfolder, relative to /SD/MC-1000/CAS ("" = the CAS root).
// Every CAS operation (LOAD, SAVE, recovery, browser) uses cas_dir_.
static char cas_sub_[128] = {0};
static fabgl::FileBrowser *txt_fs_ = nullptr;
static char txt_dir_[96] = {0};
static fabgl::FileBrowser *bin_fs_ = nullptr;
static char bin_dir_[96] = {0};

// Find existing components without assuming the spelling/case on the card.
// On FAT cards, directories may have been created on another computer using
// MC-1000, mc-1000, Mc-1000, CAS, cas, Cas, etc.
static bool find_child_dir(const char *parent, const char *wanted, char *result,
                           size_t size) {
  DIR *dir = opendir(parent);
  if (!dir) return false;
  bool found = false;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcasecmp(entry->d_name, wanted) != 0) continue;
    const int n = snprintf(result, size, "%s/%s", parent, entry->d_name);
    if (n < 0 || (size_t)n >= size) break;
    struct stat st;
    if (stat(result, &st) == 0 && S_ISDIR(st.st_mode)) { found = true; break; }
  }
  closedir(dir);
  return found;
}

static bool create_child_dir(const char *parent, const char *wanted,
                             char *result, size_t size) {
  if (find_child_dir(parent, wanted, result, size)) return true;
  const int n = snprintf(result, size, "%s/%s", parent, wanted);
  if (n < 0 || (size_t)n >= size) return false;
  if (mkdir(result, 0777) != 0 && errno != EEXIST) return false;
  // If another spelling already existed, preserve its actual capitalization.
  return find_child_dir(parent, wanted, result, size);
}

bool cas_directory_ready() {
  if (!fabgl::FileBrowser::mountedSDCard()) return false;
  char machine[96];
  if (!create_child_dir(SD_MOUNT_PATH, "MC-1000", machine, sizeof(machine))) return false;
  char root[96];
  if (!create_child_dir(machine, "CAS", root, sizeof(root))) return false;
  char target[sizeof(cas_dir_)];
  strncpy(target, root, sizeof(target) - 1);
  target[sizeof(target) - 1] = 0;
  if (cas_sub_[0]) {
    // Fall back to the CAS root if the subfolder disappeared (card swap).
    char full[sizeof(cas_dir_)];
    const int n = snprintf(full, sizeof(full), "%s/%s", root, cas_sub_);
    struct stat st;
    if (n > 0 && (size_t)n < sizeof(full) && stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
      memcpy(target, full, (size_t)n + 1);
    } else {
      Serial.printf("[MC1000][CAS] subfolder lost: %s\n", cas_sub_);
      cas_sub_[0] = 0;
    }
  }
  if (strcmp(cas_dir_, target) != 0) {
    // After initialization, this only changes if the SD was remounted
    // or the user entered another CAS subfolder in the browser.
    // Rebuilding the browser avoids stale directory listings.
    delete fs_;
    fs_ = nullptr;
    strncpy(cas_dir_, target, sizeof(cas_dir_) - 1);
    cas_dir_[sizeof(cas_dir_) - 1] = 0;
  }
  return true;
}

const char *cas_subdirectory() { return cas_sub_; }

bool cas_set_subdirectory(const char *sub) {
  if (!sub) sub = "";
  const size_t n = strlen(sub);
  if (n >= sizeof(cas_sub_)) return false;
  char previous[sizeof(cas_sub_)];
  memcpy(previous, cas_sub_, sizeof(previous));
  memcpy(cas_sub_, sub, n + 1);
  if (!cas_directory_ready() || strcmp(cas_sub_, sub) != 0) {
    memcpy(cas_sub_, previous, sizeof(cas_sub_));
    cas_directory_ready();
    return false;
  }
  return true;
}

bool cas_enter_subdirectory(const char *name) {
  if (!name || !*name || strchr(name, '/') || strchr(name, '\\')) return false;
  char next[sizeof(cas_sub_)];
  if (strcmp(name, "..") == 0) {
    if (!cas_sub_[0]) return false;  // already at /MC-1000/CAS
    memcpy(next, cas_sub_, sizeof(next));
    char *slash = strrchr(next, '/');
    if (slash) *slash = 0; else next[0] = 0;
  } else {
    const int n = cas_sub_[0]
        ? snprintf(next, sizeof(next), "%s/%s", cas_sub_, name)
        : snprintf(next, sizeof(next), "%s", name);
    if (n < 0 || (size_t)n >= sizeof(next)) return false;
  }
  return cas_set_subdirectory(next);
}

bool txt_directory_ready() {
  if (!fabgl::FileBrowser::mountedSDCard()) return false;
  char machine[96];
  if (!create_child_dir(SD_MOUNT_PATH, "MC-1000", machine, sizeof(machine))) return false;
  char target[96];
  if (!create_child_dir(machine, "TXT", target, sizeof(target))) return false;
  if (strcmp(txt_dir_, target) != 0) {
    delete txt_fs_;
    txt_fs_ = nullptr;
    strncpy(txt_dir_, target, sizeof(txt_dir_) - 1);
    txt_dir_[sizeof(txt_dir_) - 1] = 0;
  }
  return true;
}

bool bin_directory_ready() {
  if (!fabgl::FileBrowser::mountedSDCard()) return false;
  char machine[96];
  if (!create_child_dir(SD_MOUNT_PATH, "MC-1000", machine, sizeof(machine))) return false;
  char target[96];
  if (!create_child_dir(machine, "BIN", target, sizeof(target))) return false;
  if (strcmp(bin_dir_, target) != 0) {
    delete bin_fs_;
    bin_fs_ = nullptr;
    strncpy(bin_dir_, target, sizeof(bin_dir_) - 1);
    bin_dir_[sizeof(bin_dir_) - 1] = 0;
  }
  return true;
}

const char *bin_directory_path() { return bin_dir_; }

fabgl::FileBrowser *bin_file_browser() {
  if (!bin_directory_ready()) return nullptr;
  if (!bin_fs_) {
    bin_fs_ = new (std::nothrow) fabgl::FileBrowser(bin_dir_);
    if (!bin_fs_) Serial.println("[MC1000][BIN] FileBrowser allocation failed");
  }
  return bin_fs_;
}

bool bin_find_existing_filename(const char *wanted, char *actual, size_t actualSize) {
  return bin_directory_ready() &&
         MC1000CAS::findExistingFilename(bin_dir_, wanted, actual, actualSize);
}

const char *txt_directory_path() { return txt_dir_; }

fabgl::FileBrowser *txt_file_browser() {
  if (!txt_directory_ready()) return nullptr;
  if (!txt_fs_) {
    txt_fs_ = new (std::nothrow) fabgl::FileBrowser(txt_dir_);
    if (!txt_fs_) Serial.println("[MC1000][TXT] FileBrowser allocation failed");
  }
  return txt_fs_;
}

const char *cas_directory_path() { return cas_dir_; }

bool cas_find_existing_filename(const char *wanted, char *actual, size_t actualSize) {
  if (!cas_directory_ready()) return false;
  return MC1000CAS::findExistingFilename(cas_dir_, wanted, actual, actualSize);
}

bool cas_recover_named(const char *casFilename) {
  if (!cas_directory_ready()) return false;
  const auto result = MC1000CAS::recoverCassette(cas_directory_path(), casFilename);
  if (result == MC1000CAS::RecoveryResult::Error) {
    Serial.printf("[MC1000][CAS] recovery failed for %s\n", casFilename);
    return false;
  }
  if (result != MC1000CAS::RecoveryResult::None) {
    Serial.printf("[MC1000][CAS] recovered %s (%s)\n", casFilename,
      result == MC1000CAS::RecoveryResult::RestoredBackup ? "BACKUP" : "TMP");
    if (fs_) fs_->reload();
  }
  return true;
}

bool cas_protect_old_temp(const char *casFilename) {
  if (!cas_directory_ready()) return false;
  const bool ok = MC1000CAS::protectPreviousTemp(cas_directory_path(), casFilename);
  if (ok && fs_) fs_->reload();
  return ok;
}

void cas_recover_directory() {
  if (!cas_directory_ready()) return;
  // Scan only on mount/browser entry, not on the emulation hot path. Also
  // recognize incomplete cases with no .CAS file in the directory listing.
  DIR *dir = opendir(cas_directory_path());
  if (!dir) return;
  // Allocating entries on the heap avoids a large array on the ESP32's
  // modest FreeRTOS task stack. Do not mutate an open directory iterator.
  struct RecoveryNode { char name[240]; RecoveryNode *next; };
  RecoveryNode *head = nullptr;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    const char *nm = entry->d_name;
    const size_t len = strlen(nm);
    size_t suffix = 0;
    if (len > 4 && strcasecmp(nm + len - 4, ".TMP") == 0) suffix = 4;
    else if (len > 4 && strncasecmp(nm + len - 4, ".BA", 3) == 0 &&
             nm[len-1] >= '0' && nm[len-1] <= '9') suffix = 4;
    if (!suffix || len - suffix + 4 >= sizeof(((RecoveryNode*)0)->name)) continue;
    char candidate[240];
    memcpy(candidate, nm, len - suffix);
    candidate[len - suffix] = 0;
    // Recognize the legacy BASE.CAS.TMP and BASE.CAS.BA0 spellings.
    if (!MC1000CAS::hasCASExtension(candidate))
      memcpy(candidate + len - suffix, ".CAS", 5);
    bool already = false;
    for (RecoveryNode *n = head; n; n = n->next)
      if (strcasecmp(n->name, candidate) == 0) { already = true; break; }
    if (already) continue;
    RecoveryNode *n = (RecoveryNode *)malloc(sizeof(RecoveryNode));
    if (!n) {
      Serial.println("[MC1000][CAS] recovery scan out of memory");
      break;
    }
    strcpy(n->name, candidate);
    n->next = head;
    head = n;
  }
  closedir(dir);
  while (head) {
    RecoveryNode *next = head->next;
    cas_recover_named(head->name);
    free(head);
    head = next;
  }
  if (fs_) fs_->reload();
}

// SD on HSPI: CS=13, CLK=14, MOSI=12. MISO differs per board:
// LilyGO TTGO VGA32 = GPIO2, ROBGO_RG = GPIO35. Try both.
// (FabGL itself forces MISO=2 on ESP32-PICO-D4 and 35 on ESP32-D0WDQ5.)
static bool mount_sd_with_miso(int miso) {
  if (fabgl::FileBrowser::mountSDCard(false, SD_MOUNT_PATH, 4, 16 * 1024,
                                      miso, 12, 14, 13)) {
    Serial.printf("[MC1000] SD montado (MISO=%d)\n", miso);
    return true;
  }
  // Release the SPI bus so the next attempt re-initializes it with the
  // other MISO pin (otherwise FabGL silently reuses the old bus config).
  spi_bus_free((spi_host_device_t)HSPI_HOST);
  Serial.printf("[MC1000] SD falhou com MISO=%d\n", miso);
  return false;
}

void init_fs() {
  const bool mounted = mount_sd_with_miso(2) || mount_sd_with_miso(35);
  // bootl.rc must be read before the PS/2 keyboard is started.
  bootrc_load(mounted ? SD_MOUNT_PATH : nullptr);
  if (!mounted) {
    Serial.printf("[MC1000] Mounting SD failed at %s\n", SD_MOUNT_PATH);
  } else if (!cas_directory_ready()) {
    Serial.println("[MC1000] Cannot create/find /MC-1000/CAS");
  } else {
    Serial.printf("[MC1000] CAS directory: %s\n", cas_directory_path());
    cas_recover_directory();
  }
}

fabgl::FileBrowser *file_browser() {
  if (!cas_directory_ready()) return nullptr;
  if (!fs_) {
    fs_ = new (std::nothrow) fabgl::FileBrowser(cas_dir_);
    if (!fs_) Serial.println("[MC1000][MEM] FileBrowser allocation failed");
  }
  return fs_;
}

FILE *ext_fopen(const char *pathname, const char *mode) {
  auto *browser = file_browser();
  return browser ? browser->openFile(pathname, mode) : nullptr;
}

void ext_remove(const char *pathname) {
  auto *browser = file_browser();
  if (browser) browser->remove(pathname);
  reload_dir();
}

void reload_dir() {
  auto *browser = file_browser();
  if (browser) browser->reload();
}

uint32_t get_timestamp_ms() {
  return esp_timer_get_time()/1000;
}
