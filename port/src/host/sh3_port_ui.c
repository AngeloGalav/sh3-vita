// The port's own screens, drawn with the game's text renderer so they look like the rest of the PC menus:
//  - a credits page after the boot logos/warning, before the boot movie (nat_title_scene wraps FUN_005d9470, the
//    title scene's per-frame function: its state 7 fades out and returns code 7, which starts op.000; until the
//    page is done that state is not run). The game's text lands in the centred 4:3 area like the rest of the interface;
//  - nat_title_mode wraps FUN_005d8e80 (the title screen's per-frame mode function) for the movie test hooks;
//  - the port's version (repository VERSION file) in the title menu's bottom left corner (nat_title_menu wraps
//    FUN_005d80c0, which draws the New Game / Load / Options / Exit rows each frame).
// Text: 5fa540(w, h) glyph size, 5fa120(r, g, b, a) colour, 5ff8a0("\h...") builds the glyph string and
// 5fe450(glyphs, x, y) draws it; 5f9da0(extent, glyphs) measures it (width at extent[0]). Coordinates are the
// game's 2D space (the Options rows use x 0x12e..0x15c, y 0x60..).
#include "rt.h"
#include <SDL.h>
#include <string.h>

#ifndef SH3_VERSION
#define SH3_VERSION "dev"
#endif

void F_005d8e80_orig(CPU *c);
void F_005d80c0_orig(CPU *c);

static uint32_t gstr_cached(const char *s) {   // guest copies of our strings, made once
    static struct { const char *s; uint32_t g; } cache[24];
    for (unsigned i = 0; i < sizeof cache / sizeof cache[0]; i++) {
        if (cache[i].s == s) return cache[i].g;
        if (!cache[i].s) { cache[i].s = s; cache[i].g = gstrdup(s); return cache[i].g; }
    }
    return gstrdup(s);
}

static int text_width(CPU *c, uint32_t ext, const char *s) {
    HOSTFN;
    uint32_t g = rt_guest_call(c, 0x5ff8a0, 1, gstr_cached(s));
    rt_guest_call(c, 0x5f9da0, 2, ext, g);
    return (int32_t)R32(ext);
}

// One line of text; centred on cx when center is set. Brightness/alpha in 0..255 (the game's 0x80 = full).
static void text(CPU *c, uint32_t ext, const char *s, int x, int y, int center, unsigned grey, unsigned alpha) {
    if (center) x -= text_width(c, ext, s) / 2;
    rt_guest_call(c, 0x5fa120, 4, grey, grey, grey, alpha);
    uint32_t g = rt_guest_call(c, 0x5ff8a0, 1, gstr_cached(s));
    rt_guest_call(c, 0x5fe450, 3, g, (uint32_t)x, (uint32_t)y);
}

// ---------------- credits page ----------------
#define CREDITS_IN_MS   800
#define CREDITS_HOLD_MS 6000
#define CREDITS_OUT_MS  800
static int credits_done = -1;
static uint32_t credits_t0;
#define CREDITS_Y 9              // the block's top offset: centres it vertically in the 448-high 2D space

// Glyph size for the credits, given its height in the 2D space. That space is 512x448 and fills a 4:3 area
// (x scale 4/3*h/512, y scale h/448), so one 2D unit is 7/6 as wide as it is tall on screen: a w = h glyph
// comes out stretched. w = h*6/7 keeps the letters' own proportions.
static void glyph(CPU *c, unsigned h) { rt_guest_call(c, 0x5fa540, 2, (h * 6 + 3) / 7, h); }

