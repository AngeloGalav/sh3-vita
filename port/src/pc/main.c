// PC entry point: SDL2, loading the game exe, starting the recompiled code.
#include "rt.h"
#include "plat.h"
#include "cp1252.h"
#include <stdlib.h>

static SDL_Window *g_win;

SDL_Window *plat_sdl_window(void) { return g_win; }
int plat_width(void) { return 640; }
int plat_height(void) { return 480; }
void plat_show_window(void) { SDL_ShowWindow(g_win); }

int plat_message_box(const char *title, const char *text, unsigned type) {
    // Both GOG call sites are MB_OK startup errors. Do not fabricate acceptance
    // of an unsupported confirmation or of an invisible headless dialogue.
    if ((type & 15u) || getenv("SH4_HEADLESS")) return 0;
    unsigned icon = (type & 0xf0u) == 0x10u ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION;
    char title_utf8[768], text_utf8[6144];
    rt_cp1252_utf8(title_utf8, sizeof title_utf8, title);
    rt_cp1252_utf8(text_utf8, sizeof text_utf8, text);
    return SDL_ShowSimpleMessageBox(icon, title_utf8, text_utf8, g_win) == 0 ? 1 : 0;
}

int plat_pump_events(void) {
    SDL_Event e;
    int quit = 0;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT) quit = 1;
    return quit;
}

static SDL_GLContext g_gl;
void plat_gl_init(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g_gl = SDL_GL_CreateContext(g_win);
    if (!g_gl) rt_fatal("contexte OpenGL : %s", SDL_GetError());
    SDL_GL_SetSwapInterval(getenv("SH4_NOVSYNC") ? 0 : 1);
}
#include <SDL_opengl.h>
#include "d3d8.h"
// SH3_SHOTS="30,120": dump the back buffer (PPM) before presenting the given frames (host bring-up screenshots).
static void shot_maybe(void) {
    static unsigned frame; static const char *spec; static int init;
    if (!init) { init = 1; spec = getenv("SH3_SHOTS"); }
    frame++;
    if (!spec) return;
    char buf[256]; snprintf(buf, sizeof buf, ",%s,", spec);
    char key[32]; snprintf(key, sizeof key, ",%u,", frame);
    if (!strstr(buf, key)) return;
    int w = 0, h = 0; SDL_GL_GetDrawableSize(g_win, &w, &h);
    if (w <= 0 || h <= 0) return;
    unsigned char *px = malloc((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    char path[300]; snprintf(path, sizeof path, "shot_%u.ppm", frame);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h); for (int y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f); fclose(f); rt_log("[shot] %s %dx%d\n", path, w, h); }
    {   // SH3 camera globals (camera update 0x4c3ea0): eye vec4 0xba99c4, look-at target vec4 0xba99d4
        float v[8]; memcpy(v, g_mem + 0xba99c4, sizeof v);
        rt_log("[shot-cam] frame=%u eye=%g,%g,%g target=%g,%g,%g\n", frame, v[0], v[1], v[2], v[4], v[5], v[6]);
    }
    { extern uint64_t g_heap_used, g_heap_peak;   // guest TLSF heap (base 0x07200000): sizes the Vita guest block
      rt_log("[shot-heap] frame=%u used=%llu KB peak=%llu KB\n", frame, (unsigned long long)(g_heap_used >> 10), (unsigned long long)(g_heap_peak >> 10)); }
    if (g_dev) {   // D3D state at the shot: fixed-function view/proj and VS constants c0..c7 (row-major as set by the game)
        const float *m[2] = {g_dev->view, g_dev->proj}; static const char *nm[2] = {"view", "proj"};
        for (int k = 0; k < 2; k++)
            rt_log("[shot-%s] %g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g\n", nm[k], m[k][0], m[k][1], m[k][2], m[k][3],
                   m[k][4], m[k][5], m[k][6], m[k][7], m[k][8], m[k][9], m[k][10], m[k][11], m[k][12], m[k][13], m[k][14], m[k][15]);
        for (int r = 0; r < 8; r++)
            rt_log("[shot-vsc] c%d = %g %g %g %g\n", r, g_dev->vsc[r][0], g_dev->vsc[r][1], g_dev->vsc[r][2], g_dev->vsc[r][3]);
    }
    free(px);
}
void plat_gl_swap(void) { shot_maybe(); SDL_GL_SwapWindow(g_win); }
void plat_drawable_size(int *w, int *h) { SDL_GL_GetDrawableSize(g_win, w, h); }

void rt_platform_fatal(const char *msg) {
    if (getenv("SH4_HEADLESS")) return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "sh4vita", msg, g_win);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : getenv("SH3_GAME_DIR") ? getenv("SH3_GAME_DIR") : "/game";
    SDL_strlcpy(g_game_dir, dir, sizeof g_game_dir);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int ww = 960, wh = 544;
    if (getenv("SH4_WIN")) sscanf(getenv("SH4_WIN"), "%dx%d", &ww, &wh);   // video capture window
    g_win = SDL_CreateWindow("Silent Hill 4 (sh4vita)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | (getenv("SH4_WIN") ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_RESIZABLE));
    /* Optional PC-only diagnostic: reproduce the guest-memory ceiling of a Vita. */
    unsigned guest_mb = 512;
    const char *guest_limit = getenv("SH4_PC_GUEST_MB");
    if (guest_limit) {
        char *end = NULL;
        unsigned long parsed = strtoul(guest_limit, &end, 10);
        if (end != guest_limit && !*end && parsed >= 64 && parsed <= 512)
            guest_mb = (unsigned)parsed;
    }
    if (rt_init(guest_mb << 20)) rt_fatal("guest memory: allocation failed");
    rt_log("[mem] PC guest memory: %u MB\n", guest_mb);
    char exe[600];
    SDL_snprintf(exe, sizeof exe, "%s/sh3.exe", g_game_dir);
    int r = rt_load_exe(exe);
    if (r) rt_fatal("cannot load %s (%d)", exe, r);
    rt_fill_iat();
    rt_log("game loaded from %s\n", g_game_dir);
    { extern void prof_start(void); prof_start(); }
    rt_run_entry();
    SDL_Quit();
    return 0;
}
