/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#pragma once

#include "common.h"

#ifdef USE_SDL
#include "key_sdl.h"
#endif

#ifdef USE_FABGL
#include "key_fabgl.h"
#endif
