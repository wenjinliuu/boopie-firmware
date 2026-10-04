/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives xiaozhi/boopie_xz_proto.c for test_boopie_xiaozhi.py: "info" prints
 * the check-in's body; "uuid" a UUID from fixed bytes; "parse" reads an
 * answer from stdin and prints what it found, as JSON.
 */

#include <stdio.h>
#include <string.h>

#include "boopie_xz_proto.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        return 2;
    }
    if (!strcmp(argv[1], "info")) {
        boopie_xz_info_t in = { "aa:bb:cc:dd:ee:ff", "12345678-1234-4123-8123-123456789abc", "1.2.3", 16777216, 100000 };
        char out[640];
        int n = boopie_xz_info_json(&in, out, sizeof out);
        char small[64];
        printf("{\"n\": %d, \"small\": %d, \"body\": %s}\n", n, boopie_xz_info_json(&in, small, sizeof small), out);
        return 0;
    }
    if (!strcmp(argv[1], "uuid")) {
        uint8_t rnd[16];
        for (int i = 0; i < 16; i++) {
            rnd[i] = (uint8_t)(0xf0 + i);
        }
        char u[37];
        boopie_xz_uuid(rnd, u);
        printf("\"%s\"\n", u);
        return 0;
    }
    if (!strcmp(argv[1], "parse")) {
        static char in[16384];
        size_t n = fread(in, 1, sizeof in - 1, stdin);
        in[n] = '\0';
        boopie_xz_ota_t o;
        bool ok = boopie_xz_parse_ota(in, &o);
        printf("{\"ok\": %d, \"has_code\": %d, \"code\": \"%s\", \"has_challenge\": %d, \"timeout_ms\": %d, "
               "\"has_websocket\": %d, \"ws_url\": \"%s\", \"token_len\": %d, \"has_time\": %d, \"time_ms\": %lld, "
               "\"firmware\": %d}\n",
               ok, o.has_code, o.code, o.has_challenge, o.timeout_ms, o.has_websocket, o.ws_url, (int)strlen(o.ws_token),
               o.has_time, (long long)o.time_ms, o.offered_firmware);
        return 0;
    }
    if (!strcmp(argv[1], "mood")) {
        printf("[");
        for (int i = 2; i < argc; i++) {
            printf("%s%d", i > 2 ? ", " : "", boopie_xz_mood(argv[i]));
        }
        printf("]\n");
        return 0;
    }
    if (!strcmp(argv[1], "hello")) {
        char out[256];
        boopie_xz_hello_json(out, sizeof out);
        printf("%s\n", out);
        return 0;
    }
    if (!strcmp(argv[1], "listen") && argc >= 4) {
        char out[256], ab[256];
        boopie_xz_listen_json(argv[2], argv[3], out, sizeof out);
        boopie_xz_abort_json(argv[2], ab, sizeof ab);
        printf("[%s, %s]\n", out, ab);
        return 0;
    }
    if (!strcmp(argv[1], "msg") || !strcmp(argv[1], "mcp")) {
        static char in[16384];
        size_t n = fread(in, 1, sizeof in - 1, stdin);
        in[n] = '\0';
        if (!strcmp(argv[1], "msg")) {
            boopie_xz_msg_t m;
            bool ok = boopie_xz_parse_msg(in, &m);
            printf("{\"ok\": %d, \"type\": %d, \"session\": \"%s\", \"text\": \"%s\", \"rate\": %d, \"frame\": %d}\n", ok,
                   m.type, m.session, m.text, m.sample_rate, m.frame_ms);
            return 0;
        }
        static char out[4096];
        char name[64] = "", args[512] = "";
        int id = 0;
        int r = boopie_xz_mcp_reply(in, "s1", argc >= 3 ? argv[2] : "[]", out, sizeof out, name, sizeof name, args,
                                    sizeof args, &id);
        if (r == -2) {
            char res[512];
            boopie_xz_mcp_result("s1", id, "好的", false, res, sizeof res);
            printf("{\"r\": -2, \"name\": \"%s\", \"args\": %s, \"id\": %d, \"result\": %s}\n", name, args, id, res);
        } else if (r < 0) {
            printf("{\"r\": %d}\n", r);
        } else {
            printf("{\"r\": %d, \"out\": %s}\n", r, out);
        }
        return 0;
    }
    return 2;
}
