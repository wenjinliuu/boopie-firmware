/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The list of what the board can do (boopie_tools.h). Descriptions are for
 * the AI: when to call it, what it does, kept short, as all of them go to
 * 小智 in one message (under 8 KB, as upstream's MCP server keeps a page).
 */

#include "boopie_tools.h"

#include <string.h>

#define S BOOPIE_TOOL_STRING
#define I BOOPIE_TOOL_INTEGER
#define B BOOPIE_TOOL_BOOLEAN

static const boopie_tool_t TOOLS[] = {
    { "pet.status", "How the pet on the screen is: its name, hungry or not, mood, level, experience and stars.", { { 0 } } },
    { "pet.name",
      "Name the pet on the screen when the user gives it a name; without name, reports it. Call the pet by this name.",
      { { "name", S, false, 0, 0, "Up to 8 characters, Chinese is fine; empty: back to its character's own name." } } },
    { "pet.feed", "Feed the pet when the user asks to; it only eats when hungry.", { { 0 } } },
    { "display.avatar",
      "Change the character on screen, its colour, background, skin, accessories, pet expression or a reaction. "
      "Without parameters, reports them.",
      { { "avatar", S, false, 0, 0, "muse, boopie, gpt, codex, klaude, whale or doubao." },
        { "colour", S, false, 0, 0, "Body colour as RRGGBB, or default; not for muse." },
        { "expression", S, false, 0, 0, "Shown while idle: hungry, eating, sleepy, sad or dizzy; idle clears it." },
        { "reaction", S, false, 0, 0, "surprise, blush, confetti or hearts." },
        { "background", S, false, 0, 0,
          "default, stars, fireflies, snow, petals, bubbles, matrix, neon_grid or glitch." },
        { "skin", S, false, 0, 0, "A skin the user has bought, by id (such as boopie_starry), or none." },
        { "accessory", S, false, 0, 0,
          "bow, crown, scarf, party_hat (by level); straw_hat, halo, medal (earned). One hat at a time." },
        { "on", B, false, 0, 0, "With reaction or accessory: on (default) or off." } } },
    { "game.start", "Open a game for the user to play with the pet; rounds earn experience and stars.",
      { { "game", S, false, 0, 0,
          "whack (戳戳布比, the default), catch (接零食, tilt), maze (重力迷宫, tilt) or hop (跳跳布比, tap)." } } },
    { "app.open",
      "Show a page on the screen when the user asks to see or open it.",
      { { "app", S, true, 0, 0,
          "home (the pet's face), nest (小窝, its home), chat (聊天记录), album (相册), noise (白噪音) or settings." },
        { "room", S, false, 0, 0, "With nest: living, bedroom, outside, woods or beach." },
        { "page", S, false, 0, 0,
          "With settings: wifi, bluetooth, vpn, avatar, brain, sound, sleep, battery or storage." } } },
    { "noise.play",
      "Play white noise, rain or waves to sleep or focus to; it pauses while you talk and fades out in time.",
      { { "kind", S, false, 0, 0, "white, pink (softer), rain (the default) or waves." },
        { "minutes", I, false, 0, 600, "Before it fades out; 0 plays till stopped. Default 30." } } },
    { "noise.stop", "Stop the white noise, rain or waves.", { { 0 } } },
    { "device.sound",
      "Set the speaker's volume or mute it, when the user asks; without parameters, reports them.",
      { { "volume", I, false, 0, 100, "0 to 100." },
        { "muted", B, false, 0, 0, "true: replies are shown, not spoken; false: spoken again." } } },
    { "device.brightness", "Set the screen's brightness; without it, reports it.",
      { { "brightness", I, false, 10, 100, "10 to 100." } } },
    { "device.battery", "The battery: percent, charging or not, and its voltage.", { { 0 } } },
    { "garden.status",
      "The pet's farm (农场, outside 小窝): what's planted in each plot, how grown, thirsty or ready. The user plants, "
      "waters and picks on the screen.",
      { { 0 } } },
    { "world.weather",
      "Whenever you tell the user today's weather, also set it here, so the pet's world has it too, for the day.",
      { { "kind", S, true, 0, 0, "sunny, cloudy, rain or snow." } } },
    { "storage.clear",
      "When the user asks to delete their chat history, pictures or saved notes: asks on the screen, and only their "
      "tap clears. Tell them to confirm there.",
      { { "what", S, true, 0, 0, "chat, album, notes or all." } } },
};

#undef S
#undef I
#undef B

int boopie_tools_count(void)
{
    return (int)(sizeof TOOLS / sizeof TOOLS[0]);
}

const boopie_tool_t *boopie_tools_get(int i)
{
    return i >= 0 && i < boopie_tools_count() ? &TOOLS[i] : NULL;
}

const boopie_tool_t *boopie_tools_find(const char *name)
{
    if (!name) {
        return NULL;
    }
    if (!strncmp(name, "self.", 5)) {
        name += 5;
    }
    for (int i = 0; i < boopie_tools_count(); i++) {
        if (!strcmp(TOOLS[i].name, name)) {
            return &TOOLS[i];
        }
    }
    return NULL;
}

static const char *type_name(boopie_tool_type_t t)
{
    return t == BOOPIE_TOOL_INTEGER ? "integer" : t == BOOPIE_TOOL_BOOLEAN ? "boolean" : "string";
}

static cJSON *param_json(const boopie_tool_param_t *p)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type_name(p->type));
    cJSON_AddStringToObject(o, "description", p->description);
    if (p->type == BOOPIE_TOOL_INTEGER && (p->min || p->max)) {
        cJSON_AddNumberToObject(o, "minimum", p->min);
        cJSON_AddNumberToObject(o, "maximum", p->max);
    }
    return o;
}

void boopie_tools_describe(cJSON *commands)
{
    for (int i = 0; i < boopie_tools_count(); i++) {
        const boopie_tool_t *t = &TOOLS[i];
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "description", t->description);
        cJSON *req = cJSON_AddObjectToObject(c, "required");
        cJSON *opt = cJSON_AddObjectToObject(c, "optional");
        for (int k = 0; k < BOOPIE_TOOL_PARAMS_MAX && t->params[k].name; k++) {
            cJSON_AddItemToObject(t->params[k].required ? req : opt, t->params[k].name, param_json(&t->params[k]));
        }
        cJSON_AddItemToObject(commands, t->name, c);
    }
}

char *boopie_tools_mcp_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < boopie_tools_count(); i++) {
        const boopie_tool_t *t = &TOOLS[i];
        cJSON *o = cJSON_CreateObject();
        char name[48] = "self.";
        strncat(name, t->name, sizeof name - 6);
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddStringToObject(o, "description", t->description);
        cJSON *schema = cJSON_AddObjectToObject(o, "inputSchema");
        cJSON_AddStringToObject(schema, "type", "object");
        cJSON *props = cJSON_AddObjectToObject(schema, "properties");
        cJSON *req = NULL;
        for (int k = 0; k < BOOPIE_TOOL_PARAMS_MAX && t->params[k].name; k++) {
            cJSON_AddItemToObject(props, t->params[k].name, param_json(&t->params[k]));
            if (t->params[k].required) {
                if (!req) {
                    req = cJSON_AddArrayToObject(schema, "required");
                }
                cJSON_AddItemToArray(req, cJSON_CreateString(t->params[k].name));
            }
        }
        cJSON_AddItemToArray(arr, o);
    }
    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}
