// SH3 Options > Configuration: the "Screen Mode" row (full screen / window, meaningless on a port and greyed out
// there) becomes a "Textures" row that picks the texture quality (texq_get/texq_set in d3d8/resource.c).
//
// nat_cfg_texq replaces FUN_005e62a0, the row handler called once per frame by the Configuration page
// (FUN_005e68f0). It is the original routine with three values instead of two:
//   - left/right (input state 0xbcc17c = 6/5, or the mouse arrows 0x20/0x21) changes the pending value,
//   - confirm (state 1) applies it and saves it to sh3port.ini,
//   - a pending value different from the applied one sets the row's "changed" bit (0x800000 of 0xbcc1dc),
//     like the game's own rows, and the same game routines draw the label, value, arrows and help line.
// The game's screen-mode byte (0xbcc1b6) and its apply calls (0x4023d0/0x4023e0) are no longer touched.
// The label/help messages 0x8b/0x8c are rewritten in the loaded option message bank (0xb577f0) for the
// Latin-script languages; Japanese/Korean/Chinese keep the "Screen Mode" label (other encoding).
#include "rt.h"
#include "win32/win32.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

int texq_get(void);
void texq_set(int q);

#define ROW_ID      0x17
#define SEL_ITEM    0xbcc178u   // selected row id
#define INPUT_STATE 0xbcc17cu   // 1 confirm, 5 right, 6 left
#define ROW_ENABLED 0xbcc182u   // [row id] != 0: selectable
#define ROW_CHANGED 0xbcc1dcu   // bit per row: pending != applied
#define MSG_BANK    0xb577f0u

static const char *const VALUES[3] = {"\\hPerformance", "\\hBalanced", "\\hHigh"};
static uint32_t g_vals[3];
static int g_pending = -1;
static uint32_t g_last_ms;

// ---- persistence: sh3port.ini (game folder, or the SH3_SAVE_DIR overlay) ----
static int ini_loaded;
int texq_ini_value(void) {   // -1 when absent; called once by texq_get before the env default
    char path[700], line[64];
    w32_host_path("sh3port.ini", path, sizeof path);
    FILE *f = fopen(path, "rb");
    int q = -1;
    ini_loaded = 1;
    if (!f) return -1;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "textures=", 9)) q = line[9] - '0';
    fclose(f);
    return q >= 0 && q <= 2 ? q : -1;
}
static void ini_save(int q) {
    char path[700];
    w32_host_path_w("sh3port.ini", path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) { rt_log("[texq] cannot write %s\n", path); return; }
    fprintf(f, "textures=%d\n", q);
    fclose(f);
    rt_log("[texq] saved textures=%d to %s\n", q, path);
}

// ---- message bank relabel ----
static void enc(uint8_t *d, const char *s) {   // option-bank text: header 00 80, ASCII - 0x20, space 0, end ff ff
    *d++ = 0x00; *d++ = 0x80;
    for (; *s; s++) *d++ = *s == ' ' ? 0 : (uint8_t)(*s - 0x20);
    *d++ = 0xff; *d = 0xff;
}
static int msg_is(const uint8_t *m, const char *s) {
    if (m[0] != 0x00 || m[1] != 0x80) return 0;
    m += 2;
    for (; *s; s++, m++) if (*m != (*s == ' ' ? 0 : (uint8_t)(*s - 0x20))) return 0;
    return m[0] == 0xff && m[1] == 0xff;
}
static size_t msg_len(const uint8_t *m, size_t max) {   // bytes before the ff ff terminator
    for (size_t i = 0; i + 1 < max; i++) if (m[i] == 0xff && m[i + 1] == 0xff) return i;
    return 0;
}
static void relabel(void) {
    static const struct { const char *orig, *label, *help; } L[] = {
        {"Screen Mode", "Textures", "Set texture quality"},
        {"Mode ecran", "Textures", "Qualite des textures"},
        {"Bildschirm-Modus", "Texturen", "Texturqualitaet"},
        {"Modalit\xe0 Schermo", "Texture", "Qualita delle texture"},
        {"Modo de la Pantalla", "Texturas", "Calidad de texturas"},
    };
    uint8_t *bank = g_mem + MSG_BANK;
    uint16_t n;
    memcpy(&n, bank, 2);
    if (n <= 0x8c || n > 400) return;
    uint16_t o_l, o_h;
    memcpy(&o_l, bank + 2 + 2 * 0x8b, 2);
    memcpy(&o_h, bank + 2 + 2 * 0x8c, 2);
    uint8_t *ml = bank + 2u * o_l, *mh = bank + 2u * o_h;
    for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) {
        if (!msg_is(ml, L[i].orig)) continue;
        size_t room_l = msg_len(ml, 128), room_h = msg_len(mh, 128);
        if (strlen(L[i].label) + 2 <= room_l) enc(ml, L[i].label);
        if (strlen(L[i].help) + 2 <= room_h) enc(mh, L[i].help);
        rt_log("[texq] Configuration row 0x17 relabelled (%s -> %s)\n", L[i].orig, L[i].label);
        return;
    }
}

