/* SPDX-License-Identifier: MIT
 * Runtime CCE MC-1000 ROM loader. No ROM bytes are part of this source.
 */
#include "rom_sd.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

namespace MC1000ROM {
namespace {

enum class Kind { Directory, RegularFile };

bool resolveChild(const char *parent, const char *name, Kind kind,
                  char *resolved, size_t capacity) {
  DIR *dir = opendir(parent);
  if (!dir) return false;
  bool found = false;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcasecmp(entry->d_name, name) != 0) continue;
    const int length = snprintf(resolved, capacity, "%s/%s", parent, entry->d_name);
    if (length < 0 || (size_t)length >= capacity) break;
    struct stat info;
    if (stat(resolved, &info) != 0) continue;
    if (kind == Kind::Directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) {
      found = true;
      break;
    }
  }
  closedir(dir);
  return found;
}

} // anonymous namespace

Status load(const char *sdMount, uint8_t *destination, size_t capacity,
            char *resolvedPath, size_t resolvedPathSize) {
  if (!sdMount || !destination || capacity < ROM_BYTES ||
      !resolvedPath || resolvedPathSize == 0)
    return Status::INVALID_ARGUMENT;
  resolvedPath[0] = '\0';
  struct stat root;
  if (stat(sdMount, &root) != 0 || !S_ISDIR(root.st_mode))
    return Status::SD_MISSING;

  // Fixed bounds also prevent malformed directory entries from overflowing a
  // stack buffer. The resolved path is retained for boot diagnostics only.
  char machine[192], roms[192], file[192];
  if (!resolveChild(sdMount, "MC-1000", Kind::Directory, machine, sizeof(machine)))
    return Status::MC1000_DIR_MISSING;
  if (!resolveChild(machine, "ROMS", Kind::Directory, roms, sizeof(roms)))
    return Status::ROMS_DIR_MISSING;
  if (!resolveChild(roms, "MC-1000.ROM", Kind::RegularFile, file, sizeof(file)))
    return Status::ROM_FILE_MISSING;

  struct stat info;
  if (stat(file, &info) != 0) return Status::ROM_READ_FAILED;
  if (info.st_size != (off_t)ROM_BYTES) return Status::ROM_FILE_SIZE;
  if (strlen(file) + 1 > resolvedPathSize) return Status::PATH_TOO_LONG;

  FILE *stream = fopen(file, "rb");
  if (!stream) return Status::ROM_READ_FAILED;
  const size_t n = fread(destination, 1, ROM_BYTES, stream);
  const int extra = fgetc(stream);  // catches growth after the size check
  const bool good = n == ROM_BYTES && extra == EOF && !ferror(stream);
  fclose(stream);
  if (!good) {
    memset(destination, 0, ROM_BYTES);  // no partially valid ROM can be used
    return Status::ROM_READ_FAILED;
  }
  memcpy(resolvedPath, file, strlen(file) + 1);
  return Status::OK;
}

const char *statusText(Status status) {
  switch (status) {
    case Status::OK: return "OK";
    case Status::INVALID_ARGUMENT: return "ROM buffer/path invalid";
    case Status::SD_MISSING: return "SD not available";
    case Status::MC1000_DIR_MISSING: return "MC-1000 folder missing";
    case Status::ROMS_DIR_MISSING: return "ROMS folder missing";
    case Status::ROM_FILE_MISSING: return "MC-1000.ROM not found";
    case Status::ROM_FILE_SIZE: return "MC-1000.ROM must be exactly 16384 bytes";
    case Status::ROM_READ_FAILED: return "Unable to read entire MC-1000.ROM";
    case Status::PATH_TOO_LONG: return "ROM pathname too long";
  }
  return "Unknown ROM error";
}
} // namespace MC1000ROM
