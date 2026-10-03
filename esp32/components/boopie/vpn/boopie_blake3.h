/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * BLAKE3, as much as Shadowsocks 2022 needs: inputs of one chunk (up to
 * 1024 bytes) and outputs of one block (up to 64 bytes), plain hashing and
 * key derivation. Portable C from the spec; checked against the official
 * implementation in tests/test_boopie_vpn.py.
 */

#define BOOPIE_BLAKE3_INPUT_MAX 1024

/* Returns 0, or -1 if the input or output is too long for this subset. */
int boopie_blake3_hash(const void *in, size_t n, uint8_t *out, size_t out_len);
int boopie_blake3_derive_key(const char *context, const void *material, size_t n, uint8_t *out, size_t out_len);
