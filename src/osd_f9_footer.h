/* SPDX-License-Identifier: MIT
 * F9 CAS browser footer for the MC-1000 VGA32 OSD.
 * 22 interior cells: counter in the left field; RETURN on the right.
 */
#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace MC1000OSD {
static constexpr size_t kF9FooterColumns = 22;
static constexpr char kF9ReturnHint[] = "RETORNAR (ESC)";

inline void f9BrowserFooter(char (&out)[kF9FooterColumns + 1],
                            int position, int count) {
  memset(out, ' ', kF9FooterColumns);
  out[kF9FooterColumns] = '\0';

  const size_t hintLen = sizeof(kF9ReturnHint) - 1;
  const size_t hintStart = kF9FooterColumns - hintLen;
  memcpy(out + hintStart, kF9ReturnHint, hintLen);

  char counter[32];
  snprintf(counter, sizeof(counter), "%d/%d", position, count);
  const size_t counterLen = strlen(counter);
  // Leave a gap between the two fields. For unusually large directories,
  // display the tail of the count with an ellipsis rather than overwriting
  // the ESC instruction or overflowing the fixed-width OSD row.
  const size_t counterLimit = hintStart > 0 ? hintStart - 1 : 0;
  if (counterLen <= counterLimit) {
    memcpy(out, counter, counterLen);
  } else if (counterLimit > 2) {
    memcpy(out, "..", 2);
    memcpy(out + 2, counter + counterLen - (counterLimit - 2), counterLimit - 2);
  }
}
}  // namespace MC1000OSD
