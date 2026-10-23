/* SPDX-License-Identifier: MIT
 * Bounded vertical navigation for the MC-1000 SD/CAS browser.
 * Indices always refer to the full directory snapshot, never a screen row.
 */
#pragma once

namespace MC1000CAS {

struct BrowserPosition {
  int selected;
  int top;
};

inline BrowserPosition browserMoveVertical(int count, int visible,
                                          int selected, int top, int delta) {
  BrowserPosition pos = {0, 0};
  if (count <= 0 || visible <= 0) return pos;
  if (visible > count) visible = count;
  if (selected < 0) selected = 0;
  if (selected >= count) selected = count - 1;
  const int maxTop = count - visible;
  if (top < 0) top = 0;
  if (top > maxTop) top = maxTop;

  // Use comparisons instead of adding delta, to avoid overflow on large lists.
  if (delta < 0 && selected > 0) --selected;
  else if (delta > 0 && selected < count - 1) ++selected;

  // Shift precisely one row when selection crosses the displayed window.
  if (selected < top) top = selected;
  else if (selected - top >= visible) top = selected - visible + 1;
  pos.selected = selected;
  pos.top = top;
  return pos;
}

inline bool browserHasAbove(int top) { return top > 0; }
inline bool browserHasBelow(int count, int visible, int top) {
  return count > 0 && visible > 0 && top < count - visible;
}

}  // namespace MC1000CAS