static void credits_draw(CPU *c, unsigned a) {   // a: 0..128 (fade)
    uint32_t sp = c->esp;
    c->esp -= 0x40;
    uint32_t ext = c->esp;
    unsigned w = a, g = a * 5 / 8;
    glyph(c, 0x18);
    text(c, ext, "\\hAI-assisted port by AngeloGalav", 256, CREDITS_Y + 76, 1, w, 0x80);
    glyph(c, 0x12);
    text(c, ext, "\\hgithub.com/AngeloGalav", 256, CREDITS_Y + 108, 1, g, 0x80);
    glyph(c, 0x18);
    text(c, ext, "\\hBased on the work of:", 256, CREDITS_Y + 158, 1, w, 0x80);
    static const char *const L[][2] = {
        {"\\hvallfrr's sh4vita", "\\hgithub.com/vallfrr/sh4vita"},
        {"\\hcutarev's sh3-nx", "\\hgithub.com/cutarev/sh3-nx"},
        {"\\hRinnegatamante's vitaGL", "\\hgithub.com/Rinnegatamante/vitaGL"},
        {"\\hThirteenAG's Widescreen Fix", "\\hgithub.com/ThirteenAG/WidescreenFixesPack"},
    };
    for (int i = 0; i < 4; i++) {
        glyph(c, 0x16);
        text(c, ext, L[i][0], 256, CREDITS_Y + 196 + 58 * i, 1, w, 0x80);
        glyph(c, 0x12);
        text(c, ext, L[i][1], 256, CREDITS_Y + 222 + 58 * i, 1, g, 0x80);
    }
    rt_guest_call(c, 0x5fa570, 0);
    c->esp = sp;
}

// Test only (host): SH3_MOVIE_TEST="frame:index[,frame:index...]" plays an entry of the game's movie table (0x710c48, stride 8:
// 1 op, 2 muxx, 3 huxx, 5 henshin, 6 ed_n, 7 ed_u, 8 ed_b) through the game's own movie mode (0xc), from the title screen.
// The title returns mode code 7 (its "attract movie" case, which starts index 1); the next input poll restarts it with `index`.
static int mt_idx = -1, mt_armed;
// Test only: SH3_MOVIE_GAME="frame:index:count:table_hex[,...]" calls the game's own movie start (FUN_004bafd0, as its event scripts do:
// index, subtitle-entry count, subtitle table) from wherever the game is, and with SH3_MOVIE_GAME_FLAG=1 raises the flag the ending scripts raise (0x715d0cc bit 26).
static void movie_test_game(CPU *c) {
    HOSTFN;
    static const char *spec; static int init;
    extern unsigned g_frame;
    if (!init) { init = 1; spec = getenv("SH3_MOVIE_GAME"); }
    unsigned f, a, b; int idx, used = 0;
    if (!spec || sscanf(spec, "%u:%d:%u:%x%n", &f, &idx, &a, &b, &used) != 4 || g_frame < f) return;
    spec += used; if (*spec == ',') spec++;
    rt_log("[movie-test] frame %u: FUN_004bafd0(%d, %u, 0x%x) from the running game\n", g_frame, idx, a, b);
    rt_guest_call(c, 0x4bafd0, 3, (uint32_t)idx, a, b);
    if (getenv("SH3_MOVIE_GAME_FLAG")) W32(0x715d0cc, R32(0x715d0cc) | 0x4000000u);   // ending movies only (ed_*): the ending scripts raise it
}
void sh3_movie_test_poll(CPU *c) {
    movie_test_game(c);
    if (!mt_armed) return;
    mt_armed = 0;
    rt_log("[movie-test] starting table index %d\n", mt_idx);
    rt_guest_call(c, 0x59c9c0, 3, (uint32_t)mt_idx, 0, 0);
}
static int movie_test_title(CPU *c) {
    HOSTFN;
    static const char *spec; static int init;
    extern unsigned g_frame;
    if (!init) { init = 1; spec = getenv("SH3_MOVIE_TEST"); }
    unsigned f; int idx, used = 0;
    if (!spec || sscanf(spec, "%u:%d%n", &f, &idx, &used) != 2 || g_frame < f) return 0;
    spec += used; if (*spec == ',') spec++;
    mt_idx = idx; mt_armed = 1;
    rt_log("[movie-test] frame %u: title -> movie mode, index %d\n", g_frame, idx);
    c->eax = 7;
    c->esp += 4;   // ret
    return 1;
}

