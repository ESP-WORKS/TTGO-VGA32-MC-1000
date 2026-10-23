/* SPDX-License-Identifier: MIT
 * Host SD filename rules for MC-1000 cassette files.
 * The original 14-byte tape header is a separate entity: do not change it.
 */
#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

namespace MC1000CAS {

inline bool hasCASExtension(const char *name) {
  if (!name) return false;
  const size_t n = strlen(name);
  return n >= 4 && name[n - 4] == '.' &&
         (name[n - 3] == 'C' || name[n - 3] == 'c') &&
         (name[n - 2] == 'A' || name[n - 2] == 'a') &&
         (name[n - 1] == 'S' || name[n - 1] == 's');
}

// Append .CAS only when the *last* extension is not already .CAS.
// In particular, FILE.TXT becomes FILE.TXT.CAS, not FILE.CAS.
// Input is a basename (never an SD path); output is null-terminated.
inline bool makeSaveFilename(const char *name, char *out, size_t outSize) {
  if (!name || !*name || !out || !outSize || strcmp(name, ".") == 0 ||
      strcmp(name, "..") == 0) return false;
  const size_t len = strlen(name);
  for (size_t i = 0; i < len; ++i)
    if (name[i] == '/' || name[i] == '\\') return false;
  const size_t suffix = hasCASExtension(name) ? 0 : 4;
  if (len + suffix >= outSize) return false;
  memcpy(out, name, len);
  if (suffix) memcpy(out + len, ".CAS", 4);
  out[len + suffix] = 0;
  return true;
}

// F9 returns the physical SD directory spelling, not necessarily uppercase.
// Keep that exact spelling (including mixed-case .CaS) while storing the
// basename without its final CAS extension for existing LOAD/SAVE semantics.
// Reject overlong selections rather than silently selecting a different file.
inline bool selectedCASBaseName(const char *filename, char *out, size_t outSize) {
  if (!filename || !out || !outSize) return false;
  const char *base = filename;
  for (const char *p = filename; *p; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  if (!hasCASExtension(base)) return false;
  const size_t n = strlen(base) - 4;
  if (!n || n >= outSize) return false;
  memcpy(out, base, n);
  out[n] = 0;
  return true;
}

// F9 marker comparison follows FAT case-insensitive filename rules, using
// complete physical names rather than their shortened OSD labels.
inline bool sameCASFileName(const char *a, const char *b) {
  return a && b && *a && *b && strcasecmp(a, b) == 0;
}

// FAT is case-insensitive. Look up the actual on-card spelling before any
// existence check or rename, even if the mounted VFS uses exact-case stat().
inline bool findExistingFilename(const char *folder, const char *wanted,
                                 char *actual, size_t actualSize) {
  if (!folder || !wanted || !*wanted || !actual || !actualSize) return false;
  DIR *dir = opendir(folder);
  if (!dir) return false;
  bool found = false;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcasecmp(entry->d_name, wanted) != 0) continue;
    char path[320];
    const int n = snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
    if (n < 0 || (size_t)n >= sizeof(path)) continue;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    const size_t actualLen = strlen(entry->d_name);
    if (actualLen >= actualSize) continue;
    memcpy(actual, entry->d_name, actualLen + 1);
    found = true;
    break;
  }
  closedir(dir);
  return found;
}

} // namespace MC1000CAS
