/* SPDX-License-Identifier: MIT
 * Recovery of interrupted SD cassette commits. Portable FAT-friendly file I/O.
 * Never overwrite an unverified CAS and never silently discard a temporary.
 */
#pragma once
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "cas_filename.h"
#include "cas_format.h"

namespace MC1000CAS {

enum class RecoveryResult { None, RestoredBackup, PromotedTemp, Error };

inline bool casPath(const char *dir, const char *file, char *path, size_t capacity) {
  if (!dir || !file || !*file || strchr(file, '/') || strchr(file, '\\')) return false;
  const int n = snprintf(path, capacity, "%s/%s", dir, file);
  return n > 0 && (size_t)n < capacity;
}

inline bool fileExists(const char *dir, const char *file) {
  char actual[256];
  return findExistingFilename(dir, file, actual, sizeof(actual));
}

// Treat a cassette as valid only if its real size exactly matches its header.
inline bool validCassetteFile(const char *dir, const char *file) {
  char path[512];
  if (!casPath(dir, file, path, sizeof(path))) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  bool ok = fseek(f, 0, SEEK_END) == 0;
  const long size = ok ? ftell(f) : -1;
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) ok = false;
  uint8_t hdr[HEADER_SIZE] = {};
  if (ok) ok = fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
  fclose(f);
  Header parsed;
  return ok && decode(hdr, (uint32_t)(size < 0 ? 0 : size), &parsed);
}

inline bool renameCasFile(const char *dir, const char *src, const char *dst) {
  char from[512], to[512];
  return casPath(dir, src, from, sizeof(from)) &&
         casPath(dir, dst, to, sizeof(to)) && rename(from, to) == 0;
}

inline bool casSibling(const char *casFile, const char *extension,
                       char *out, size_t capacity) {
  if (!hasCASExtension(casFile)) return false;
  const size_t len = strlen(casFile) - 4;
  const size_t suffix = strlen(extension);
  if (!len || len + suffix >= capacity) return false;
  memcpy(out, casFile, len);
  memcpy(out + len, extension, suffix + 1);
  return true;
}

// New releases use BASE.TMP/BASE.BA0; v0.12 could also generate
// BASE.CAS.TMP/BASE.CAS.BA0 when BASIC supplied the .CAS extension.
inline bool findSibling(const char *dir, const char *casFile, const char *ext,
                        char *actual, size_t capacity) {
  char base[256];
  if (casSibling(casFile, ext, base, sizeof(base)) &&
      findExistingFilename(dir, base, actual, capacity) &&
      validCassetteFile(dir, actual)) return true;
  char legacy[256];
  const size_t a = strlen(casFile), b = strlen(ext);
  if (a + b >= sizeof(legacy)) return false;
  memcpy(legacy, casFile, a);
  memcpy(legacy + a, ext, b + 1);
  return findExistingFilename(dir, legacy, actual, capacity) &&
         validCassetteFile(dir, actual);
}

inline bool parkOneTemp(const char *dir, const char *temp) {
  const size_t len = strlen(temp);
  if (len < 5 || strcasecmp(temp + len - 4, ".TMP") != 0) return false;
  for (unsigned i = 0; i < 10; ++i) {
    char dest[256];
    const int n = snprintf(dest, sizeof(dest), "%.*s.REC%u",
                           (int)(len - 4), temp, i);
    if (n < 0 || (size_t)n >= sizeof(dest) || fileExists(dir, dest)) continue;
    return renameCasFile(dir, temp, dest);
  }
  return false;
}

// Preserve both styles of TMP. If the archive directory is full, block SAVE.
inline bool protectPreviousTemp(const char *dir, const char *casFile) {
  char actual[256], newTemp[256], legacy[256];
  if (!casSibling(casFile, ".TMP", newTemp, sizeof(newTemp))) return false;
  const int n = snprintf(legacy, sizeof(legacy), "%s.TMP", casFile);
  if (n < 0 || (size_t)n >= sizeof(legacy)) return false;
  if (findExistingFilename(dir, newTemp, actual, sizeof(actual)) &&
      !parkOneTemp(dir, actual)) return false;
  if (strcasecmp(newTemp, legacy) != 0 &&
      findExistingFilename(dir, legacy, actual, sizeof(actual)) &&
      !parkOneTemp(dir, actual)) return false;
  return true;
}

inline RecoveryResult recoverCassette(const char *dir, const char *casFile) {
  if (!dir || !hasCASExtension(casFile)) return RecoveryResult::Error;
  char target[256] = {}, actualTarget[256] = {};
  if (strlen(casFile) >= sizeof(target)) return RecoveryResult::Error;
  strcpy(target, casFile);
  bool exists = findExistingFilename(dir, target, actualTarget, sizeof(actualTarget));
  if (exists && validCassetteFile(dir, actualTarget)) return RecoveryResult::None;

  // Prefer the original cassette saved by the overwrite transaction. A valid
  // temporary is a fallback only if no intact backup exists.
  char source[256] = {};
  for (unsigned i = 0; i < 10; ++i) {
    char actual[256], ext[8];
    snprintf(ext, sizeof(ext), ".BA%u", i);
    if (findSibling(dir, target, ext, actual, sizeof(actual)) &&
        validCassetteFile(dir, actual)) {
      strcpy(source, actual);
      break;
    }
  }
  bool backup = source[0] != 0;
  if (!backup) {
    char actual[256];
    if (findSibling(dir, target, ".TMP", actual, sizeof(actual)) &&
        validCassetteFile(dir, actual)) strcpy(source, actual);
  }
  if (!source[0]) return RecoveryResult::None;

  // Preserve a damaged destination under .BAD0..9, and roll back on failure.
  char movedBad[256] = {};
  if (exists) {
    bool moved = false;
    for (unsigned i = 0; i < 10; ++i) {
      char ext[8], candidate[256];
      snprintf(ext, sizeof(ext), ".BAD%u", i);
      if (!casSibling(target, ext, candidate, sizeof(candidate))) return RecoveryResult::Error;
      if (fileExists(dir, candidate)) continue;
      if (!renameCasFile(dir, actualTarget, candidate)) return RecoveryResult::Error;
      strcpy(movedBad, candidate);
      moved = true;
      break;
    }
    if (!moved) return RecoveryResult::Error;
  }
  if (!renameCasFile(dir, source, target)) {
    if (movedBad[0]) renameCasFile(dir, movedBad, actualTarget);
    return RecoveryResult::Error;
  }
  return backup ? RecoveryResult::RestoredBackup : RecoveryResult::PromotedTemp;
}

} // namespace MC1000CAS
