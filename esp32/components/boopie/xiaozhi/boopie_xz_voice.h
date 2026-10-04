/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "muse_chat.h"

/*
 * Talking to 小智 (docs/boopie-xiaozhi.md, step 2): push-to-talk turns as
 * Muse's own (muse_chat.h), so the voice task drives either the same way.
 * Speech goes up as Opus over the WebSocket the binding gave; the reply comes
 * back as Opus and is read here as 16 kHz mono. The connection opens on the
 * first press and closes after a while with nothing said. Voice task only,
 * as Muse's; built into the muse component.
 */

/* Bound, chosen and Wi-Fi up: a press can start a turn. */
bool boopie_xz_ready(void);

void boopie_xz_turn_begin(void);
void boopie_xz_turn_audio(const int16_t *pcm, size_t frames);
size_t boopie_xz_turn_audio_wait(const int16_t *pcm, size_t frames, int wait_ms);
void boopie_xz_turn_end(void);
void boopie_xz_turn_cancel(void);
/* HEARD (what it heard), REPLY (each sentence), DONE, ERROR; never SENT. */
muse_hatch_ev_t boopie_xz_turn_event(char *text, size_t cap);
/* The sentence being said after `played` frames of the reply. */
bool boopie_xz_turn_caption(size_t played, char *out, size_t cap);
size_t boopie_xz_turn_read(int16_t *pcm, size_t frames, int wait_ms);
