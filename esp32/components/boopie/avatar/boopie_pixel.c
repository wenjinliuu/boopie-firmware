/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A line-by-line port of tools/boopie/avatar_proto.py: the names, the order of
 * the drawing and the numbers are the prototype's, so a change there carries
 * over by reading the two side by side. Rounding follows Python's (half to
 * even, rintf/rint) so the frames match pixel for pixel but for a few where
 * float and double part ways.
 */

#include "boopie_pixel.h"
#include "boopie_whack.h"
#include "boopie_catch.h"
#include "boopie_maze.h"
#include "boopie_hop.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define N BOOPIE_PX
#define PI 3.14159265358979f
#define TAU 6.28318530717959f

typedef struct {
    uint8_t r, g, b;
} rgb_t;

typedef struct {
    rgb_t out, dark, mid, light, high, glow;
} ramp_t;

/* A mask, a bit a pixel: bit x of row y. */
typedef uint64_t mask_t[N];

#define RGB(v) { (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v) }

static const rgb_t EYE = { 30, 22, 46 };
static const rgb_t WHITE = { 255, 255, 255 };
static const rgb_t CHEEK = { 255, 122, 152 };
static const rgb_t GOLD = { 255, 206, 84 };
static const rgb_t RED = { 255, 84, 96 };

static const float BAYER[4][4] = {
    { 0 / 16.0f, 8 / 16.0f, 2 / 16.0f, 10 / 16.0f },
    { 12 / 16.0f, 4 / 16.0f, 14 / 16.0f, 6 / 16.0f },
    { 3 / 16.0f, 11 / 16.0f, 1 / 16.0f, 9 / 16.0f },
    { 15 / 16.0f, 7 / 16.0f, 13 / 16.0f, 5 / 16.0f },
};

#define WORKING_AFTER 2.4
#define WORKING_LOOP 1.2

/* ---------------------------------------------------------------- tables */

/* The state light's colour; has = false takes the character's own. */
typedef struct {
    bool has;
    rgb_t c;
} light_t;

static const light_t LIGHT[BOOPIE_EXPR_COUNT] = {
    [BOOPIE_EXPR_BOOT] = { true, { 255, 255, 255 } },
    [BOOPIE_EXPR_IDLE] = { false, { 0, 0, 0 } },
    [BOOPIE_EXPR_LISTENING] = { true, { 110, 190, 255 } },
    [BOOPIE_EXPR_THINKING] = { true, { 200, 130, 255 } },
    [BOOPIE_EXPR_SPEAKING] = { true, { 110, 235, 170 } },
    [BOOPIE_EXPR_ERROR] = { true, { 255, 84, 96 } },
    [BOOPIE_EXPR_OFF] = { true, { 90, 84, 110 } },
    [BOOPIE_EXPR_HAPPY] = { true, { 255, 222, 90 } },
    [BOOPIE_EXPR_HUNGRY] = { true, { 255, 170, 80 } },
    [BOOPIE_EXPR_EATING] = { true, { 255, 190, 110 } },
    [BOOPIE_EXPR_SLEEPY] = { true, { 120, 112, 170 } },
    [BOOPIE_EXPR_SAD] = { true, { 130, 160, 230 } },
    [BOOPIE_EXPR_DIZZY] = { true, { 255, 200, 120 } },
};

/* Muse's official accents for the core states. */
static const uint32_t ACCENT[BOOPIE_EXPR_COUNT] = {
    [BOOPIE_EXPR_BOOT] = 0xa9c0ff,
    [BOOPIE_EXPR_IDLE] = 0xa77dff,
    [BOOPIE_EXPR_LISTENING] = 0x5cb8ff,
    [BOOPIE_EXPR_THINKING] = 0xe07bff,
    [BOOPIE_EXPR_SPEAKING] = 0x6ff0bf,
    [BOOPIE_EXPR_ERROR] = 0xff5c5c,
    [BOOPIE_EXPR_OFF] = 0x7c72d0,
    [BOOPIE_EXPR_HAPPY] = 0xa77dff,
    [BOOPIE_EXPR_HUNGRY] = 0xffaa50,
    [BOOPIE_EXPR_EATING] = 0xffbe6e,
    [BOOPIE_EXPR_SLEEPY] = 0x7c72d0,
    [BOOPIE_EXPR_SAD] = 0x6e8ce6,
    [BOOPIE_EXPR_DIZZY] = 0xffc878,
};

static const double LOOP[BOOPIE_EXPR_COUNT] = {
    [BOOPIE_EXPR_BOOT] = 2.0, [BOOPIE_EXPR_IDLE] = 3.0, [BOOPIE_EXPR_LISTENING] = 1.6,
    [BOOPIE_EXPR_THINKING] = WORKING_AFTER + 2 * WORKING_LOOP, [BOOPIE_EXPR_SPEAKING] = 1.2,
    [BOOPIE_EXPR_ERROR] = 2.0, [BOOPIE_EXPR_OFF] = 2.4, [BOOPIE_EXPR_HAPPY] = 1.2,
    [BOOPIE_EXPR_HUNGRY] = 2.4, [BOOPIE_EXPR_EATING] = 1.2, [BOOPIE_EXPR_SLEEPY] = 3.0,
    [BOOPIE_EXPR_SAD] = 2.4, [BOOPIE_EXPR_DIZZY] = 1.6,
};

static const double OVERLAY_LOOP[BOOPIE_OVERLAY_COUNT] = {
    [BOOPIE_OVERLAY_SURPRISE] = 1.2, [BOOPIE_OVERLAY_BLUSH] = 2.4,
    [BOOPIE_OVERLAY_CONFETTI] = 1.6, [BOOPIE_OVERLAY_HEARTS] = 1.2,
    [BOOPIE_OVERLAY_LOW_BATTERY] = 2.0, [BOOPIE_OVERLAY_CHARGING] = 1.6,
    [BOOPIE_OVERLAY_FOOD] = 1.6,
};

/* ---------------------------------------------------------------- colour */

