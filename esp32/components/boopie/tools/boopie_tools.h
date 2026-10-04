/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the board can do for an AI (docs/boopie-tools.md), listed once and
 * offered to both: Muse as Link commands (link.register), 小智 as MCP tools
 * ("self." before the name). The AI decides from what's said whether to call
 * one; nothing here listens for words.
 *
 * boopie_tools_spec.c is the list and its two forms (plain C, cJSON: tested
 * on the host); boopie_tools.c does them, on the device.
 */

typedef enum { BOOPIE_TOOL_STRING, BOOPIE_TOOL_INTEGER, BOOPIE_TOOL_BOOLEAN } boopie_tool_type_t;

typedef struct {
    const char *name;
    boopie_tool_type_t type;
    bool required;
    int min, max;   /* integers: both 0 for no bounds */
    const char *description;
} boopie_tool_param_t;

#define BOOPIE_TOOL_PARAMS_MAX 8

typedef struct {
    const char *name;   /* "pet.name" (小智: "self.pet.name") */
    const char *description;
    boopie_tool_param_t params[BOOPIE_TOOL_PARAMS_MAX];
} boopie_tool_t;

int boopie_tools_count(void);
const boopie_tool_t *boopie_tools_get(int i);
/* By name, with or without "self."; NULL if none. */
const boopie_tool_t *boopie_tools_find(const char *name);

/* Muse: each tool into commands, as {"description", "required": {...}, "optional": {...}}. */
void boopie_tools_describe(cJSON *commands);
/* 小智: the tools as MCP's tools/list wants them, a JSON array; caller frees (cJSON_free). */
char *boopie_tools_mcp_json(void);

/*
 * Does one (boopie_tools.c, device only): params may be NULL. Returns
 * {"ok": true, ...} or {"ok": false, "error": {"code", "message"}}; NULL if
 * the name isn't one of these. From any task.
 */
cJSON *boopie_tools_call(const char *name, const cJSON *params);

#ifdef __cplusplus
}
#endif
