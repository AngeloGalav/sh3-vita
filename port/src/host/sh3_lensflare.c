// The game's own "flare always visible" switch, forced on the Vita. The switch was found by vallfrr's sh3vita
// (github.com/vallfrr/sh3vita, GPL-3.0, port/diag_lens_query.c).
//
// F_0049fac0 (the lens-flare / flashlight visibility test) starts with:
//   push "lensflare_ztest"; call F_005ef950 (settings getter); cmp eax, 1; jne ...; ret (eax = 1)
// When the setting reads 1 — the PC's low lens-flare quality path — the function returns "visible"
// before any render-target switch, marker draw or 1x1 readback. On the Vita that probe is both slow
// (two mid-frame readbacks + target switches per light per frame: surf_LockRect 2.4 %, [q] sync
// 2.5 ms/frame, ReadPixels 1.6 ms/frame in the 2026-11-17 profile) and unreliable (stale readbacks).
//
// nat_cfg_lens wraps F_005ef950 and answers 1 only for the key "lensflare_ztest"; every other key
// goes to the original. The only code xref to that string in the exe is F_0049fac0 (0x49facd), so
// nothing else can observe the change. Cost: a light behind a wall is no longer hidden by the probe
// (the angle/distance gating elsewhere is untouched).
//
// Default ON on the Vita, OFF elsewhere; SH3_LENSFORCE=0 restores the original probe, =1 forces it on.
#include "rt.h"
#include <stdlib.h>
#include <string.h>

extern void F_005ef950_orig(CPU *c);

void nat_cfg_lens(CPU *c) {
    HOSTFN;
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("SH3_LENSFORCE");
#ifdef __vita__
        on = !e || atoi(e) != 0;
#else
        on = e && atoi(e) != 0;
#endif
    }
    static const char key_name[] = "lensflare_ztest";
    uint32_t key = R32(c->esp + 4);
    if (on && key + sizeof(key_name) <= g_mem_size && !memcmp(mb + key, key_name, sizeof(key_name))) {
        static unsigned n;
        if (!(n++)) rt_log("[lensflare] lensflare_ztest forced to 1: F_0049fac0 returns visible, probe bypassed"
                           " (SH3_LENSFORCE=0 restores)\n");
        c->eax = 1;
        c->esp += 4;   // cdecl ret: without it F_0049fac0 pops edi/esi/ebx one slot off and the game quits
        return;
    }
    F_005ef950_orig(c);
}
