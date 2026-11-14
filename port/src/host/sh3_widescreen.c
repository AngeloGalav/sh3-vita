// SH3 widescreen at the port's render size (960x544 on the Vita).
//
// Port of ThirteenAG's SilentHill3.WidescreenFix (GPL-3.0, github.com/ThirteenAG/WidescreenFixesPack)
// to the static recompiler, following the approach of sh3-nx (cutarev/sh3-nx): what the original
// fix patches mid-function lives in tools/patch_exe.py; what it hooks as whole functions is here,
// wired through the WRAPS/OVERRIDES tables of recomp/x86rec.py. The addresses and the geometry
// were cross-checked against the 2003 exe and against sh3-nx's src/game/sh3.c.
//
// The game draws the 3D view with two scale factors (horizontal 0x43AB80, vertical 0x43ABC0) and
// lays its 2D out in the PS2's 512x448 screen, converted to render-target pixels by gsx/gsy
// (0x682BA0/0x682BD0). At a wide render size: the gameplay camera widens (Hor+), cutscenes keep
// their 4:3 width and lose height instead of showing borders (the borders themselves are zeroed
// by patch_exe.py), and the 2D (menus, HUD, text) stays in a centred 4:3 area; elements meant to
// span the whole screen (fades, backdrops, effects positioned from the 3D view) keep the full
// mapping, identified by the call's return address like the original fix does.
//
// The render size is the window's drawable size: 960x544 on the Vita, the host window (960x544
// by default). The device and the game's display-mode globals are forced to it at every
// CreateDevice/Reset and at the game's own mode setters, so disp.ini and the Options resolution
// row cannot change it (the row is locked to the same size in the d3d8/win32 shims).
#include "rt.h"

void plat_drawable_size(int *w, int *h);

// The port's render size, in pixels.
void sh3_ws_size(int *w, int *h) {
    int dw = 0, dh = 0;
    plat_drawable_size(&dw, &dh);
    if (dw <= 0 || dh <= 0) { dw = 960; dh = 544; }
    *w = dw; *h = dh;
}

static double ws_aspect(void) {
    int w, h; sh3_ws_size(&w, &h);
    return (double)w / (double)h;
}
static double ws_fov(void) { return (4.0 / 3.0) / ws_aspect(); }  // 1 at 4:3, < 1 when wide

// ---- forcing the render size on the game ----
// 0x72C780/84: the display mode the game wants; 0x72C788/8C: the size it lays the 2D and its
// render targets out in (0x416900/0x416910 return these; gsx/gsy read the width through them).
// Both feed the D3DPRESENT_PARAMETERS at 0x72C664.
#define MODE_W 0x72c780u
#define MODE_H 0x72c784u
#define RT_W   0x72c788u
#define RT_H   0x72c78cu

static void force_mode(CPU *c) {
    HOSTFN;
    int w, h; sh3_ws_size(&w, &h);
    W32(MODE_W, (uint32_t)w); W32(MODE_H, (uint32_t)h);
    W32(RT_W, (uint32_t)w);   W32(RT_H, (uint32_t)h);
}

extern void F_004193a0_orig(CPU *c);
void nat_ws_create(CPU *c) { force_mode(c); F_004193a0_orig(c); }     // creates the device
extern void F_00419760_orig(CPU *c);
void nat_ws_reset(CPU *c)  { force_mode(c); F_00419760_orig(c); }     // resets it after a mode change

// set display mode (w, h) / set the 2D and render-target size (w, h): override the arguments.
extern void F_00416ae0_orig(CPU *c);
void nat_ws_setmode(CPU *c) {
    HOSTFN;
    int w, h; sh3_ws_size(&w, &h);
    W32(c->esp + 4, (uint32_t)w); W32(c->esp + 8, (uint32_t)h);
    F_00416ae0_orig(c);
}
extern void F_00416b70_orig(CPU *c);
void nat_ws_set2d(CPU *c) {
    HOSTFN;
    int w, h; sh3_ws_size(&w, &h);
    W32(c->esp + 4, (uint32_t)w); W32(c->esp + 8, (uint32_t)h);
    F_00416b70_orig(c);
}