static inline uint8_t u8r(double v)
{
    v = rint(v);
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static rgb_t mix(rgb_t a, rgb_t b, double t)
{
    return (rgb_t){ u8r(a.r + (b.r - a.r) * t), u8r(a.g + (b.g - a.g) * t), u8r(a.b + (b.b - a.b) * t) };
}

static rgb_t hex(uint32_t v)
{
    return (rgb_t)RGB(v);
}

static double pmod1(double v)
{
    v = fmod(v, 1.0);
    return v < 0 ? v + 1.0 : v;
}

/* Python's colorsys. */
static void rgb_to_hls(double r, double g, double b, double *h, double *l, double *s)
{
    double maxc = fmax(r, fmax(g, b)), minc = fmin(r, fmin(g, b));
    double sumc = maxc + minc, rangec = maxc - minc;
    *l = sumc / 2.0;
    if (minc == maxc) {
        *h = 0;
        *s = 0;
        return;
    }
    *s = *l <= 0.5 ? rangec / sumc : rangec / (2.0 - maxc - minc);
    double rc = (maxc - r) / rangec, gc = (maxc - g) / rangec, bc = (maxc - b) / rangec;
    double hh;
    if (r == maxc) {
        hh = bc - gc;
    } else if (g == maxc) {
        hh = 2.0 + rc - bc;
    } else {
        hh = 4.0 + gc - rc;
    }
    *h = pmod1(hh / 6.0);
}

static double hls_v(double m1, double m2, double hue)
{
    hue = pmod1(hue);
    if (hue < 1.0 / 6.0) {
        return m1 + (m2 - m1) * hue * 6.0;
    }
    if (hue < 0.5) {
        return m2;
    }
    if (hue < 2.0 / 3.0) {
        return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0;
    }
    return m1;
}

static rgb_t hls_to_rgb8(double h, double l, double s)
{
    double r, g, b;
    if (s == 0) {
        r = g = b = l;
    } else {
        double m2 = l <= 0.5 ? l * (1.0 + s) : l + s - l * s;
        double m1 = 2.0 * l - m2;
        r = hls_v(m1, m2, h + 1.0 / 3.0);
        g = hls_v(m1, m2, h);
        b = hls_v(m1, m2, h - 1.0 / 3.0);
    }
    return (rgb_t){ u8r(r * 255), u8r(g * 255), u8r(b * 255) };
}

static double clamp01(double v)
{
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static ramp_t ramp(uint32_t colour)
{
    double h, l, s;
    rgb_to_hls(((colour >> 16) & 0xff) / 255.0, ((colour >> 8) & 0xff) / 255.0, (colour & 0xff) / 255.0,
               &h, &l, &s);
#define C(dl, ds, dh) hls_to_rgb8(pmod1(h + (dh)), clamp01(l + (dl)), clamp01(s + (ds)))
    return (ramp_t){ .out = C(-0.52, 0.1, 0.03), .dark = C(-0.16, 0.05, 0.02), .mid = C(0, 0, 0),
                     .light = C(0.1, 0, 0), .high = C(0.2, -0.1, 0), .glow = C(0.16, 0.25, -0.03) };
#undef C
}

/* ---------------------------------------------------------------- icons */

typedef struct {
    const char *const *rows;
    uint8_t nrows;
    const char *keys;      /* the characters drawn ... */
    rgb_t colours[4];      /* ... in these colours */
} icon_t;

enum {
    I_HEART, I_Z, I_EXCL, I_DROP, I_DOT, I_NOTE, I_BOWL, I_COOKIE, I_BATTERY, I_BOLT, I_BUBBLE,
    I_CODE, I_LAPTOP, I_RICE, I_DRUMSTICK, I_ONIGIRI, I_FISH, I_BIG_COOKIE, I_CANDY, I_STORM, I_COUNT,
};

#define ROWS(...) (const char *const[]){ __VA_ARGS__ }, sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(char *)

static const icon_t ICONS[I_COUNT] = {
    [I_HEART] = { ROWS(".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."), "#", { { 255, 90, 140 } } },
    [I_Z] = { ROWS("####", "..#.", ".#..", "####"), "#", { { 200, 200, 255 } } },
    [I_EXCL] = { ROWS("##", "##", "##", "##", "..", "##"), "#", { { 255, 230, 90 } } },
    [I_DROP] = { ROWS(".#.", "###", "###", ".#."), "#", { { 120, 190, 255 } } },
    [I_DOT] = { ROWS("##", "##"), "#", { { 220, 210, 255 } } },
    [I_NOTE] = { ROWS("..##", "..#.", "..#.", "###.", "##.."), "#", { { 150, 245, 200 } } },
    [I_BOWL] = { ROWS("#.#.#..", ".#.#...", "#######", ".#####.", "..###.."), "#", { { 255, 255, 255 } } },
    [I_COOKIE] = { ROWS(".###.", "#o##o", "###o#", "#o###", ".###."), "#o", { { 214, 150, 80 }, { 110, 64, 34 } } },
    [I_BATTERY] = { ROWS("#######.", "#r....##", "#r....##", "#######."), "#r", { { 220, 220, 230 }, { 255, 84, 96 } } },
    [I_BOLT] = { ROWS("..##", ".##.", "####", ".##.", "##.."), "#", { { 255, 230, 90 } } },
    [I_BUBBLE] = { ROWS(".#####.", "#.....#", "#.....#", "#.....#", ".#####.", "....#..", ".....#."), "#",
                   { { 200, 195, 225 } } },
    [I_CODE] = { ROWS("#...#", "#.#.#", "#...#"), "#", { { 150, 245, 200 } } },
    [I_LAPTOP] = { ROWS("..##########..", ".############.", ".#####oo#####.", ".############.",
                        ".############.", "ssssssssssssss", ".kkkkkkkkkkkk."),
                   "#osk", { { 70, 66, 96 }, { 150, 245, 200 }, { 160, 156, 186 }, { 110, 106, 136 } } },
    /* Food: one shows when the pet's hungry, and the same sprites fall in the snack game. */
    [I_RICE] = { ROWS("..o...o...o", ".o...o...o.", "wwwwwwwwwww", "#wwwwwwwww#", ".#########.", "..#######..",
                      "...#####..."),
                 "wo#", { { 250, 248, 236 }, { 170, 170, 190 }, { 240, 140, 60 } } },
    [I_DRUMSTICK] = { ROWS(".ddddd.....", "dbbbbbd....", "dbhhbbbd...", "dbhbbbbd...", "dbbbbbbd...", ".dbbbbdw...",
                           "..ddddww...", "......ww.w.", ".......wwww", "........ww."),
                      "dbhw", { { 150, 70, 30 }, { 214, 120, 50 }, { 250, 190, 110 }, { 245, 240, 225 } } },
    [I_ONIGIRI] = { ROWS("....ww....", "...wwww...", "..wwwwww..", ".wwwwwwww.", "wwwnnnnwww", "wwwnnnnwww",
                         ".wwnnnnww."),
                    "wn", { { 250, 248, 236 }, { 40, 70, 50 } } },
    [I_FISH] = { ROWS("...ffff....f", "..ffffff..ff", ".fefffffffff", ".fffffffffff", "..llllll..ff", "...llll....f"),
                 "fle", { { 90, 160, 255 }, { 190, 225, 255 }, { 20, 20, 40 } } },
    [I_BIG_COOKIE] = { ROWS("..#####..", ".##o####.", "#####o###", "##o######", "####o##o#", ".#######.",
                            "..#####.."),
                       "#o", { { 214, 150, 80 }, { 110, 64, 34 } } },
    [I_CANDY] = { ROWS("p.......p", "pp.ryr.pp", "ppryryrpp", "pp.ryr.pp", "p.......p"),
                  "pry", { { 255, 150, 200 }, { 255, 90, 120 }, { 255, 230, 120 } } },
    /* The storm cloud the snack game's pet must dodge. */
    [I_STORM] = { ROWS("...ooo....", ".oogggoo..", "oggggggoo.", "ogkgggkggo", "oggggggggo", ".oooyyooo.",
                       "....yy....", "...yy.....", "...y......"),
                  "ogky", { { 70, 70, 96 }, { 150, 150, 176 }, { 40, 40, 60 }, { 255, 220, 80 } } },
};
/* What the pet may be offered, as boopie_food_t. */
static const uint8_t FOOD_ICONS[BOOPIE_FOOD_COUNT] = {
    [BOOPIE_FOOD_RICE] = I_RICE, [BOOPIE_FOOD_DRUMSTICK] = I_DRUMSTICK, [BOOPIE_FOOD_ONIGIRI] = I_ONIGIRI,
    [BOOPIE_FOOD_FISH] = I_FISH, [BOOPIE_FOOD_COOKIE] = I_BIG_COOKIE,
};

/* Codex's face: 3 x 5 glyphs. */
static const char *glyph_rows(char ch)
{
    switch (ch) {
    case '>': return "#..|.#.|..#|.#.|#..";
    case '<': return "..#|.#.|#..|.#.|..#";
    case '_': return "...|...|...|...|###";
    case '-': return "...|...|###|...|...";
    case '^': return "...|.#.|#.#|...|...";
    case '|': return ".#.|.#.|.#.|.#.|.#.";
    case 'x': return "#.#|#.#|.#.|#.#|#.#";
    case 'o': return "...|.#.|#.#|.#.|...";
    default: return "...|...|...|...|...";
    }
}

/* The knot of GPT's logo, 16 x 16: # the bands, . the holes between them. */
static const char *const KNOT[16] = {
    "     ####       ",
    "    ##..#####   ",
    "   ##...##..##  ",
    "  ##..##.....## ",
    " #.#.##..###..# ",
    "#..#.#.##..#### ",
    "#..#.##..#...## ",
    "#..#......##..##",
    "##..##......#..#",
    " ##..##..##.#..#",
    " ####..##.#.#..#",
    " #..###..##.#.# ",
    " ##.....##..##  ",
    "  ##..##...##   ",
    "   #####..##    ",
    "       ####     ",
};

/* ---------------------------------------------------------------- pose */

typedef enum { E_OPEN, E_BLINK, E_HAPPY, E_WIDE, E_HALF, E_SPIRAL, E_X, E_WORRIED, E_DOWN, E_LOOK } eyes_t;
typedef enum { M_W, M_SMILE, M_O, M_TALK, M_FROWN, M_WAVY, M_FLAT, M_CHOMP } mouth_t;
typedef enum { B_NORMAL, B_BIG, B_NONE } blush_t;
typedef enum { A_FACE, A_BODY } anchor_t;

/* A hand at the body's edge (side -1 or 1, dy), or in front of the face or
 * body at (dx, dy) from it. */
typedef struct {
    bool front;
    int side;
    anchor_t anchor;
    float dx, dy;
} hand_t;

typedef enum { FX_SPARK, FX_PX, FX_LPX, FX_FPX, FX_ICON, FX_FICON, FX_BICON } fx_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t icon;
    uint8_t r;          /* FX_SPARK's arm */
    float x, y;
    rgb_t c;
} fx_t;

#define FX_MAX 40

typedef struct {
    float t;
    float dx, dy;            /* body offset */
    float squash;            /* > 1 wider and shorter */
    float scale;
    float tilt;              /* x shear per row above the feet */
    bool feet;
    int nhands;
    hand_t hands[2];
    float antenna;           /* degrees right of vertical */
    float antenna_len;
    bool has_light;
    rgb_t light;
    float light_level;
    eyes_t eyes;
    int lookx, looky;
    mouth_t mouth;
    int talk;
    blush_t blush;
    int spin;
    bool laptop;
    rgb_t accent;
    float aura, rings, level, sparkle_speed;
    int nfx;
    fx_t fx[FX_MAX];
    double dim;
    boopie_scene_t scene;
    double scene_t;
} pose_t;

static void side_hands(pose_t *p, float l, float r)
{
    p->nhands = 2;
    p->hands[0] = (hand_t){ .side = -1, .dy = l };
    p->hands[1] = (hand_t){ .side = 1, .dy = r };
}

static hand_t front(anchor_t a, float dx, float dy)
{
    return (hand_t){ .front = true, .anchor = a, .dx = dx, .dy = dy };
}

static fx_t *fx_add(pose_t *p, fx_kind_t kind, float x, float y)
{
    static fx_t s_spare;
    fx_t *e = p->nfx < FX_MAX ? &p->fx[p->nfx++] : &s_spare;
    *e = (fx_t){ .kind = (uint8_t)kind, .x = x, .y = y, .c = { 255, 246, 200 }, .r = 1 };
    return e;
}

static void fx_icon(pose_t *p, fx_kind_t kind, int icon, float x, float y)
{
    fx_add(p, kind, x, y)->icon = (uint8_t)icon;
}

static void fx_px(pose_t *p, fx_kind_t kind, float x, float y, rgb_t c)
{
    fx_add(p, kind, x, y)->c = c;
}

/* In double: a pose often lands on exactly 0.5, and Python rounds that the way
 * double does. */
static double wave(double t, double period, double lo, double hi)
{
    return lo + (hi - lo) * (0.5 + 0.5 * sin(2 * 3.14159265358979323846 * t / period));
}

static float minf(float a, float b)
{
    return a < b ? a : b;
}

static double pymodd(double a, double b)
{
    double m = fmod(a, b);
    return m < 0 ? m + b : m;
}

static int pymod(int a, int b)
{
    int m = a % b;
    return m < 0 ? m + b : m;
}

static int floordiv(int a, int b)
{
    return (int)floorf((float)a / (float)b);
}

/* The expression at t seconds into it; live < 0 makes up a voice level. */
static void pose_for(pose_t *p, boopie_expr_t name, double t, double length, float live)
{
    float bob = (float)rint(wave(t, 1.5, 0, 1));
    bool blink = pymodd(t, 3.0) > 2.75;
    *p = (pose_t){
        .t = (float)t, .squash = 1, .scale = 1, .feet = true, .antenna = 18, .antenna_len = 9,
        .has_light = LIGHT[name].has, .light = LIGHT[name].c, .light_level = 1, .eyes = E_OPEN,
        .mouth = M_W, .talk = 1, .blush = B_NORMAL, .accent = hex(ACCENT[name]), .aura = 0.75,
        .level = 0.3, .sparkle_speed = 0.6, .dim = 1,
    };
    side_hands(p, 3, 3);
    p->sparkle_speed = name == BOOPIE_EXPR_THINKING ? 2.8
                     : name == BOOPIE_EXPR_LISTENING ? 1.2
                     : name == BOOPIE_EXPR_SPEAKING ? 1.5 : 0.6;
    switch (name) {
    case BOOPIE_EXPR_IDLE:
        p->dy = bob;
        p->eyes = blink ? E_BLINK : E_OPEN;
        p->antenna = 18 + 8 * sin(TAU * t / 3);
        p->light_level = wave(t, 3, 0.75, 1);
        break;
    case BOOPIE_EXPR_BOOT:
        if (t < 0.5) {            /* just the light, blinking on */
            p->scale = 0.3;
            p->feet = false;
            p->nhands = 0;
            p->light_level = (int)(t * 10) % 2 ? 1.0 : 0.2;
            p->eyes = E_BLINK;
        } else {
            double k = fmin(1.0, (t - 0.5) / 0.4);
            double over = sin(fmin(1.0, (t - 0.5) / 0.6) * PI) * 0.15;
            p->scale = 0.3 + 0.7 * k + over;
            p->squash = 1.0 + over;
            p->eyes = t < 1.2 ? E_BLINK : E_OPEN;
            p->mouth = t < 1.4 ? M_W : M_SMILE;
            if (t >= 1.2 && t < 1.6) {
                fx_add(p, FX_SPARK, 14, 18);
                fx_add(p, FX_SPARK, 50, 16);
            }
        }
        break;
    case BOOPIE_EXPR_LISTENING: {
        p->eyes = E_WIDE;
        p->antenna = 0;
        p->antenna_len = 10;
        p->tilt = -0.06;
        side_hands(p, 3, -4);
        p->mouth = M_SMILE;
        p->light_level = wave(t, 0.4, 0.6, 1);
        p->rings = 0.9;
        p->level = live >= 0 ? 0.3 + 0.5 * live : wave(t, 0.4, 0.3, 0.8);
        double r = pymodd(t / length * 3, 1);
        for (int k = 0; k < 2; k++) {
            double rr = 3 + pymodd(r + k * 0.5, 1) * 6;
            for (int a = -40; a <= 40; a += 20) {
                double ar = a * PI / 180;
                fx_px(p, FX_LPX, 2 + rr * cos(ar), rr * sin(ar), (rgb_t){ 110, 190, 255 });
            }
        }
        break;
    }
    case BOOPIE_EXPR_THINKING:
        if (t >= WORKING_AFTER) {  /* typing away on a tiny laptop, bits of code flying up */
            t = pymodd(t - WORKING_AFTER, WORKING_LOOP);
            int tap = (int)(t / 0.15) % 2;
            p->eyes = E_LOOK;
            p->lookx = 0;
            p->looky = 1;
            p->mouth = (int)(t / 0.6) % 2 ? M_FLAT : M_W;
            p->laptop = true;
            p->nhands = 2;
            p->hands[0] = front(A_BODY, -6, 14 - tap);
            p->hands[1] = front(A_BODY, 6, 13 + tap);
            p->antenna = 12 + 4 * tap;
            p->light_level = 0.6 + 0.4 * tap;
            for (int i = 0; i < 2; i++) {
                double k = pymodd(t / WORKING_LOOP + i / 2.0, 1);
                fx_icon(p, FX_BICON, I_CODE, -16 + i * 27 + rint(k * 3), 4 - k * 22);
            }
        } else {
            length = WORKING_AFTER;
            p->eyes = E_LOOK;
            p->lookx = -2;
            p->looky = -1;
            p->mouth = M_FLAT;
            p->hands[1] = front(A_FACE, 4, 7);
            p->antenna = 10;
            double orbit = t / 0.8 * TAU;
            fx_px(p, FX_LPX, 5 * cos(orbit), 3 * sin(orbit), WHITE);
            int dots = (int)(t / length * 4) % 4;
            for (int i = 0; i < dots; i++) {
                fx_icon(p, FX_ICON, I_DOT, 6 + i * 5, 20 - i * 2);
            }
        }
        break;
    case BOOPIE_EXPR_SPEAKING: {
        double level = live >= 0 ? live : fabs(sin(t * 7.3)) * 0.6 + fabs(sin(t * 3.1)) * 0.4;
        p->mouth = M_TALK;
        p->talk = (int)(level * 3.4);
        p->dy = bob;
        p->eyes = pymodd(t, 1.2) > 1.1 ? E_BLINK : E_OPEN;
        side_hands(p, 3 - rint(level * 3), 3);
        p->light_level = 0.55 + 0.45 * level;
        p->rings = 0.6;
        p->level = level;
        if (pymodd(t, 1.2) < 0.6) {
            fx_icon(p, FX_ICON, I_NOTE, 52, 24 - (int)(pymodd(t, 0.6) * 10));
        }
        break;
    }
    case BOOPIE_EXPR_ERROR:
        p->eyes = E_WORRIED;
        p->mouth = M_WAVY;
        p->antenna = 70;
        p->antenna_len = 8;
        p->light_level = (int)(t * 4) % 2 ? 1.0 : 0.25;
        fx_icon(p, FX_FICON, I_DROP, 13, -10 + (int)(t / length * 6));
        if ((int)(t * 2) % 2 == 0) {
            fx_icon(p, FX_ICON, I_EXCL, 8, 14);
        }
        break;
    case BOOPIE_EXPR_OFF:
        if (t < 1.2) {
            double up = wave(t, 0.4, 0, 1);
            side_hands(p, 3, -4 - (float)rint(up * 3));
            p->eyes = E_HAPPY;
            p->mouth = M_SMILE;
        } else {
            double k = fmin(1.0, (t - 1.2) / 0.8);
            p->eyes = E_BLINK;
            p->mouth = M_W;
            p->squash = 1.0 + 0.12 * k;
            p->antenna = 18 + 40 * k;
            p->light_level = 1 - k;
            p->aura = 0.75 * (1 - k);
            p->dim = 1 - 0.55 * k;
        }
        break;
    case BOOPIE_EXPR_HAPPY: {
        double k = t / length;
        if (k < 0.12) {
            p->squash = 1.2;
        } else if (k < 0.5) {
            double s = (k - 0.12) / 0.38;
            p->dy = -4 * sin(s * PI);
            p->squash = 0.9;
            p->feet = s < 0.1 || s > 0.9;
        } else if (k < 0.62) {
            p->squash = 1.15;
        }
        p->eyes = E_HAPPY;
        p->mouth = k < 0.62 ? M_O : M_SMILE;
        if (k >= 0.12 && k < 0.62) {
            side_hands(p, -3, -3);
        }
        p->antenna = 18 + 25 * sin(k * 18) * (1 - k);
        p->blush = B_BIG;
        static const float X0[2] = { 6, 52 };
        for (int i = 0; i < 2; i++) {
            double y = 30 - k * 22 - i * 3;
            if (y > 2) {
                fx_icon(p, FX_ICON, I_HEART, X0[i], y);
            }
        }
        break;
    }
    case BOOPIE_EXPR_HUNGRY:
        p->eyes = E_LOOK;
        p->lookx = 1;
        p->looky = -1;
        p->mouth = M_WAVY;
        p->hands[1] = front(A_BODY, -2, 9);
        p->antenna = 30;
        p->light_level = wave(t, 1.2, 0.5, 0.9);
        /* Up to the left, its tail to the head: the right has the mic hint and
         * the round screen's edge close by. */
        fx_icon(p, FX_ICON, I_BUBBLE, 6, 9);
        fx_icon(p, FX_ICON, I_BOWL, 6, 10);
        if ((int)(t * 2) % 2) {    /* a rumble by the tummy */
            for (int i = 0; i < 3; i++) {
                fx_px(p, FX_PX, 9 - i, 46 + (i % 2), (rgb_t){ 200, 195, 225 });
            }
        }
        break;
    case BOOPIE_EXPR_EATING: {
        int chomp = (int)(t / 0.3) % 2;
        p->eyes = E_HAPPY;
        p->mouth = M_CHOMP;
        p->talk = chomp;
        p->nhands = 2;
        p->hands[0] = front(A_FACE, -5, 8);
        p->hands[1] = front(A_FACE, 5, 8);
        fx_icon(p, FX_FICON, I_COOKIE, -3, 4 + (chomp ? 0 : 1));
        double crumb = pymodd(t, 0.6) / 0.6;
        fx_px(p, FX_FPX, -6 + (int)(crumb * 2), 10 + crumb * 8, (rgb_t){ 214, 150, 80 });
        fx_px(p, FX_FPX, 4 - (int)(crumb * 2), 11 + crumb * 7, (rgb_t){ 214, 150, 80 });
        p->blush = B_BIG;
        break;
    }
    case BOOPIE_EXPR_SLEEPY: {
        double breath = wave(t, 3, 0, 1);
        p->eyes = E_BLINK;
        p->mouth = breath > 0.7 ? M_O : M_W;
        p->squash = 1.0 + 0.06 * breath;
        p->antenna = 55 + 8 * sin(TAU * t / 3);
        p->light_level = 0.35;
        double k = t / length;
        for (int i = 0; i < 3; i++) {
            double kk = pymodd(k + i / 3.0, 1);
            fx_icon(p, FX_ICON, I_Z, 52 + kk * 7, 30 - kk * 26);
        }
        break;
    }
    case BOOPIE_EXPR_SAD: {
        p->eyes = E_WORRIED;
        p->mouth = M_FROWN;
        p->dy = 1;
        p->antenna = 60;
        p->light_level = 0.55;
        double k = t / length;
        if (k < 0.7) {
            fx_icon(p, FX_FICON, I_DROP, -9, 1 + (int)(k * 10));
        }
        break;
    }
    case BOOPIE_EXPR_DIZZY: {
        double s = sin(TAU * t / length);
        p->dx = rint(2 * s);
        p->tilt = 0.08 * s;
        p->eyes = E_SPIRAL;
        p->spin = (int)(t * 8);
        p->mouth = M_WAVY;
        p->antenna = 18 + 35 * s;
        for (int i = 0; i < 3; i++) {
            double a = TAU * (t / 0.8 + i / 3.0);
            fx_t *e = fx_add(p, FX_SPARK, 32 + 13 * cos(a), 9 + 3 * sin(a));
            e->c = GOLD;
        }
        break;
    }
    default:
        break;
    }
}

/* A small deterministic hash for the confetti. */
static float hash01(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return (float)(v & 0xffffff) / 16777216.0;
}

static rgb_t hsv(float h, double s, float v)
{
    double r = 0, g = 0, b = 0;
    int i = (int)(h * 6) % 6;
    float f = h * 6 - floor(h * 6), p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
    switch (i) {
    case 0: r = v; g = u; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = u; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = u; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return (rgb_t){ u8r(r * 255), u8r(g * 255), u8r(b * 255) };
}

/* Lays an overlay over a pose: shared by every character, drawn in any expression. */
static int s_food_icon = I_RICE;   /* what the food overlay offers this frame */

static void overlay(pose_t *p, boopie_overlay_t name, double t, double length)
{
    switch (name) {
    case BOOPIE_OVERLAY_SURPRISE: {
        double k = t / length;
        if (k < 0.35) {
            p->dy -= 3 * sin(k / 0.35 * PI);
        }
        if (p->eyes != E_BLINK && p->eyes != E_X && p->eyes != E_SPIRAL) {
            p->eyes = E_WIDE;
        }
        if ((int)(t * 5) % 2 == 0) {
            fx_icon(p, FX_ICON, I_EXCL, 8, 12);
        }
        break;
    }
    case BOOPIE_OVERLAY_BLUSH:
        p->blush = B_BIG;
        if (pymodd(t, length) > length * 0.6) {
            fx_icon(p, FX_ICON, I_HEART, 50, 10 - (int)((pymodd(t, length) - length * 0.6) * 8));
        }
        break;
    case BOOPIE_OVERLAY_CONFETTI:
        for (uint32_t i = 0; i < 14; i++) {
            double x = 4 + (int)(hash01(i * 3 + 1) * 56);
            double y = pymodd((int)(hash01(i * 3 + 2) * 64) + t * 30, 64);
            fx_px(p, FX_PX, x, y, hsv(hash01(i * 3 + 3), 0.7, 1));
        }
        break;
    case BOOPIE_OVERLAY_HEARTS: {
        double k = t / length;
        static const float X0[2] = { 6, 52 };
        for (int i = 0; i < 2; i++) {
            double y = 30 - k * 22 - i * 3;
            if (y > 2) {
                fx_icon(p, FX_ICON, I_HEART, X0[i], y);
            }
        }
        break;
    }
    case BOOPIE_OVERLAY_LOW_BATTERY:
        if ((int)(t * 2) % 2) {
            fx_icon(p, FX_ICON, I_BATTERY, 50, 6);
            p->has_light = true;
            p->light = RED;
            p->light_level = fmin(p->light_level, 0.6);
        }
        break;
    case BOOPIE_OVERLAY_FOOD: {   /* something to eat, bobbing to be tapped: centred and
                                   * bottom-aligned on the rice bowl's place */
        const icon_t *ic = &ICONS[s_food_icon];
        int w = (int)strlen(ic->rows[0]);
        fx_icon(p, FX_ICON, s_food_icon, 49 + (int)floor((11 - w) / 2.0), 55 - ic->nrows + (int)wave(t, 1.6, 0, 2));
        break;
    }
    case BOOPIE_OVERLAY_CHARGING:
        fx_icon(p, FX_ICON, I_BOLT, 53, 4 + (int)wave(t, 0.8, 0, 2));
        for (int i = 0; i < 3; i++) {
            double k = pymodd(t / length + i / 3.0, 1);
            fx_px(p, FX_PX, 8 + i * 24, 54 - k * 36, (rgb_t){ 255, 230, 90 });
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- canvas */

static rgb_t s_img[N][N];
static mask_t s_body;           /* everything shaded so far: the rings stay off it */
static rgb_t s_eye;
static bool s_has_shine;
static rgb_t s_shine;
static bool s_has_rim;
static rgb_t s_rim;

static inline float bay(int x, int y)
{
    return BAYER[y & 3][x & 3];
}

/* Where put() draws: the frame, or the overlay layer (and its mask). */
static rgb_t (*s_dst)[N] = s_img;
static uint64_t *s_dst_mask;

static void put(float x, float y, rgb_t c)
{
    int xi = (int)rintf(x), yi = (int)rintf(y);
    if (xi >= 0 && xi < N && yi >= 0 && yi < N) {
        s_dst[yi][xi] = c;
        if (s_dst_mask) {
            s_dst_mask[yi] |= 1ull << xi;
        }
    }
}

static inline bool m_get(const mask_t m, int x, int y)
{
    return (m[y] >> x) & 1u;
}

static void m_clear(mask_t m)
{
    memset(m, 0, sizeof(mask_t));
}

static void m_or(mask_t d, const mask_t a)
{
    for (int y = 0; y < N; y++) {
        d[y] |= a[y];
    }
}

static void m_and(mask_t d, const mask_t a)
{
    for (int y = 0; y < N; y++) {
        d[y] &= a[y];
    }
}

static void m_andnot(mask_t d, const mask_t a)
{
    for (int y = 0; y < N; y++) {
        d[y] &= ~a[y];
    }
}

static bool m_any(const mask_t m)
{
    for (int y = 0; y < N; y++) {
        if (m[y]) {
            return true;
        }
    }
    return false;
}

static void flat(const mask_t m, rgb_t c)
{
    for (int y = 0; y < N; y++) {
        for (uint64_t row = m[y]; row; row &= row - 1) {
            s_img[y][__builtin_ctzll(row)] = c;
        }
    }
}

static void outline(const mask_t m, rgb_t c)
{
    for (int y = 0; y < N; y++) {
        uint64_t ring = (m[y] << 1) | (m[y] >> 1);
        if (y > 0) {
            ring |= m[y - 1];
        }
        if (y < N - 1) {
            ring |= m[y + 1];
        }
        for (ring &= ~m[y]; ring; ring &= ring - 1) {
            s_img[y][__builtin_ctzll(ring)] = c;
        }
    }
}

/* Fill the mask with the ramp, lit from the top left by the slope of its blur, dithered. */
static void shaded(const mask_t m, const ramp_t *rp)
{
    static float h[N][N], tmp[N][N];
    if (!m_any(m)) {
        return;
    }
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            h[y][x] = m_get(m, x, y) ? 1.0f : 0.0f;
        }
    }
    for (int pass = 0; pass < 3; pass++) {   /* a 5 x 5 box, edges held, three times */
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                float s = 0;
                for (int k = -2; k <= 2; k++) {
                    int xx = x + k < 0 ? 0 : (x + k >= N ? N - 1 : x + k);
                    s += h[y][xx];
                }
                tmp[y][x] = s;
            }
        }
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                float s = 0;
                for (int k = -2; k <= 2; k++) {
                    int yy = y + k < 0 ? 0 : (y + k >= N ? N - 1 : y + k);
                    s += tmp[yy][x];
                }
                h[y][x] = s / 25.0f;
            }
        }
    }
    rgb_t rim = s_has_rim ? mix(rp->mid, s_rim, 0.35) : (rgb_t){ 0, 0, 0 };
    for (int y = 0; y < N; y++) {
        for (uint64_t row = m[y]; row; row &= row - 1) {
            int x = __builtin_ctzll(row);
            float gx = x == 0 ? h[y][1] - h[y][0] : x == N - 1 ? h[y][N - 1] - h[y][N - 2]
                     : (h[y][x + 1] - h[y][x - 1]) / 2;
            float gy = y == 0 ? h[1][x] - h[0][x] : y == N - 1 ? h[N - 1][x] - h[N - 2][x]
                     : (h[y + 1][x] - h[y - 1][x]) / 2;
            float d = (-gx * -0.55f - gy * -0.83f) * 10 + (h[y][x] - 0.95f);
            d += (bay(x, y) - 0.5f) * 0.18f;
            s_img[y][x] = d <= -0.3f ? rp->dark : d <= 0.2f ? rp->mid : d <= 0.55f ? rp->light : rp->high;
            if (s_has_rim && h[y][x] < 0.8f && bay(x, y) < 0.3f) {
                float out = (-gx * 0.6f - gy * 0.8f) / (hypotf(gx, gy) + 1e-6f);
                if (out > 0.75f) {
                    s_img[y][x] = rim;
                }
            }
        }
    }
    m_or(s_body, m);
}

static void icon(int which, float x, float y)
{
    const icon_t *ic = &ICONS[which];
    for (int j = 0; j < ic->nrows; j++) {
        for (int i = 0; ic->rows[j][i]; i++) {
            const char *k = strchr(ic->keys, ic->rows[j][i]);
            if (ic->rows[j][i] != '.' && k) {
                int ci = (int)(k - ic->keys);
                put(x + i, y + j, ic->colours[ci]);
            }
        }
    }
}

static void spark(float x, float y, rgb_t c, int r)
{
    put(x, y, c);
    for (int k = 1; k <= r; k++) {
        put(x + k, y, c);
        put(x - k, y, c);
        put(x, y + k, c);
        put(x, y - k, c);
    }
}


/* ---------------------------------------------------------------- scenes */

static const char *const SCENE_KEYS[BOOPIE_SCENE_COUNT] = {
    "default", "stars", "fireflies", "snow", "petals", "bubbles", "matrix", "neon_grid", "glitch",
};
static const char *const SCENE_NAMES[BOOPIE_SCENE_COUNT] = {
    "默认光晕", "星空", "萤火", "飘雪", "花瓣", "气泡", "代码雨", "霓虹网格", "像素故障",
};

const char *boopie_scene_key(boopie_scene_t s)
{
    return (int)s >= 0 && s < BOOPIE_SCENE_COUNT ? SCENE_KEYS[s] : NULL;
}

const char *boopie_scene_name(boopie_scene_t s)
{
    return (int)s >= 0 && s < BOOPIE_SCENE_COUNT ? SCENE_NAMES[s] : NULL;
}

/* A stable hash of up to three ints, 0..1 (the prototype's h01). */
static double h01(int n, int a, int b, int c)
{
    const int v[3] = { a, b, c };
    uint32_t x = 0x9E3779B9u;
    for (int i = 0; i < n; i++) {
        x ^= (uint32_t)v[i] * 0x85EBCA6Bu + 0x632BE5ABu;
        x *= 0x27D4EB2Du;
        x ^= x >> 15;
    }
    return (x & 0xFFFFFF) / 16777216.0;
}

/* put() for the scenes, whose maths is in double as the prototype's. */
static void putd(double x, double y, rgb_t c)
{
    int xi = (int)rint(x), yi = (int)rint(y);
    if (xi >= 0 && xi < N && yi >= 0 && yi < N) {
        s_dst[yi][xi] = c;
        if (s_dst_mask) {
            s_dst_mask[yi] |= 1ull << xi;
        }
    }
}

static void firefly(int i, double t)
{
    double x = 32 + 26 * sin(t * 0.4 * (1 + h01(2, i, 1, 0)) + i * 2.1);
    double y = 30 + 20 * sin(t * 0.33 * (1 + h01(2, i, 2, 0)) + i * 1.3);
    double glow = 0.5 + 0.5 * sin(t * 3 + i * 1.7);
    putd(x, y, mix((rgb_t){ 80, 90, 30 }, (rgb_t){ 230, 255, 140 }, glow));
    if (glow > 0.5) {
        rgb_t h = mix((rgb_t){ 20, 24, 10 }, (rgb_t){ 110, 130, 50 }, glow);
        putd(x + 1, y, h);
        putd(x - 1, y, h);
        putd(x, y + 1, h);
        putd(x, y - 1, h);
    }
}

static void snowflake(int i, double t, bool front)
{
    double sp = 5 + h01(2, i, 2, 0) * 6 + (front ? 4 : 0);
    double y = pymodd(h01(2, i, 3, 0) * 64 + t * sp, 68) - 2;
    double x = h01(2, i, 1, 0) * 64 + sin(t * 1.3 + i) * 2;
    putd(x, y, front ? (rgb_t){ 250, 250, 255 } : (rgb_t){ 150, 160, 200 });
    if (front) {
        putd(x + 1, y, (rgb_t){ 200, 210, 240 });
    }
}

static void petal(int i, double t, bool front)
{
    double sp = 7 + h01(2, i, 2, 0) * 6;
    double y = pymodd(h01(2, i, 3, 0) * 64 + t * sp, 68) - 2;
    double x = pymodd(h01(2, i, 1, 0) * 70 + t * 5 + sin(t * 1.8 + i) * 3, 70) - 3;
    int a = pymod((int)(t * 3 + i), 2);
    putd(x, y, front ? (rgb_t){ 255, 190, 215 } : (rgb_t){ 170, 110, 140 });
    putd(x + 1, y + a, front ? (rgb_t){ 230, 130, 170 } : (rgb_t){ 120, 70, 100 });
}

/* ---------------------------------------------------------------- ambient */

static boopie_ambient_t s_amb;

static float s_sky_show = 1;

