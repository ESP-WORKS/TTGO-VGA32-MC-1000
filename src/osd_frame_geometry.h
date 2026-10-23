/*
 * SPDX-License-Identifier: MIT
 * 12-pixel OSD cell frame geometry. Keep horizontal dividers OUTSIDE text.
 */
#pragma once
namespace MC1000OSDFrame {
inline int firstRow(bool confirmation, bool browsing) {
  return confirmation ? 5 : (browsing ? 1 : 0);
}
inline int lastRow(bool confirmation, bool browsing) {
  (void)browsing;
  return confirmation ? 11 : 14;
}
inline bool horizontalRuleAt(int y, bool confirmation, bool browsing) {
  const int cell = 12;
  const int top = firstRow(confirmation, browsing) * cell + 5;
  const int bottom = lastRow(confirmation, browsing) * cell + 6;
  const int primary = (confirmation ? 7 : (browsing ? 3 : 2)) * cell + 5;
  const int footer = 12 * cell + 5; // F1 footer is row 13; divider is row 12.
  return y == top || y == top + 1 || y == bottom - 1 || y == bottom ||
         y == primary || y == primary + 1 ||
         (!confirmation && !browsing && (y == footer || y == footer + 1)) ||
         (!confirmation && browsing && (y == 12 * cell + 5 || y == 12 * cell + 6));
}
}
