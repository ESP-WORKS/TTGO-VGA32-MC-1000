/* SPDX-License-Identifier: MIT */
#ifndef MC1000_OSD_F1_LAYOUT_H
#define MC1000_OSD_F1_LAYOUT_H

#include <stddef.h>
#include <string.h>

namespace MC1000OSD {
// F1 is 26 columns wide (24 printable between vertical borders); the
// F2/F3 browsers stay at their original 24-column width.
// Column zero is reserved for the selection cursor; labels are left-aligned.
// Optional ON/OFF states occupy the last cells against the right border.
static constexpr size_t kF1InteriorColumns = 24;
inline void f1MenuItem(char (&out)[kF1InteriorColumns + 1],
                       const char *label, bool selected,
                       const char *state = nullptr) {
  memset(out, ' ', kF1InteriorColumns);
  out[0] = selected ? '>' : ' ';
  out[kF1InteriorColumns] = '\0';
  if (!label) return;
  // All functional labels start immediately after the selection cursor,
  // including INJETAR PROGRAMA. Keep right-aligned shortcuts/states intact.
  const size_t labelLength = strlen(label);
  const size_t labelStart = 1;
  const size_t stateLength = state ? strlen(state) : 0;
  const size_t stateStart = stateLength <= kF1InteriorColumns
                              ? kF1InteriorColumns - stateLength : 0;
  const size_t maxLabelEnd = stateLength && stateStart > 0
                                 ? stateStart - 1 : kF1InteriorColumns;
  const size_t available = maxLabelEnd > labelStart
                               ? maxLabelEnd - labelStart : 0;
  memcpy(out + labelStart, label, labelLength < available ? labelLength : available);
  if (state && stateLength <= kF1InteriorColumns)
    memcpy(out + stateStart, state, stateLength);
}
inline void f1MenuCentered(char (&out)[kF1InteriorColumns + 1],
                           const char *label, bool selected) {
  memset(out, ' ', kF1InteriorColumns);
  out[kF1InteriorColumns] = '\0';
  out[0] = selected ? '>' : ' ';
  if (!label) return;
  size_t len = strlen(label);
  if (len > kF1InteriorColumns - 1) len = kF1InteriorColumns - 1;
  const size_t start = (kF1InteriorColumns - len) / 2;
  memcpy(out + start, label, len);
}
} // namespace MC1000OSD

#endif // MC1000_OSD_F1_LAYOUT_H
