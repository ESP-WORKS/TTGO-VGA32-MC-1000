/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 MC-1000 emulator contributors
 *
 * Independent instruction-level Z80 interpreter for the MC-1000 port.
 * This file does NOT derive from the previously bundled fMSX/EMULib Z80.
 * Instruction encodings, flag semantics and timing are hardware facts.
 */
#pragma once
#include <stdint.h>

#ifndef BYTE_TYPE_DEFINED
#define BYTE_TYPE_DEFINED
typedef unsigned char byte;
#endif
#ifndef WORD_TYPE_DEFINED
#define WORD_TYPE_DEFINED
typedef unsigned short uint16;
#endif

// Z80 status register bits; public architectural constants.
enum { C_FLAG=1, N_FLAG=2, P_FLAG=4, V_FLAG=4, H_FLAG=16, Z_FLAG=64, S_FLAG=128 };
enum { IFF_1=1, IFF_IM1=2, IFF_IM2=4, IFF_2=8, IFF_EI=32, IFF_HALT=128 };
enum { INT_NONE=0xffff };

typedef union Z80RegisterPair {
  uint16 W;
  struct { uint8_t l, h; } B; // ESP32 is little-endian
} Z80RegisterPair;

typedef struct Z80 {
  Z80RegisterPair AF,BC,DE,HL,IX,IY,PC,SP,AF1,BC1,DE1,HL1;
  uint8_t I,R,IFF;
  int ICount;
  uint8_t im, ei_delay;
} Z80;

#ifdef __cplusplus
extern "C" {
#endif
// Host callbacks implemented in mc1000.cpp.
byte RdZ80(uint16 address);
void WrZ80(uint16 address, byte value);
byte InZ80(uint16 port);
void OutZ80(uint16 port, byte value);
void ResetZ80(Z80 *cpu);
void ExecZ80(Z80 *cpu);
void IntZ80(Z80 *cpu, uint16 data);
#ifdef __cplusplus
}
#endif
