/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 MC-1000 emulator contributors
 *
 * Independent AY-3-8910 register/tone/noise/envelope implementation.
 * Does not contain the previously bundled EMULib AY8910 code.
 */
#pragma once
#include <stdint.h>
#include "devdrivers/soundgen.h"
#include "freertos/FreeRTOS.h"

class AY38910 : public fabgl::WaveformGenerator {
public:
  AY38910() = default;
  void Init(fabgl::SoundGenerator *generator);
  void setFrequency(int value) { (void)value; }
  int getSample();
  void Reset();
  void Write(uint8_t data);
  void WrCtrl(uint8_t data);
  void WrData(uint8_t data) { Write(data); }
  uint8_t RdData();
  void Loop(int ms) { (void)ms; } // envelopes advance at audio sample rate
  uint8_t reg() const { return reg_; }
private:
  uint8_t regs_[16] = {};
  uint8_t reg_ = 0;
  uint32_t tone_phase_[3] = {};
  uint32_t tone_step_[3] = {};
  uint32_t noise_threshold_ = 0;
  uint32_t envelope_step_ = 0;
  void RecomputeToneStep(int channel);
  void RecomputeEnvelopeStep();
  uint32_t noise_phase_ = 0;
  uint32_t envelope_phase_ = 0;
  uint32_t noise_lfsr_ = 0x1ffff;
  int8_t envelope_count_ = 15;
  uint8_t envelope_attack_ = 0, envelope_hold_ = 0, envelope_alternate_ = 0;
  bool envelope_holding_ = false;
  portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
  void RestartEnvelope();
};
