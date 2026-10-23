/* SPDX-License-Identifier: MIT
 * Host extension for the MC-1000 BASIC: TSAVE produces a headerless game BIN
 * accepted by MC1000BIN::identify() and executed by TLOAD at $0200.
 * No change to the user's MC-1000 ROM or the F1 menu is required.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "bin_game_format.h"

namespace MC1000TSAVE {
struct Spec {
  char filename[64]; // safe 8-bit FAT basename (including .BIN)
  uint16_t start, end, entry;
};
enum class ParseResult { NotCommand, Help, Valid, Invalid };
static inline void spaces(const char *&p) {
  while (*p == ' ' || *p == '\t') ++p;
}
static inline bool decimal(const char *&p, uint16_t &out) {
  spaces(p);
  if (*p < '0' || *p > '9') return false;
  uint32_t n = 0;
  do {
    const uint32_t digit = (uint32_t)(*p++ - '0');
    if (n > (65535u - digit) / 10u) return false;
    n = n * 10u + digit;
  } while (*p >= '0' && *p <= '9');
  out = (uint16_t)n;
  spaces(p);
  return true;
}
static inline bool safeName(const char *source, size_t length, char *out, size_t capacity) {
  if (!source || !out || length == 0 || length > 48) return false;
  // Explicit .BIN is accepted, but no other dots, paths or FAT special chars.
  if (length > 4 && source[length-4] == '.' &&
      toupper((unsigned char)source[length-3]) == 'B' &&
      toupper((unsigned char)source[length-2]) == 'I' &&
      toupper((unsigned char)source[length-1]) == 'N') length -= 4;
  if (length == 0 || length + 5 > capacity) return false;
  for (size_t i=0;i<length;++i) {
    const unsigned char c = (unsigned char)source[i];
    // ASCII-only is portable across FAT name normalization on ESP32.
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') || c == '_' || c == '-')) return false;
    out[i] = (char)toupper(c);
  }
  memcpy(out + length, ".BIN", 5);
  return true;
}
static inline bool validSpec(const Spec &s) {
  // $0200-$02FF is BASIC's live command line buffer. TSAVE typed at the
  // prompt has already modified that region; never pretend to save it.
  return s.start >= 0x0300 && s.end < 0xc000 && s.end >= s.start &&
         s.entry >= s.start && s.entry <= s.end &&
         strlen(s.filename) > 4 && strlen(s.filename) < sizeof(s.filename);
}
static inline ParseResult parse(const char *line, Spec *out) {
  if (!line || !out) return ParseResult::NotCommand;
  const char *p = line;
  spaces(p);
  for (const char *kw="TSAVE"; *kw; ++kw, ++p)
    if (toupper((unsigned char)*p) != *kw) return ParseResult::NotCommand;
  if (*p && *p != ' ' && *p != '\t' && *p != '\"') return ParseResult::NotCommand;
  spaces(p);
  if (!*p) return ParseResult::Help;
  if (*p++ != '"') return ParseResult::Invalid;
  const char *name = p;
  while (*p && *p != '"') ++p;
  if (*p != '"') return ParseResult::Invalid;
  const size_t length = (size_t)(p - name);
  Spec spec = {};
  if (!safeName(name, length, spec.filename, sizeof(spec.filename))) return ParseResult::Invalid;
  ++p;
  spaces(p);
  if (*p++ != ',' || !decimal(p, spec.start)) return ParseResult::Invalid;
  if (*p++ != ',' || !decimal(p, spec.end)) return ParseResult::Invalid;
  spec.entry = spec.start;
  if (*p == ',') {
    ++p;
    if (!decimal(p, spec.entry)) return ParseResult::Invalid;
  }
  spaces(p);
  if (*p || !validSpec(spec)) return ParseResult::Invalid;
  *out = spec;
  return ParseResult::Valid;
}
static inline size_t payloadLength(const Spec &s) {
  return (size_t)s.end - MC1000BIN::kGameRAMStart + 1;
}
// Caller stages payloadLength() bytes and commits to SD only if identify()
// accepts the complete result. 'ram' covers $0000-$BFFF (48 KiB).
static inline bool build(const Spec &s, const uint8_t *ram, uint8_t *dst,
                         size_t capacity) {
  if (!validSpec(s) || !ram || !dst || capacity < payloadLength(s)) return false;
  const size_t length = payloadLength(s);
  memset(dst, 0, length);
  // Guest runtime state, not the BASIC command input buffer. Never mutate RAM.
  memcpy(dst, ram + 0x100, 0x100);
  dst[0x104 - 0x100] = 1; // PLAY
  dst[0x105 - 0x100] = 1; // PLAYMX
  dst[0x106 - 0x100] = 0; // HEAD = game mode
  dst[0x120 - 0x100] = 0xc9; // JOB: RET, no stale BASIC hook
  dst[0x130 - 0x100] = 0xc9; // JOBM: RET, no stale BASIC hook
  dst[0x139 - 0x100] = 0; // AVALUE
  dst[0x142 - 0x100] = 0; // BVALUE
  dst[0x14b - 0x100] = 0; // CVALUE
  dst[0x15e - 0x100] = 0; // CHECK
  // Entry trampoline. Padding $0203..(start-1) is zero-filled, and the
  // application remains at its ORIGINAL absolute Z80 addresses.
  dst[MC1000BIN::kCodeOffset + 0] = 0xc3; // JP nn
  dst[MC1000BIN::kCodeOffset + 1] = (uint8_t)s.entry;
  dst[MC1000BIN::kCodeOffset + 2] = (uint8_t)(s.entry >> 8);
  memcpy(dst + s.start - 0x100, ram + s.start, (size_t)s.end - s.start + 1);
  MC1000BIN::Image image = {};
  return MC1000BIN::identify(dst, length, &image) && image.offset == 0 &&
         image.length == length;
}
} // namespace MC1000TSAVE