void boopie_pixel_set_ambient(const boopie_ambient_t *a)
{
    s_amb = a ? *a : (boopie_ambient_t){ 0 };
}

void boopie_pixel_set_sky_show(float show)
{
    s_sky_show = show < 0 ? 0 : show > 1 ? 1 : show;
}

/* A pixel of the sun or moon, as much as shows: dithered out as it goes. */
static void sky_put(double x, double y, rgb_t c)
{
    int xi = (int)rint(x), yi = (int)rint(y);
    if (s_sky_show >= 1 || BAYER[(yi & 3)][(xi & 3)] < s_sky_show) {
        putd(x, y, c);
    }
}

static void disc_with(void (*put)(double, double, rgb_t), double cx, double cy, double r, rgb_t c)
{
    for (int dy = -(int)r - 1; dy <= (int)r + 1; dy++) {
        for (int dx = -(int)r - 1; dx <= (int)r + 1; dx++) {
            if (dx * dx + dy * dy <= r * r + 0.5) {
                put(cx + dx, cy + dy, c);
            }
        }
    }
}

static void disc(double cx, double cy, double r, rgb_t c)
{
    disc_with(putd, cx, cy, r, c);
}

static void cloud(double x, double y, rgb_t c, rgb_t lit)
{
    static const char *const SHAPE[] = { "  ####  ", " ###### ", "########" };
    for (int r = 0; r < 3; r++) {
        for (int k = 0; SHAPE[r][k]; k++) {
            if (SHAPE[r][k] == '#') {
                putd(x + k, y + r, r == 0 ? lit : c);
            }
        }
    }
}

static void sun(double cx, double cy, double r, double t)
{
    disc_with(sky_put, cx, cy, r, (rgb_t){ 255, 214, 90 });
    sky_put(cx - 1, cy - 1, (rgb_t){ 255, 246, 190 });
    for (int k = 0; k < 8; k++) {   /* rays, turning slowly, every other one longer */
        double a = k * 0.7853981633974483 + t * 0.15;
        double len = r + 2 + (k % 2) * (1 + 0.6 * sin(t * 2 + k));
        sky_put(cx + cos(a) * len, cy + sin(a) * len, (rgb_t){ 200, 150, 40 });
    }
}

static void night_stars(double t, int n)
{
    for (int i = 0; i < n; i++) {
        int x = (int)(h01(2, i, 11, 0) * 64), y = (int)(h01(2, i, 12, 0) * 26);
        if (abs(x - 32) < 12 && y > 6) {
            continue;   /* not over its head */
        }
        double tw = 0.5 + 0.5 * sin(t * (0.8 + h01(2, i, 13, 0)) + i);
        if (tw > 0.3) {
            putd(x, y, mix((rgb_t){ 30, 30, 60 }, (rgb_t){ 190, 190, 235 }, tw));
        }
    }
}

static void moon(double cx, double cy, double r, bool full)
{
    disc_with(sky_put, cx, cy, r, (rgb_t){ 250, 236, 170 });
    if (full) {
        sky_put(cx - 1, cy, (rgb_t){ 220, 200, 130 });
        sky_put(cx + 1, cy + 1, (rgb_t){ 220, 200, 130 });
        return;
    }
    disc_with(sky_put, cx + r * 0.6, cy - r * 0.4, r, (rgb_t){ 0, 0, 0 });   /* the crescent's shadow: the sky is black */
}

static void raindrop(int i, double t, bool front)
{
    double sp = 34 + h01(2, i, 21, 0) * 14;
    double y = pymodd(h01(2, i, 22, 0) * 70 + t * sp, 72) - 4;
    double x = pymodd(h01(2, i, 23, 0) * 70 - y * 0.25, 70) - 3;
    rgb_t c = front ? (rgb_t){ 170, 200, 245 } : (rgb_t){ 95, 120, 175 };
    putd(x, y, c);
    putd(x - 0.25, y - 1, c);
    if (front) {
        putd(x - 0.5, y - 2, (rgb_t){ 90, 110, 160 });
    }
}

static void lantern(double x, double t, int i)
{
    double sw = sin(t * 1.4 + i * 2) * 1.2;
    for (int y = 0; y < 5; y++) {
        putd(x + sw * y / 9, y, (rgb_t){ 160, 120, 60 });   /* the cord */
    }
    double cx = x + sw * 0.6;
    putd(cx - 1, 5, (rgb_t){ 230, 180, 60 });
    putd(cx, 5, (rgb_t){ 230, 180, 60 });
    putd(cx + 1, 5, (rgb_t){ 230, 180, 60 });
    for (int y = 6; y < 10; y++) {
        for (int dx = -2; dx <= 2; dx++) {
            bool edge = (y == 6 || y == 9) && (dx == -2 || dx == 2);
            if (!edge) {
                putd(cx + dx, y, dx == -1 ? (rgb_t){ 255, 120, 90 } : (rgb_t){ 210, 30, 40 });
            }
        }
    }
    putd(cx, 10, (rgb_t){ 230, 180, 60 });
    putd(cx, 11, (rgb_t){ 230, 180, 60 });
}

static void firework(double t, int i)
{
    double period = 3.2;
    int burst = (int)((t + i * 1.6) / period);
    double k = pymodd(t + i * 1.6, period) / 1.1;   /* the burst's first 1.1 s */
    if (k > 1) {
        return;
    }
    double cx = 10 + h01(3, burst, i, 31) * 44, cy = 6 + h01(3, burst, i, 32) * 12;
    static const rgb_t COLS[] = { { 255, 90, 90 }, { 255, 220, 90 }, { 120, 220, 255 }, { 255, 140, 230 } };
    rgb_t c = COLS[(burst + i) % 4];
    for (int a = 0; a < 10; a++) {
        double ang = a * 0.6283185307179586;
        double rr = 1 + k * 6;
        putd(cx + cos(ang) * rr, cy + sin(ang) * rr + k * k * 2, mix(c, (rgb_t){ 20, 10, 20 }, k));
    }
}

static void bat(double t, int i)
{
    double x = pymodd(t * (6 + i * 2) + i * 30, 80) - 8;
    double y = 6 + i * 6 + sin(t * 2 + i) * 2;
    int up = pymod((int)(t * 6 + i), 2);
    rgb_t c = { 150, 110, 190 };
    putd(x, y, c);
    putd(x, y + 1, c);
    putd(x - 1, y - up, c);
    putd(x + 1, y - up, c);
    putd(x - 2, y + up - 1, c);
    putd(x + 2, y + up - 1, c);
    putd(x - 3, y + up * 2 - 1, c);
    putd(x + 3, y + up * 2 - 1, c);
    putd(x - 1, y, (rgb_t){ 255, 220, 90 });   /* its eyes */
    putd(x + 1, y, (rgb_t){ 255, 220, 90 });
}

static void heart(double x, double y, rgb_t c)
{
    putd(x - 1, y, c);
    putd(x + 1, y, c);
    putd(x - 1, y + 1, c);
    putd(x, y + 1, c);
    putd(x + 1, y + 1, c);
    putd(x, y + 2, c);
}

static void balloon(int i, double t)
{
    static const rgb_t COLS[] = { { 255, 100, 120 }, { 110, 200, 255 }, { 255, 210, 80 }, { 160, 230, 130 } };
    double sp = 3 + h01(2, i, 41, 0) * 2;
    double y = 66 - pymodd(h01(2, i, 42, 0) * 80 + t * sp, 84);
    double x = (i % 2 ? 52 : 7) + h01(2, i, 43, 0) * 5 + sin(t + i) * 1.2;
    rgb_t c = COLS[i % 4];
    disc(x, y, 1.6, c);
    putd(x - 1, y - 1, mix(c, WHITE, 0.6));
    for (int k = 2; k < 6; k++) {
        putd(x + sin(t * 2 + k) * 0.4, y + k, (rgb_t){ 120, 120, 140 });
    }
}

static void xmas_lights(double t)
{
    static const rgb_t COLS[] = { { 255, 70, 70 }, { 90, 230, 110 }, { 255, 210, 70 }, { 100, 170, 255 } };
    for (int x = 4; x <= 60; x++) {
        double y = 3 + (x - 32) * (x - 32) / 110.0;
        putd(x, y, (rgb_t){ 40, 70, 40 });
        if (x % 5 == 2) {
            int k = x / 5;
            bool lit = pymod((int)(t * 2) + k, 3) != 0;
            putd(x, y + 1, lit ? COLS[k % 4] : mix(COLS[k % 4], (rgb_t){ 0, 0, 0 }, 0.7));
        }
    }
}

static void ambient_back(double t)
{
    if (!s_amb.on) {
        return;
    }
    bool night = s_amb.sky == BOOPIE_SKY_NIGHT;
    bool wet = s_amb.weather == BOOPIE_AMB_RAIN;
    /* The sky. */
    if (night && !wet && s_amb.weather != BOOPIE_AMB_CLOUDY) {
        night_stars(t, 18);
    }
    if (s_amb.sky == BOOPIE_SKY_DUSK) {
        for (int x = 0; x < 64; x++) {   /* the evening glow, low down, fading to the sides */
            for (int y = 48; y < 64; y++) {
                double d = hypot((x - 32) / 34.0, (64 - y) / 16.0);
                if (d < 1 && BAYER[y % 4][x % 4] < (1 - d) * 0.55) {
                    putd(x, y, mix((rgb_t){ 70, 30, 50 }, (rgb_t){ 150, 70, 40 }, 1 - d));
                }
            }
        }
    }
    if (s_amb.fest == BOOPIE_AMB_FEST_MOON) {
        moon(9, 9, 4.5, true);
    } else if (night && !wet) {
        moon(9, 8, 3.5, false);
    } else if (!night && s_amb.weather == BOOPIE_AMB_SUNNY) {
        if (s_amb.sky == BOOPIE_SKY_DUSK) {
            disc_with(sky_put, 52, 46, 3, (rgb_t){ 255, 140, 70 });   /* setting */
            sky_put(51, 45, (rgb_t){ 255, 200, 140 });
        } else {
            sun(50, s_amb.sky == BOOPIE_SKY_MORNING ? 12 : 8, 3, t);
        }
    }
    if (s_amb.weather == BOOPIE_AMB_CLOUDY || wet) {
        rgb_t c = wet ? (rgb_t){ 95, 100, 120 } : night ? (rgb_t){ 75, 80, 100 } : (rgb_t){ 150, 158, 175 };
        rgb_t lit = wet ? (rgb_t){ 130, 136, 155 } : night ? (rgb_t){ 105, 110, 130 } : (rgb_t){ 210, 215, 225 };
        cloud(pymodd(t * 1.2 + 14, 84) - 12, 5, c, lit);
        cloud(pymodd(t * 0.8 + 56, 84) - 12, 13, c, lit);
    }
    if (wet) {
        for (int i = 0; i < 16; i++) {
            raindrop(i, t, false);
        }
    }
    if (s_amb.weather == BOOPIE_AMB_SNOW || (s_amb.fest == BOOPIE_AMB_FEST_XMAS && !wet)) {
        for (int i = 0; i < 16; i++) {
            snowflake(i, t, false);
        }
    }
    /* The festival. */
    switch (s_amb.fest) {
    case BOOPIE_AMB_FEST_LANTERNS:
        lantern(6, t, 0);
        lantern(58, t, 1);
        if (night) {
            firework(t, 0);
            firework(t, 1);
        }
        break;
    case BOOPIE_AMB_FEST_HALLOWEEN:
        bat(t, 0);
        bat(t, 1);
        for (int dy = -2; dy <= 2; dy++) {   /* a pumpkin by its feet, glowing */
            for (int dx = -3; dx <= 3; dx++) {
                if (dx * dx / 9.0 + dy * dy / 4.0 <= 1.1) {
                    putd(7 + dx, 58 + dy, dx == 0 ? (rgb_t){ 200, 90, 20 } : (rgb_t){ 240, 130, 30 });
                }
            }
        }
        putd(7, 55, (rgb_t){ 90, 140, 50 });
        putd(6, 57, (rgb_t){ 255, 230, 120 });
        putd(8, 57, (rgb_t){ 255, 230, 120 });
        break;
    case BOOPIE_AMB_FEST_XMAS:
        xmas_lights(t);
        break;
    case BOOPIE_AMB_FEST_HEARTS:
        for (int i = 0; i < 6; i++) {
            double y = 64 - pymodd(h01(2, i, 51, 0) * 70 + t * (4 + i % 3), 70);
            double x = (i % 2 ? 54 : 6) + h01(2, i, 52, 0) * 6 + sin(t * 1.5 + i) * 1.5;
            heart(x, y, i % 3 ? (rgb_t){ 230, 80, 120 } : (rgb_t){ 255, 150, 180 });
        }
        break;
    case BOOPIE_AMB_FEST_LEAVES:
        for (int i = 0; i < 8; i++) {
            double y = pymodd(h01(2, i, 61, 0) * 64 + t * 5, 68) - 2;
            double x = pymodd(h01(2, i, 62, 0) * 70 + sin(t + i) * 4, 70) - 3;
            putd(x, y, (rgb_t){ 90, 170, 80 });
            putd(x + 1, y + 1, (rgb_t){ 60, 130, 60 });
        }
        break;
    case BOOPIE_AMB_FEST_BALLOONS:
        for (int i = 0; i < 4; i++) {
            balloon(i, t);
        }
        break;
    default:
        break;
    }
}

static void ambient_front(double t)
{
    if (!s_amb.on) {
        return;
    }
    if (s_amb.weather == BOOPIE_AMB_RAIN) {
        for (int i = 16; i < 22; i++) {
            raindrop(i, t, true);
        }
    } else if (s_amb.weather == BOOPIE_AMB_SNOW || s_amb.fest == BOOPIE_AMB_FEST_XMAS) {
        for (int i = 22; i < 28; i++) {
            snowflake(i, t, true);
        }
    }
}

static void scene_back(boopie_scene_t scene, double t)
{
    switch (scene) {
    case BOOPIE_SCENE_DEFAULT:
        ambient_back(t);
        break;
    case BOOPIE_SCENE_STARS: {
        for (int i = 0; i < 34; i++) {
            int x = (int)(h01(2, i, 1, 0) * 64), y = (int)(h01(2, i, 2, 0) * 50);
            double tw = 0.5 + 0.5 * sin(t * (1.5 + h01(2, i, 3, 0) * 2) + i);
            if (tw > 0.35) {
                putd(x, y, mix((rgb_t){ 40, 40, 70 }, (rgb_t){ 230, 230, 255 }, tw));
            }
            if (tw > 0.9 && i % 5 == 0) {
                rgb_t c = { 90, 90, 140 };
                putd(x + 1, y, c);
                putd(x - 1, y, c);
                putd(x, y + 1, c);
                putd(x, y - 1, c);
            }
        }
        double k = pymodd(t, 4.0) / 0.6;   /* a shooting star every 4 s */
        if (k < 1) {
            for (int j = 0; j < 6; j++) {
                putd(50 - k * 30 + j, 4 + k * 12 - j * 0.4, mix(WHITE, (rgb_t){ 60, 60, 110 }, j / 6.0));
            }
        }
        break;
    }
    case BOOPIE_SCENE_FIREFLIES:
        for (int i = 0; i < 9; i++) {
            if (i % 3) {
                firefly(i, t);   /* every third flies in front */
            }
        }
        break;
    case BOOPIE_SCENE_SNOW:
        for (int i = 0; i < 22; i++) {
            snowflake(i, t, false);
        }
        break;
    case BOOPIE_SCENE_PETALS:
        for (int i = 0; i < 12; i++) {
            petal(i, t, false);
        }
        break;
    case BOOPIE_SCENE_BUBBLES:
        for (int i = 0; i < 9; i++) {
            double sp = 6 + h01(2, i, 2, 0) * 6;
            int r = 1 + (int)(h01(2, i, 4, 0) * 2);
            double y = 66 - pymodd(h01(2, i, 3, 0) * 70 + t * sp, 72);
            double x = h01(2, i, 1, 0) * 64 + sin(t * 2 + i) * 1.5;
            for (int dx = -r; dx <= r; dx++) {
                for (int dy = -r; dy <= r; dy++) {
                    double d = hypot(dx, dy);
                    if (r - 0.5 <= d && d <= r + 0.5) {
                        putd(x + dx, y + dy, (rgb_t){ 90, 160, 210 });
                    }
                }
            }
            putd(x - r / 2.0, y - r / 2.0, (rgb_t){ 220, 240, 255 });
        }
        break;
    case BOOPIE_SCENE_MATRIX:
        for (int i = 0; i < 16; i++) {
            int x = i * 4 + 1;
            double sp = 14 + h01(2, i, 2, 0) * 16;
            int ln = 6 + (int)(h01(2, i, 3, 0) * 8);
            double head = pymodd(h01(2, i, 1, 0) * 90 + t * sp, 90) - 10;
            for (int j = 0; j < ln; j++) {
                int y = (int)head - j;
                if (y >= 0 && y < 64 && h01(3, i, y, j == 0 ? (int)(t * 6) : 0) > 0.25) {
                    putd(x, y, j == 0 ? (rgb_t){ 200, 255, 210 }
                                      : mix((rgb_t){ 30, 200, 90 }, (rgb_t){ 6, 40, 18 }, (double)j / ln));
                }
            }
        }
        break;
    case BOOPIE_SCENE_NEON_GRID: {
        const int horizon = 44;
        for (int x = 0; x < 64; x++) {   /* a sunset band */
            for (int y = horizon - 10; y < horizon; y++) {
                if (BAYER[y % 4][x % 4] < (y - horizon + 10) / 12.0) {
                    putd(x, y, (rgb_t){ 90, 30, 90 });
                }
            }
        }
        for (int k = 0; k < 7; k++) {    /* rows rushing toward you */
            double d = pymodd(k + t * 0.8, 7) / 7;
            double y = horizon + d * d * 20;
            for (int x = 0; x < 64; x++) {
                putd(x, y, d > 0.4 ? (rgb_t){ 200, 60, 200 } : (rgb_t){ 110, 40, 130 });
            }
        }
        for (int k = -8; k <= 8; k++) {  /* rails to the vanishing point */
            for (int y = horizon; y < 64; y++) {
                putd(32 + k * 2.2 * (y - horizon) / 6 + k * 0.6, y, (rgb_t){ 130, 50, 160 });
            }
        }
        break;
    }
    default:
        break;
    }
}

static void scene_front(boopie_scene_t scene, double t)
{
    if (scene == BOOPIE_SCENE_DEFAULT) {
        ambient_front(t);
    } else if (scene == BOOPIE_SCENE_FIREFLIES) {
        for (int i = 0; i < 9; i += 3) {
            firefly(i, t);
        }
    } else if (scene == BOOPIE_SCENE_SNOW) {
        for (int i = 22; i < 30; i++) {
            snowflake(i, t, true);
        }
    } else if (scene == BOOPIE_SCENE_PETALS) {
        for (int i = 12; i < 16; i++) {
            petal(i, t, true);
        }
    }
}

/* np.roll of one row by shift, of all three channels or red alone. */
static void roll_row(int y, int shift, bool red_only)
{
    rgb_t row[N];
    memcpy(row, s_img[y], sizeof(row));
    for (int x = 0; x < N; x++) {
        rgb_t from = row[pymod(x - shift, N)];
        if (red_only) {
            s_img[y][x].r = from.r;
        } else {
            s_img[y][x] = from;
        }
    }
}

static void scene_post(boopie_scene_t scene, double t)
{
    if (scene != BOOPIE_SCENE_GLITCH) {
        return;
    }
    for (int y = 1; y < N; y += 2) {     /* scanlines */
        for (int x = 0; x < N; x++) {
            rgb_t *c = &s_img[y][x];
            *c = (rgb_t){ (uint8_t)(c->r * 0.82), (uint8_t)(c->g * 0.82), (uint8_t)(c->b * 0.82) };
        }
    }
    int burst = (int)(t / 1.6);
    if (pymodd(t, 1.6) >= 0.35) {        /* a burst every 1.6 s */
        return;
    }
    for (int b = 0; b < 3; b++) {
        int y0 = (int)(h01(3, burst, b, 1) * 56);
        int hgt = 2 + (int)(h01(3, burst, b, 2) * 6);
        int shift = (int)((h01(3, burst, b, 3) - 0.5) * 10);
        for (int y = y0; y < y0 + hgt && y < N; y++) {
            roll_row(y, shift, false);
        }
    }
    for (int y = 0; y < N; y++) {        /* red split */
        roll_row(y, 1, true);
    }
    for (int k = 0; k < 10; k++) {
        int x = (int)(h01(3, burst, k, 4) * 64), y = (int)(h01(3, burst, k, 5) * 64);
        for (int i = x; i < x + 3 && i < N; i++) {
            s_img[y][i] = k % 2 ? (rgb_t){ 80, 255, 200 } : (rgb_t){ 255, 60, 160 };
        }
    }
}

/* ---------------------------------------------------------------- frame */

/* Maps a character's rest coordinates to the screen for one pose: squash and
 * scale about the feet, then shift and shear. */
typedef struct {
    float dx, dy, tilt;
    float cx, base, sx, sy;
} frame_t;

static frame_t frame(const pose_t *p, float cx, float base, float size, float squash_k, float jump_k)
{
    frame_t f = { .dx = p->dx, .dy = p->dy, .tilt = p->tilt, .cx = cx, .base = base };
    if (p->dy < 0 && jump_k != 1.0f) {     /* tall characters jump lower, clear of the status line */
        f.dy = p->dy * jump_k;
    }
    float squash = 1 + (p->squash - 1) * squash_k;   /* tall characters squash less */
    f.sx = squash * p->scale * size;
    f.sy = p->scale / squash * size;
    return f;
}

