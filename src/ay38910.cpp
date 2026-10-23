/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 MC-1000 emulator contributors
 *
 * Independent register-level AY-3-8910 PSG emulation for the TTGO VGA32.
 * Clock/tone/noise/envelope divisors follow published chip specifications.
 */
#include "ay38910.h"
#include <string.h>

namespace {
constexpr uint32_t kClockHz = 1789772; // MC-1000: Z80 crystal / 2
constexpr uint32_t kSampleHz = 32900;
// Approximate AY logarithmic DAC response (arbitrary normalized units).
constexpr uint8_t kVolume[16] = {0,1,2,3,4,6,9,12,16,23,32,45,64,90,127,180};
static inline uint32_t phaseStep(uint32_t numerator, uint32_t period) {
  if (!period) period = 1;
  return (uint32_t)(((uint64_t)numerator << 32) / ((uint64_t)period * kSampleHz));
}
}

// Divisions run only when the relevant Z80 register changes. The sample
// callback reads these immutable-between-writes steps under the same mutex.
void AY38910::RecomputeToneStep(int channel) {
  const int low = channel * 2;
  const uint32_t period = (uint32_t(regs_[low + 1] & 15) << 8) | regs_[low];
  tone_step_[channel] = phaseStep(kClockHz, 16u * (period ? period : 1u));
}

void AY38910::RecomputeEnvelopeStep() {
  const uint32_t period = uint32_t(regs_[11]) | (uint32_t(regs_[12]) << 8);
  envelope_step_ = phaseStep(kClockHz, 256u * (period ? period : 1u));
}

void AY38910::RestartEnvelope() {
  const uint8_t s=regs_[13]&15;
  envelope_attack_ = (s&4)?15:0;
  envelope_hold_ = s&1;
  envelope_alternate_ = (s&2)?1:0;
  if (!(s&8)) { envelope_hold_=1; envelope_alternate_=envelope_attack_!=0; }
  envelope_count_=15;
  envelope_phase_=0;
  envelope_holding_=false;
}

void AY38910::Reset() {
  portENTER_CRITICAL(&mutex_);
  memset(regs_,0,sizeof(regs_));
  regs_[7]=0x3f; // mute all tone/noise on reset; register-controlled amplitude 0
  memset(tone_phase_,0,sizeof(tone_phase_));
  reg_=0;noise_phase_=0;envelope_phase_=0;noise_lfsr_=0x1ffff;
  RestartEnvelope();
  for (int ch = 0; ch < 3; ++ch) RecomputeToneStep(ch);
  noise_threshold_ = 16u * kSampleHz; // noise period zero acts as one
  RecomputeEnvelopeStep();
  portEXIT_CRITICAL(&mutex_);
}

void AY38910::Init(fabgl::SoundGenerator *generator) {
  Reset();
  generator->setVolume(126);
  generator->attach(this);
  enable(true);
  generator->play(true);
}

void AY38910::WrCtrl(uint8_t data) {
  portENTER_CRITICAL(&mutex_);
  // jsMC1000 AY38910.selectRegister(): out-of-range selections do not
  // redirect an existing register latch to an unrelated register.
  if (data < 16) reg_=data;
  portEXIT_CRITICAL(&mutex_);
}

void AY38910::Write(uint8_t data) {
  portENTER_CRITICAL(&mutex_);
  if(reg_==1 || reg_==3 || reg_==5) data&=0x0f;
  if(reg_==6)data&=0x1f;
  if(reg_==8 || reg_==9 || reg_==10 || reg_==13)data&=0x1f;
  regs_[reg_]=data;
  if (reg_ <= 5) RecomputeToneStep(reg_ / 2);
  else if (reg_ == 6) {
    const uint32_t period = (regs_[6] & 31) ? (regs_[6] & 31) : 1;
    noise_threshold_ = 16u * period * kSampleHz;
  } else if (reg_ == 11 || reg_ == 12) RecomputeEnvelopeStep();
  if(reg_==13){regs_[13]&=0x0f;RestartEnvelope();}
  portEXIT_CRITICAL(&mutex_);
}

uint8_t AY38910::RdData() {
  portENTER_CRITICAL(&mutex_);
  // On the MC-1000 AY port A is disconnected, so it reads as all ones.
  // Port B (register 15) is resolved by MC1000::ReadKeyboardMatrix().
  uint8_t value=reg_==14 ? 0xff : regs_[reg_];
  portEXIT_CRITICAL(&mutex_);
  return value;
}

int AY38910::getSample() {
  portENTER_CRITICAL(&mutex_);
  const uint8_t mixer=regs_[7];
  noise_phase_ += kClockHz;
  while(noise_phase_ >= noise_threshold_){
    noise_phase_ -= noise_threshold_;
    const uint32_t feedback=(noise_lfsr_^(noise_lfsr_>>3))&1u;
    noise_lfsr_=(noise_lfsr_>>1)|(feedback<<16);
  }
  static_assert(sizeof(uint32_t)==4,"32-bit phase required");
  // Envelope has 16 levels and advances on the chip's /256 divider.
  const uint32_t prevEnv=envelope_phase_;
  envelope_phase_+=envelope_step_;
  if(envelope_phase_<prevEnv && !envelope_holding_){
    if(--envelope_count_<0){
      if(envelope_hold_){
        if(envelope_alternate_)envelope_attack_^=15;
        envelope_count_=0;
        envelope_holding_=true;
      }else{
        if(envelope_alternate_)envelope_attack_^=15;
        envelope_count_=15;
      }
    }
  }
  const uint8_t envVolume=(uint8_t)(envelope_count_^envelope_attack_)&15;
  int32_t mix=0;
  for(int ch=0;ch<3;ch++){
    tone_phase_[ch]+=tone_step_[ch];
    const bool toneOn=(tone_phase_[ch]&0x80000000u)!=0;
    const bool toneGate=((mixer&(1u<<ch))!=0)||toneOn;
    const bool noiseGate=((mixer&(8u<<ch))!=0)||((noise_lfsr_&1)!=0);
    const uint8_t amplitude=regs_[8+ch];
    const uint8_t v=(amplitude&0x10)?envVolume:(amplitude&0x0f);
    const int32_t level=(int32_t)kVolume[v];
    if(level)mix+=(toneGate&&noiseGate)?level:-level;
  }
  portEXIT_CRITICAL(&mutex_);
  // FabGL expects signed 8-bit samples. Three maximum-volume channels
  // peak at +/-540 DAC units: scale to +/-90 to avoid clipping.
  return (int)(mix / 6);
}
