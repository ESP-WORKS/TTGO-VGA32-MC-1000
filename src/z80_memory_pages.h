/* SPDX-License-Identifier: MIT
 * MC-1000 CPU bus: direct 2 KiB page lookup for standard RAM / ROM / VRAM.
 * High ROM ($C000-$FFFF) stays in the original hardware decoder because a
 * read there disables the low ROM bank. No original ROM is embedded here.
 */
#pragma once
#include <stdint.h>

class Z80MemoryPages {
public:
  void Configure(uint8_t *ram, uint8_t *vram, uint8_t *crtc, uint8_t *lowRom,
                 bool lowRomEnabled, bool videoBankRam, bool crtcBank,
                 bool expansion) {
    for (unsigned i=0; i<24; ++i) { read_[i]=nullptr; write_[i]=nullptr; }
    for (unsigned i=0; i<4; ++i) {
      read_[i] = lowRomEnabled ? at(lowRom, i) : at(ram, i);
      if (!lowRomEnabled) write_[i] = at(ram, i);
    }
    // $2000-$27FF is an independent optional CRTC RAM bank.
    read_[4] = write_[4] = crtcBank ? crtc : at(ram, 4);
    for (unsigned i=5; i<8; ++i)
      read_[i] = write_[i] = at(ram, i);
    if (expansion) {
      for (unsigned i=8; i<16; ++i) read_[i]=write_[i]=at(ram, i);
      for (unsigned i=20; i<24; ++i) read_[i]=write_[i]=at(ram, i);
    }
    for (unsigned i=16; i<20; ++i)
      read_[i] = write_[i] = videoBankRam
        ? (expansion ? at(ram, i) : nullptr) : at(vram, i-16);
  }
  // Expose the already-configured page aliases to the CPU's hot bus callbacks.
  // Banks are republished only when the MC-1000 I/O decoder changes them.
  const uint8_t *ReadPage(unsigned page) const {
    return page < 24 ? read_[page] : nullptr;
  }
  uint8_t *WritePage(unsigned page) const {
    return page < 24 ? write_[page] : nullptr;
  }
  inline bool Read(uint16_t address, uint8_t &value) const {
    if (address >= 0xC000) return false;
    const uint8_t *p = read_[address >> 11];
    if (!p) return false;
    value = p[address & 0x07FF];
    return true;
  }
  inline bool Write(uint16_t address, uint8_t value) const {
    if (address >= 0xC000) return false;
    uint8_t *p = write_[address >> 11];
    if (!p) return false;
    p[address & 0x07FF] = value;
    return true;
  }
private:
  static uint8_t *at(uint8_t *base, unsigned page) {
    return base ? base + page * 0x0800 : nullptr;
  }
  uint8_t *read_[24] = {};
  uint8_t *write_[24] = {};
};
