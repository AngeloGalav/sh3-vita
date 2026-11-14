// SH3 natives for hot math helpers of the game's statically linked runtime (console profile 2026-11-14:
// _ftol2 5.2 %, D3DX matrix/vector math ~8 % of the game thread). Each replaces a recompiled function
// through recomp/x86rec.py WRAPS, so the original stays available as F_xxx_orig.
//
// Exactness: the recompiled code evaluates the x87 stack in double. These natives do the same operations
// in double, in the same order, and round to float at the same stores, so the results are bit-identical
// to the recompiled originals (not to a real x87, which the recompiled code already differs from).
// Contraction into fused multiply-adds would change the rounding: it is disabled for this file.
//
// SH3_NATVERIFY=1: every call runs the original for real and compares what the native would have
// produced (outputs, eax/edx, x87 stack pointer, esp); mismatches are logged (first 40) and counted.
#pragma GCC optimize("fp-contract=off")
#include "rt.h"
#include <stdlib.h>

static int verify = -1;
static unsigned n_bad, n_checked;
static inline int ver(void) {
    if (verify < 0) {
        verify = getenv("SH3_NATVERIFY") != NULL;
        if (verify) rt_log("[nat] verify mode: originals run, natives only compared\n");
    }
    return verify;
}
static void bad(const char *fn, const char *what, uint32_t a, uint32_t b) {
    if (n_bad++ < 40) rt_log("[nat] MISMATCH %s %s: native %08x original %08x\n", fn, what, a, b);
}
enum { K_FTOL, K_MAT, K_V3T, K_V4T, K_V3N, K_NORM, K_NORMC, K_N };
static unsigned n_kind[K_N];
static void tick(int k) {
    n_kind[k]++;
    if (!(++n_checked % 2000000))
        rt_log("[nat] verify: %u calls checked, %u mismatches (ftol2 %u, matmul %u, vec3xform %u, vec4xform %u, vec3normal %u, vec3norm %u + %u computed)\n",
               n_checked, n_bad, n_kind[K_FTOL], n_kind[K_MAT], n_kind[K_V3T], n_kind[K_V4T], n_kind[K_V3N], n_kind[K_NORM], n_kind[K_NORMC]);
}

static inline float gf(uint32_t a) { uint8_t *const mb = g_mem; return rt_f32(R32(a)); }
static inline uint32_t fbits(float f) { return rt_u32f(f); }

// ---- _ftol2 (0x662c40): pops ST(0), returns it truncated to int64 in edx:eax ----
// Out of range or NaN gives the x87 "integer indefinite" 0x8000000000000000, as the original does.
static int64_t ftol(double v) {
    return (v > -9223372036854775808.0 && v < 9223372036854775808.0) || v == -9223372036854775808.0 ? (int64_t)v : INT64_MIN;
}
extern void F_00662c40_orig(CPU *c);
void nat_crt_ftol2(CPU *c) {
    int64_t r = ftol(ST(0));
    if (ver()) {
        uint32_t esp = c->esp, ft = c->ftop;
        F_00662c40_orig(c);
        if (c->eax != (uint32_t)r || c->edx != (uint32_t)((uint64_t)r >> 32)) { bad("ftol2", "eax", (uint32_t)r, c->eax); bad("ftol2", "edx", (uint32_t)((uint64_t)r >> 32), c->edx); }
        if (c->esp != esp + 4 || c->ftop != ((ft + 1) & 7)) bad("ftol2", "esp/ftop", esp + 4, c->esp);
        tick(K_FTOL);
        return;
    }
    FPOP();
    c->eax = (uint32_t)r;
    c->edx = (uint32_t)((uint64_t)r >> 32);
    c->esp += 4;   // cdecl ret
}

// ---- D3DXMatrixMultiply (0x648639, stdcall out, m1, m2; returns out) ----
// out[i][j] = (m1[i][0]*m2[0][j] + m1[i][2]*m2[2][j]) + (m1[i][1]*m2[1][j] + m1[i][3]*m2[3][j]);
// the original handles out == m1 / out == m2 by keeping a row/column in registers: same values.
static void matmul(uint32_t m1, uint32_t m2, float o[16]) {
    float a[16], b[16];
    for (int k = 0; k < 16; k++) { a[k] = gf(m1 + 4 * k); b[k] = gf(m2 + 4 * k); }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double p0 = (double)a[4 * i] * b[j], p1 = (double)a[4 * i + 1] * b[4 + j];
            double p2 = (double)a[4 * i + 2] * b[8 + j], p3 = (double)a[4 * i + 3] * b[12 + j];
            o[4 * i + j] = (float)((p0 + p2) + (p1 + p3));
        }
}
extern void F_00648639_orig(CPU *c);
void nat_d3dx_matmul(CPU *c) {
    uint8_t *const mb = g_mem;
    uint32_t out = R32(c->esp + 4), m1 = R32(c->esp + 8), m2 = R32(c->esp + 12);
    float o[16];
    matmul(m1, m2, o);
    if (ver()) {
        F_00648639_orig(c);
        for (int k = 0; k < 16; k++) if (fbits(o[k]) != R32(out + 4 * k)) { bad("matmul", "elem", fbits(o[k]), R32(out + 4 * k)); break; }
        if (c->eax != out) bad("matmul", "eax", out, c->eax);
        tick(K_MAT);
        return;
    }
    for (int k = 0; k < 16; k++) W32(out + 4 * k, fbits(o[k]));
    c->eax = out;
    c->esp += 16;   // stdcall ret 0xc
}

