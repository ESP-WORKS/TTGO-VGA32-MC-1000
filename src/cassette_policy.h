/* SPDX-License-Identifier: MIT
 * Cassette filename precedence, independent of the SD and ROM implementations.
 */
#pragma once
#include <stdint.h>

namespace MC1000CAS {
enum class LoadSource : uint8_t { BasicName, BrowserSelection, BrowserPrompt };
inline LoadSource chooseLoadSource(bool matrix, bool guestHasName, bool browserSelected) {
  if (!matrix && guestHasName) return LoadSource::BasicName;
  return browserSelected ? LoadSource::BrowserSelection : LoadSource::BrowserPrompt;
}
inline bool useSelectedMatrixDestination(bool hasSelection, bool emptyBrowser,
                                         bool requestedNew) {
  return hasSelection && !emptyBrowser && !requestedNew;
}
inline bool guestHeaderHasName(const uint8_t *name14) {
  if (!name14) return false;
  for (unsigned i=0; i<14; ++i) {
    const uint8_t c=name14[i];
    if (c==0 || c==13) break;
    if (c>32 && c<=126) return true;
  }
  return false;
}
} // namespace MC1000CAS
