/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 MC-1000 emulator contributors
 * MC-1000 ROM 18-byte cassette record header: 14 name bytes, LE start/end.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace MC1000CAS {
static const size_t HEADER_SIZE = 18;
struct Header {
  uint8_t name[14];
  uint16_t start;
  uint16_t end;
  size_t length;
};
inline void encode(uint8_t out[HEADER_SIZE], const uint8_t name[14], uint16_t start, uint16_t end) {
  for (int i = 0; i < 14; ++i) out[i] = name[i];
  out[14] = (uint8_t)start;
  out[15] = (uint8_t)(start >> 8);
  out[16] = (uint8_t)end;
  out[17] = (uint8_t)(end >> 8);
}
inline bool decode(const uint8_t in[HEADER_SIZE], uint32_t fileSize, Header *out) {
  if (!in || !out || fileSize < HEADER_SIZE) return false;
  const uint16_t start = (uint16_t)in[14] | ((uint16_t)in[15] << 8);
  const uint16_t end = (uint16_t)in[16] | ((uint16_t)in[17] << 8);
  // A single MC-1000 CAS contains exactly one 18-byte header and the
  // declared body. Reject both truncated files and trailing garbage/records:
  // accepting either could silently load an unintended cassette image.
  if (end <= start || fileSize != (uint32_t)HEADER_SIZE + (uint32_t)(end - start))
    return false;
  for (int i = 0; i < 14; ++i) out->name[i] = in[i];
  out->start = start;
  out->end = end;
  out->length = end - start;
  return true;
}
} // namespace MC1000CAS
