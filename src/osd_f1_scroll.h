/* SPDX-License-Identifier: MIT
 * Fixed-height F1 menu viewport, scroll only its functional lines.
 */
#pragma once
namespace MC1000F1Scroll {
static constexpr int kVisibleItems = 9;
static constexpr int kFunctionalItems = 12;
static constexpr int kTotalChoices = kFunctionalItems + 1; // fixed ESC footer
inline int keepVisible(int selection, int top) {
  if (selection >= kFunctionalItems) return top;
  if (selection < top) top = selection;
  if (selection >= top + kVisibleItems) top = selection - kVisibleItems + 1;
  return top;
}
} // namespace MC1000F1Scroll
