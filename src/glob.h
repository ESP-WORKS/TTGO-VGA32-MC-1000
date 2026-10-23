/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#pragma once

#include <stdlib.h>

typedef struct {
     size_t   gl_pathc;    /* Count of paths matched so far  */
     char   **gl_pathv;    /* List of matched pathnames.  */
     size_t   gl_offs;     /* Slots to reserve in gl_pathv.  */
     unsigned char *gl_isdir; /* Optional: 1 = entry is a directory (CAS browser) */
} emu_glob_t;

#ifdef __cplusplus
extern "C" {
#endif

int emu_glob(const char *pattern, int flags,
         int (*errfunc) (const char *epath, int eerrno),
         emu_glob_t *pglob);
void emu_globfree(emu_glob_t *pglob);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace fabgl { class FileBrowser; }
int emu_glob_browser(const char *pattern, emu_glob_t *pglob,
                     fabgl::FileBrowser *browser);
// Same as emu_glob_browser(), optionally listing subdirectories (before the
// matched files, in FileBrowser order) and the ".." parent entry.
int emu_glob_browser_ex(const char *pattern, emu_glob_t *pglob,
                        fabgl::FileBrowser *browser,
                        bool includeDirs, bool includeParent);
#endif
