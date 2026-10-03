/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/*
 * The Muse developer token (SDK token, mgst_...), entered by whoever sets the
 * board up (phone setup) rather than built into the firmware, so each person
 * uses their own from gadgets.muse.ai and no one's ships inside it. Kept in
 * NVS; main/identity.c hands it to pairing. A change takes a restart.
 */

#define BOOPIE_SDK_TOKEN_LEN 48   /* mgst_ and 43 base64url characters */

/* The saved token into out (room for BOOPIE_SDK_TOKEN_LEN + 1); false if none. */
bool boopie_sdk_token(char *out);

/* Whether t looks like a token from gadgets.muse.ai. */
bool boopie_sdk_token_valid(const char *t);

/* Saves t (empty: forgets it); false if it isn't a token or can't be saved. */
bool boopie_sdk_token_set(const char *t);
