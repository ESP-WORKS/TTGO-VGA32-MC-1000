/* SPDX-License-Identifier: MIT
 * 19-column scrolling view of a physical filename; never used as file key.
 */
#pragma once
#include <stddef.h>
#include <string.h>
#include <ctype.h>
namespace MC1000CAS {
static const size_t BROWSER_LABEL_WIDTH = 19;
inline size_t browserMaxOffset(const char *name) {
  if (!name) return 0;
  const size_t n = strlen(name);
  return n > BROWSER_LABEL_WIDTH ? n - BROWSER_LABEL_WIDTH + 2 : 0;
}
inline size_t browserScroll(const char *name, size_t offset, bool right) {
  size_t max = browserMaxOffset(name);
  if (offset > max) offset = max;
  if (right) return offset + 5 < max ? offset + 5 : max;
  return offset > 5 ? offset - 5 : 0;
}
inline void browserDisplayLabel(const char *name, size_t offset, char out[20]) {
  if (!name) name = "";
  const size_t n = strlen(name);
  const size_t max = browserMaxOffset(name);
  if (offset > max) offset = max;
  memset(out, ' ', BROWSER_LABEL_WIDTH);
  out[BROWSER_LABEL_WIDTH] = 0;
  if (n <= BROWSER_LABEL_WIDTH) {
    for (size_t i = 0; i < n; ++i)
      out[i] = (char)toupper((unsigned char)name[i]);
    return;
  }
  // Long filenames reserve edge columns for scrolling hints. All source
  // characters, including the extension, remain reachable by LEFT/RIGHT.
  const size_t start = offset;
  const size_t chars = BROWSER_LABEL_WIDTH - 2;
  for (size_t i = 0; i < chars && start + i < n; ++i)
    out[i + 1] = (char)toupper((unsigned char)name[start + i]);
  out[0] = start ? '<' : ' ';
  out[BROWSER_LABEL_WIDTH - 1] = start + chars < n ? '>' : ' ';
}
} // namespace MC1000CAS
