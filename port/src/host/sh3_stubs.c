// Symbols that the SH4-derived d3d8/dx/pc layers still reference but that have no meaning for SH3
// (SH4 audit/model hooks, camera scale, resolution patches). All are inert.
#include "rt.h"
#include <stdint.h>
#include <stddef.h>

int g_audit_checks, g_audit_enabled, g_audit_frames, g_options_active, g_vita_bindings_open;
unsigned g_cursor_seen;
int nat_model_bounds(CPU *c) { (void)c; return 0; }
int nat_model_uv(CPU *c) { (void)c; return 0; }
void prof_start(void) {}
void rt_apply_res(void) {}
void rt_keep_res(void) {}
int rt_audit_capture(unsigned frame, char *path, size_t size) { (void)frame; (void)path; (void)size; return 0; }
void rt_audit_frame(unsigned frame) { (void)frame; }
void rt_audit_input(CPU *c, uint32_t keys, uint32_t size) { (void)c; (void)keys; (void)size; }
unsigned rt_audit_motion(void) { return 0; }
float rt_camera_scale(void) { return 1.0f; }
void rt_controls_present(void) {}
int rt_native_model_bounds_enabled(void) { return 0; }
int rt_native_model_uv_enabled(void) { return 0; }
int rt_window_camera_active(void) { return 0; }
int sh4_model_uv_prepare(const uint8_t *s, uint32_t n, void *p, void *uv, void *st) { (void)s; (void)n; (void)p; (void)uv; (void)st; return 0; }
void sh4_model_uv_apply(const uint8_t *s, uint8_t *d, uint32_t n, const void *uv, void *st, float ox, float oy) { (void)s; (void)d; (void)n; (void)uv; (void)st; (void)ox; (void)oy; }
void sh4_model_uv_apply_bulk(const uint8_t *s, uint8_t *d, uint32_t n, const void *uv, void *st, float ox, float oy) { (void)s; (void)d; (void)n; (void)uv; (void)st; (void)ox; (void)oy; }
void sh4_model_uv_apply_prefetched(const uint8_t *s, uint8_t *d, uint32_t n, const void *uv, void *st, float ox, float oy) { (void)s; (void)d; (void)n; (void)uv; (void)st; (void)ox; (void)oy; }

// ---- SH3 natives replacing CRT code whose in-.text jump tables the recompiler cannot follow ----
#include <string.h>
void nat_crt_memcpy(CPU *c) {   // cdecl void *memcpy(dst, src, n): returns dst; overlap-safe like the MS CRT memmove alias
    uint8_t *const mb = g_mem;
    uint32_t d = R32(c->esp + 4), s = R32(c->esp + 8), n = R32(c->esp + 12);
    if (n) memmove(mb + d, mb + s, n);
    c->eax = d;
    c->esp += 4;
}

#include <math.h>
void nat_crt_cifmod(CPU *c) {   // _CIfmod (x87 intrinsic): pops y = ST(0), replaces x = ST(1) with fmod(x, y); near ret
    double y = ST(0), x = ST(1);
    FPOP();
    ST(0) = fmod(x, y);
    c->esp += 4;
}
void rt_audit_watchdog(void) {}   // SH4 hardware-audit hook (src/rt/audit.c, skipped for SH3) called by the Vita watchdog