static inline float f_ry(const frame_t *f, int y)
{
    return f->base + ((y + 0.5f) - f->dy - f->base) / f->sy;
}

static inline float f_rx(const frame_t *f, int x, int y)
{
    float ys = (y + 0.5f) - f->dy;
    return f->cx + ((x + 0.5f) - f->dx - f->cx - f->tilt * (ys - f->base)) / f->sx;
}

/* ORs the ellipse into m. */
static void ell(const frame_t *f, float cx, float cy, float rx, float ry, mask_t m)
{
    for (int y = 0; y < N; y++) {
        float v = (f_ry(f, y) - cy) / ry;
        v *= v;
        if (v > 1) {
            continue;
        }
        for (int x = 0; x < N; x++) {
            float u = (f_rx(f, x, y) - cx) / rx;
            if (u * u + v <= 1) {
                m[y] |= 1ull << x;
            }
        }
    }
}

/* ORs the rectangle [x0, x1) x [y0, y1) into m. */
static void rect(const frame_t *f, float x0, float y0, float x1, float y1, mask_t m)
{
    for (int y = 0; y < N; y++) {
        float ry = f_ry(f, y);
        if (ry < y0 || ry >= y1) {
            continue;
        }
        for (int x = 0; x < N; x++) {
            float rx = f_rx(f, x, y);
            if (rx >= x0 && rx < x1) {
                m[y] |= 1ull << x;
            }
        }
    }
}

/* Rest point -> screen point. */
static void pt(const frame_t *f, float x, float y, float *sx, float *sy)
{
    *sy = f->base + (y - f->base) * f->sy + f->dy;
    *sx = f->cx + (x - f->cx) * f->sx + f->dx + f->tilt * (*sy - f->dy - f->base);
}


/* ---------------------------------------------------------------- faces */

static void eye(float ex, float ey, const pose_t *p, int side, bool square)
{
    rgb_t E = s_eye;
    switch (p->eyes) {
    case E_BLINK:
        for (int dx = -1; dx <= 1; dx++) {
            put(ex + dx, ey + 1, E);
        }
        break;
    case E_HAPPY: {
        static const int8_t D[5][2] = { { -2, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 }, { 2, 1 } };
        for (int i = 0; i < 5; i++) {
            put(ex + D[i][0], ey + D[i][1], E);
        }
        break;
    }
    case E_DOWN: {
        static const int8_t D[5][2] = { { -2, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }, { 2, 0 } };
        for (int i = 0; i < 5; i++) {
            put(ex + D[i][0], ey + D[i][1], E);
        }
        break;
    }
    case E_X:
        for (int d = -2; d <= 2; d++) {
            put(ex + d, ey + d, E);
            put(ex + d, ey - d, E);
        }
        break;
    case E_SPIRAL: {
        static const int8_t RING[12][2] = { { -1, -2 }, { 0, -2 }, { 1, -2 }, { 2, -1 }, { 2, 0 }, { 2, 1 },
                                            { 1, 2 }, { 0, 2 }, { -1, 2 }, { -2, 1 }, { -2, 0 }, { -2, -1 } };
        int gap = (p->spin * 3 + (side > 0) * 6) % 12;
        for (int i = 0; i < 12; i++) {
            if (i != gap && i != (gap + 1) % 12) {
                put(ex + RING[i][0], ey + RING[i][1], E);
            }
        }
        put(ex, ey, E);
        break;
    }
    case E_HALF:
        for (int dx = -2; dx < 2; dx++) {
            put(ex + dx, ey, E);
        }
        for (int dx = -1; dx <= 1; dx++) {
            put(ex + dx, ey + 1, E);
            put(ex + dx, ey + 2, E);
        }
        break;
    case E_WIDE:
        for (int dy = -3; dy <= 3; dy++) {
            for (int dx = -2; dx < 2; dx++) {
                if (square || !(dy * dy == 9 && (dx == -2 || dx == 1))) {
                    put(ex + dx, ey + dy, E);
                }
            }
        }
        if (s_has_shine) {
            put(ex - 1, ey - 2, s_shine);
            put(ex, ey + 1, s_shine);
        }
        break;
    default: {      /* open / worried / look */
        int lx = p->lookx, ly = p->looky;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (square || !(dy * dy == 4 && dx != 0)) {
                    put(ex + dx + lx, ey + dy + ly, E);
                }
            }
        }
        if (s_has_shine) {
            put(ex - 1 + lx, ey - 1 + ly, s_shine);
        }
        if (p->eyes == E_WORRIED) {
            for (int i = 0; i < 3; i++) {
                put(ex - side * (1 - i), ey - 4 - (i == 0 ? 1 : 0), E);
            }
        }
        break;
    }
    }
}

static void mouth(float mx, float my, const pose_t *p, rgb_t E, rgb_t inside)
{
    switch (p->mouth) {
    case M_W: {
        static const int8_t D[5][2] = { { -2, 0 }, { -1, 1 }, { 0, 0 }, { 1, 1 }, { 2, 0 } };
        for (int i = 0; i < 5; i++) {
            put(mx + D[i][0], my + D[i][1], E);
        }
        break;
    }
    case M_SMILE: {
        static const int8_t D[5][2] = { { -2, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }, { 2, 0 } };
        for (int i = 0; i < 5; i++) {
            put(mx + D[i][0], my + D[i][1], E);
        }
        break;
    }
    case M_O:
        put(mx, my, E);
        put(mx - 1, my + 1, E);
        put(mx + 1, my + 1, E);
        put(mx, my + 2, E);
        put(mx, my + 1, inside);
        break;
    case M_TALK: {
        int h = p->talk < 0 ? 0 : (p->talk > 3 ? 3 : p->talk);
        for (int dx = -1; dx <= 1; dx++) {
            put(mx + dx, my, E);
            for (int dy = 1; dy <= h; dy++) {
                put(mx + dx, my + dy, inside);
            }
        }
        if (h) {
            for (int dx = -1; dx <= 1; dx++) {
                put(mx + dx, my + h + 1, E);
            }
        }
        break;
    }
    case M_FROWN: {
        static const int8_t D[5][2] = { { -2, 1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 2, 1 } };
        for (int i = 0; i < 5; i++) {
            put(mx + D[i][0], my + D[i][1], E);
        }
        break;
    }
    case M_WAVY: {
        static const int8_t D[7][2] = { { -3, 1 }, { -2, 0 }, { -1, 1 }, { 0, 1 }, { 1, 0 }, { 2, 1 }, { 3, 1 } };
        for (int i = 0; i < 7; i++) {
            put(mx + D[i][0], my + D[i][1], E);
        }
        break;
    }
    case M_FLAT:
        for (int dx = -1; dx <= 1; dx++) {
            put(mx + dx, my + 1, E);
        }
        break;
    case M_CHOMP:
        for (int dx = -2; dx <= 2; dx++) {
            put(mx + dx, my, E);
        }
        if (p->talk) {
            for (int dx = -1; dx <= 1; dx++) {
                put(mx + dx, my + 1, inside);
                put(mx + dx, my + 2, E);
            }
        }
        break;
    }
}

static void blush(float fx, float fy, const pose_t *p, int dx_cheek, rgb_t colour)
{
    int w = p->blush == B_BIG ? 2 : (p->blush == B_NONE ? 0 : 1);
    if (!w) {
        return;
    }
    for (int side = -1; side <= 1; side += 2) {
        for (int dx = -w; dx <= w; dx++) {
            put(fx + side * dx_cheek + dx, fy + 4, colour);
            if (w == 2) {
                put(fx + side * dx_cheek + dx, fy + 5, colour);
            }
        }
    }
}

/* ---------------------------------------------------------------- characters */

typedef struct {
    float face_x, face_y, body_x, body_y, light_x, light_y;
    float hat_x, hat_y;                    /* accessory slots: a hat's bottom centre, */
    float neck_x, neck_y, neck_w;          /* a scarf's centre and width */
    bool show_face;
    bool has_screen;                       /* Codex: its screen, for skins */
    float screen_x0, screen_y0, screen_x1, screen_y1;
} anchors_t;

/* Where accessories go, in rest coordinates: a hat's bottom centre, and a
 * scarf's centre and width. */
static void slots(const frame_t *f, anchors_t *a, float hx, float hy, float nx, float ny, float w)
{
    pt(f, hx, hy, &a->hat_x, &a->hat_y);
    pt(f, nx, ny, &a->neck_x, &a->neck_y);
    a->neck_w = w * f->sx;
}

/* The live skin's colours (EYE, CHEEK and Codex's own without one). */
static rgb_t s_eye_base = { 30, 22, 46 };
static rgb_t s_cheek = { 255, 122, 152 };
static rgb_t s_codex_screen = { 30, 34, 84 };
static rgb_t s_codex_glyph = { 120, 236, 240 };
static bool s_glyph_follows_light = true;   /* the prompt takes the state light's colour */
static bool s_klaude_eye_set;               /* a skin's eye colour for Klaude */
static rgb_t s_whale_belly[2] = { { 214, 224, 255 }, { 240, 244, 255 } };   /* shade, light */

typedef struct rig rig_t;
struct rig {
    const char *key, *name;
    uint32_t colour;
    float size, squash_k, jump_k;
    void (*draw)(const rig_t *r, const pose_t *p, anchors_t *a);
    void (*face)(const rig_t *r, int fx, int fy, const pose_t *p);
};

/* The live character's colours. */
static ramp_t s_rp;        /* its body */
static ramp_t s_rp2;       /* GPT's knot, Doubao's hair */
static ramp_t s_rp3;       /* Doubao's top */

static rgb_t light_colour(const pose_t *p)
{
    rgb_t base = p->has_light ? p->light : s_rp.glow;
    return mix((rgb_t){ 20, 16, 30 }, base, clamp01(p->light_level));
}

static void face_default(const rig_t *r, int fx, int fy, const pose_t *p)
{
    (void)r;
    for (int side = -1; side <= 1; side += 2) {
        eye(fx + side * 7, fy, p, side, false);
    }
    blush(fx, fy, p, 11, s_cheek);
    mouth(fx + floordiv(p->lookx, 2), fy + 5, p, s_eye, CHEEK);
}

static frame_t rig_frame(const rig_t *r, const pose_t *p, float cx, float base)
{
    return frame(p, cx, base, r->size, r->squash_k, r->jump_k);
}

/* A stem from (base_x, base_y) in rest coordinates, leaning p->antenna
 * degrees, bulb on top; returns the bulb's centre. */
static void antenna(const frame_t *f, float base_x, float base_y, const pose_t *p, rgb_t colour, float bulb_r,
                    rgb_t glow, rgb_t out, float *ox, float *oy)
{
    float bx, by;
    pt(f, base_x, base_y, &bx, &by);
    float a = p->antenna * PI / 180;
    int steps = (int)p->antenna_len;
    for (int i = 0; i <= steps; i++) {
        float bend = a * powf((float)i / steps, 1.3f);
        put(bx + sinf(bend) * i * 0.95f, by - cosf(bend) * i, colour);
    }
    float tx = bx + sinf(a) * p->antenna_len * 0.95f;
    float ty = by - cosf(a) * p->antenna_len - bulb_r;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float dx = x + 0.5f - tx, dy = y + 0.5f - ty, disc = dx * dx + dy * dy;
            if (disc <= bulb_r * bulb_r + 0.3f) {
                s_img[y][x] = glow;
            } else if (disc <= (bulb_r + 1) * (bulb_r + 1) + 0.3f) {
                s_img[y][x] = out;
            }
        }
    }
    if (p->light_level > 0.5f) {
        put(tx - 1, ty - 1, WHITE);
    }
    *ox = tx;
    *oy = ty;
}

static bool feet_on(const pose_t *p)
{
    return p->feet && p->scale > 0.6f;
}

/* 布比: a round mochi sprite, one antenna leaning right with a glowing bulb. */
static void draw_boopie(const rig_t *r, const pose_t *p, anchors_t *a)
{
    const float cx = 32, cy = 40, rx = 17, ry = 15;
    frame_t f = rig_frame(r, p, cx, cy + ry);
    mask_t body;
    m_clear(body);
    ell(&f, cx, cy, rx, ry, body);
    if (feet_on(p)) {
        ell(&f, cx - 7, cy + ry - 1, 4, 2.5f, body);
        ell(&f, cx + 7, cy + ry - 1, 4, 2.5f, body);
    }
    for (int i = 0; i < p->nhands; i++) {
        if (!p->hands[i].front) {
            ell(&f, cx + p->hands[i].side * rx, cy + p->hands[i].dy, 3 / p->squash, 2.6f * p->squash, body);
        }
    }
    shaded(body, &s_rp);
    outline(body, s_rp.out);
    antenna(&f, cx + 3, cy - ry + 1, p, s_rp.out, 2.4f, light_colour(p), s_rp.out, &a->light_x, &a->light_y);
    pt(&f, cx, cy - 1, &a->face_x, &a->face_y);
    slots(&f, a, 27, 27, cx, 47, 26);
    pt(&f, cx, cy, &a->body_x, &a->body_y);
    a->show_face = p->scale > 0.6f;
}

/* GPT: a white mochi, Boopie's shape and face, with the knot of its logo
 * clipped on its head like a hair clip. */
static const rgb_t GPT_INK = { 18, 18, 24 };
static const rgb_t GPT_HOLE = { 34, 34, 44 };

static void draw_gpt(const rig_t *r, const pose_t *p, anchors_t *a)
{
    const float cx = 32, cy = 41, rx = 16, ry = 14;
    frame_t f = rig_frame(r, p, cx, cy + ry);
    mask_t body;
    m_clear(body);
    ell(&f, cx, cy, rx, ry, body);
    if (feet_on(p)) {
        ell(&f, cx - 7, cy + ry - 1, 4, 2.5f, body);
        ell(&f, cx + 7, cy + ry - 1, 4, 2.5f, body);
    }
    for (int i = 0; i < p->nhands; i++) {
        if (!p->hands[i].front) {
            ell(&f, cx + p->hands[i].side * rx, cy + p->hands[i].dy, 3, 2.6f, body);
        }
    }
    shaded(body, &s_rp);
    outline(body, GPT_INK);
    const int n = 16;
    const float kx = 42, ky = 25;     /* the clip, up on the right like Boopie's antenna */
    mask_t bands, holes;
    m_clear(bands);
    m_clear(holes);
    for (int y = 0; y < N; y++) {
        /* a hair over, as float misses exact boundaries the prototype's double hits */
        int gy = (int)floorf(f_ry(&f, y) - (ky - n / 2.0f) + 1e-4f);
        if (gy < 0 || gy >= n) {
            continue;
        }
        for (int x = 0; x < N; x++) {
            int gx = (int)floorf(f_rx(&f, x, y) - (kx - n / 2.0f) + 1e-4f);
            if (gx >= 0 && gx < n) {
                char ch = KNOT[gy][gx];
                if (ch == '#') {
                    bands[y] |= 1ull << x;
                } else if (ch == '.') {
                    holes[y] |= 1ull << x;
                }
            }
        }
    }
    flat(holes, GPT_HOLE);
    bool rim = s_has_rim;
    s_has_rim = false;                /* the bands are too thin for a rim light */
    shaded(bands, &s_rp2);
    s_has_rim = rim;
    m_or(bands, holes);
    outline(bands, GPT_INK);
    pt(&f, cx, cy - 1, &a->face_x, &a->face_y);
    pt(&f, cx, cy, &a->body_x, &a->body_y);
    pt(&f, 42, 17, &a->light_x, &a->light_y);
    slots(&f, a, 25, 29, cx, 48, 24);
    a->show_face = true;
}

/* Codex: a cloud-headed robot whose face is a terminal; its eyes are the prompt, >_ . */

static void draw_codex(const rig_t *r, const pose_t *p, anchors_t *a)
{
    frame_t f = rig_frame(r, p, 32, 56);
    mask_t head, body, whole, screen, corners;
    m_clear(head);
    m_clear(body);
    ell(&f, 32, 27, 17, 12, head);
    static const float PUFF[6][3] = { { 22, 18, 6 }, { 30, 15, 7 }, { 38, 15, 6.5f },
                                      { 44, 20, 5.5f }, { 17, 25, 5 }, { 47, 27, 4.5f } };
    for (int i = 0; i < 6; i++) {
        ell(&f, PUFF[i][0], PUFF[i][1], PUFF[i][2], PUFF[i][2], head);
    }
    ell(&f, 32, 45, 9, 7, body);
    if (feet_on(p)) {
        ell(&f, 28, 52, 2.6f, 3.4f, body);
        ell(&f, 36, 52, 2.6f, 3.4f, body);
    }
    for (int i = 0; i < p->nhands; i++) {
        if (!p->hands[i].front) {
            ell(&f, 32 + p->hands[i].side * 11, 44 + p->hands[i].dy * 0.8f, 2.8f, 2.4f, body);
        }
    }
    memcpy(whole, head, sizeof(mask_t));
    m_or(whole, body);
    shaded(body, &s_rp);
    shaded(head, &s_rp);
    outline(whole, s_rp.out);
    m_clear(screen);
    m_clear(corners);
    rect(&f, 21, 21, 44, 34, screen);
    rect(&f, 21, 21, 22, 22, corners);
    rect(&f, 43, 21, 44, 22, corners);
    rect(&f, 21, 33, 22, 34, corners);
    rect(&f, 43, 33, 44, 34, corners);
    m_andnot(screen, corners);
    flat(screen, s_codex_screen);
    float gx, gy;
    pt(&f, 32, 45, &gx, &gy);
    static const int8_t CHEST[5][2] = { { -3, -1 }, { -2, 0 }, { -3, 1 }, { 0, 0 }, { 1, 0 } };   /* >- on the chest */
    for (int i = 0; i < 5; i++) {
        put(gx + CHEST[i][0], gy + CHEST[i][1], mix(s_codex_glyph, s_rp.mid, 0.4));
    }
    a->has_screen = true;
    pt(&f, 21, 21, &a->screen_x0, &a->screen_y0);
    pt(&f, 44, 34, &a->screen_x1, &a->screen_y1);
    pt(&f, 32, 27, &a->face_x, &a->face_y);
    pt(&f, 32, 40, &a->body_x, &a->body_y);
    pt(&f, 44, 14, &a->light_x, &a->light_y);
    slots(&f, a, 31, 10, 32, 38, 16);
    a->show_face = p->scale > 0.6f;
}

static void glyph(char ch, int x, int y, rgb_t c)
{
    const char *g = glyph_rows(ch);
    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 3; i++) {
            if (g[j * 4 + i] == '#') {
                put(x + i - 1, y + j - 2, c);
            }
        }
    }
}

static void face_codex(const rig_t *r, int fx, int fy, const pose_t *p)
{
    (void)r;
    rgb_t col = p->has_light && s_glyph_follows_light ? p->light : s_codex_glyph;
    col = mix(s_codex_screen, col, fmax(0.25, clamp01(p->light_level)));
    int L = fx - 5, R = fx + 4;
    if (p->mouth == M_TALK) {               /* the cursor grows with the voice */
        glyph('>', L, fy, col);
        for (int dy = -p->talk; dy < 3; dy++) {
            for (int dx = 0; dx < 3; dx++) {
                put(R + dx - 1, fy + dy, col);
            }
        }
        return;
    }
    char a = '>', b = '_';
    switch (p->eyes) {
    case E_BLINK: a = b = '-'; break;
    case E_HAPPY: case E_DOWN: a = b = '^'; break;
    case E_WIDE: a = b = '|'; break;
    case E_WORRIED: a = '>'; b = '<'; break;
    case E_X: a = b = 'x'; break;
    case E_SPIRAL: a = b = 'o'; break;
    case E_HALF: a = '-'; b = '_'; break;
    default: break;
    }
    if (p->eyes == E_OPEN && (int)(p->t * 2) % 2) {   /* the cursor blinks */
        b = ' ';
    }
    int lx = p->eyes == E_LOOK ? p->lookx : 0, ly = p->eyes == E_LOOK ? p->looky : 0;
    glyph(a, L + lx, fy + ly, col);
    glyph(b, R + lx, fy + ly, col);
    if (p->blush == B_BIG) {
        for (int side = -1; side <= 1; side += 2) {
            for (int dx = -1; dx <= 1; dx++) {
                put(fx + side * 9 + dx, fy + 4, CHEEK);
            }
        }
    }
}

/* 小克: a blocky orange critter with square eyes, stubby side arms and four little legs. */
static void draw_klaude(const rig_t *r, const pose_t *p, anchors_t *a)
{
    frame_t f = rig_frame(r, p, 32, 51);
    mask_t body;
    m_clear(body);
    rect(&f, 17, 22, 47, 44, body);
    for (int i = 0; i < p->nhands; i++) {
        if (!p->hands[i].front) {
            float up = minf(0, p->hands[i].dy) * 1.6f;
            float x0 = p->hands[i].side < 0 ? 11 : 47;
            rect(&f, x0, 31 + up, x0 + 6, 36 + up, body);
        }
    }
    if (feet_on(p)) {
        static const float LEGS[4] = { 20, 25, 36, 41 };
        for (int i = 0; i < 4; i++) {
            rect(&f, LEGS[i], 44, LEGS[i] + 3, 51, body);
        }
    }
    shaded(body, &s_rp);
    outline(body, s_rp.out);
    pt(&f, 32, 30, &a->face_x, &a->face_y);
    pt(&f, 32, 35, &a->body_x, &a->body_y);
    pt(&f, 44, 16, &a->light_x, &a->light_y);
    slots(&f, a, 32, 23, 32, 42, 30);
    a->show_face = p->scale > 0.6f;
}

