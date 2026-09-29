/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// Public access path for the E0470A01 waveform tables (Read Pico, LgfxEpd path).
//
// The tables, their full derivation and the target-model analysis live in
// src/lut/E0470Waveforms.h, next to the SDK's other controller LUTs. Headers
// under a library's src/ are reachable only from inside that library in this
// SDK - the one cross-library case, SdmmcBlockDevice, is forward-declared from
// SDCardManager's public header instead - so a board-support library cannot
// include src/lut/... directly. This header is the supported entry point for
// board configs, which live in their own library:
//
//   #include <LgfxEpdWaveforms.h>
//
// It is a pure forwarder: it declares nothing of its own, and everything it
// pulls in is gated on FREEINK_DEVICE_READPICO inside the target header, so
// including it from a board that does not need the tables is harmless.

#include "../src/lut/E0470Waveforms.h"