// ---- the 3D camera ----
// FUN_0043ab80 returns the horizontal scale, FUN_0043abc0 the vertical one, in ST(0).
// Gameplay widens the view (Hor+); a cutscene (flag 0x81FAB5) keeps the 4:3 width and loses
// height instead. The picked-up item and the mouse cursor keep the 4:3 width (their calls return
// to 0x599c29 / 0x4ddb98).
#define CUTSCENE_FLAG 0x81fab5u

extern void F_0043ab80_orig(CPU *c);
void nat_ws_scalex(CPU *c) {
    HOSTFN;
    uint32_t from = R32(c->esp);
    F_0043ab80_orig(c);
    if (!R8(CUTSCENE_FLAG) && from != 0x599c29u && from != 0x4ddb98u)
        ST(0) *= ws_fov();
}
extern void F_0043abc0_orig(CPU *c);
void nat_ws_scaley(CPU *c) {
    HOSTFN;
    F_0043abc0_orig(c);
    if (R8(CUTSCENE_FLAG)) ST(0) /= ws_fov();
}

// ---- the 2D ----
// float gsx(int x) (FUN_00682ba0, replaced whole): the 2D x, in 1/16 units around 2048, to
// render-target pixels. The interface keeps a centred 4:3 area (width h*4/3); the elements meant
// to span the screen keep the original full-width mapping, matched by the call's return address
// (ThirteenAG's list of stretched call sites, verified against this exe: all 16 call gsx).
static const uint32_t full_width_calls[] = {
    0x468B8A, 0x468BDF, 0x468C25, 0x468C71, 0x468E31, 0x468E78, 0x468EB7, 0x468F0C,
    0x47A684, 0x47A6D8, 0x47A726, 0x47A771, 0x47A7B9, 0x49FAF0, 0x5F6D27, 0x5F80F4,
};

void nat_ws_gsx(CPU *c) {
    HOSTFN;
    uint32_t from = R32(c->esp);
    int32_t x = (int32_t)R32(c->esp + 4);
    int full = 0;
    for (unsigned i = 0; i < sizeof full_width_calls / sizeof *full_width_calls; i++)
        full |= from == full_width_calls[i];
    double a = ws_aspect();
    double w = (double)(int32_t)rt_guest_call(c, 0x402ba0, 0);   // the render target's width
    double left = full ? 256.0 : 192.0 * a, scale = full ? 1.0 / 512.0 : 1.0 / (384.0 * a);
    FPUSH(w * ((double)x * rt_f32(R32(0x68e6e0)) - rt_f32(R32(0x68e6dc)) + left) * scale);
    c->esp += 4;   // ret (cdecl: the caller pops the argument)
}

// The cave 0x401ed8, called from 0x5f1d15 in place of `fstp [ecx-0x18]; fld st(0)`: the low-health
// cross in the lower right corner keeps its place in the 4:3 area instead of riding the wide edge.
void nat_ws_health(CPU *c) {
    HOSTFN;
    double x = ST(0);
    FPOP();
    int w, h; sh3_ws_size(&w, &h);
    double v = x * ws_fov() + ((double)w - (double)h * 4.0 / 3.0);
    W32(c->ecx - 0x18, rt_u32f((float)v));
    FPUSH(ST(0));
    c->esp += 4;   // ret
}

// ---- Options: L / R switch the Configuration pages ----
// The Configuration page (FUN_005e68f0) shows its rows in three pages (row ids 0x00-0x0A,
// 0x0B-0x15, 0x16-0x1C) and switches them on input state 7/8 (FUN_005e3380 fills the byte at
// 0xbcc17c from the keyboard table; the mouse arrows 0x1e/0x1f raise the same states). The Vita
// pad has no binding that reaches 7/8, so the pages could not be changed on the console. Wrap the
// page's input poll: on a fresh L/R press with no other input this frame, raise the state.
#define OPT_INPUT_STATE 0xbcc17cu

extern int g_sh3_pad_l, g_sh3_pad_r;   // held state of the pad's L/R, set by dinput.c each pad poll

extern void F_005e3380_orig(CPU *c);
void nat_opt_page_lr(CPU *c) {
    HOSTFN;
    F_005e3380_orig(c);
    static int prev_l, prev_r;
    int l = g_sh3_pad_l, r = g_sh3_pad_r;
    if (mb[OPT_INPUT_STATE] == 0) {
        if (l && !prev_l) mb[OPT_INPUT_STATE] = 7;
        else if (r && !prev_r) mb[OPT_INPUT_STATE] = 8;
    }
    prev_l = l; prev_r = r;
}