static void face_klaude(const rig_t *r, int fx, int fy, const pose_t *p)
{
    (void)r;
    bool white = p->light.r == 255 && p->light.g == 255 && p->light.b == 255;
    s_eye = p->has_light && !white ? mix(WHITE, p->light, 0.35) : (rgb_t){ 255, 246, 236 };
    if (s_klaude_eye_set) {
        s_eye = s_eye_base;
    }
    s_has_shine = false;
    for (int side = -1; side <= 1; side += 2) {
        eye(fx + side * 7, fy, p, side, true);
    }
    blush(fx, fy, p, 11, (rgb_t){ 255, 176, 150 });
    if (p->mouth == M_TALK || p->mouth == M_O || p->mouth == M_CHOMP || p->mouth == M_WAVY || p->mouth == M_FROWN) {
        mouth(fx, fy + 6, p, (rgb_t){ 92, 34, 22 }, (rgb_t){ 170, 60, 50 });
    }
}

/* DeepSeek 小鲸鱼: a round little whale with a white tummy and a perky tail;
 * its spout is the state light. */
static void draw_whale(const rig_t *r, const pose_t *p, anchors_t *a)
{
    const float cx = 31, cy = 41, rx = 16, ry = 15;
    frame_t f = rig_frame(r, p, cx, cy + ry);
    float an = (p->antenna - 18) * PI / 180;   /* the tail swings like Boopie's antenna */
    float tx = 46 + 3 * sinf(an), ty = 21 - 1.5f * cosf(an);
    mask_t body, belly, inner;
    m_clear(body);
    ell(&f, 44 + 1.5f * sinf(an), 27, 2.6f, 7.5f, body);   /* the stalk reaches the flukes */
    ell(&f, tx - 3, ty, 3.5f, 2, body);
    ell(&f, tx + 3, ty - 0.5f, 3.5f, 2, body);
    ell(&f, cx, cy, rx, ry, body);
    for (int i = 0; i < p->nhands; i++) {
        if (!p->hands[i].front) {
            ell(&f, cx + p->hands[i].side * (rx + 0.5f), cy + 6 + p->hands[i].dy * 0.8f, 3.2f, 2.0f, body);
        }
    }
    shaded(body, &s_rp);
    m_clear(belly);
    m_clear(inner);
    ell(&f, cx, cy + 8, 10.5f, 6, belly);
    ell(&f, cx, cy, rx - 1.2f, ry - 1.2f, inner);
    m_and(belly, inner);
    flat(belly, s_whale_belly[1]);
    float bx, by;
    pt(&f, 0, cy + 10, &bx, &by);
    for (int y = 0; y < N; y++) {
        for (uint64_t row = belly[y]; row; row &= row - 1) {
            int x = __builtin_ctzll(row);
            if (bay(x, y) > 0.5f && y + 0.5f > by) {
                s_img[y][x] = s_whale_belly[0];
            }
        }
    }
    outline(body, s_rp.out);
    float hx, hy;
    pt(&f, cx - 2, cy - ry, &hx, &hy);
    rgb_t col = light_colour(p);
    int h = 2 + (int)rint(3 * clamp01(p->light_level));
    for (int i = 0; i < h; i++) {
        put(hx, hy - 1 - i, col);
    }
    const int SPRAY[4][2] = { { -2, -h }, { 2, -h }, { -3, -h + 2 }, { 3, -h + 2 } };
    for (int i = 0; i < 4; i++) {
        put(hx + SPRAY[i][0], hy + SPRAY[i][1], col);
    }
    pt(&f, cx, cy - 2, &a->face_x, &a->face_y);
    slots(&f, a, 37, 27, cx, 47, 24);
    pt(&f, cx, cy, &a->body_x, &a->body_y);
    a->light_x = hx;
    a->light_y = hy - h - 1;
    a->show_face = p->scale > 0.6f;
}

/* 豆包: a girl with a brown bob and big eyes, in a black top; her hair clip is the state light. */
static void draw_doubao(const rig_t *r, const pose_t *p, anchors_t *a)
{
    frame_t f = rig_frame(r, p, 32, 57);
    mask_t torso, cut, legs, arms, hair, face, bangs, skin, whole, clip;
    m_clear(torso);
    m_clear(cut);
    ell(&f, 32, 49, 10, 7, torso);
    rect(&f, 0, 43, 64, 57, cut);
    m_and(torso, cut);
    m_clear(legs);
    if (feet_on(p)) {
        rect(&f, 27, 54, 31, 57, legs);
        rect(&f, 33, 54, 37, 57, legs);
    }
    m_clear(arms);
    for (int i = 0; i < p->nhands; i++) {
        if (!p->hands[i].front) {
            ell(&f, 32 + p->hands[i].side * 11, 48 + p->hands[i].dy * 0.8f, 2.6f, 2.4f, arms);
        }
    }
    m_clear(hair);
    m_clear(cut);
    ell(&f, 32, 26, 18, 16, hair);
    rect(&f, 0, 0, 64, 40, cut);
    m_and(hair, cut);
    rect(&f, 14, 22, 21, 40, hair);
    rect(&f, 43, 22, 50, 40, hair);
    m_clear(face);
    ell(&f, 32, 30, 13, 11, face);
    m_clear(bangs);
    ell(&f, 36, 17, 14, 7, bangs);
    ell(&f, 22, 18, 7, 6, bangs);
    memcpy(skin, face, sizeof(mask_t));
    m_andnot(skin, bangs);
    m_or(skin, arms);
    memcpy(whole, hair, sizeof(mask_t));
    m_or(whole, torso);
    m_or(whole, legs);
    m_or(whole, arms);
    m_or(whole, face);
    m_or(torso, legs);
    shaded(torso, &s_rp3);
    shaded(hair, &s_rp2);
    shaded(skin, &s_rp);
    outline(whole, s_rp2.out);
    float bx, by;
    pt(&f, 32, 48, &bx, &by);
    put(bx, by, (rgb_t){ 230, 230, 236 });
    m_clear(clip);
    ell(&f, 45, 16, 2.4f, 1.6f, clip);
    flat(clip, light_colour(p));
    outline(clip, s_rp2.out);
    pt(&f, 32, 30, &a->face_x, &a->face_y);
    pt(&f, 32, 44, &a->body_x, &a->body_y);
    pt(&f, 45, 16, &a->light_x, &a->light_y);
    slots(&f, a, 29, 11, 32, 43, 12);
    a->show_face = p->scale > 0.6f;
}

static void face_doubao(const rig_t *r, int fx, int fy, const pose_t *p)
{
    (void)r;
    s_eye = (rgb_t){ 58, 36, 30 };
    for (int side = -1; side <= 1; side += 2) {
        eye(fx + side * 6, fy, p, side, false);
        if (p->eyes == E_OPEN || p->eyes == E_LOOK || p->eyes == E_WORRIED || p->eyes == E_WIDE) {   /* lashes */
            put(fx + side * 6 + side * 2, fy - 2 + p->looky, s_eye);
        }
    }
    blush(fx, fy, p, 9, (rgb_t){ 250, 150, 150 });
    mouth(fx, fy + 5, p, (rgb_t){ 190, 90, 80 }, (rgb_t){ 235, 120, 120 });
}

static const rig_t RIGS[BOOPIE_CHAR_COUNT] = {
    [BOOPIE_CHAR_BOOPIE] = { "boopie", "布比", 0xff9ec8, 1.18f, 1, 1, draw_boopie, face_default },
    [BOOPIE_CHAR_GPT] = { "gpt", "GPT", 0xe8e8e8, 1.18f, 1, 1, draw_gpt, face_default },
    [BOOPIE_CHAR_CODEX] = { "codex", "Codex", 0x5b86f5, 1.08f, 0.35f, 0.4f, draw_codex, face_codex },
    [BOOPIE_CHAR_KLAUDE] = { "klaude", "小克", 0xf28c5e, 1.18f, 1, 1, draw_klaude, face_klaude },
    [BOOPIE_CHAR_WHALE] = { "whale", "DeepSeek 小鲸鱼", 0x5a7dff, 1.18f, 1, 1, draw_whale, face_default },
    [BOOPIE_CHAR_DOUBAO] = { "doubao", "豆包", 0xf2c9b4, 1.08f, 0.35f, 0.4f, draw_doubao, face_doubao },
};

static boopie_char_t s_char = BOOPIE_CHAR_BOOPIE;
static bool s_char_set;

const char *boopie_char_key(boopie_char_t c)
{
    return (int)c >= 0 && c < BOOPIE_CHAR_COUNT ? RIGS[c].key : NULL;
}

const char *boopie_char_name(boopie_char_t c)
{
    return (int)c >= 0 && c < BOOPIE_CHAR_COUNT ? RIGS[c].name : NULL;
}

uint32_t boopie_char_default_colour(boopie_char_t c)
{
    return (int)c >= 0 && c < BOOPIE_CHAR_COUNT ? RIGS[c].colour : RIGS[0].colour;
}

/* ---------------------------------------------------------------- skins */

/* A skin's optional colour: SET(0xRRGGBB), or 0 for the character's own. */
#define SET(rgb) (0x1000000u | (rgb))
/* From here on only ever append: the index is what NVS keeps of what's owned. */
typedef enum {
    FX_BODY_NONE, FX_BODY_STARRY, FX_BODY_JELLY, FX_BODY_INK, FX_BODY_PORCELAIN, FX_BODY_CYBER, FX_BODY_NEON,
    FX_BODY_ICE, FX_BODY_LAVA, FX_BODY_KOI, FX_BODY_GLOW_SPOTS, FX_BODY_WINTER,
} body_fx_t;
typedef enum { FX_FACE_NONE, FX_FACE_SCANLINES } face_fx_t;
typedef enum { FX_BACK_NONE, FX_BACK_EMBERS } back_fx_t;

typedef struct {
    const char *key, *name;
    boopie_char_t character;  /* BOOPIE_SKIN_MUSE for Muse's own */
    bool collector;           /* 典藏 */
    uint16_t price;           /* stars */
    boopie_scene_t scene;
    uint32_t colour, eye, cheek, glow, outline;
    uint32_t screen, glyph;   /* Codex */
    bool glyph_fixed;         /* Codex's prompt stays its own colour */
    uint32_t bands;           /* GPT's knot */
    uint32_t belly[2];        /* the whale's, shade and light */
    uint32_t hair, top;       /* Doubao */
    uint32_t visor;           /* Muse: its face window */
    body_fx_t body_fx;
    face_fx_t face_fx;
    back_fx_t back_fx;
    uint16_t limited;         /* bit f: on sale only in festival f (boopie_fest_t); 0 always */
    uint8_t muse_fx;          /* Muse's: drawn over its own frame (MUSE_FX_*) */
} skin_t;

enum { MUSE_FX_NONE = 0, MUSE_FX_SHORTS, MUSE_FX_KNIGHT };

#define SKIN(k, n, c, coll, stars, sc, col) .key = k, .name = n, .character = c, .collector = coll, \
    .price = stars, .scene = sc, .colour = col

/* As the prototype's SKINS (tools/boopie/avatar_proto.py). */
static const skin_t SKINS[] = {
    { SKIN("boopie_starry", "星空", BOOPIE_CHAR_BOOPIE, true, 300, BOOPIE_SCENE_STARS, 0x1f1856),
      .eye = SET(0xececff), .cheek = SET(0xd678d2), .glow = SET(0xffeca0), .outline = SET(0x8c78ec), .body_fx = FX_BODY_STARRY },
    { SKIN("boopie_jelly", "果冻", BOOPIE_CHAR_BOOPIE, false, 150, BOOPIE_SCENE_BUBBLES, 0x7fe6d4),
      .cheek = SET(0xff8caa), .outline = SET(0x288c82), .body_fx = FX_BODY_JELLY },
    { SKIN("muse_astronaut", "宇航员", BOOPIE_SKIN_MUSE, true, 300, BOOPIE_SCENE_STARS, 0xe8ecf2), .visor = SET(0x1d2b4a) },
    { SKIN("muse_matcha", "抹茶", BOOPIE_SKIN_MUSE, false, 150, BOOPIE_SCENE_FIREFLIES, 0x8fbf6a) },
    { SKIN("gpt_ink", "水墨", BOOPIE_CHAR_GPT, true, 300, BOOPIE_SCENE_DEFAULT, 0xdadada),
      .cheek = SET(0xc87878), .bands = SET(0xc83a3a), .body_fx = FX_BODY_INK },
    { SKIN("gpt_porcelain", "青花瓷", BOOPIE_CHAR_GPT, false, 150, BOOPIE_SCENE_PETALS, 0xf4f6fa),
      .cheek = SET(0x96aae6), .bands = SET(0x3a5bb8), .body_fx = FX_BODY_PORCELAIN },
    { SKIN("codex_neon", "霓虹", BOOPIE_CHAR_CODEX, true, 300, BOOPIE_SCENE_NEON_GRID, 0x120e24),
      .cheek = SET(0xff3cc8), .outline = SET(0x00f0ff), .screen = SET(0x0a0014), .glyph = SET(0xff46d2), .glyph_fixed = true,
      .body_fx = FX_BODY_NEON },
    { SKIN("codex_glitch", "赛博故障", BOOPIE_CHAR_CODEX, false, 150, BOOPIE_SCENE_GLITCH, 0x5a3cf0),
      .cheek = SET(0x00ffc8), .outline = SET(0x140a3c), .screen = SET(0x060614), .glyph = SET(0x00ffc8), .glyph_fixed = true,
      .body_fx = FX_BODY_CYBER },
    { SKIN("klaude_ice", "冰块", BOOPIE_CHAR_KLAUDE, true, 300, BOOPIE_SCENE_SNOW, 0xa9dcf2),
      .eye = SET(0x285a8c), .cheek = SET(0x96c8f0), .outline = SET(0x468cbe), .body_fx = FX_BODY_ICE },
    { SKIN("klaude_lava", "熔岩", BOOPIE_CHAR_KLAUDE, false, 150, BOOPIE_SCENE_DEFAULT, 0x1c1414),
      .eye = SET(0xffec78), .cheek = SET(0xff5028), .outline = SET(0xbe1e14), .body_fx = FX_BODY_LAVA, .back_fx = FX_BACK_EMBERS },
    { SKIN("whale_koi", "锦鲤", BOOPIE_CHAR_WHALE, true, 300, BOOPIE_SCENE_BUBBLES, 0xf6f2ec),
      .outline = SET(0x964632), .belly = { SET(0xece6de), SET(0xfaf8f4) }, .body_fx = FX_BODY_KOI },
    { SKIN("whale_deepsea", "深海", BOOPIE_CHAR_WHALE, false, 150, BOOPIE_SCENE_FIREFLIES, 0x16285e),
      .eye = SET(0xdcffff), .outline = SET(0x3cb4dc), .glow = SET(0x50f0ff), .belly = { SET(0x1e3468), SET(0x284886) },
      .body_fx = FX_BODY_GLOW_SPOTS },
    { SKIN("doubao_sakura", "樱花", BOOPIE_CHAR_DOUBAO, false, 150, BOOPIE_SCENE_PETALS, 0xf2c9b4),
      .glow = SET(0xffa0c8), .hair = SET(0xd9809e), .top = SET(0xf6c6d6) },
    { SKIN("doubao_winter", "冬装", BOOPIE_CHAR_DOUBAO, true, 300, BOOPIE_SCENE_SNOW, 0xf2c9b4),
      .top = SET(0xe6d9bf), .body_fx = FX_BODY_WINTER },
    /* A third for each, a new palette, and three for festivals only. */
    { SKIN("muse_patrick", "派大星", BOOPIE_SKIN_MUSE, false, 200, BOOPIE_SCENE_BUBBLES, 0xffbccd),
      .muse_fx = MUSE_FX_SHORTS },
    { SKIN("muse_berry", "莓果", BOOPIE_SKIN_MUSE, false, 200, BOOPIE_SCENE_PETALS, 0xb46ab4) },
    { SKIN("gpt_jade", "翡翠", BOOPIE_CHAR_GPT, false, 200, BOOPIE_SCENE_FIREFLIES, 0x9fd8b8),
      .cheek = SET(0xe69696), .bands = SET(0x2e7d5b) },
    { SKIN("codex_retro", "复古绿屏", BOOPIE_CHAR_CODEX, false, 200, BOOPIE_SCENE_MATRIX, 0xd8d2bc),
      .cheek = SET(0xe69678), .outline = SET(0x6e6450), .screen = SET(0x081e0a), .glyph = SET(0x50ff78),
      .glyph_fixed = true },
    { SKIN("klaude_peach", "蜜桃", BOOPIE_CHAR_KLAUDE, false, 200, BOOPIE_SCENE_PETALS, 0xffc0a8),
      .eye = SET(0x5a2820), .cheek = SET(0xff7a8a), .outline = SET(0xc86450) },
    { SKIN("whale_sunset", "晚霞", BOOPIE_CHAR_WHALE, false, 200, BOOPIE_SCENE_DEFAULT, 0xff9e7a),
      .outline = SET(0xb4503c), .belly = { SET(0xffd6b0), SET(0xfff0dc) } },
    { SKIN("doubao_sport", "运动装", BOOPIE_CHAR_DOUBAO, false, 200, BOOPIE_SCENE_DEFAULT, 0xf2c9b4),
      .hair = SET(0x3a2a20), .top = SET(0x3a7bd5) },
    { SKIN("boopie_newyear", "新春", BOOPIE_CHAR_BOOPIE, true, 250, BOOPIE_SCENE_DEFAULT, 0xcc2a2a),
      .cheek = SET(0xffb0a0), .glow = SET(0xffd34a), .outline = SET(0x8a1010), .limited = 1u << 1 | 1u << 5 },
    { SKIN("klaude_pumpkin", "南瓜", BOOPIE_CHAR_KLAUDE, true, 250, BOOPIE_SCENE_DEFAULT, 0xff8c1a),
      .eye = SET(0x3a1a00), .cheek = SET(0xffc850), .outline = SET(0xa04a00), .limited = 1u << 3 },
    { SKIN("whale_xmas", "圣诞树", BOOPIE_CHAR_WHALE, true, 250, BOOPIE_SCENE_SNOW, 0x2f8f4f),
      .glow = SET(0xffd34a), .outline = SET(0x145a28), .belly = { SET(0xd8f0d8), SET(0xf4fff4) }, .limited = 1u << 4 },
    { SKIN("muse_knight", "骑士", BOOPIE_SKIN_MUSE, true, 300, BOOPIE_SCENE_DEFAULT, 0xd9c7a8),
      .muse_fx = MUSE_FX_KNIGHT },
};
#define SKIN_COUNT (int)(sizeof(SKINS) / sizeof(SKINS[0]))
_Static_assert(SKIN_COUNT <= 32, "NVS keeps what's owned in a u32");

static int s_skin = -1;

uint32_t boopie_skin_limited(int skin)
{
    return skin >= 0 && skin < (int)(sizeof SKINS / sizeof SKINS[0]) ? SKINS[skin].limited : 0;
}

int boopie_skin_count(void)
{
    return SKIN_COUNT;
}

static const skin_t *skin_at(int i)
{
    return i >= 0 && i < SKIN_COUNT ? &SKINS[i] : NULL;
}

const char *boopie_skin_key(int i)
{
    return skin_at(i) ? SKINS[i].key : NULL;
}

const char *boopie_skin_name(int i)
{
    return skin_at(i) ? SKINS[i].name : NULL;
}

boopie_char_t boopie_skin_character(int i)
{
    return skin_at(i) ? SKINS[i].character : BOOPIE_CHAR_COUNT;
}

int boopie_skin_price(int i)
{
    return skin_at(i) ? SKINS[i].price : 0;
}

bool boopie_skin_collector(int i)
{
    return skin_at(i) && SKINS[i].collector;
}

boopie_scene_t boopie_skin_scene(int i)
{
    return skin_at(i) ? SKINS[i].scene : BOOPIE_SCENE_DEFAULT;
}

