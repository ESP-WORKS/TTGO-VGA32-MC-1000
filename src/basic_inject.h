/* SPDX-License-Identifier: MIT
 * MC-1000 BASIC TXT injector. Token encoding and system pointers adapted
 * from the MIT-licensed jsmc1000 MC1000.js setBasicProgram() implementation.
 * Copyright (c) 2012-2024 jsmc1000 contributors.
 * The guest RAM is not touched until both passes have succeeded.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace MC1000Basic {

enum class Status {
  OK, InvalidFormat, TooLarge, ReadError, NoMemory, NotReady
};

struct Result {
  Status status;
  size_t bytes;
  unsigned lines;
};

// Keep token order identical to jsmc1000/MC1000.js MC1000.TOKENS.
static const char *const tokens[] = {
  "END", "FOR", "NEXT", "DATA", "EXIT", "INPUT", "DIM", "READ",
  "LET", "GOTO", "RUN", "IF", "RESTORE", "GOSUB", "RETURN", "REM",
  "STOP", "OUT", "ON", "HOME", "WAIT", "DEF", "POKE", "PRINT",
  "PR#", "SOUND", "GR", "HGR", "COLOR", "TEXT", "PLOT", "TRO",
  "UNPLOT", "SET", "CALL", "DRAW", "UNDRAW", "TEMPO", "WIDTH", "CONT",
  "LIST", "CLEAR", "LOAD", "SAVE", "NEW", "TLOAD", "COLUMN", "AUTO",
  "FAST", "SLOW", "EDIT", "INVERSE", "NORMAL", "DEBUG", "TAB(", "TO",
  "FN", "SPC(", "THEN", "NOT", "STEP", "VTAB(", "+", "-",
  "*", "/", "^", "AND", "OR", ">", "=", "<",
  "SGN", "INT", "ABS", "USR", "FRE", "INP", "POS", "SQR",
  "RND", "LOG", "EXP", "COS", "SIN", "TAN", "ATN", "PEEK",
  "LEN", "STR$", "VAL", "ASC", "CHR$", "LEFT$", "RIGHT$", "MID$"
};
static constexpr size_t kTokenCount = sizeof(tokens) / sizeof(tokens[0]);
static constexpr size_t kMaxSourceLine = 4096;

inline bool asciiDigit(char c) { return c >= '0' && c <= '9'; }
inline char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }
inline int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = upper(c);
  return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// An output sink that can count bytes (dst=nullptr) or fill an exact-size
// staging buffer. No Z80 memory is written in either mode.
struct Sink {
  uint8_t *dst;
  size_t capacity;
  size_t used;
  bool put(uint8_t b) {
    if (used >= capacity) return false;
    if (dst) dst[used] = b;
    ++used;
    return true;
  }
  bool patch(size_t at, uint16_t v) {
    if (at + 1 >= used) return false;
    if (dst) { dst[at] = (uint8_t)v; dst[at+1] = (uint8_t)(v >> 8); }
    return true;
  }
};

enum class Mode { Token, DataLeading, Data, Rem, RemLeading, String };

// Check that a numbered line starts with a BASIC statement or a variable
// assignment. This is a format check, not a full BASIC syntax checker; the
// ROM still interprets statements at RUN time.
inline bool looksLikeBasic(const char *line, size_t j, size_t n) {
  while (j < n && (line[j] == ' ' || line[j] == '\t' || line[j] == ':')) ++j;
  if (j == n) return false;
  if (line[j] == '?') return true;  // BASIC PRINT shorthand
  // Statement tokens in jsmc1000 token table occupy indices 0..53.
  for (size_t t = 0; t <= 53; ++t) {
    const size_t len = strlen(tokens[t]);
    if (j + len > n) continue;
    size_t k = 0;
    while (k < len && upper(line[j+k]) == tokens[t][k]) ++k;
    if (k == len) return true;
  }
  // BASIC also permits implicit LET, e.g. A=1 or X$(I)="HELLO".
  if (!((line[j] >= 'A' && line[j] <= 'Z') ||
        (line[j] >= 'a' && line[j] <= 'z'))) return false;
  for (; j < n && line[j] != ':'; ++j) {
    if (line[j] == '=') return true;
    if (line[j] == '\"') break;
  }
  return false;
}

inline Status encodeLine(const char *line, size_t n, uint16_t start,
                         Sink &out, unsigned &lineCount) {
  size_t j = 0;
  while (j < n && (line[j] == ' ' || line[j] == '\t')) ++j;
  if (j == n) return Status::OK;  // blank line
  if (!asciiDigit(line[j])) return Status::InvalidFormat;
  unsigned number = 0;
  do {
    number = number * 10 + (unsigned)(line[j++] - '0');
    if (number > 65535) return Status::InvalidFormat;
  } while (j < n && asciiDigit(line[j]));
  // Follow jsmc1000: keep TXT line order, even if line numbers are not sorted.
  if (j < n && line[j] != ' ' && line[j] != '\t' && line[j] != ':')
    return Status::InvalidFormat;
  while (j < n && (line[j] == ' ' || line[j] == '\t')) ++j;
  // A bare line number is legal (deletes a line when typed), but it must not
  // silently erase the rest of an injected program. Reject it as a TXT line.
  if (j == n || !looksLikeBasic(line, j, n)) return Status::InvalidFormat;
  const size_t link = out.used;
  if (!out.put(0) || !out.put(0) || !out.put((uint8_t)number) ||
      !out.put((uint8_t)(number >> 8))) return Status::TooLarge;
  Mode mode = Mode::Token;
  Mode resume = Mode::Token;
  for (; j < n; ++j) {
    char c = upper(line[j]);
    if (c == '\t') c = ' ';
    if ((unsigned char)c < 32 || (unsigned char)c > 126)
      return Status::InvalidFormat;
    if (mode == Mode::Token || mode == Mode::DataLeading || mode == Mode::RemLeading) {
      if (c == ' ') continue;
      if (mode == Mode::Token) {
        bool matched = false;
        for (size_t t = 0; t < kTokenCount; ++t) {
          const size_t len = strlen(tokens[t]);
          if (j + len > n) continue;
          size_t k = 0;
          while (k < len && upper(line[j+k]) == tokens[t][k]) ++k;
          if (k == len) {
            if (!out.put((uint8_t)(0x80 + t))) return Status::TooLarge;
            j += len - 1;
            matched = true;
            if (t == 3) mode = Mode::DataLeading; // DATA
            else if (t == 15 || t == 42 || t == 43) mode = Mode::RemLeading; // REM LOAD SAVE
            break;
          }
        }
        if (matched) continue;
      } else if (mode == Mode::DataLeading) mode = Mode::Data;
      else mode = Mode::Rem;
    }
    if (c == '`' && j + 1 < n) {
      const uint8_t escaped = (uint8_t)(upper(line[++j]) ^ 0x80);
      if (!escaped) return Status::InvalidFormat;
      if (!out.put(escaped)) return Status::TooLarge;
    } else if (c == '~' && j + 2 < n && hex(line[j+1]) >= 0 && hex(line[j+2]) >= 0) {
      const uint8_t v = (uint8_t)((hex(line[j+1]) << 4) | hex(line[j+2]));
      if (!v) return Status::InvalidFormat;
      if (!out.put(v)) return Status::TooLarge;
      j += 2;
    } else if (!out.put((uint8_t)c)) return Status::TooLarge;
    if (mode == Mode::Token && c == '"') { resume = Mode::Token; mode = Mode::String; }
    else if (mode == Mode::Data && c == '"') { resume = Mode::Data; mode = Mode::String; }
    else if (mode == Mode::String && c == '"') mode = resume;
    else if (mode == Mode::Data && c == ':') mode = Mode::Token;
  }
  if (!out.put(0)) return Status::TooLarge;
  const size_t next = (size_t)start + out.used;
  if (next >= 0xC000 || !out.patch(link, (uint16_t)next)) return Status::TooLarge;
  ++lineCount;
  return Status::OK;
}

inline Result pass(FILE *f, uint16_t start, Sink &sink) {
  Result result = {Status::OK, 0, 0};
  if (!f || start < 0x03d0 || start >= 0x8000) {
    result.status = Status::NotReady; return result;
  }
  // Local buffer allocated once per pass; never on the ESP32's small stack.
  char *line = (char *)malloc(kMaxSourceLine + 1);
  if (!line) { result.status = Status::NoMemory; return result; }
  size_t n = 0;
  int c;
  bool first = true;
  while ((c = fgetc(f)) != EOF) {
    if (first) {
      first = false;
      if (c == 0xEF) { // UTF-8 BOM is allowed; non-ASCII program text is not.
        if (fgetc(f) != 0xBB || fgetc(f) != 0xBF) {
          result.status = Status::InvalidFormat; break;
        }
        continue;
      }
    }
    if (c == '\n' || c == '\r') {
      if (c == '\r') { const int d = fgetc(f); if (d != '\n' && d != EOF) ungetc(d, f); }
      result.status = encodeLine(line, n, start, sink, result.lines);
      if (result.status != Status::OK) break;
      n = 0;
      continue;
    }
    if (n >= kMaxSourceLine) { result.status = Status::TooLarge; break; }
    line[n++] = (char)c;
  }
  if (result.status == Status::OK && ferror(f)) result.status = Status::ReadError;
  if (result.status == Status::OK && n)
    result.status = encodeLine(line, n, start, sink, result.lines);
  if (result.status == Status::OK && result.lines == 0) result.status = Status::InvalidFormat;
  if (result.status == Status::OK && (!sink.put(0) || !sink.put(0))) result.status = Status::TooLarge;
  result.bytes = sink.used;
  free(line);
  return result;
}

// Two complete read/parse passes. On any failure, guest RAM and the BASIC
// end pointer at $03B7/$03B8 are left unchanged. The caller must pause Z80.
inline Result inject(FILE *f, uint8_t *ram, size_t ramSize) {
  Result result = {Status::NotReady, 0, 0};
  if (!f || !ram || ramSize < 0x4000) return result;
  const uint16_t start = (uint16_t)ram[0x349] | ((uint16_t)ram[0x34a] << 8);
  if (start < 0x03d0 || start >= 0x8000 ||
      start >= ramSize || start >= 0xC000) return result;
  const size_t maxBytes = ramSize - start < (size_t)(0xC000 - start)
                        ? ramSize - start : (size_t)(0xC000 - start);
  Sink measure = {nullptr, maxBytes, 0};
  result = pass(f, start, measure);
  if (result.status != Status::OK) return result;
  uint8_t *staging = (uint8_t *)malloc(result.bytes);
  if (!staging) { result.status = Status::NoMemory; return result; }
  if (fseek(f, 0, SEEK_SET) != 0) {
    free(staging); result.status = Status::ReadError; return result;
  }
  Sink encoded = {staging, result.bytes, 0};
  Result second = pass(f, start, encoded);
  if (second.status == Status::OK &&
      (second.bytes != result.bytes || second.lines != result.lines))
    second.status = Status::ReadError;
  if (second.status == Status::OK) {
    memcpy(ram + start, staging, second.bytes);
    const uint16_t end = (uint16_t)(start + second.bytes);
    ram[0x3b7] = (uint8_t)end;
    ram[0x3b8] = (uint8_t)(end >> 8);
  }
  free(staging);
  return second;
}
} // namespace MC1000Basic
