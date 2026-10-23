/* SPDX-License-Identifier: MIT
 * Validate and copy cassette blocks to the effective MC-1000 write banks.
 * The cassette reader stages all bytes before committing any guest memory.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "z80_memory_pages.h"

namespace MC1000CAS {
inline bool mappedCassetteDestination(const Z80MemoryPages &pages,
                                     uint8_t *lowRam, uint16_t dest,
                                     size_t length) {
  if (!lowRam || !length || dest < 0x0200 ||
      (uint32_t)dest + length > 0xC000u) return false;
  uint32_t addr=dest, end=addr+(uint32_t)length;
  while (addr<end) {
    // The lower ROM overlay is disabled after a successful BASIC cassette
    // load, just as in the existing loader. Defer that bank switch until
    // all ranges have been checked and data has been read successfully.
    if (addr>=0x2000 && !pages.WritePage(addr >> 11)) return false;
    const uint32_t next=(addr & ~0x7FFu)+0x800u;
    addr = next<end ? next : end;
  }
  return true;
}
inline bool copyCassetteToMappedMemory(const Z80MemoryPages &pages,
                                      uint8_t *lowRam, uint16_t dest,
                                      const uint8_t *data, size_t length) {
  if (!data || !mappedCassetteDestination(pages,lowRam,dest,length)) return false;
  uint32_t addr=dest, pos=0;
  while (pos<length) {
    const uint32_t remain=(uint32_t)length-pos;
    const uint32_t n=0x800u-(addr & 0x7ffu);
    const size_t chunk=remain<n ? remain:n;
    uint8_t *page=addr<0x2000 ? lowRam+(addr & ~0x7ffu) : pages.WritePage(addr >> 11);
    memcpy(page+(addr & 0x7ffu),data+pos,chunk);
    addr+=(uint32_t)chunk;pos+=chunk;
  }
  return true;
}
} // namespace MC1000CAS