// One frame of the credits page; 0 once it is done (then it never shows again).
static int credits_frame(CPU *c) {
    HOSTFN;
    if (credits_done < 0) credits_done = getenv("SH3_NO_CREDITS") != NULL;
    if (credits_done) return 0;
    uint32_t now = SDL_GetTicks();
    if (!credits_t0) { credits_t0 = now; rt_log("[port-ui] credits page\n"); }
    uint32_t t = now - credits_t0;
    // confirm skips once the page is in (same call the title menu uses for "decide")
    if (t > CREDITS_IN_MS && t < CREDITS_IN_MS + CREDITS_HOLD_MS && rt_guest_call(c, 0x5fe970, 4, 0, 0, 1, 1))
        credits_t0 = now - (CREDITS_IN_MS + CREDITS_HOLD_MS), t = CREDITS_IN_MS + CREDITS_HOLD_MS;
    unsigned a;
    if (t < CREDITS_IN_MS) a = t * 128 / CREDITS_IN_MS;
    else if (t < CREDITS_IN_MS + CREDITS_HOLD_MS) a = 128;
    else if (t < CREDITS_IN_MS + CREDITS_HOLD_MS + CREDITS_OUT_MS) a = 128 - (t - CREDITS_IN_MS - CREDITS_HOLD_MS) * 128 / CREDITS_OUT_MS;
    else { credits_done = 1; rt_log("[port-ui] credits done\n"); return 0; }
    rt_guest_call(c, 0x5faad0, 0);   // the mode functions' per-frame 2D reset (FUN_005d8e80 starts with it)
    credits_draw(c, a);
    return 1;
}

// FUN_005d9470 wrapper (no arguments; returns 1 to stay in the title scene). Its state stack: depth [0xbcc658],
// states [0xbcc65c + 4*i]; the function first deepens it (FUN_005f1b50) and reads the top. State 7 is the one
// that fades out and returns code 7 = start the boot movie (table index 1, op.000): the credits run in its place.
#define SCENE_DEPTH  0xbcc658u
#define SCENE_STATES 0xbcc65cu
void F_005d9470_orig(CPU *c);
void nat_title_scene(CPU *c) {
    HOSTFN;
    if (credits_done != 1 && R32(SCENE_STATES + 4 * (R32(SCENE_DEPTH) + 1)) == 7 && credits_frame(c)) {
        c->eax = 1;
        c->esp += 4;                 // ret
        return;
    }
    F_005d9470_orig(c);
}

// FUN_005d8e80 wrapper (no arguments, returns the title's next-mode code; 0 = stay)
void nat_title_mode(CPU *c) {
    HOSTFN;
    if (movie_test_title(c)) return;
    F_005d8e80_orig(c);
}

// FUN_005d80c0 wrapper (int draw_menu(int interactive)): the original, then the version in the corner
void nat_title_menu(CPU *c) {
    HOSTFN;
    F_005d80c0_orig(c);              // returns with its own ret; eax = the chosen row
    uint32_t r = c->eax, sp = c->esp;
    c->esp -= 0x40;
    uint32_t ext = c->esp;
    rt_guest_call(c, 0x5fa540, 2, 0x12, 0x12);
    text(c, ext, "\\hv" SH3_VERSION, 16, 430, 0, 0x50, 0x80);
    rt_guest_call(c, 0x5fa570, 0);
    c->esp = sp;
    c->eax = r;
}

// FUN_005f3460 wrapper: builds the default input lists when there is no key.ini (pad list [0xbcc740], count
// [0xbcc744], pairs of (action id, input code), button n = code 12 + n; copied from 0x7214e4). The Vita layout
// (dx/pad_mapping.h) wants run on square and the flashlight with cancel on circle: swap the default buttons of
// dash (action 6, button 2) and light (action 7, button 3). A key.ini, once the game has written one, wins.
void F_005f3460_orig(CPU *c);
void nat_pad_defaults(CPU *c) {
    HOSTFN;
    F_005f3460_orig(c);
    uint32_t list = R32(0xbcc740), n = R32(0xbcc744);
    if (!list) return;
    for (uint32_t i = 0; i < n && i < 64; i++) {
        uint8_t a = R8(list + 2 * i), code = R8(list + 2 * i + 1);
        if (a == 6 && code == 14) W8(list + 2 * i + 1, 15);        // dash: button 2 -> 3
        else if (a == 7 && code == 15) W8(list + 2 * i + 1, 14);   // light: button 3 -> 2
    }
    rt_log("[port-ui] pad defaults: dash on button 3, light on button 2\n");
}