int boopie_skin_from_key(const char *key)
{
    for (int i = 0; key && i < SKIN_COUNT; i++) {
        if (strcmp(SKINS[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static inline bool inb(int x, int y)
{
    return x >= 0 && x < N && y >= 0 && y < N && m_get(s_body, x, y);
}

static inline float bayf(int x, int y)
{
    return BAYER[y % 4][x % 4];
}

static rgb_t rgbf(uint8_t r, uint8_t g, uint8_t b)
{
    return (rgb_t){ r, g, b };
}

/* The patterns over the body, as the prototype's skin_body. */
static void skin_body(const skin_t *sk, const pose_t *pose, const anchors_t *a)
{
    const double bx = a->body_x, by = a->body_y, t = pose->scene_t;   /* runs on, as the background */
    switch (sk->body_fx) {
    case FX_BODY_JELLY:     /* a glossy highlight and bubbles rising inside */
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                double u = (x - bx + 8) / 6, v = (y - by + 8) / 3.5;
                if (inb(x, y) && u * u + v * v <= 1 && bayf(x, y) < 0.7) {
                    put(x, y, rgbf(225, 255, 250));
                }
            }
        }
        for (int i = 0; i < 6; i++) {
            double k = pymodd(t * (0.15 + h01(2, i, 3, 0) * 0.2) + h01(2, i, 1, 0), 1);
            int x = (int)rint(bx + (h01(2, i, 2, 0) - 0.5) * 22), y = (int)rint(by + 10 - k * 22);
            if (inb(x, y)) {
                put(x, y, rgbf(200, 255, 245));
            }
        }
        break;
    case FX_BODY_INK:       /* ink pooling in the lower body, a few drops */
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (!inb(x, y)) {
                    continue;
                }
                double d = (y - by) / 14 + (h01(3, x / 3, y / 3, 5) - 0.5) * 0.5;
                if (d > 0.2 && bayf(x, y) < d) {
                    put(x, y, mix(rgbf(150, 150, 150), rgbf(40, 40, 44), d < 1 ? d : 1));
                }
            }
        }
        for (int i = 0; i < 4; i++) {
            int x = (int)rint(bx + (h01(2, i, 6, 0) - 0.5) * 24), y = (int)rint(by + (h01(2, i, 7, 0) - 0.8) * 16);
            if (inb(x, y)) {
                put(x, y, rgbf(30, 30, 34));
            }
        }
        break;
    case FX_BODY_PORCELAIN: /* a blue band and little blue flowers */
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (inb(x, y) && 7 <= y - by && y - by <= 9 + (x % 4 == 0)) {
                    put(x, y, rgbf(50, 80, 180));
                }
            }
        }
        for (int i = 0; i < 5; i++) {
            int x = (int)rint(bx + (h01(2, i, 8, 0) - 0.5) * 22), y = (int)rint(by + (h01(2, i, 9, 0) - 0.9) * 12);
            static const int8_t D[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (int k = 0; k < 5; k++) {
                if (inb(x + D[k][0], y + D[k][1])) {
                    put(x + D[k][0], y + D[k][1], k ? rgbf(70, 100, 200) : rgbf(220, 230, 250));
                }
            }
        }
        break;
    case FX_BODY_CYBER:     /* cyan lines across the body, magenta specks */
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (!inb(x, y)) {
                    continue;
                }
                if ((y + (int)(t * 6)) % 4 == 0) {
                    put(x, y, rgbf(0, 230, 210));
                } else if (h01(3, x, y, (int)(t * 4)) > 0.97) {
                    put(x, y, rgbf(255, 40, 180));
                }
            }
        }
        break;
    case FX_BODY_NEON: {    /* a magenta tube inside the cyan one, and a glow outside */
        static const int8_t D1[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        static const int8_t D2[4][2] = { { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 } };
        static const int8_t NX[4] = { -3, -2, 2, 3 }, NY[5] = { -3, -2, 0, 2, 3 };
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (inb(x, y)) {
                    bool inner = true, edge1 = true;
                    for (int k = 0; k < 4; k++) {
                        inner &= inb(x + D2[k][0], y + D2[k][1]);
                        edge1 &= inb(x + D1[k][0], y + D1[k][1]);
                    }
                    if (edge1 && !inner) {
                        put(x, y, rgbf(255, 60, 200));
                    }
                } else {
                    bool near = false, touch = false;
                    for (int i = 0; i < 4 && !near; i++) {
                        for (int j = 0; j < 5 && !near; j++) {
                            near = inb(x + NX[i], y + NY[j]);
                        }
                    }
                    for (int k = 0; k < 4; k++) {
                        touch |= inb(x + D1[k][0], y + D1[k][1]);
                    }
                    if (near && !touch && bayf(x, y) < 0.35) {
                        put(x, y, rgbf(0, 90, 110));
                    }
                }
            }
        }
        break;
    }
    case FX_BODY_ICE: {     /* a shine across it, frost at the edges, a crack */
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (!inb(x, y)) {
                    continue;
                }
                double d = (x - bx) + (y - by);
                if (-14 <= d && d <= -11 && bayf(x, y) < 0.8) {
                    put(x, y, rgbf(240, 252, 255));
                }
                bool edge = !(inb(x - 2, y) && inb(x + 2, y) && inb(x, y - 2) && inb(x, y + 2));
                if (edge && bayf(x, y) < 0.5) {
                    put(x, y, rgbf(225, 245, 255));
                }
            }
        }
        int x = (int)rint(bx + 6), y = (int)rint(by - 6);
        for (int k = 0; k < 7; k++) {
            x += k % 2 ? 1 : 0;
            y += 1;
            if (inb(x, y)) {
                put(x, y, rgbf(250, 255, 255));
            }
        }
        break;
    }
    case FX_BODY_LAVA: {    /* cooled black plates, molten red seams between them, pulsing */
        double sx[7], sy[7];
        for (int i = 0; i < 7; i++) {
            sx[i] = bx + (h01(2, i, 30, 0) - 0.5) * 30;
            sy[i] = by + (h01(2, i, 31, 0) - 0.5) * 22;
        }
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (!inb(x, y) || (fabs(x - a->face_x) < 11 && fabs(y - a->face_y) < 4)) {
                    continue;   /* the rock stays dark round the eyes, so they glow */
                }
                double d0 = 1e9, d1 = 1e9;
                for (int i = 0; i < 7; i++) {
                    double d = hypot(x - sx[i], y - sy[i]);
                    if (d < d0) {
                        d1 = d0;
                        d0 = d;
                    } else if (d < d1) {
                        d1 = d;
                    }
                }
                double gap = d1 - d0, heat = 0.75 + 0.25 * sin(t * 2.5 + (x + y) * 0.3);
                if (gap < 0.7) {
                    put(x, y, mix(rgbf(210, 30, 10), rgbf(255, 210, 60), heat));
                } else if (gap < 1.6 && bayf(x, y) < 0.55) {
                    put(x, y, mix(rgbf(90, 10, 5), rgbf(190, 35, 12), heat));
                }
            }
        }
        break;
    }
    case FX_BODY_KOI:       /* red-orange patches */
        for (int i = 0; i < 4; i++) {
            double px = bx + (h01(2, i, 12, 0) - 0.5) * 24, py = by + (h01(2, i, 13, 0) - 0.7) * 16;
            double rx = 4 + h01(2, i, 14, 0) * 4, ry = 3 + h01(2, i, 15, 0) * 3;
            for (int y = 0; y < N; y++) {
                for (int x = 0; x < N; x++) {
                    double u = (x - px) / rx, v = (y - py) / ry;
                    if (inb(x, y) && u * u + v * v <= 1) {
                        put(x, y, bayf(x, y) > 0.2 ? rgbf(238, 92, 50) : rgbf(250, 140, 90));
                    }
                }
            }
        }
        break;
    case FX_BODY_GLOW_SPOTS: /* glowing spots, pulsing */
        for (int i = 0; i < 9; i++) {
            int x = (int)rint(bx + (h01(2, i, 16, 0) - 0.5) * 28), y = (int)rint(by + (h01(2, i, 17, 0) - 0.7) * 18);
            double g = 0.5 + 0.5 * sin(t * 2 + i);
            if (inb(x, y)) {
                put(x, y, mix(rgbf(40, 90, 140), rgbf(120, 255, 255), g));
            }
        }
        break;
    case FX_BODY_WINTER: {  /* a cable-knit jumper and a red scarf */
        double nx = a->neck_x, ny = a->neck_y;
        const rgb_t knit[6] = { s_rp3.out, s_rp3.dark, s_rp3.mid, s_rp3.light, s_rp3.high, s_rp3.glow };
        for (int y = (int)rint(ny) + 2; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (!inb(x, y) || !(x % 3 == 0 || (x % 3 == 1 && y % 2))) {
                    continue;
                }
                rgb_t c = s_img[y][x];
                for (int k = 0; k < 6; k++) {
                    if (c.r == knit[k].r && c.g == knit[k].g && c.b == knit[k].b) {
                        put(x, y, rgbf(200, 186, 158));
                        break;
                    }
                }
            }
        }
        const rgb_t red = { 230, 50, 60 }, dark = { 165, 25, 40 };
        for (int dy = -2; dy < 2; dy++) {      /* a thick red scarf */
            for (int dx = -10; dx <= 10; dx++) {
                putd(nx + dx, ny + dy, pymod(dx + dy, 4) == 0 ? dark : red);
            }
        }
        for (int dy = 2; dy < 9; dy++) {       /* its end, hanging */
            for (int dx = 4; dx <= 6; dx++) {
                putd(nx + dx + (dy > 6), ny + dy, dy == 8 || dx == 6 ? dark : red);
            }
        }
        break;
    }
    default:
        break;
    }
    if (sk->body_fx != FX_BODY_STARRY) {
        return;
    }
    /* A galaxy: deeper at the top, a milky band, stars of every size, a little
     * dipper, a crescent-moon bulb. */
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            if (!inb(x, y)) {
                continue;
            }
            double k = (y - by) / 10;
            if (k < -0.4 && bayf(x, y) < -k - 0.3) {
                put(x, y, rgbf(18, 14, 52));
            }
            double band = fabs((x - bx) * 0.55 + (y - by) * 0.9 - 2);
            if (band < 4.5) {
                double w = 1 - band / 4.5;
                if (bayf(x, y) < w * 0.9) {
                    put(x, y, h01(3, x, y, 40) > 0.5 ? rgbf(150, 90, 210) : rgbf(90, 120, 230));
                }
                if (w > 0.5 && h01(3, x, y, 41) > 0.9) {
                    put(x, y, rgbf(240, 230, 255));
                }
            }
        }
    }
    static const rgb_t TINT[3] = { { 255, 250, 230 }, { 200, 230, 255 }, { 255, 220, 240 } };
    for (int i = 0; i < 16; i++) {
        int x = (int)rint(bx + (h01(2, i, 7, 0) - 0.5) * 30), y = (int)rint(by + (h01(2, i, 8, 0) - 0.5) * 24);
        if (!inb(x, y)) {
            continue;
        }
        double tw = 0.5 + 0.5 * sin(t * (1 + h01(2, i, 9, 0) * 2) + i * 1.3);
        rgb_t col = TINT[i % 3];
        put(x, y, mix(rgbf(70, 60, 140), col, tw));
        if (i < 3 && tw > 0.4) {   /* three bright ones, four-pointed */
            static const int8_t D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (int k = 0; k < 4; k++) {
                if (inb(x + D[k][0], y + D[k][1])) {
                    put(x + D[k][0], y + D[k][1], mix(rgbf(90, 80, 170), col, tw * 0.8));
                }
            }
        }
    }
    const double st[4][2] = { { bx - 9, by - 3 }, { bx - 5, by - 6 }, { bx + 1, by - 5 }, { bx + 5, by - 8 } };
    for (int i = 0; i < 3; i++) {
        for (int k = 1; k < 8; k++) {
            int x = (int)rint(st[i][0] + (st[i + 1][0] - st[i][0]) * k / 8);
            int y = (int)rint(st[i][1] + (st[i + 1][1] - st[i][1]) * k / 8);
            if (inb(x, y) && k % 2) {
                put(x, y, rgbf(110, 100, 190));
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        if (inb((int)rint(st[i][0]), (int)rint(st[i][1]))) {
            putd(st[i][0], st[i][1], rgbf(255, 255, 255));
        }
    }
    for (int dy = -3; dy < 4; dy++) {
        for (int dx = -3; dx < 4; dx++) {
            if ((dx - 1.2) * (dx - 1.2) + (dy + 1.0) * (dy + 1.0) <= 4.2 && dx * dx + dy * dy <= 6.5) {
                putd(a->light_x + dx, a->light_y + dy, rgbf(12, 10, 30));
            }
        }
    }
}

/* Behind the character, over the background: embers rising off the lava. */
static void skin_back(const skin_t *sk, double t)
{
    if (sk->back_fx != FX_BACK_EMBERS) {
        return;
    }
    for (int i = 0; i < 14; i++) {
        double k = pymodd(t * (0.25 + h01(2, i, 50, 0) * 0.3) + h01(2, i, 51, 0), 1);
        double x = h01(2, i, 52, 0) * 64 + sin(t * 2 + i) * 2, y = 62 - k * 62;
        putd(x, y, mix(rgbf(255, 200, 60), rgbf(120, 20, 10), k));
        if (k < 0.3) {
            putd(x, y + 1, mix(rgbf(200, 60, 20), rgbf(80, 10, 5), k * 3));
        }
    }
}

/* An old CRT's scanlines over Codex's screen. */
static void skin_face(const skin_t *sk, const anchors_t *a)
{
    if (sk->face_fx != FX_FACE_SCANLINES || !a->has_screen) {
        return;
    }
    int x0 = (int)rint(a->screen_x0), y0 = (int)rint(a->screen_y0);
    int x1 = (int)rint(a->screen_x1), y1 = (int)rint(a->screen_y1);
    for (int y = y0 < 0 ? 0 : y0; y < (y1 > N ? N : y1); y++) {
        if (y % 2 == 0) {
            continue;
        }
        for (int x = x0 < 0 ? 0 : x0; x < (x1 > N ? N : x1); x++) {
            rgb_t *c = &s_img[y][x];
            *c = (rgb_t){ (uint8_t)(c->r * 0.62), (uint8_t)(c->g * 0.62), (uint8_t)(c->b * 0.62) };
        }
    }
}

static void apply_colours(boopie_char_t c, uint32_t colour)
{
    const skin_t *sk = skin_at(s_skin);
    if (sk && sk->character != c) {
        sk = NULL;
    }
    s_rp = ramp(sk ? sk->colour : colour == BOOPIE_COLOUR_DEFAULT ? RIGS[c].colour : colour & 0xffffff);
    s_eye_base = EYE;
    s_cheek = CHEEK;
    s_codex_screen = (rgb_t){ 30, 34, 84 };
    s_codex_glyph = (rgb_t){ 120, 236, 240 };
    s_glyph_follows_light = true;
    s_klaude_eye_set = false;
    s_whale_belly[0] = (rgb_t){ 214, 224, 255 };
    s_whale_belly[1] = (rgb_t){ 240, 244, 255 };
    if (sk) {
        s_klaude_eye_set = sk->eye != 0;
        if (sk->belly[0]) {
            s_whale_belly[0] = hex(sk->belly[0]);
            s_whale_belly[1] = hex(sk->belly[1]);
        }
        if (sk->glow) {
            s_rp.glow = hex(sk->glow);
        }
        if (sk->outline) {
            s_rp.out = hex(sk->outline);
        }
        if (sk->eye) {
            s_eye_base = hex(sk->eye);
        }
        if (sk->cheek) {
            s_cheek = hex(sk->cheek);
        }
        if (sk->screen) {
            s_codex_screen = hex(sk->screen);
        }
        if (sk->glyph) {
            s_codex_glyph = hex(sk->glyph);
        }
        s_glyph_follows_light = !sk->glyph_fixed;
    }
}

static uint32_t rgb_hex(rgb_t c)
{
    return (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b;
}

bool boopie_skin_muse_colours(int skin, boopie_muse_colours_t *out)
{
    const skin_t *sk = skin_at(skin);
    if (!sk || sk->character != BOOPIE_SKIN_MUSE) {
        return false;
    }
    ramp_t r = ramp(sk->colour);
    *out = (boopie_muse_colours_t){
        .fur = { rgb_hex(r.dark), rgb_hex(r.mid), rgb_hex(r.light), rgb_hex(r.high) },
        .out = rgb_hex(r.out),
        .out2 = rgb_hex(mix(r.out, r.dark, 0.5)),
        .face = sk->visor != 0,
    };
    if (sk->visor) {   /* a dark visor, the face lit on it */
        ramp_t v = ramp(sk->visor & 0xffffff);
        out->panel[0] = rgb_hex(v.dark);
        out->panel[1] = rgb_hex(v.mid);
        out->panel[2] = rgb_hex(v.light);
        out->features = 0xe6f5ff;
    }
    return true;
}

/* ---------------------------------------------------------------- accessories */

/* As the prototype's ACCESSORIES and HAT_SPRITES. */
typedef struct {
    const char *key, *name;
    const char *const *rows;   /* a hat's sprite, NULL for the scarf */
    int nrows;
    const char *keys;          /* its colour letters ... */
    const uint32_t *colours;   /* ... and their colours */
} acc_t;

static const char *const BOW_ROWS[] = { "##.....##", "#pp#.#pp#", "#ppp#ppp#", "#pp#.#pp#", "##.....##" };
static const uint32_t BOW_COLOURS[] = { 0x962850, 0xff78aa };
static const char *const CROWN_ROWS[] = { "g...g...g", "gg.ggg.gg", "ggggggggg", "grgggggbg", "ddddddddd" };
static const uint32_t CROWN_COLOURS[] = { 0xffd246, 0xbe821e, 0xf03c50, 0x50aaff };
static const char *const PARTY_ROWS[] = { "...w...", "..www..", "...#...", "..#p#..", "..#y#..", ".#ppp#.", ".#yyy#.",
                                          "#ppppp#", "#######" };
static const uint32_t PARTY_COLOURS[] = { 0xffffff, 0x3c286e, 0x78c8ff, 0xffdc5a };

static const acc_t ACCS[BOOPIE_ACC_COUNT] = {
    [BOOPIE_ACC_BOW] = { "bow", "蝴蝶结", BOW_ROWS, 5, "#p", BOW_COLOURS },
    [BOOPIE_ACC_CROWN] = { "crown", "小皇冠", CROWN_ROWS, 5, "gdrb", CROWN_COLOURS },
    [BOOPIE_ACC_SCARF] = { "scarf", "红围巾", NULL, 0, NULL, NULL },
    [BOOPIE_ACC_PARTY_HAT] = { "party_hat", "生日帽", PARTY_ROWS, 9, "w#py", PARTY_COLOURS },
};

static uint32_t s_wear;

const char *boopie_acc_key(boopie_acc_t a)
{
    return (int)a >= 0 && a < BOOPIE_ACC_COUNT ? ACCS[a].key : NULL;
}

const char *boopie_acc_name(boopie_acc_t a)
{
    return (int)a >= 0 && a < BOOPIE_ACC_COUNT ? ACCS[a].name : NULL;
}

bool boopie_acc_is_hat(boopie_acc_t a)
{
    return (int)a >= 0 && a < BOOPIE_ACC_COUNT && ACCS[a].rows != NULL;
}

bool boopie_acc_from_key(const char *key, boopie_acc_t *out)
{
    for (int i = 0; key && i < BOOPIE_ACC_COUNT; i++) {
        if (strcmp(ACCS[i].key, key) == 0) {
            *out = (boopie_acc_t)i;
            return true;
        }
    }
    return false;
}

void boopie_pixel_set_wear(uint32_t worn)
{
    s_wear = worn & (BOOPIE_ACC_BIT(BOOPIE_ACC_COUNT) - 1);
}

static void wear(boopie_acc_t which, const anchors_t *a)
{
    const acc_t *ac = &ACCS[which];
    if (ac->rows) {   /* a hat, its bottom row on the slot */
        int w = (int)strlen(ac->rows[0]);
        int x0 = (int)rint(a->hat_x - w / 2.0), y0 = (int)rint(a->hat_y) - ac->nrows + 1;
        for (int j = 0; j < ac->nrows; j++) {
            for (int i = 0; i < w; i++) {
                const char *k = strchr(ac->keys, ac->rows[j][i]);
                if (k && *k) {
                    put(x0 + i, y0 + j, hex(ac->colours[k - ac->keys]));
                }
            }
        }
        return;
    }
    const rgb_t red = { 230, 60, 70 }, dark = { 160, 30, 45 };   /* the scarf */
    double nx = a->neck_x, ny = a->neck_y, w = a->neck_w;
    int half = (int)rint(w / 2);
    for (int dx = -half; dx <= half; dx++) {
        for (int dy = 0; dy < 3; dy++) {
            putd(nx + dx, ny + dy, pymod(dx + dy, 4) == 0 ? dark : red);
        }
    }
    for (int dy = 3; dy < 8; dy++) {   /* the tail */
        for (int dx = 0; dx < 2; dx++) {
            putd(nx + w / 4 + dx + (dy > 5), ny + dy, dy == 7 ? dark : red);
        }
    }
}

/* All worn, in the prototype's order: hats, then the scarf. */
static void wear_all(const anchors_t *a)
{
    static const boopie_acc_t ORDER[] = { BOOPIE_ACC_BOW, BOOPIE_ACC_PARTY_HAT, BOOPIE_ACC_CROWN, BOOPIE_ACC_SCARF };
    for (int i = 0; i < BOOPIE_ACC_COUNT; i++) {
        if (s_wear & BOOPIE_ACC_BIT(ORDER[i])) {
            wear(ORDER[i], a);
        }
    }
}

void boopie_pixel_set_skin(int skin)
{
    s_skin = skin_at(skin) ? skin : -1;
}

void boopie_pixel_set_character(boopie_char_t c, uint32_t colour)
{
    if ((int)c < 0 || c >= BOOPIE_CHAR_COUNT) {
        c = BOOPIE_CHAR_BOOPIE;
    }
    s_char = c;
    s_char_set = true;
    apply_colours(c, colour);
    const skin_t *sk = skin_at(s_skin);
    if (sk && sk->character != c) {
        sk = NULL;
    }
    s_rp2 = s_rp3 = s_rp;
    if (c == BOOPIE_CHAR_GPT) {
        s_rp.out = GPT_INK;
        s_rp2 = ramp(sk && sk->bands ? sk->bands & 0xffffff : 0xf6f6f6);   /* the knot */
        s_rp2.out = GPT_INK;
    } else if (c == BOOPIE_CHAR_DOUBAO) {
        s_rp2 = ramp(sk && sk->hair ? sk->hair & 0xffffff : 0x6b4a3e);
        s_rp3 = ramp(sk && sk->top ? sk->top & 0xffffff : 0x3a3a44);
    }
}

/* ---------------------------------------------------------------- rendering */

/* Muse's glow: a dithered disc fading out from the centre, in two tints of the accent. */
static void aura(float cx, float cy, float radius, float strength, rgb_t acc)
{
    if (strength <= 0) {
        return;
    }
    rgb_t a1 = { u8r(acc.r * 0.16), u8r(acc.g * 0.16), u8r(acc.b * 0.16) };
    rgb_t a2 = { u8r(acc.r * 0.34), u8r(acc.g * 0.34), u8r(acc.b * 0.34) };
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float dx = x + 0.5f - cx, dy = (y + 0.5f - cy) * 1.1f;
            float d = sqrtf(dx * dx + dy * dy) / radius;
            if (d >= 1) {
                continue;
            }
            float i = (1 - d) * strength;
            float b = bay(x, y);
            if (i > 0.55f + b * 0.35f) {
                s_img[y][x] = a2;
            } else if (i > b * 0.9f) {
                s_img[y][x] = a1;
            }
        }
    }
}

