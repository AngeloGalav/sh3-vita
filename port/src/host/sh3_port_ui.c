// The port's own screens, drawn with the game's text renderer so they look like the rest of the PC menus:
//  - a credits page between the boot logos/warning and the title screen (nat_title_mode wraps FUN_005d8e80,
//    the title screen's per-frame mode function: until the page is done the title is not entered yet);
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

static void credits_draw(CPU *c, unsigned a) {   // a: 0..128 (fade)
    uint32_t sp = c->esp;
    c->esp -= 0x40;
    uint32_t ext = c->esp;
    unsigned w = a, g = a * 5 / 8;
    rt_guest_call(c, 0x5fa540, 2, 0x18, 0x18);
    text(c, ext, "\\hAI-assisted port by AngeloGalav", 256, 76, 1, w, 0x80);
    rt_guest_call(c, 0x5fa540, 2, 0x12, 0x12);
    text(c, ext, "\\hgithub.com/AngeloGalav", 256, 108, 1, g, 0x80);
    rt_guest_call(c, 0x5fa540, 2, 0x18, 0x18);
    text(c, ext, "\\hBased on the work of:", 256, 158, 1, w, 0x80);
    static const char *const L[][2] = {
        {"\\hvallfrr's sh4vita", "\\hgithub.com/vallfrr/sh4vita"},
        {"\\hcutarev's sh3-nx", "\\hgithub.com/cutarev/sh3-nx"},
        {"\\hAngeloGalav's sh2-vita", "\\hgithub.com/AngeloGalav/sh2-vita"},
        {"\\hSilent Hill 3 PC Fix by Steam006 (2.8.9.1)", "\\hcommunity.pcgamingwiki.com/files/file/1331"},
    };
    for (int i = 0; i < 4; i++) {
        rt_guest_call(c, 0x5fa540, 2, 0x16, 0x16);
        text(c, ext, L[i][0], 256, 196 + 58 * i, 1, w, 0x80);
        rt_guest_call(c, 0x5fa540, 2, 0x12, 0x12);
        text(c, ext, L[i][1], 256, 222 + 58 * i, 1, g, 0x80);
    }
    rt_guest_call(c, 0x5fa570, 0);
    c->esp = sp;
}

// FUN_005d8e80 wrapper (no arguments, returns the title's next-mode code; 0 = stay)
void nat_title_mode(CPU *c) {
    HOSTFN;
    if (credits_done < 0) credits_done = getenv("SH3_NO_CREDITS") != NULL;
    if (credits_done) { F_005d8e80_orig(c); return; }
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
    else { credits_done = 1; rt_log("[port-ui] credits done\n"); F_005d8e80_orig(c); return; }
    rt_guest_call(c, 0x5faad0, 0);   // the mode functions' per-frame 2D reset (FUN_005d8e80 starts with it)
    credits_draw(c, a);
    c->eax = 0;
    c->esp += 4;                     // ret
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
