/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#include "glob.h"
#include "sysdep.h"
#include <ctype.h>
#include <string.h>

// Minimal pattern matcher that supports '*' and '?' for ESP32 build
static bool matchPattern(const char *pattern, const char *str) {
  while (*pattern && *str) {
    if (*pattern == '*') {
      // Skip consecutive '*' to avoid excessive recursion
      while (*pattern == '*') ++pattern;
      if (!*pattern) return true; // Trailing '*' matches everything
      while (*str) {
        if (matchPattern(pattern, str)) return true;
        ++str;
      }
      return false;
    } else if (*pattern == '?' || toupper((unsigned char)*pattern) == toupper((unsigned char)*str)) {
      ++pattern;
      ++str;
    } else {
      return false;
    }
  }
  // Match remaining trailing '*' in pattern
  while (*pattern == '*') ++pattern;
  return *pattern == '\0' && *str == '\0';
}

int emu_glob_browser(const char *pattern, emu_glob_t *pglob,
                     fabgl::FileBrowser *browser) {
  return emu_glob_browser_ex(pattern, pglob, browser, false, false);
}

int emu_glob_browser_ex(const char *pattern, emu_glob_t *pglob,
                        fabgl::FileBrowser *browser,
                        bool includeDirs, bool includeParent) {
  if (!pglob) return -1;
  pglob->gl_pathc = 0;
  pglob->gl_pathv = nullptr;
  pglob->gl_isdir = nullptr;
  if (!browser) { pglob->gl_offs = 0; return 0; }
  int c = browser->count();
  if (c <= 0) { pglob->gl_offs = 0; return 0; }
  pglob->gl_pathv = (char **)malloc(sizeof(char*) * (size_t)c);
  pglob->gl_isdir = (unsigned char *)malloc((size_t)c);
  if (!pglob->gl_pathv || !pglob->gl_isdir) {
    free(pglob->gl_pathv); free(pglob->gl_isdir);
    pglob->gl_pathv = nullptr; pglob->gl_isdir = nullptr;
    pglob->gl_offs = 0;
    return -1;
  }
  int tapfiles = 0;

  // Pass 0: directories (".." first). Pass 1: files matching the pattern.
  for (int pass = includeDirs ? 0 : 1; pass < 2; ++pass) {
    for (int i = 0; i < c; ++i) {
      const fabgl::DirItem *item = browser->get(i);
      char const *name = item->name;
      if (pass == 0) {
        if (!item->isDir) continue;
        const bool parent = strcmp(name, "..") == 0;
        if (parent && !includeParent) continue;
        if (!parent && name[0] == '.') continue;  // hidden folders
        pglob->gl_isdir[tapfiles] = 1;
        pglob->gl_pathv[tapfiles++] = (char *)name;
        continue;
      }
      if (item->isDir) continue;
      bool matched = false;
      const char *part = pattern;
      while (*part && !matched) {
        const char *sep = strchr(part, ';');
        size_t plen = sep ? (size_t)(sep - part) : strlen(part);
        char one[64];
        if (plen > 0 && plen < sizeof(one)) {
          memcpy(one, part, plen); one[plen] = '\0';
          matched = matchPattern(one, name);
        }
        if (!sep) break;
        part = sep + 1;
      }
      if (matched) {
        pglob->gl_isdir[tapfiles] = 0;
        pglob->gl_pathv[tapfiles++] = (char *)name;
      }
    }
  }
  pglob->gl_pathc = tapfiles;
  pglob->gl_offs = 0;
  return 0;
}

void emu_globfree(emu_glob_t *pglob) {
  if (!pglob) return;
  free(pglob->gl_pathv);
  pglob->gl_pathv = nullptr;
  free(pglob->gl_isdir);
  pglob->gl_isdir = nullptr;
  pglob->gl_pathc = 0;
  pglob->gl_offs = 0;
}

int emu_glob(const char *pattern, int flags,
             int (*errfunc)(const char *, int), emu_glob_t *pglob) {
  (void)flags;
  (void)errfunc;
  return emu_glob_browser(pattern, pglob, file_browser());
}
