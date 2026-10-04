/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives tools/boopie_tools_spec.c for test_boopie_tools.py: prints both forms
 * of the list, Muse's and 小智's, and the 小智 tools/list answer, as JSON.
 */

#include <stdio.h>
#include <stdlib.h>

#include "boopie_tools.h"
#include "boopie_xz_proto.h"

int main(void)
{
    cJSON *commands = cJSON_CreateObject();
    boopie_tools_describe(commands);
    char *muse = cJSON_PrintUnformatted(commands);
    char *mcp = boopie_tools_mcp_json();
    static char reply[16384];
    char name[64], args[512];
    int id;
    int n = boopie_xz_mcp_reply("{\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":2}}", "s",
                                mcp, reply, sizeof reply, name, sizeof name, args, sizeof args, &id);
    printf("{\"muse\": %s, \"mcp\": %s, \"reply_len\": %d, \"self_found\": %d, \"bad_found\": %d}\n", muse, mcp, n,
           boopie_tools_find("self.pet.feed") != NULL, boopie_tools_find("self.nope") != NULL);
    cJSON_free(muse);
    cJSON_free(mcp);
    cJSON_Delete(commands);
    return 0;
}