static uint32_t call(CPU *c, uint32_t fn, int n, uint32_t a, uint32_t b, uint32_t c3, uint32_t d, uint32_t e) {
    return rt_guest_call(c, fn, n, a, b, c3, d, e);
}

// FUN_005e62a0 replacement (no arguments, returns with ret)
void nat_cfg_texq(CPU *c) {
    HOSTFN;
    uint32_t ret_sp = c->esp;
    c->esp -= 0x80;                       // scratch: two 32-byte text extents + the 16-byte arrow box
    uint32_t ext = c->esp, box = c->esp + 0x60;
    if (!g_vals[0]) for (int i = 0; i < 3; i++) g_vals[i] = gstrdup(VALUES[i]);
    uint32_t now = SDL_GetTicks();
    if (g_pending < 0 || now - g_last_ms > 250) g_pending = texq_get();   // page (re)entered: show the applied value
    g_last_ms = now;
    relabel();
    mb[ROW_ENABLED + ROW_ID] = 1;
    int w[3], wmax = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t gl = call(c, 0x5ff8a0, 1, g_vals[i], 0, 0, 0, 0);
        call(c, 0x5f9da0, 2, ext, gl, 0, 0, 0);
        w[i] = (int32_t)R32(ext);
        if (w[i] > wmax) wmax = w[i];
    }
    int q = g_pending;
    int8_t sel = (int8_t)mb[SEL_ITEM];
    if (sel == ROW_ID) {
        call(c, 0x601940, 0, 0, 0, 0, 0, 0);
        call(c, 0x601970, 5, 0x100, 0x79, 0x122, 0x95, 0x20);
        call(c, 0x601970, 5, (uint32_t)(wmax + 0x13e), 0x79, (uint32_t)(wmax + 0x15c), 0x95, 0x21);
        uint32_t hit = call(c, 0x601a40, 0, 0, 0, 0, 0, 0);
        if (hit && (hit == 0x20 || hit == 0x21) && call(c, 0x600bc0, 0, 0, 0, 0, 0, 0))
            mb[INPUT_STATE] = hit == 0x20 ? 6 : 5;
        uint8_t st = mb[INPUT_STATE];
        if (st == 1) {
            call(c, 0x46cd50, 3, 3, 0x3f800000u, 0, 0, 0);
            if (q != texq_get()) { texq_set(q); ini_save(q); }
        } else if (st == 5 || st == 6) {
            call(c, 0x46cd50, 3, 5, 0x3f800000u, 0, 0, 0);
            q = st == 5 ? (q + 1) % 3 : (q + 2) % 3;
        }
        memset(mb + box, 0, 0x10);
        W16(box + 2, 0xf890);
        mb[box + 4] = 1; mb[box + 5] = 1;
        W32(box + 8, (uint32_t)(((wmax + 0x58) * 0x10) / 2));
        W32(box + 0xc, (uint32_t)wmax);
        call(c, 0x5e3960, 1, box, 0, 0, 0, 0);
    }
    uint32_t chg = R32(ROW_CHANGED);
    W32(ROW_CHANGED, q != texq_get() ? chg | (1u << ROW_ID) : chg & ~(1u << ROW_ID));
    c->ecx = ROW_ID;
    int state = (int)call(c, 0x5e3660, 0, 0, 0, 0, 0, 0);
    int x = (wmax - w[q]) / 2;
    if (state == 5) {
        c->eax = 6; call(c, 0x5e3c60, 3, g_vals[q], (uint32_t)(x + 0x130), 0x7b, 0, 0);
        c->eax = 5; call(c, 0x5e3c60, 3, g_vals[q], (uint32_t)(x + 0x12c), 0x77, 0, 0);
    } else {
        c->eax = (uint32_t)state; call(c, 0x5e3c60, 3, g_vals[q], (uint32_t)(x + 0x12e), 0x79, 0, 0);
    }
    c->eax = (uint32_t)state; c->ecx = 3; c->edx = ROW_ID;
    call(c, 0x5e3130, 1, 0x8b, 0, 0, 0, 0);
    if ((int8_t)mb[SEL_ITEM] == ROW_ID) {
        c->eax = 1; c->ecx = 5; c->edx = ROW_ID;
        call(c, 0x5e3130, 1, 0x8c, 0, 0, 0, 0);
    }
    g_pending = q;
    c->esp = ret_sp + 4;                  // ret
}