// ---- D3DXVec3Transform (0x6483a7, out vec4, v vec3, m): ((x*m0j + y*m1j) + z*m2j) + m3j ----
// ---- D3DXVec4Transform (0x648491, out vec4, v vec4, m): ((x*m0j + y*m1j) + z*m2j) + w*m3j ----
// ---- D3DXVec3TransformNormal (0x648432, out vec3, v vec3, m): (x*m0j + y*m1j) + z*m2j ----
enum { V3T, V4T, V3N };
static int vxform(int kind, uint32_t v, uint32_t m, float o[4]) {
    double x = gf(v), y = gf(v + 4), z = gf(v + 8), w = kind == V4T ? gf(v + 12) : 0;
    int n = kind == V3N ? 3 : 4;
    for (int j = 0; j < n; j++) {
        double s = (x * gf(m + 4 * j) + y * gf(m + 16 + 4 * j)) + z * gf(m + 32 + 4 * j);
        if (kind == V3T) s = s + gf(m + 48 + 4 * j);
        else if (kind == V4T) s = s + w * gf(m + 48 + 4 * j);
        o[j] = (float)s;
    }
    return n;
}
static void vx_common(CPU *c, int kind, void (*orig)(CPU *), const char *name, int tk) {
    uint8_t *const mb = g_mem;
    uint32_t out = R32(c->esp + 4), v = R32(c->esp + 8), m = R32(c->esp + 12);
    float o[4];
    int n = vxform(kind, v, m, o);
    if (ver()) {
        orig(c);
        for (int k = 0; k < n; k++) if (fbits(o[k]) != R32(out + 4 * k)) { bad(name, "elem", fbits(o[k]), R32(out + 4 * k)); break; }
        if (c->eax != out) bad(name, "eax", out, c->eax);
        tick(tk);
        return;
    }
    for (int k = 0; k < n; k++) W32(out + 4 * k, fbits(o[k]));
    c->eax = out;
    c->esp += 16;   // stdcall ret 0xc
}
extern void F_006483a7_orig(CPU *c);
extern void F_00648491_orig(CPU *c);
extern void F_00648432_orig(CPU *c);
void nat_d3dx_vec3xform(CPU *c) { vx_common(c, V3T, F_006483a7_orig, "vec3xform", K_V3T); }
void nat_d3dx_vec4xform(CPU *c) { vx_common(c, V4T, F_00648491_orig, "vec4xform", K_V4T); }
void nat_d3dx_vec3normal(CPU *c) { vx_common(c, V3N, F_00648432_orig, "vec3normal", K_V3N); }

// ---- D3DXVec3Normalize (0x64856f, stdcall out, v; returns out) ----
// Length via the game's own reciprocal-square-root table (0x726820, pairs of floats indexed by the
// exponent/top mantissa bits); vectors whose squared length is within ~1e-5 of 1 are copied unchanged.
static int vnorm(uint32_t v, float o[3]) {
    float x = gf(v), y = gf(v + 4), z = gf(v + 8);
    double sum = ((double)x * x + (double)y * y) + (double)z * z;
    uint32_t bits = fbits((float)sum);
    if (bits == 0) { o[0] = o[1] = o[2] = 0.0f; return 1; }
    uint32_t d = fbits((float)(sum - 1.0)) & 0x7fffffffu;
    if ((int32_t)d <= 0x3727c5ac) { o[0] = x; o[1] = y; o[2] = z; return 0; }   // copy
    uint32_t idx = (bits >> 12) & 0xff8u;
    double r = (double)rt_f32((bits & 0xffffffu) | 0x3f000000u) * gf(0x726820u + idx) + gf(0x726824u + idx);
    r = r * rt_f32(((0xbeffffffu - bits) >> 1) & 0xff800000u);
    o[0] = (float)(x * r); o[1] = (float)(y * r); o[2] = (float)(z * r);
    return 1;
}
extern void F_0064856f_orig(CPU *c);
void nat_d3dx_vec3norm(CPU *c) {
    uint8_t *const mb = g_mem;
    uint32_t out = R32(c->esp + 4), v = R32(c->esp + 8);
    float o[3];
    int computed = vnorm(v, o);
    if (ver()) {
        F_0064856f_orig(c);
        for (int k = 0; k < 3; k++) if (fbits(o[k]) != R32(out + 4 * k)) { bad("vec3norm", "elem", fbits(o[k]), R32(out + 4 * k)); break; }
        if (c->eax != out) bad("vec3norm", "eax", out, c->eax);
        tick(computed ? K_NORMC : K_NORM);
        return;
    }
    for (int k = 0; k < 3; k++) W32(out + 4 * k, fbits(o[k]));
    c->eax = out;
    c->esp += 12;   // stdcall ret 0x8
}
