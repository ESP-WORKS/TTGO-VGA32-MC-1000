/*
 * SPDX-License-Identifier: MIT
 * MC-1000 port for TTGO VGA32 (2026).
 * Based in part on the MIT-licensed TTGO SPC-1000 port,
 * Copyright (c) 2024 Jindor. Modifications by MC-1000 contributors.
 * See THIRD_PARTY_NOTICES.md for component-specific provenance.
 */
#pragma once

#define MODERN_KEY_LAYOUT
#define MC1000_TARGET
#define USE_FABGL

#define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#define MAX(x,y) (((x) <= (y)) ? (y) : (x))

typedef unsigned char byte;