static rgb_t dim34(rgb_t c)
{
    return (rgb_t){ u8r(c.r * 0.34), u8r(c.g * 0.34), u8r(c.b * 0.34) };
}

/* Expanding dotted rings, as Muse's while listening and speaking. */
static void rings(float cx, float cy, float t, float level, float speed, rgb_t acc)
{
    for (int k = 0; k < 2; k++) {
        float ph = (float)pymodd(t * speed + k * 0.5f, 1);
        float r = 20 + ph * 11;
        float fade = (1 - ph) * (0.35f + level);
        int n = (int)(r * 2.2f);
        for (int j = 0; j < n; j++) {
            float ang = j * TAU / n;
            int x = (int)rintf(cx + cosf(ang) * r), y = (int)rintf(cy + sinf(ang) * r * 0.92f);
            if (x >= 0 && x < N && y >= 0 && y < N && bay(x, y) < fade && !m_get(s_body, x, y)) {
                put(x, y, fade > 0.6f ? acc : dim34(acc));
            }
        }
    }
}

static void sparkles(float cx, float cy, float t, float speed, bool front_, rgb_t acc)
{
    const int count = 6;
    rgb_t spk = mix(acc, WHITE, 0.45);
    for (int i = 0; i < count; i++) {
        float ang = t * speed + i * TAU / count;
        float s = sinf(ang);
        if ((s > 0) != front_) {
            continue;
        }
        float rr = 25 + 2 * sinf(i * 1.9f + t * 0.7f);
        float x = rintf(cx + cosf(ang) * rr), y = rintf(cy - 3 + s * rr * 0.42f);
        float tw = 0.5f + 0.5f * sinf(t * 5 + i * 1.7f);
        rgb_t arm = front_ ? acc : dim34(acc);
        if (tw > 0.8f) {
            spark(x, y, front_ ? WHITE : spk, 0);
            for (int k = 1; k <= 2; k++) {
                rgb_t c = k == 1 && front_ ? spk : arm;
                put(x + k, y, c);
                put(x - k, y, c);
                put(x, y + k, c);
                put(x, y - k, c);
            }
        } else if (tw > 0.45f) {
            spark(x, y, arm, 1);
        } else if (tw > 0.15f) {
            put(x, y, arm);
        }
    }
}

static float s_acc[3];
static bool s_acc_init;

static void render(const rig_t *rig, const pose_t *pose)
{
    memset(s_img, 0, sizeof(s_img));
    m_clear(s_body);
    s_eye = s_eye_base;
    s_has_shine = true;
    s_shine = WHITE;
    s_has_rim = false;
    rgb_t acc = pose->accent;
    float t = pose->t;
    /* background layers, as Muse's: the glow, the ground shadow, sparkles behind */
    aura(32, 37, 29 + pose->level * 4 + sinf(t * 1.5f), pose->aura + pose->level * 0.4f, acc);
    for (int dx = -12; dx <= 12; dx++) {
        if (abs(dx) < 12 - pymod(dx, 2)) {
            put(32 + dx, 59, (rgb_t){ 22, 18, 34 });
        }
    }
    scene_back(pose->scene, pose->scene_t);
    const skin_t *sk = skin_at(s_skin);
    if (sk && sk->character != s_char) {
        sk = NULL;
    }
    if (sk) {
        skin_back(sk, pose->scene_t);
    }
    sparkles(32, 40, t, pose->sparkle_speed, false, acc);
    s_has_rim = true;
    s_rim = mix(acc, WHITE, 0.2);
    anchors_t an = { 0 };
    rig->draw(rig, pose, &an);
    if (sk) {
        skin_body(sk, pose, &an);
    }
    if (pose->rings > 0) {
        rings(32, an.face_y + 2, t, pose->level, pose->rings, acc);
    }
    if (an.show_face) {
        rig->face(rig, (int)rintf(an.face_x), (int)rintf(an.face_y), pose);
        if (sk) {
            skin_face(sk, &an);
        }
    }
    wear_all(&an);
    if (pose->laptop) {
        icon(I_LAPTOP, rintf(an.body_x) - 7, rintf(an.body_y) + 8);
    }
    mask_t fr;
    m_clear(fr);
    for (int i = 0; i < pose->nhands; i++) {
        const hand_t *h = &pose->hands[i];
        if (!h->front) {
            continue;
        }
        float ax = h->anchor == A_FACE ? an.face_x : an.body_x;
        float ay = h->anchor == A_FACE ? an.face_y : an.body_y;
        for (int y = 0; y < N; y++) {
            float v = (y + 0.5f - ay - h->dy) / 2.2f;
            for (int x = 0; x < N; x++) {
                float u = (x + 0.5f - ax - h->dx) / 2.7f;
                if (u * u + v * v <= 1) {
                    fr[y] |= 1ull << x;
                }
            }
        }
    }
    if (m_any(fr)) {
        flat(fr, s_rp.light);
        outline(fr, s_rp.out);
    }
    sparkles(32, 40, t, pose->sparkle_speed, true, acc);
    scene_front(pose->scene, pose->scene_t);
    for (int i = 0; i < pose->nfx; i++) {
        const fx_t *e = &pose->fx[i];
        switch (e->kind) {
        case FX_SPARK: spark(e->x, e->y, e->c, e->r); break;
        case FX_PX: put(e->x, e->y, e->c); break;
        case FX_LPX: put(an.light_x + e->x, an.light_y + e->y, e->c); break;
        case FX_FPX: put(an.face_x + e->x, an.face_y + e->y, e->c); break;
        case FX_ICON: icon(e->icon, e->x, e->y); break;
        case FX_FICON: icon(e->icon, rintf(an.face_x + e->x), rintf(an.face_y + e->y)); break;
        case FX_BICON: icon(e->icon, rintf(an.body_x + e->x), rintf(an.body_y + e->y)); break;
        }
    }
    scene_post(pose->scene, pose->scene_t);
    if (pose->dim < 1) {
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                rgb_t *c = &s_img[y][x];
                *c = (rgb_t){ (uint8_t)(c->r * pose->dim), (uint8_t)(c->g * pose->dim), (uint8_t)(c->b * pose->dim) };
            }
        }
    }
}

/* ---------------------------------------------------------------- public */

uint32_t boopie_pixel_accent(boopie_expr_t e)
{
    return ACCENT[boopie_expr_valid(e) ? e : BOOPIE_EXPR_IDLE];
}

float boopie_pixel_loop(boopie_expr_t e)
{
    return (float)LOOP[boopie_expr_valid(e) ? e : BOOPIE_EXPR_IDLE];
}

float boopie_overlay_loop(boopie_overlay_t o)
{
    return (int)o >= 0 && o < BOOPIE_OVERLAY_COUNT ? (float)OVERLAY_LOOP[o] : 1.0f;
}

static uint16_t s_565[N * N];
static uint16_t s_565_dim[N * N];   /* the block edge shade gives the enlarged pixels a faint grid texture */
static void to_565_all(void);

static inline uint16_t to565(float r, float g, float b)
{
    int ri = (int)(r + 0.5f), gi = (int)(g + 0.5f), bi = (int)(b + 0.5f);
    ri = ri > 255 ? 255 : ri;
    gi = gi > 255 ? 255 : gi;
    bi = bi > 255 ? 255 : bi;
    return (uint16_t)(((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3));
}

void boopie_pixel_render(const boopie_pixel_pose_t *in)
{
    if (!s_char_set) {
        boopie_pixel_set_character(BOOPIE_CHAR_BOOPIE, BOOPIE_COLOUR_DEFAULT);
    }
    boopie_expr_t e = boopie_expr_valid(in->expr) ? in->expr : BOOPIE_EXPR_IDLE;
    s_food_icon = FOOD_ICONS[(int)in->food >= 0 && in->food < BOOPIE_FOOD_COUNT ? in->food : BOOPIE_FOOD_RICE];
    double length = LOOP[e];
    double t = in->t < 0 ? 0 : in->t;
    /* BOOT and OFF play once and hold; THINKING runs on into working; the rest loop. */
    if (e != BOOPIE_EXPR_BOOT && e != BOOPIE_EXPR_OFF && e != BOOPIE_EXPR_THINKING) {
        t = pymodd(t, length);
    }
    static pose_t pose;
    pose_for(&pose, e, t, length, in->level);
    for (int o = 0; o < BOOPIE_OVERLAY_COUNT; o++) {
        if (in->overlays & BOOPIE_OVERLAY_BIT(o)) {
            double ot = in->overlay_t[o] < 0 ? 0 : in->overlay_t[o];
            overlay(&pose, (boopie_overlay_t)o, pymodd(ot, OVERLAY_LOOP[o]), OVERLAY_LOOP[o]);
        }
    }
    /* Ease the accent between states, as Muse's palette does. */
    rgb_t tgt = pose.accent;
    float k = s_acc_init && in->dt > 0 ? 1.0f - expf(-in->dt * 7.0f) : 1.0f;
    s_acc[0] += (tgt.r - s_acc[0]) * k;
    s_acc[1] += (tgt.g - s_acc[1]) * k;
    s_acc[2] += (tgt.b - s_acc[2]) * k;
    s_acc_init = true;
    pose.accent = (rgb_t){ u8r(s_acc[0]), u8r(s_acc[1]), u8r(s_acc[2]) };
    pose.scene = (int)in->scene >= 0 && in->scene < BOOPIE_SCENE_COUNT ? in->scene : BOOPIE_SCENE_DEFAULT;
    pose.scene_t = in->scene_t;
    render(&RIGS[s_char], &pose);
    to_565_all();
}

const uint8_t *boopie_pixel_rgb(void)
{
    return &s_img[0][0].r;
}

/*
 * Screen pixel -> grid cell, with 0x80 set on a cell's last screen pixel. When
 * cells are 3+ pixels, that edge is drawn dimmer so the pixel grid shows.
 */
#define MAP_MAX 512
static uint8_t s_map[MAP_MAX];
static int s_size;

void boopie_pixel_set_size(int px)
{
    s_size = px < MAP_MAX ? px : MAP_MAX;
    bool grid = s_size >= 3 * N;
    for (int i = 0; i < s_size; i++) {
        int cell = i * N / s_size;
        bool edge = grid && (i + 1) * N / s_size != cell;
        s_map[i] = (uint8_t)(cell | (edge ? 0x80 : 0));
    }
}

void boopie_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    int n = x1 - x0 + 1;
    const uint8_t *xmap = &s_map[x0];
    const uint16_t *prev = NULL;
    uint8_t prev_m = 0;
    for (int y = y0; y <= y1; y++, dst += stride_px) {
        uint8_t m = s_map[y];
        if (prev && m == prev_m) {
            memcpy(dst, prev, n * sizeof(uint16_t));
            continue;
        }
        const uint16_t *row = &s_565[(m & 0x7f) * N];
        const uint16_t *dim = &s_565_dim[(m & 0x7f) * N];
        if (m & 0x80) {
            for (int i = 0; i < n; i++) {
                dst[i] = dim[xmap[i] & 0x7f];
            }
        } else {
            for (int i = 0; i < n; i++) {
                uint8_t xm = xmap[i];
                dst[i] = xm & 0x80 ? dim[xm & 0x7f] : row[xm & 0x7f];
            }
        }
        prev = dst;
        prev_m = m;
    }
}

/* ---------------------------------------------------------------- compose */

static rgb_t s_layer[N][N];
static mask_t s_layer_mask;

static void to_565_all(void)
{
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            rgb_t c = s_img[y][x];
            s_565[y * N + x] = to565(c.r, c.g, c.b);
            s_565_dim[y * N + x] = to565(c.r * 0.72f, c.g * 0.72f, c.b * 0.72f);
        }
    }
}

static anchors_t s_slots;
static bool s_slots_set;

void boopie_pixel_set_slots(const float slots[5])
{
    s_slots_set = slots != NULL;
    if (slots) {
        s_slots.hat_x = slots[0];
        s_slots.hat_y = slots[1];
        s_slots.neck_x = slots[2];
        s_slots.neck_y = slots[3];
        s_slots.neck_w = slots[4];
    }
}

/*
 * A Muse skin's extras, over Muse's own frame (whose shape its renderer
 * keeps): green shorts on 派大星, armour, a bow and a sword on the knight.
 * Fur is palette entries 3 to 6 there (dark to highlight, avatar/muse_pixel.c).
 */
#define MUSE_FUR0 3
#define MUSE_FUR1 6

static void muse_skin_over(const uint8_t *fb)
{
    const skin_t *sk = skin_at(s_skin);
    if (!sk || sk->character != BOOPIE_SKIN_MUSE || !sk->muse_fx || !s_slots_set) {
        return;
    }
    int bottom = -1;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            uint8_t i = fb[y * N + x];
            if (i >= MUSE_FUR0 && i <= MUSE_FUR1) {
                bottom = y;
            }
        }
    }
    if (bottom < 0) {
        return;
    }
    int neck = (int)lroundf(s_slots.neck_y);
    if (sk->muse_fx == MUSE_FX_SHORTS) {
        static const rgb_t GREEN[4] = { { 70, 140, 50 }, { 110, 185, 70 }, { 140, 210, 90 }, { 175, 230, 120 } };
        int top = bottom - 9 > neck + 3 ? bottom - 9 : neck + 3;
        for (int y = top; y <= bottom; y++) {
            for (int x = 0; x < N; x++) {
                uint8_t i = fb[y * N + x];
                if (i >= MUSE_FUR0 && i <= MUSE_FUR1) {
                    bool flower = (x * 7 + y * 3) % 11 == 0 && y > top;
                    s_img[y][x] = flower ? (rgb_t){ 170, 90, 210 } : GREEN[i - MUSE_FUR0];
                }
            }
        }
        return;
    }
    /* The knight: plates from the neck down, the arms in gauntlets. */
    static const rgb_t STEEL[4] = { { 110, 116, 130 }, { 160, 166, 180 }, { 200, 205, 216 }, { 236, 240, 248 } };
    for (int y = neck + 1; y <= bottom; y++) {
        for (int x = 0; x < N; x++) {
            uint8_t i = fb[y * N + x];
            if (i >= MUSE_FUR0 && i <= MUSE_FUR1) {
                bool seam = y == neck + 5 || y == neck + 10;
                s_img[y][x] = seam ? (rgb_t){ 90, 96, 110 } : STEEL[i - MUSE_FUR0];
            }
        }
    }
    float hx = s_slots.hat_x, hy = s_slots.hat_y, w = s_slots.neck_w;
    static const char *const BOW[] = { "##.##", "#p#p#", "##.##" };
    for (int r = 0; r < 3; r++) {      /* a little pink bow on the hood */
        for (int k = 0; k < 5; k++) {
            if (BOW[r][k] != '.') {
                put(hx + w * 0.3f + k - 2, hy + 6 + r, BOW[r][k] == 'p' ? (rgb_t){ 255, 200, 215 } : (rgb_t){ 240, 150, 175 });
            }
        }
    }
    float sx = s_slots.neck_x - w / 2 - 2;    /* the sword, held up on its left */
    for (int y = (int)hy - 6; y <= neck + 4; y++) {
        put(sx, y, (rgb_t){ 225, 230, 240 });
        put(sx + 1, y, (rgb_t){ 150, 156, 170 });
    }
    put(sx, hy - 7, (rgb_t){ 225, 230, 240 });
    for (int k = -2; k <= 3; k++) {
        put(sx + k, neck + 5, (rgb_t){ 220, 180, 80 });
    }
    for (int y = neck + 6; y <= neck + 8; y++) {
        put(sx, y, (rgb_t){ 110, 70, 40 });
        put(sx + 1, y, (rgb_t){ 90, 56, 30 });
    }
}

void boopie_pixel_compose(const uint8_t *fb, const uint16_t *palette, uint32_t bg_mask,
                          const boopie_pixel_pose_t *in)
{
    mask_t bg;
    m_clear(bg);
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            uint8_t i = fb[y * N + x];
            uint16_t c = palette[i];
            s_img[y][x] = (rgb_t){ (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
            if (i < 32 && (bg_mask >> i) & 1u) {
                bg[y] |= 1ull << x;
            }
        }
    }
    boopie_scene_t scene = (int)in->scene >= 0 && in->scene < BOOPIE_SCENE_COUNT ? in->scene : BOOPIE_SCENE_DEFAULT;
    /* The background scene shows only where the frame is background. */
    m_clear(s_layer_mask);
    s_dst = s_layer;
    s_dst_mask = s_layer_mask;
    scene_back(scene, in->scene_t);
    s_dst = s_img;
    s_dst_mask = NULL;
    for (int y = 0; y < N; y++) {
        for (uint64_t row = s_layer_mask[y] & bg[y]; row; row &= row - 1) {
            int x = __builtin_ctzll(row);
            s_img[y][x] = s_layer[y][x];
        }
    }
    scene_front(scene, in->scene_t);
    muse_skin_over(fb);
    if (s_slots_set) {
        wear_all(&s_slots);
    }
    s_food_icon = FOOD_ICONS[(int)in->food >= 0 && in->food < BOOPIE_FOOD_COUNT ? in->food : BOOPIE_FOOD_RICE];
    static pose_t pose;   /* the overlays draw at fixed places */
    memset(&pose, 0, sizeof(pose));
    pose.light_level = 1;
    for (int o = 0; o < BOOPIE_OVERLAY_COUNT; o++) {
        if (in->overlays & BOOPIE_OVERLAY_BIT(o)) {
            double ot = in->overlay_t[o] < 0 ? 0 : in->overlay_t[o];
            overlay(&pose, (boopie_overlay_t)o, pymodd(ot, OVERLAY_LOOP[o]), OVERLAY_LOOP[o]);
        }
    }
    for (int i = 0; i < pose.nfx; i++) {
        const fx_t *e = &pose.fx[i];
        if (e->kind == FX_ICON) {
            icon(e->icon, e->x, e->y);
        } else if (e->kind == FX_PX) {
            put(e->x, e->y, e->c);
        }
    }
    scene_post(scene, in->scene_t);
    to_565_all();
}

/* ---------------------------------------------------------------- games */

/*
 * The pet's head, 13 x 10, as it pops out of a hole: o outline, d dark, b body,
 * l light, e eye, c cheek, a its light (Boopie's bulb, the whale's spout), w
 * white, k ink, s screen or face, g Codex's glyph, h Doubao's hair. Coloured
 * from the character as it's dressed now (its colour, its skin).
 */
#define HEAD_W 13
#define HEAD_H 10
static const char *const HEADS[BOOPIE_CHAR_COUNT + 1][HEAD_H] = {
    [BOOPIE_CHAR_BOOPIE] = { "........aa...", "........aa...", ".......o.....", "...ooooooo...", "..ollbbbbbo..",
                             ".olbbbbbbbbo.", ".obbebbbebbo.", ".obbebbbebbo.", ".ocbbbbbbbco.", ".obbbbbbbbbo." },
    [BOOPIE_CHAR_GPT] = { "........kkk..", ".......kwkwk.", "...ooookkkk..", "..ollbbbbbo..", ".olbbbbbbbbo.",
                          ".obbebbbebbo.", ".obbebbbebbo.", ".ocbbbbbbbco.", ".obbbbbbbbbo.", ".obbbbbbbbbo." },
    [BOOPIE_CHAR_CODEX] = { "...oo..oo....", "..ollooblo...", ".olbbbbbbboo.", ".obsssssssbo.", "obsssssssssbo",
                            "obsgsssggssbo", "obssgssssssbo", "obsgsssssssbo", ".obsssssssbo.", ".obbbbbbbbbo." },
    [BOOPIE_CHAR_KLAUDE] = { ".............", ".ooooooooooo.", ".ollllbbbbbo.", ".olbbbbbbbbo.", ".obbkbbbkbbo.",
                             ".obbkbbbkbbo.", ".obbbbbbbbbo.", ".obbbbbbbbbo.", ".odddddddddo.", ".obbbbbbbbbo." },
    [BOOPIE_CHAR_WHALE] = { "..a.a........", "...a.........", "...a.........", "..ooooooo....", ".ollbbbbboo..",
                            "obbbbbbbbbbo.", "obbebbbebbbo.", "obbebbbebbbo.", "ocwwwwwwwwco.", "owwwwwwwwwwo." },
    [BOOPIE_CHAR_DOUBAO] = { "...hhhhhhh...", "..hhhhhhhhh..", ".hhhhhhhhhhh.", ".hhbbbbbbbhh.", ".hbbbbbbbbbh.",
                             ".hbbebbbebbh.", ".hbbebbbebbh.", ".hcbbbbbbbch.", ".hbbbbbbbbbh.", ".hbbbbbbbbbh." },
    [BOOPIE_CHAR_COUNT] = { "....ooooo....", "..oollllloo..", ".olllbbbbbbo.", ".obsssssssbo.", "obsssssssssbo",
                            "obskssssksbbo", "obskssssksbbo", "obscssssscbbo", ".obsssssssbo.", ".obbbbbbbbbo." },   /* Muse */
};
/* The rain cloud that mustn't be poked. */
static const char *const CLOUD[HEAD_H] = { ".............", "....ooooo....", "..oowwwwwoo..", ".owwwwwwwwwo.",
                                          "owwkwwwwwkwwo", "owwwkwwwkwwwo", "owwwwwwwwwwwo", ".owwwwwwwwwo.",
                                          "..ooooooooo..", "...d..d..d..." };
