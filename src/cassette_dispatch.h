/* SPDX-License-Identifier: MIT
 * Decode BASIC cassette callers from the original MC-1000 ROM call stack.
 * This module contains no ROM bytes and is safe for the public release.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace MC1000CAS {
enum class SaveOutcome : uint8_t { Success, Canceled, Error };
inline bool saveSucceeded(SaveOutcome status) { return status == SaveOutcome::Success; }
inline const char *saveOutcomeName(SaveOutcome s) {
  return s == SaveOutcome::Success ? "SUCCESS" :
         (s == SaveOutcome::Canceled ? "CANCELED" : "ERROR");
}
enum TapeCall : uint8_t {
  TAPE_NONE, TAPE_PROGRAM_SAVE, TAPE_PROGRAM_LOAD_HEADER,
  TAPE_PROGRAM_LOAD_BODY, TAPE_MATRIX_SAVE,
  TAPE_MATRIX_LOAD_HEADER, TAPE_MATRIX_LOAD_BODY
};
// The same CAS selector serves both normal and matrix commands. Its heading
// reflects the caller, without changing file selection or tape behavior.
inline const char *cassetteBrowserTitle(TapeCall call) {
  return (call == TAPE_PROGRAM_SAVE || call == TAPE_MATRIX_SAVE)
             ? "SALVAR CAS" : "CARREGAR CAS";
}
// Fast PC filter: most CPU instructions are not in the ROM tape routines.
// Keep the existing handler below unchanged for every intercepted entrypoint.
inline bool isTapeInterceptPC(uint16_t pc) {
  return pc == 0xDE8A || pc == 0xC4B7 || pc == 0xC162 || pc == 0xC188;
}
inline TapeCall classifyTapeCall(uint16_t pc, uint16_t returnAddress) {
  if (pc == 0xC4B7)
    return returnAddress == 0xDED9 ? TAPE_MATRIX_SAVE : TAPE_PROGRAM_SAVE;
  if (pc == 0xC162)
    return returnAddress == 0xDECF ? TAPE_MATRIX_LOAD_HEADER : TAPE_PROGRAM_LOAD_HEADER;
  if (pc == 0xC188)
    return returnAddress == 0xDED7 ? TAPE_MATRIX_LOAD_BODY : TAPE_PROGRAM_LOAD_BODY;
  return TAPE_NONE;
}
// In LOAD* the ROM has already dimensioned the destination array and
// placed its exact [start,end) address range in $FB-$FE. Never overflow it.
inline bool matrixLengthFits(uint16_t start, uint16_t end, size_t length) {
  return start >= 0x0200 && end <= 0xC000 && end > start &&
         length == (size_t)(end - start);
}
} // namespace MC1000CAS