/* Score digits, 3 x 5, and + - x. */
static const char *const DIGITS[13][5] = {
    { "###", "#.#", "#.#", "#.#", "###" }, { ".#.", "##.", ".#.", ".#.", "###" }, { "###", "..#", "###", "#..", "###" },
    { "###", "..#", ".##", "..#", "###" }, { "#.#", "#.#", "###", "..#", "..#" }, { "###", "#..", "###", "..#", "###" },
    { "###", "#..", "###", "#.#", "###" }, { "###", "..#", ".#.", ".#.", ".#." }, { "###", "#.#", "###", "#.#", "###" },
    { "###", "#.#", "###", "..#", "###" }, { "...", ".#.", "###", ".#.", "..." }, { "...", "...", "###", "...", "..." },
    { "...", "#.#", ".#.", "#.#", "..." },
};

static bool s_head_defaults;   /* heads in each character's own colours, not as dressed now */

static rgb_t head_colour(int head, char ch, bool gold)
{
    if (s_head_defaults && head < BOOPIE_CHAR_COUNT) {
        ramp_t own = ramp(RIGS[head].colour);
        switch (ch) {
        case 'o': return head == BOOPIE_CHAR_GPT ? GPT_INK : own.out;
        case 'd': return own.dark;
        case 'b': return own.mid;
        case 'l': return own.light;
        case 'e': return EYE;
        case 'c': return CHEEK;
        case 'w': return head == BOOPIE_CHAR_WHALE ? (rgb_t){ 240, 244, 255 } : head == BOOPIE_CHAR_GPT
                                                                                   ? ramp(0xf6f6f6).light
                                                                                   : (rgb_t){ 245, 245, 250 };
        case 's': return (rgb_t){ 30, 34, 84 };
        case 'g': return (rgb_t){ 120, 236, 240 };
        case 'h': return ramp(0x6b4a3e).mid;
        default: break;
        }
    }
    ramp_t r = gold ? ramp(0xffd246) : head == BOOPIE_CHAR_COUNT ? ramp(0xe6d7bd) : s_rp;
    switch (ch) {
    case 'o': return r.out;
    case 'd': return r.dark;
    case 'b': return r.mid;
    case 'l': return r.light;
    case 'e': return s_eye_base;
    case 'c': return head == BOOPIE_CHAR_COUNT ? (rgb_t){ 244, 170, 160 } : s_cheek;
    case 'a': return head == BOOPIE_CHAR_WHALE ? (rgb_t){ 150, 210, 255 } : (rgb_t){ 255, 236, 160 };
    case 'w': return head == BOOPIE_CHAR_WHALE ? s_whale_belly[1] : head == BOOPIE_CHAR_GPT ? s_rp2.light
                                                                                              : (rgb_t){ 245, 245, 250 };
    case 'k': return head == BOOPIE_CHAR_COUNT ? (rgb_t){ 18, 13, 11 } : (rgb_t){ 18, 18, 24 };
    case 's': return head == BOOPIE_CHAR_COUNT ? (rgb_t){ 246, 223, 189 } : s_codex_screen;
    case 'g': return s_codex_glyph;
    case 'h': return s_rp2.mid;
    default: return (rgb_t){ 0, 0, 0 };
    }
}

static void draw_glyph(int which, int x, int y, int scale, rgb_t c)
{
    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 3; i++) {
            if (DIGITS[which][j][i] == '#') {
                for (int v = 0; v < scale; v++) {
                    for (int u = 0; u < scale; u++) {
                        put(x + i * scale + u, y + j * scale + v, c);
                    }
                }
            }
        }
    }
}

/* A number centred on cx, its top at y; sign: a leading + or -. */
static void draw_number(int value, int sign, int cx, int y, int scale, rgb_t c)
{
    int digits[6], n = 0;
    int v = value < 0 ? -value : value;
    do {
        digits[n++] = v % 10;
        v /= 10;
    } while (v && n < 6);
    int glyphs = n + (sign != 0);
    int w = glyphs * 4 * scale - scale;
    int x = cx - w / 2;
    if (sign) {
        draw_glyph(sign > 0 ? 10 : 11, x, y, scale, c);
        x += 4 * scale;
    }
    for (int i = n - 1; i >= 0; i--) {
        draw_glyph(digits[i], x, y, scale, c);
        x += 4 * scale;
    }
}

static void time_ring(float left);

void boopie_pixel_render_whack(const boopie_whack_t *g, int head)
{
    if (head < 0 || head > BOOPIE_CHAR_COUNT) {
        head = BOOPIE_CHAR_BOOPIE;
    }
    memset(s_img, 0, sizeof(s_img));
    s_dst = s_img;
    s_dst_mask = NULL;
    time_ring(1 - g->t / BOOPIE_WHACK_SECONDS);
    for (int i = 0; i < BOOPIE_WHACK_HOLES; i++) {
        float hx, hy;
        boopie_whack_hole_pos(i, &hx, &hy);
        int cx = (int)rintf(hx), base = (int)rintf(hy) + 3;
        /* The hole, behind. */
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -6; dx <= 6; dx++) {
                if (dx * dx / 36.0f + dy * dy / 6.25f <= 1) {
                    put(cx + dx, base + dy, (rgb_t){ 14, 10, 26 });
                }
            }
        }
        const boopie_whack_hole_t *h = &g->holes[i];
        int shown = (int)rintf(boopie_whack_rise(h) * HEAD_H);
        if (shown > 0) {
            bool cloud = h->kind == BOOPIE_WHACK_CLOUD;
            const char *const *rows = cloud ? CLOUD : HEADS[head];
            for (int j = 0; j < shown; j++) {   /* the top of the head first, out of the hole */
                for (int k = 0; k < HEAD_W; k++) {
                    char ch = rows[j][k];
                    if (ch == '.') {
                        continue;
                    }
                    rgb_t c = cloud ? (ch == 'o' ? (rgb_t){ 70, 70, 96 } : ch == 'w' ? (rgb_t){ 156, 156, 180 }
                                       : ch == 'k' ? (rgb_t){ 40, 40, 60 } : (rgb_t){ 110, 170, 255 })
                                    : head_colour(head, ch, h->kind == BOOPIE_WHACK_GOLD);
                    put(cx - HEAD_W / 2 + k, base - shown + j, c);
                }
            }
            if (h->kind == BOOPIE_WHACK_GOLD && !h->hit && ((int)(h->t * 8) & 1)) {
                spark(cx + 6, base - shown - 1, (rgb_t){ 255, 246, 200 }, 1);
            }
        }
        /* Its near lip, over whatever's in it. */
        for (int dx = -6; dx <= 6; dx++) {
            int dy = (int)rintf(2.5f * sqrtf(1 - dx * dx / 36.0f));
            put(cx + dx, base + dy, (rgb_t){ 70, 58, 110 });
        }
        if (h->hit) {   /* what the tap scored, floating up */
            float k = h->t - h->hit_t;
            rgb_t pc = h->points < 0 ? (rgb_t){ 255, 90, 100 } : h->points >= 3 ? (rgb_t){ 255, 210, 70 }
                                                                                 : (rgb_t){ 255, 255, 255 };
            draw_number(h->points, h->points < 0 ? -1 : 1, cx, base - 12 - (int)(k * 12), 1, pc);
        }
    }
    /* The score in the middle, and the combo under it. */
    draw_number(g->score, 0, 32, 29, 2, (rgb_t){ 242, 239, 255 });
    if (g->combo >= BOOPIE_WHACK_COMBO) {
        draw_glyph(12, 28, 41, 1, (rgb_t){ 255, 210, 70 });
        draw_glyph(2, 32, 41, 1, (rgb_t){ 255, 210, 70 });
    }
    /* Read back with boopie_pixel_rgb(): the avatar's scaled buffers are left alone. */
}

/* The time left, round the edge: green, then yellow, then red. */
static void time_ring(float left)
{
    rgb_t tc = left > 0.5f ? (rgb_t){ 110, 240, 160 } : left > 0.2f ? (rgb_t){ 255, 210, 80 } : (rgb_t){ 255, 90, 100 };
    for (int i = 0; i < 60; i++) {
        float a = i / 60.0f * 6.2831853f - 1.5707963f;
        bool lit = i < (int)ceilf(left * 60);
        put(32 + 30 * cosf(a), 32 + 30 * sinf(a), lit ? tc : (rgb_t){ 34, 28, 52 });
    }
}

/* An icon centred on (x, y). */
static void icon_at(int which, float x, float y)
{
    const icon_t *ic = &ICONS[which];
    icon(which, rintf(x - (float)strlen(ic->rows[0]) / 2), rintf(y - ic->nrows / 2.0f));
}

/* What a catch or a find scored, floating up from (x, y) for `k` seconds. */
static void points_up(int points, float x, float y, float k)
{
    rgb_t pc = points < 0 ? (rgb_t){ 255, 90, 100 } : points >= 3 ? (rgb_t){ 255, 210, 70 } : (rgb_t){ 255, 255, 255 };
    draw_number(points, points < 0 ? -1 : 1, (int)rintf(x), (int)rintf(y - k * 12), 1, pc);
}

void boopie_pixel_render_catch(const boopie_catch_t *g, int head)
{
    if (head < 0 || head > BOOPIE_CHAR_COUNT) {
        head = BOOPIE_CHAR_BOOPIE;
    }
    memset(s_img, 0, sizeof(s_img));
    s_dst = s_img;
    s_dst_mask = NULL;
    time_ring(1 - g->t / BOOPIE_CATCH_SECONDS);
    /* The score at the top, behind what falls. */
    draw_number(g->score, 0, 32, 9, 2, (rgb_t){ 90, 84, 130 });
    /* The ground the pet stands on. */
    for (int x = 8; x <= 56; x++) {
        put(x, BOOPIE_CATCH_PET_Y + HEAD_H, (rgb_t){ 52, 44, 84 });
    }
    for (int i = 0; i < BOOPIE_CATCH_ITEMS; i++) {
        const boopie_catch_item_t *it = &g->items[i];
        if (it->kind == BOOPIE_CATCH_NONE) {
            continue;
        }
        if (it->caught) {
            points_up(it->points, it->x, BOOPIE_CATCH_PET_Y - 8, g->t - it->caught_t);
            continue;
        }
        int which = it->kind == BOOPIE_CATCH_CLOUD ? I_STORM : it->kind == BOOPIE_CATCH_GOLD ? I_CANDY
                                                                                              : FOOD_ICONS[it->food % BOOPIE_FOOD_COUNT];
        icon_at(which, it->x, it->y);
        if (it->kind == BOOPIE_CATCH_GOLD && ((int)(g->t * 8) & 1)) {
            spark(it->x + 6, it->y - 3, (rgb_t){ 255, 246, 200 }, 1);
        }
    }
    /* The pet, dizzy a moment after a cloud. */
    int px = (int)rintf(g->x) - HEAD_W / 2;
    bool dizzy = g->dizzy > 0;
    for (int j = 0; j < HEAD_H; j++) {
        for (int k = 0; k < HEAD_W; k++) {
            char ch = HEADS[head][j][k];
            if (ch == '.') {
                continue;
            }
            rgb_t c = head_colour(head, ch, false);
            if (dizzy && ch == 'e') {
                c = (rgb_t){ 255, 255, 255 };
            }
            put(px + k, BOOPIE_CATCH_PET_Y + j, c);
        }
    }
    if (dizzy) {
        for (int i = 0; i < 3; i++) {
            float a = g->t * 9 + i * 2.094f;
            put(g->x + 6 * cosf(a), BOOPIE_CATCH_PET_Y - 2 + 1.5f * sinf(a), (rgb_t){ 255, 220, 80 });
        }
    }
    if (g->combo >= BOOPIE_CATCH_COMBO) {
        draw_glyph(12, 26, 21, 1, (rgb_t){ 255, 210, 70 });
        draw_glyph(2, 30, 21, 1, (rgb_t){ 255, 210, 70 });
    }
}

void boopie_pixel_render_maze(const boopie_maze_t *g)
{
    memset(s_img, 0, sizeof(s_img));
    s_dst = s_img;
    s_dst_mask = NULL;
    time_ring(1 - g->t / BOOPIE_MAZE_SECONDS);
    int ox, oy, size = boopie_maze_size(g);
    boopie_maze_origin(g, &ox, &oy);
    bool flash = g->cleared > 0 && ((int)(g->cleared * 10) & 1);
    rgb_t wall = flash ? (rgb_t){ 255, 210, 70 } : (rgb_t){ 120, 90, 210 };
    rgb_t floor_ = { 22, 18, 40 };
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            put(ox + x, oy + y, boopie_maze_wall(g, x, y) ? wall : floor_);
        }
    }
    /* The way out: a door in the last cell, glowing. */
    int gx = ox + (g->n - 1) * BOOPIE_MAZE_CELL + 1, gy = oy + (g->n - 1) * BOOPIE_MAZE_CELL + 1;
    float glow = 0.6f + 0.4f * sinf(g->t * 5);
    for (int y = 0; y < BOOPIE_MAZE_CELL - 1; y++) {
        for (int x = 0; x < BOOPIE_MAZE_CELL - 1; x++) {
            bool edge = x == 0 || y == 0 || x == BOOPIE_MAZE_CELL - 2 || y == BOOPIE_MAZE_CELL - 2;
            rgb_t c = edge ? (rgb_t){ 110, 240, 160 } : (rgb_t){ (uint8_t)(40 * glow), (uint8_t)(200 * glow), (uint8_t)(120 * glow) };
            put(gx + x, gy + y, c);
        }
    }
    /* The star, twinkling. */
    if (g->star >= 0) {
        float sx = ox + (g->star % g->n) * BOOPIE_MAZE_CELL + 3, sy = oy + (g->star / g->n) * BOOPIE_MAZE_CELL + 3;
        spark(sx, sy, (rgb_t){ 255, 210, 70 }, ((int)(g->t * 4) & 1) ? 1 : 0);
        if (!((int)(g->t * 4) & 1)) {
            put(sx + 1, sy + 1, (rgb_t){ 255, 246, 200 });
            put(sx - 1, sy - 1, (rgb_t){ 255, 246, 200 });
        }
    }
    /* The ball: 3 x 3, rounded, with a shine. */
    int bx = ox + (int)floorf(g->x) - 1, by = oy + (int)floorf(g->y) - 1;
    static const char *const BALL[3] = { "lbl", "bbb", "dbd" };
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            char ch = BALL[j][i];
            rgb_t c = ch == 'l' ? (rgb_t){ 255, 230, 240 } : ch == 'b' ? (rgb_t){ 255, 120, 170 } : (rgb_t){ 190, 70, 120 };
            put(bx + i, by + j, c);
        }
    }
    put(bx, by, (rgb_t){ 255, 255, 255 });
    /* The score under the maze. */
    draw_number(g->score, 0, 32, oy + size + 2 < 57 ? oy + size + 2 : 57, 1, (rgb_t){ 242, 239, 255 });
}

void boopie_pixel_render_hop(const boopie_hop_t *g, int head)
{
    if (head < 0 || head > BOOPIE_CHAR_COUNT) {
        head = BOOPIE_CHAR_BOOPIE;
    }
    memset(s_img, 0, sizeof(s_img));
    s_dst = s_img;
    s_dst_mask = NULL;
    /* Far off: a night sky's stars, twinkling, and clouds drifting at a third of the pillars' pace. */
    static const uint8_t STARS[][2] = { { 14, 12 }, { 30, 8 }, { 46, 14 }, { 54, 26 }, { 9, 30 }, { 38, 22 },
                                        { 24, 40 }, { 50, 40 }, { 18, 20 } };
    for (int i = 0; i < (int)(sizeof STARS / sizeof STARS[0]); i++) {
        bool lit = ((int)(g->t * 2 + i * 0.7f) % 3) != 0;
        put(STARS[i][0], STARS[i][1], lit ? (rgb_t){ 90, 86, 140 } : (rgb_t){ 46, 42, 80 });
    }
    float drift = g->t * g->speed / 3;
    for (int c = 0; c < 2; c++) {
        float cx = fmodf(70 + c * 40 - drift, 80) - 8, cy = 18 + c * 15;
        for (int dx = -5; dx <= 5; dx++) {
            put(cx + dx, cy, (rgb_t){ 40, 36, 72 });
            if (dx > -4 && dx < 4) put(cx + dx, cy - 1, (rgb_t){ 40, 36, 72 });
            if (dx > -2 && dx < 2) put(cx + dx, cy - 2, (rgb_t){ 40, 36, 72 });
        }
    }
    /* The pillars: green, lit on the left, with a cap at each end of the gap. */
    for (int i = 0; i < BOOPIE_HOP_PIPES; i++) {
        const boopie_hop_pipe_t *pp = &g->pipes[i];
        int x0 = (int)floorf(pp->x);
        int top = (int)rintf(pp->gap_y - pp->gap_h / 2), bottom = (int)rintf(pp->gap_y + pp->gap_h / 2);
        for (int y = 0; y < BOOPIE_HOP_GROUND; y++) {
            bool cap = y == top - 1 || y == top - 2 || y == bottom || y == bottom + 1;
            if (y >= top && y < bottom) {
                continue;
            }
            int from = cap ? x0 - 1 : x0, to = cap ? x0 + BOOPIE_HOP_PIPE_W : x0 + BOOPIE_HOP_PIPE_W - 1;
            for (int x = from; x <= to; x++) {
                rgb_t c = x == from + 1 ? (rgb_t){ 160, 245, 180 } : x == to ? (rgb_t){ 40, 120, 72 }
                                                                             : (rgb_t){ 90, 205, 125 };
                if (cap && (y == top - 1 || y == bottom + 1)) {
                    c = (rgb_t){ 40, 120, 72 };   /* the cap's lip */
                }
                put(x, y, c);
            }
        }
        if (pp->star) {
            float sx = pp->x + BOOPIE_HOP_PIPE_W / 2.0f, sy = pp->gap_y;
            spark(sx, sy, (rgb_t){ 255, 210, 70 }, ((int)(g->t * 6) & 1) ? 1 : 2);
            put(sx, sy, (rgb_t){ 255, 246, 200 });
        }
    }
    /* The ground, scrolling with the pillars. */
    int shift = (int)(g->t * g->speed);
    for (int x = 0; x < N; x++) {
        put(x, BOOPIE_HOP_GROUND, (rgb_t){ 110, 230, 140 });
        put(x, BOOPIE_HOP_GROUND + 1, (rgb_t){ 60, 160, 90 });
        for (int y = BOOPIE_HOP_GROUND + 2; y < N; y++) {
            bool stripe = ((x + shift + y) / 3) % 2;
            put(x, y, stripe ? (rgb_t){ 120, 80, 50 } : (rgb_t){ 96, 62, 40 });
        }
    }
    /* The pet: dazed after a crash, a puff under it just after a hop. */
    int px = (int)rintf(BOOPIE_HOP_X) - HEAD_W / 2, py = (int)rintf(g->y) - HEAD_H / 2;
    bool dazed = g->crashed > 0;
    for (int j = 0; j < HEAD_H; j++) {
        for (int k = 0; k < HEAD_W; k++) {
            char ch = HEADS[head][j][k];
            if (ch == '.') {
                continue;
            }
            rgb_t c = head_colour(head, ch, false);
            if (dazed && ch == 'e') {
                c = (rgb_t){ 255, 255, 255 };
            }
            put(px + k, py + j, c);
        }
    }
    if (g->hopped < 0.15f && !dazed) {
        put(BOOPIE_HOP_X - 3, py + HEAD_H + 1, (rgb_t){ 200, 200, 230 });
        put(BOOPIE_HOP_X, py + HEAD_H + 2, (rgb_t){ 200, 200, 230 });
        put(BOOPIE_HOP_X + 3, py + HEAD_H + 1, (rgb_t){ 200, 200, 230 });
    }
    if (dazed) {
        for (int i = 0; i < 3; i++) {
            float a = g->t * 9 + i * 2.094f;
            put(BOOPIE_HOP_X + 6 * cosf(a), py - 2 + 1.5f * sinf(a), (rgb_t){ 255, 220, 80 });
        }
    }
    /* The score on top of it all. */
    draw_number(g->score, 0, 32, 7, 2, (rgb_t){ 242, 239, 255 });
}

void boopie_pixel_head_image(int head, uint16_t *dst, int scale)
{
    if (head < 0 || head > BOOPIE_CHAR_COUNT) {
        head = BOOPIE_CHAR_BOOPIE;
    }
    s_head_defaults = true;
    int w = HEAD_W * scale;
    for (int j = 0; j < HEAD_H; j++) {
        for (int i = 0; i < HEAD_W; i++) {
            char ch = HEADS[head][j][i];
            uint16_t c = 0;
            if (ch != '.') {
                rgb_t rgb = head_colour(head, ch, false);
                c = to565(rgb.r, rgb.g, rgb.b);
            }
            for (int y = 0; y < scale; y++) {
                for (int x = 0; x < scale; x++) {
                    dst[(j * scale + y) * w + i * scale + x] = c;
                }
            }
        }
    }
    s_head_defaults = false;
}
