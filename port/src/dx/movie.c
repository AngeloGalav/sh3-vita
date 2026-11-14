// Cutscenes: the game builds a DirectShow graph (source -> MPEG-1 splitter -> codecs ->
// renderer). The DirectShow base classes and the render-to-texture code are game code, already
// recompiled; the host provides everything upstream: graph, filters, pins and decoder
// (pl_mpeg). Frames are delivered to the real guest renderer filter through the standard protocol
// (ReceiveConnection / GetAllocator / GetBuffer / Receive), audio goes to the host mixer.
#include "d3d8/d3d8.h"
#include "win32/win32.h"
#include <SDL.h>
#include <stdlib.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/display.h>
#endif

#include <stdio.h>
#include <unistd.h>
// [movie] time spent in movie file reads (decode thread stats)
static double g_st_io;
static int g_movie_all_frames;
static unsigned s_movie_serial;
#define PLM_VIDEO_SKIP_B_ALLOWED() (!g_movie_all_frames)
static size_t movie_fread(void *p, size_t s, size_t n, FILE *f) {
    uint64_t t = SDL_GetPerformanceCounter();
    size_t r = fread(p, s, n, f);
    g_st_io += (double)(SDL_GetPerformanceCounter() - t) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    return r;
}
#define fread movie_fread
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"
#undef fread
#include "movie_stream_cache.h"

#define M(p, name) static void p##_##name(CPU *c)
enum { MK_GRAPH = 100, MK_CTL, MK_POS, MK_EV, MK_AUD, MK_SINK, MK_FILTER, MK_PIN, MK_ENUM, MK_ALLOC, MK_SAMP };
#define S_FALSE_ 1u
#define E_NOINTERFACE_ 0x80004002u
#define E_NOTIMPL_ 0x80004001u
#define VFW_E_TIMEOUT 0x80040227u
#define VFW_E_NOT_FOUND 0x80040216u

typedef struct Movie Movie;
typedef struct { ComObj com; Movie *m; int role; int dir; const char *id; uint32_t peer; } HObj;
typedef struct { ComObj com; Movie *m; uint32_t buf, size, len; int64_t t0, t1; int sync, busy, have_time; } Samp;
#define NSAMP 3
enum { F_SRC, F_SPLIT, F_ACODEC, F_VCODEC, F_DSR };

struct Movie {
    plm_t *plm;                                   // video only ("movie" thread, core 1)
    plm_demux_t *adm;                             // audio: second demuxer on the same file ("movie_audio" thread, core 2)
    plm_audio_t *aud;
    char path[600];
    uint32_t graph_g, ctl_g, pos_g, ev_g, aud_g, sink_g;
    HObj *graph, *filters[5], *pins[10];
    uint32_t guest[4]; int nguest;
    void *dec[2];                                // SH3 .000 decrypt readers (MovieDec), one per plm buffer
    uint32_t notify_hwnd, notify_msg, notify_data;   // IMediaEventEx::SetNotifyWindow: posted once per queued event
    uint32_t renderer, rpin, mem_in, alloc;      // guest objects (alloc: host allocator)
    Samp *samp[NSAMP];
    uint32_t buf_size;
    SDL_Thread *thread, *athread, *othread;      // video decoding (core 1), audio and output (core 2)
    volatile int run, stop, ended, dec_done;
    // Output: decoding copies the YUV frame into yuv[otail % OQ]; the output thread converts and calls Receive.
    SDL_sem *osem;
#define OQ 5                                  // decoded frames waiting for the output thread (FIFO: a burst of decoded frames is not dropped)
    uint8_t *yuv[OQ];
    plm_frame_t of[OQ];
    unsigned ohead, otail;                        // frames read / written so far (slot = n % OQ)
    int obusy;
    int oq[OQ];                                 // queued slot IDs; never recycle the consumer's slot
    unsigned odropped;
    MovieStream *stream;
    MovieCursor cursor[2];
    int state;                                    // 0 stopped, 1 paused, 2 playing
    int w, h;
    SDL_mutex *lock;
    struct { uint32_t code, p1, p2; } ev[16];
    int nev;
    int nsink;                                      // IMediaEventSink::Notify calls (EC_COMPLETE safety net)
    int vol;                                      // hundredths of a dB (0 = full, -10000 = mute)
    CPU *dec_cpu;
    double st_dec, st_conv, st_recv;              // [movie] stats per 2 s window (cumulative ms)
    int st_ndec, st_npush, st_nskip;
};
static Movie *g_mv;
int g_movie_w, g_movie_h;                         // size delivered to the game: identifies the movie texture (resource.c)

// ---------- audio: ring buffer consumed by the dsound.c mixer ----------
static float g_ring[48000 * 2 * 4];               // 4 s stereo
static volatile uint32_t g_ring_w, g_ring_r;
static volatile uint32_t g_movie_audio_played; // diagnostic clock, stereo frames actually consumed
static SDL_mutex *g_ring_lock;
static volatile int g_audio_gain_q8 = 256;

void movie_audio_push(const float *il, int frames, int rate) {
    if (!g_ring_lock) g_ring_lock = SDL_CreateMutex();
    SDL_LockMutex(g_ring_lock);
    const uint32_t cap = sizeof g_ring / sizeof g_ring[0];
    static double acc;
    double step = (double)rate / 48000.0, pos = acc;
    while (pos < frames) {
        uint32_t i = (uint32_t)pos;
        float t = (float)(pos - i);
        uint32_t j = i + 1 < (uint32_t)frames ? i + 1 : i;
        if (((g_ring_w + 2) % cap) == g_ring_r) break; // full: the surplus is dropped
        g_ring[g_ring_w] = il[2 * i] * (1 - t) + il[2 * j] * t;
        g_ring[(g_ring_w + 1) % cap] = il[2 * i + 1] * (1 - t) + il[2 * j + 1] * t;
        g_ring_w = (g_ring_w + 2) % cap;
        pos += step;
    }
    acc = pos - frames;
    SDL_UnlockMutex(g_ring_lock);
}
void movie_audio_flush(void) {
    if (!g_ring_lock) g_ring_lock = SDL_CreateMutex();
    SDL_LockMutex(g_ring_lock);
    g_ring_r = g_ring_w;
    g_movie_audio_played = 0;
    SDL_UnlockMutex(g_ring_lock);
}
// Called by the mixer (n stereo frames): adds into acc[2n].
void movie_audio_mix(float *acc, int n) {
    if (!g_ring_lock || g_ring_r == g_ring_w) return;
    const uint32_t cap = sizeof g_ring / sizeof g_ring[0];
    float g = g_audio_gain_q8 / 256.0f;
    SDL_LockMutex(g_ring_lock);
    uint32_t before = g_ring_r;
    for (int i = 0; i < n && g_ring_r != g_ring_w; i++) {
        acc[2 * i] += g_ring[g_ring_r] * g;
        acc[2 * i + 1] += g_ring[(g_ring_r + 1) % cap] * g;
        g_ring_r = (g_ring_r + 2) % cap;
    }
    g_movie_audio_played += ((g_ring_r + cap - before) % cap) / 2;
    SDL_UnlockMutex(g_ring_lock);
}

// ---------- calls into the guest code ----------
static uint32_t gcall(CPU *c, uint32_t obj, int slot, int nargs, ...) {
    HOSTFN;
    uint32_t args[10], fn = R32(R32(obj) + 4u * (uint32_t)slot);
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) args[i] = va_arg(ap, uint32_t);
    va_end(ap);
    switch (nargs) {
    case 0: return rt_guest_call(c, fn, 1, obj);
    case 1: return rt_guest_call(c, fn, 2, obj, args[0]);
    case 2: return rt_guest_call(c, fn, 3, obj, args[0], args[1]);
    case 3: return rt_guest_call(c, fn, 4, obj, args[0], args[1], args[2]);
    default: return rt_guest_call(c, fn, 5, obj, args[0], args[1], args[2], args[3]);
    }
}
static void put_guid(CPU *c, uint32_t p, uint32_t d1, uint16_t d2, uint16_t d3, const uint8_t d4[8]) {
    HOSTFN;
    W32(p, d1); W16(p + 4, d2); W16(p + 6, d3); memcpy(mb + p + 8, d4, 8);
}

// ---------- host COM object factory ----------
static uint32_t mkobj(Movie *m, int kind, const gfn *vt, int n, const char *iface, HObj **out) {
    HObj *o = calloc(1, sizeof *o);
    o->m = m;
    uint32_t g = com_wrap(&o->com, kind, vt, n, iface);
    if (out) *out = o;
    return g;
}
#define SELF HObj *self = (HObj *)com_get(ARG(1)); (void)self
M(any, QI) { STDRET(3, E_NOINTERFACE_); }
M(any, AddRef) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? (uint32_t)++o->refs : 1); }
M(any, Release) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o && o->refs > 1 ? (uint32_t)--o->refs : 1); }
static void rpush(CPU *c, uint32_t code, uint32_t p1, uint32_t p2) {
    (void)c;
    Movie *m = g_mv;
    if (!m) return;
    SDL_LockMutex(m->lock);
    if (m->nev < 16) { m->ev[m->nev].code = code; m->ev[m->nev].p1 = p1; m->ev[m->nev].p2 = p2; m->nev++; }
    uint32_t hwnd = m->notify_hwnd, msg = m->notify_msg, data = m->notify_data;
    SDL_UnlockMutex(m->lock);
    // SH3 reads its movie events from its window procedure when this message arrives (EC_COMPLETE included):
    // without it the game waits forever once a movie ends.
    if (hwnd && msg) { void w32_post_message(uint32_t, uint32_t, uint32_t, uint32_t); w32_post_message(hwnd, msg, 0, data); }
}

// ---------- host pins (IPin) and enumerator ----------
M(pin, QueryDirection) { HOSTFN; SELF; W32(ARG(2), (uint32_t)self->dir); STDRET(2, HR_OK); }
M(pin, QueryId) {
    HOSTFN; SELF;
    W32(ARG(2), 0);
    size_t n = strlen(self->id) + 1;
    uint32_t w = gheap_alloc((uint32_t)n * 2);
    if (!w) STDRET(2, 0x8007000eu); // E_OUTOFMEMORY
    for (size_t i = 0; i < n; i++) W16(w + 2 * i, (uint8_t)self->id[i]);
    W32(ARG(2), w);
    STDRET(2, HR_OK);
}
M(pin, QueryPinInfo) {
    HOSTFN; SELF;
    uint32_t p = ARG(2);
    memset(mb + p, 0, 8 + 128);
    W32(p, 0); W32(p + 4, (uint32_t)self->dir);
    for (size_t i = 0; self->id[i] && i < 60; i++) W16(p + 8 + 2 * i, (uint8_t)self->id[i]);
    STDRET(2, HR_OK);
}
M(pin, ConnectedTo) { HOSTFN; SELF; W32(ARG(2), self->peer); STDRET(2, self->peer ? HR_OK : 0x80040209u); }
M(pin, Disconnect) { HOSTFN; SELF; self->peer = 0; STDRET(1, HR_OK); }
M(pin, Ok1) { STDRET(2, HR_OK); }
M(pin, Ok2) { STDRET(3, HR_OK); }
M(pin, NotImpl2) { STDRET(2, E_NOTIMPL_); }
M(pin, NotImpl3) { STDRET(3, E_NOTIMPL_); }
M(pin, Ok0) { STDRET(1, HR_OK); }
M(pin, NewSeg) { STDRET(6, HR_OK); }
static const gfn pin_vt[] = {any_QI, any_AddRef, any_Release, pin_Ok2 /*Connect*/, pin_Ok2 /*ReceiveConnection*/,
    pin_Disconnect, pin_ConnectedTo, pin_NotImpl2 /*ConnectionMediaType*/, pin_QueryPinInfo, pin_QueryDirection,
    pin_QueryId, pin_Ok1 /*QueryAccept*/, pin_NotImpl2 /*EnumMediaTypes*/, pin_NotImpl3 /*QueryInternalConnections*/,
    pin_Ok0 /*EndOfStream*/, pin_Ok0, pin_Ok0, pin_NewSeg};

typedef struct { uint32_t pins[4]; int n, pos; } EnumState;
M(en, Next) {
    HOSTFN;
    HObj *self = (HObj *)com_get(ARG(1));
    EnumState *e = (EnumState *)(uintptr_t)self->peer;
    uint32_t want = ARG(2), out = ARG(3), got = 0;
    while (got < want && e->pos < e->n) {
        uint32_t p = e->pins[e->pos++];
        d3d_addref(com_get(p));
        W32(out + 4 * got, p);
        got++;
    }
    if (ARG(4)) W32(ARG(4), got);
    STDRET(4, got == want ? HR_OK : S_FALSE_);
}
M(en, Skip) { STDRET(2, HR_OK); }
M(en, Reset) { HOSTFN; HObj *self = (HObj *)com_get(ARG(1)); ((EnumState *)(uintptr_t)self->peer)->pos = 0; STDRET(1, HR_OK); }
M(en, Clone) { STDRET(2, E_NOTIMPL_); }
static const gfn en_vt[] = {any_QI, any_AddRef, any_Release, en_Next, en_Skip, en_Reset, en_Clone};

// ---------- host filters (IBaseFilter) ----------
M(flt, EnumPins) {
    HOSTFN; SELF;
    EnumState *e = calloc(1, sizeof *e);
    for (int i = 0; i < 10; i++)
        if (self->m->pins[i] && self->m->pins[i]->com.kind == MK_PIN && self->m->pins[i]->role == self->role)
            e->pins[e->n++] = self->m->pins[i]->com.guest;
    HObj *eo;
    uint32_t g = mkobj(self->m, MK_ENUM, en_vt, 7, "IEnumPins", &eo);
    eo->peer = (uint32_t)(uintptr_t)e;
    W32(ARG(2), g);
    STDRET(2, HR_OK);
}
M(flt, Ok1) { STDRET(1, HR_OK); }
M(flt, Ok2) { STDRET(2, HR_OK); }
M(flt, Ok3) { STDRET(3, HR_OK); }
M(flt, GetState) { HOSTFN; W32(ARG(3), 0); STDRET(3, HR_OK); }
M(flt, FindPin) { HOSTFN; W32(ARG(3), 0); STDRET(3, 0x80040209u); }
M(flt, NI) { STDRET(2, E_NOTIMPL_); }
M(flt, QueryFilterInfo) { HOSTFN; memset(mb + ARG(2), 0, 4 + 128 + 4); STDRET(2, HR_OK); }
M(flt, Join) { STDRET(3, HR_OK); }
static const gfn flt_vt[] = {any_QI, any_AddRef, any_Release, flt_NI /*GetClassID*/, flt_Ok1 /*Stop*/, flt_Ok1 /*Pause*/,
    flt_Ok3 /*Run (i64)*/, flt_GetState, flt_Ok2 /*SetSyncSource*/, flt_Ok2 /*GetSyncSource*/, flt_EnumPins,
    flt_FindPin, flt_QueryFilterInfo, flt_Join, flt_NI /*QueryVendorInfo*/};

static void make_filter(Movie *m, int which, int nin, int nout, const char *in_id, const char *out_ids[2]) {
    uint32_t g = mkobj(m, MK_FILTER, flt_vt, 15, "IBaseFilter", &m->filters[which]);
    m->filters[which]->role = which;
    (void)g;
    for (int i = 0; i < nin + nout; i++) {
        HObj *p;
        mkobj(m, MK_PIN, pin_vt, 18, "IPin", &p);
        p->role = which;
        p->dir = i < nin ? 0 : 1;
        p->id = i < nin ? in_id : out_ids[i - nin];
        for (int k = 0; k < 10; k++)
            if (!m->pins[k]) { m->pins[k] = p; break; }
    }
}


// ---------- memory allocator and samples (host side) ----------
#define MV Movie *m = ((HObj *)com_get(ARG(1)))->m
#define SAMP Samp *sp = (Samp *)com_get(ARG(1))
M(sm, AddRef) { HOSTFN; SAMP; STDRET(1, (uint32_t)++sp->com.refs); }
M(sm, Release) { HOSTFN; SAMP; int r = --sp->com.refs; if (r <= 0) { sp->com.refs = 0; sp->busy = 0; r = 0; } STDRET(1, (uint32_t)r); }
M(sm, GetPointer) { HOSTFN; SAMP; W32(ARG(2), sp->buf); STDRET(2, HR_OK); }
M(sm, GetSize) { HOSTFN; SAMP; STDRET(1, sp->size); }
M(sm, GetTime) {
    HOSTFN; SAMP;
    if (!sp->have_time) STDRET(3, 0x80040249u);
    W32(ARG(2), (uint32_t)sp->t0); W32(ARG(2) + 4, (uint32_t)((uint64_t)sp->t0 >> 32));
    W32(ARG(3), (uint32_t)sp->t1); W32(ARG(3) + 4, (uint32_t)((uint64_t)sp->t1 >> 32));
    STDRET(3, HR_OK);
}
M(sm, SetTime) {
    HOSTFN; SAMP;
    sp->t0 = (int64_t)(((uint64_t)R32(ARG(2) + 4) << 32) | R32(ARG(2)));
    sp->t1 = (int64_t)(((uint64_t)R32(ARG(3) + 4) << 32) | R32(ARG(3)));
    sp->have_time = 1;
    STDRET(3, HR_OK);
}
M(sm, IsSync) { HOSTFN; SAMP; STDRET(1, sp->sync ? HR_OK : S_FALSE_); }
M(sm, SetSync) { HOSTFN; SAMP; sp->sync = (int)ARG(2); STDRET(2, HR_OK); }
M(sm, False1) { STDRET(1, S_FALSE_); }
M(sm, Ok2) { STDRET(2, HR_OK); }
M(sm, GetLen) { HOSTFN; SAMP; STDRET(1, sp->len); }
M(sm, SetLen) { HOSTFN; SAMP; sp->len = ARG(2); STDRET(2, HR_OK); }
M(sm, GetMT) { HOSTFN; W32(ARG(2), 0); STDRET(2, S_FALSE_); }
M(sm, GetMediaTime) { STDRET(3, 0x80040251u); }
M(sm, Ok3) { STDRET(3, HR_OK); }
static const gfn sm_vt[] = {any_QI, sm_AddRef, sm_Release, sm_GetPointer, sm_GetSize, sm_GetTime, sm_SetTime,
    sm_IsSync, sm_SetSync, sm_False1 /*IsPreroll*/, sm_Ok2, sm_GetLen, sm_SetLen, sm_GetMT, sm_Ok2 /*SetMediaType*/,
    sm_False1 /*IsDiscontinuity*/, sm_Ok2, sm_GetMediaTime, sm_Ok3};

#include "movie_sample_reserve.h"
M(al, SetProperties) {
    HOSTFN; MV;
    uint32_t req = ARG(2), act = ARG(3);
    uint32_t sz = R32(req + 4);
    static uint32_t sbuf[NSAMP], scap[NSAMP];
    Samp *new_samples[NSAMP] = {0};
    for (int i = 0; i < NSAMP; ++i)
        if (m->samp[i] && m->samp[i]->busy) STDRET(3, HR_FAIL);
    for (int i = 0; i < NSAMP; ++i) if (!m->samp[i]) {
        new_samples[i] = calloc(1, sizeof(Samp));
        if (!new_samples[i]) {
            for (int j = 0; j < i; ++j) free(new_samples[j]);
            STDRET(3, 0x8007000eu);
        }
    }
    if (!movie_samples_reserve(sbuf, scap, NSAMP, sz)) {
        for (int i = 0; i < NSAMP; ++i) free(new_samples[i]);
        STDRET(3, 0x8007000eu);
    }
    m->buf_size = sz;
    for (int i = 0; i < NSAMP; i++) {
        if (!m->samp[i]) {
            m->samp[i] = new_samples[i];
            m->samp[i]->m = m;
            com_wrap(&m->samp[i]->com, MK_SAMP, sm_vt, 19, "IMediaSample");
        }
        m->samp[i]->buf = sbuf[i];
        m->samp[i]->size = sz;
    }
    if (act) { W32(act, NSAMP); W32(act + 4, sz); W32(act + 8, R32(req + 8)); W32(act + 12, 0); }
    STDRET(3, HR_OK);
}
M(al, GetProperties) { HOSTFN; MV; W32(ARG(2), NSAMP); W32(ARG(2) + 4, m->buf_size); W32(ARG(2) + 8, 1); W32(ARG(2) + 12, 0); STDRET(2, HR_OK); }
M(al, Ok1) { STDRET(1, HR_OK); }
M(al, GetBuffer) {
    HOSTFN; MV;
    for (int tries = 0; tries < 500; tries++) {
        for (int i = 0; i < NSAMP; i++)
            if (m->samp[i] && !m->samp[i]->busy) {
                Samp *sp = m->samp[i];
                sp->busy = 1; sp->com.refs = 1; sp->have_time = 0; sp->len = 0; sp->sync = 0;
                W32(ARG(2), sp->com.guest);
                STDRET(5, HR_OK);
            }
        SDL_Delay(2);
    }
    W32(ARG(2), 0);
    STDRET(5, HR_FAIL);
}
M(al, ReleaseBuffer) { HOSTFN; Samp *sp = (Samp *)com_get(ARG(2)); if (sp) sp->busy = 0; STDRET(2, HR_OK); }
static const gfn al_vt[] = {any_QI, any_AddRef, any_Release, al_SetProperties, al_GetProperties, al_Ok1 /*Commit*/,
    al_Ok1 /*Decommit*/, al_GetBuffer, al_ReleaseBuffer};

// ---------- decoding and delivery ----------
static uint32_t guest_tmp(void) {
    static uint32_t t;
    if (!t) t = gheap_calloc(256);
    return t;
}

#ifdef __ARM_NEON
#include <arm_neon.h>
// YUV 4:2:0 -> BGRA (or R5G6B5 if p565) in NEON (16 pixels x 2 rows per iteration), BT.601 in 6-bit fixed point (differs by a few
// units from plm_frame_to_bgra, cf. SH4_MOVIE_BENCH). Alpha = 255.
static inline __attribute__((always_inline)) void frame_conv(plm_frame_t *f, uint8_t *dst, int stride, const int p565) {
    int w = (int)f->width, h = (int)f->height, yw = (int)f->y.width, cw = (int)f->cb.width;
    const int16x8_t c128 = vdupq_n_s16(128), yoff = vdupq_n_s16(1192);
    const uint8x8_t k149 = vdup_n_u8(149);
    for (int j = 0; j < h; j += 2) {
        const uint8_t *cb = f->cb.data + (j >> 1) * cw, *cr = f->cr.data + (j >> 1) * cw;
        for (int i = 0; i < w; i += 16) {
            int16x8_t u = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(cb + i / 2))), c128);
            int16x8_t v = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(cr + i / 2))), c128);
            int16x8x2_t rr = vzipq_s16(vmulq_n_s16(v, 102), vmulq_n_s16(v, 102));
            int16x8_t g = vmlaq_n_s16(vmulq_n_s16(u, -25), v, -52), b = vmulq_n_s16(u, 129);
            int16x8x2_t gg = vzipq_s16(g, g), bb = vzipq_s16(b, b);
            for (int k = 0; k < 2; k++) {
                uint8x16_t yv = vld1q_u8(f->y.data + (j + k) * yw + i);
                uint8_t *d = dst + (j + k) * stride + i * (p565 ? 2 : 4);
                for (int hf = 0; hf < 2; hf++) {
                    uint8x8_t y8 = hf ? vget_high_u8(yv) : vget_low_u8(yv);
                    int16x8_t yy = vsubq_s16(vreinterpretq_s16_u16(vshrq_n_u16(vmull_u8(y8, k149), 1)), yoff);   // (Y-16)*74.5
                    uint8x8x4_t px;
                    px.val[0] = vqrshrun_n_s16(vqaddq_s16(yy, bb.val[hf]), 6);
                    px.val[1] = vqrshrun_n_s16(vqaddq_s16(yy, gg.val[hf]), 6);
                    px.val[2] = vqrshrun_n_s16(vqaddq_s16(yy, rr.val[hf]), 6);
                    if (p565) {   // R<<8, then insert G>>5 and B>>11 -> R5G6B5
                        uint16x8_t q = vsriq_n_u16(vshll_n_u8(px.val[2], 8), vshll_n_u8(px.val[1], 8), 5);
                        vst1q_u16((uint16_t *)(d + hf * 16), vsriq_n_u16(q, vshll_n_u8(px.val[0], 8), 11));
                    } else {
                        px.val[3] = vdup_n_u8(255);
                        vst4_u8(d + hf * 32, px);
                    }
                }
            }
        }
    }
}
static void frame_to_bgra(plm_frame_t *f, uint8_t *dst, int stride) {
    if (((int)f->width | (int)f->height) & 15) plm_frame_to_bgra(f, dst, stride);
    else frame_conv(f, dst, stride, 0);
}

// Movie (Vita): the frame is also produced as R5G6B5 (top-down) in one of 3 rotating buffers, converted here (core 2); the main thread
// only has to hand it to the render thread (resource.c, tex_upload). busy: 1 = being written or waiting for upload (reset to 0 by the renderer).
#define N565 6
static uint16_t *s_m565[N565];
static volatile int s_m565_busy[N565];
static int s_m565_q[N565];                    // FIFO of complete buffers in display order (q[n % N565]); a buffer is free when not busy and not queued
static unsigned s_m565_qh, s_m565_qt, s_m565_w, s_m565_h;
static unsigned s_m565_dropped;               // converted frames dropped because the display was too far behind
static SDL_mutex *s_m565_mx;
static double s_m565_ms;
static unsigned s_m565_n;
static double s_m565_pts[N565];
static uint64_t s_m565_ready[N565];
static unsigned s_m565_serial;
static volatile int s_m565_consumer;      // the renderer took at least one 565 frame for this movie
static int s_m565_offer;                  // frames offered since the movie started (probe window)
static void m565_begin(void) {
    if (!s_m565_mx) return;
    SDL_LockMutex(s_m565_mx);
    s_m565_qh = s_m565_qt; // discard only queued frames, never buffers owned by the renderer
    s_m565_dropped = 0;
    s_m565_consumer = 0; s_m565_offer = 0;
    SDL_UnlockMutex(s_m565_mx);
}
static int m565_queued(int k) { for (unsigned n = s_m565_qh; n != s_m565_qt; n++) if (s_m565_q[n % N565] == k) return 1; return 0; }
static void m565_put(Movie *m, plm_frame_t *f) {
    if (((int)f->width | (int)f->height) & 15) return;   // ponytail: sizes not a multiple of 16: fall back to the main thread conversion
    if (!s_m565_mx) s_m565_mx = SDL_CreateMutex();
    if (!s_m565_w) { s_m565_w = (unsigned)m->w; s_m565_h = (unsigned)m->h; }
    if (s_m565_w != (unsigned)m->w || s_m565_h != (unsigned)m->h) return;   // ponytail: buffers sized for the first movie (all 640x480)
    // SH3's movie texture is 512x512 (the game's own), not the 512x384 frame, so resource.c's size test (t->movie)
    // never matches and movie_565_take is never called (console: no "[movie] display" line in 30+ s of op.000):
    // converting every frame to 565 on the output thread was pure waste. Offer a probe window of 30 frames;
    // if the renderer never takes one, stop converting for this movie.
    if (!s_m565_consumer && ++s_m565_offer > 30) return;
    int k = 0;
    SDL_LockMutex(s_m565_mx);
    while (k < N565 && (s_m565_busy[k] || m565_queued(k))) k++;
    if (k < N565) s_m565_busy[k] = 1;
    else s_m565_dropped++;
    SDL_UnlockMutex(s_m565_mx);
    if (k == N565) return;   // the renderer did not return the buffers: frame skipped
    if (!s_m565[k] && !(s_m565[k] = malloc((size_t)m->w * m->h * 2))) { s_m565_busy[k] = 0; return; }
    uint64_t t0 = SDL_GetPerformanceCounter();
    s_m565_pts[k] = f->time;
    frame_conv(f, (uint8_t *)s_m565[k], m->w * 2, 1);
    s_m565_ms += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    SDL_LockMutex(s_m565_mx);
    s_m565_ready[k] = SDL_GetPerformanceCounter();
    s_m565_q[s_m565_qt % N565] = k; s_m565_qt++;
    s_m565_busy[k] = 0;
    SDL_UnlockMutex(s_m565_mx);
    if (!(++s_m565_n % 75)) { rt_log("[movie] 565 conversion (movie thread): %.1f ms\n", s_m565_ms / 75); s_m565_ms = 0; }
}
// Main thread: next 565 frame in display order not yet uploaded (NULL: nothing new). One frame per call, so every decoded frame is shown;
// if more than 3 are waiting the oldest are dropped (latency stays under ~100 ms). The buffer stays reserved until *busy drops to 0.
uint16_t *movie_565_take(uint32_t w, uint32_t h, volatile int **busy) {
    uint16_t *p = NULL;
    if (!s_m565_mx) return NULL;
    SDL_LockMutex(s_m565_mx);
    if (s_m565_qh != s_m565_qt && w == s_m565_w && h == s_m565_h) {
        while (s_m565_qt - s_m565_qh > 3) { s_m565_qh++; s_m565_dropped++; }
        int k = s_m565_q[s_m565_qh % N565]; s_m565_qh++;
        s_m565_busy[k] = 1;
        s_m565_consumer = 1;   // the renderer does use the 565 frames: keep converting (m565_put)
        {   // diagnostics: order and regularity of what is shown (windows of 120 presents)
            extern unsigned g_pframe;
            static double last_pts = -1; static unsigned win0, ntake, nback, maxgap, lastf;
            if (s_m565_serial != s_movie_serial) {
                last_pts = -1; win0 = g_pframe; ntake = nback = maxgap = lastf = 0;
            }
            double pts = s_m565_pts[k];
            if (pts < last_pts - 1e-6) nback++;
            last_pts = pts; ntake++;
            if (lastf && g_pframe - lastf > maxgap) maxgap = g_pframe - lastf;
            lastf = g_pframe;
            if (g_pframe - win0 >= 120) { rt_log("[movie] display: %u new frames shown in %u presents, %u dropped, %u out of order, longest gap %u presents\n", ntake, g_pframe - win0, s_m565_dropped, nback, maxgap); win0 = g_pframe; ntake = nback = maxgap = 0; s_m565_dropped = 0; }
        }
        *busy = &s_m565_busy[k];
        p = s_m565[k];
        // Low-rate wall-clock diagnostics; Present counts alone hide long gaps.
        static uint64_t begin, previous, max_gap;
        static unsigned shown, gaps50, gaps100;
        static double max_queue_ms;
        uint64_t now = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency();
        int first = s_m565_serial != s_movie_serial;
        if (first) {
            s_m565_serial = s_movie_serial;
            begin = previous = now; max_gap = 0; shown = gaps50 = gaps100 = 0; max_queue_ms = 0;
        }
        uint64_t gap = now - previous; previous = now;
        if (gap > max_gap) max_gap = gap;
        if (gap * 1000 > hz * 50) gaps50++;
        if (gap * 1000 > hz * 100) gaps100++;
        double age = (double)(now - s_m565_ready[k]) * 1000.0 / (double)hz;
        if (age > max_queue_ms) max_queue_ms = age;
        if (!first) shown++;
        if (now - begin >= 2 * hz) {
            rt_log("[movie-flow] shown=%.2f/s max_gap_ms=%.2f gaps50=%u gaps100=%u queue_max_ms=%.2f pts=%.3f audio_minus_video_ms=%.2f\n",
                   shown * (double)hz / (double)(now - begin), max_gap * 1000.0 / (double)hz,
                   gaps50, gaps100, max_queue_ms, s_m565_pts[k],
                   g_movie_audio_played / 48.0 - s_m565_pts[k] * 1000.0);
            begin = now; shown = gaps50 = gaps100 = 0; max_gap = 0; max_queue_ms = 0;
        }
    }
    SDL_UnlockMutex(s_m565_mx);
    return p;
}
#else
#define frame_to_bgra plm_frame_to_bgra
#define m565_put(m, f) ((void)0)
#define m565_begin() ((void)0)
#endif

static void push_frame(Movie *m, CPU *c, plm_frame_t *f) {
    HOSTFN;
    Samp *sp = NULL;
    for (int i = 0; i < NSAMP && !sp; i++)
        if (m->samp[i] && !m->samp[i]->busy) sp = m->samp[i];
    if (!sp || !m->mem_in) {
        static int nlog; if (nlog++ < 5) rt_log("[movie] push skipped: sp=%p mem_in=%08x busy=%d%d%d\n", (void *)sp, m->mem_in,
            m->samp[0] ? m->samp[0]->busy : -1, m->samp[1] ? m->samp[1]->busy : -1, m->samp[2] ? m->samp[2]->busy : -1);
        return;                                                 // the renderer did not return its samples: we skip the frame
    }
    sp->busy = 1; sp->com.refs = 1;
    uint32_t pitch = (uint32_t)m->w * 4, size = pitch * (uint32_t)m->h;
    uint64_t t0 = SDL_GetPerformanceCounter();
    frame_to_bgra(f, mb + sp->buf + (size - pitch), -(int)pitch);   // DIB bas-haut
    m565_put(m, f);
    uint64_t t1 = SDL_GetPerformanceCounter();
    sp->len = size;
    sp->t0 = (int64_t)(f->time * 1e7);
    sp->t1 = sp->t0 + 333667;
    sp->have_time = 1;
    sp->sync = 1;
    uint32_t rhr = gcall(c, m->mem_in, 6, 1, sp->com.guest);    // IMemInputPin::Receive
    uint64_t t2 = SDL_GetPerformanceCounter();
    double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
    m->st_conv += (double)(t1 - t0) * ms; m->st_recv += (double)(t2 - t1) * ms; m->st_npush++;
    if ((int32_t)rhr < 0) rt_log("[movie] Receive -> %08x\n", rhr);
    sp->com.refs = 0;
    sp->busy = 0;
}

#ifdef __vita__
// ===================== sceAvPlayer path (preconverted MP4, hardware decoder) =====================
// tools/convert_movies.py makes <name>.mp4 (H.264 High + AAC-LC 48 k) from the user's own <name>.000.
// When that file sits next to the .000 the port plays it with sceAvPlayer instead of pl_mpeg: the
// hardware decoder decodes, a pull thread feeds the audio into the same mixer ring (movie_audio_mix),
// and the video frames go to a vitaGL YUV texture (NV12, hardware CSC). The game's movie quad (its
// own 512x512 texture, which receives no frames in this path) is replaced at record time in prep_draw
// by a fullscreen video quad, so the whrlays drawn after it stay on top. The graph contract is kept:
// Run/Pause/Stop drive the guest renderer, EndOfStream -> EC_COMPLETE, position queries answer from
// the player clock. SH3_MV_MP4=0 disables the path, SH3_MV_FEED=1 keeps feeding the guest renderer
// black samples (if the game stops drawing its movie quad without frames, the console test will show
// "at Present" counts and this knob restores the old renderer behaviour at the cost of one memcpy).
#include <psp2/avplayer.h>
#include <psp2/apputil.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/sysmodule.h>
#include <malloc.h>
#include "prep.h"

int g_movie_avplayer;                       // 1 while the current movie plays through sceAvPlayer

enum { AV_EV_ERROR = 1, AV_EV_PLAYER_END = 2, AV_EV_STREAM_CREATED = 4, AV_EV_FLUSHED = 8,
       AV_EV_SEEK_OVERLOADED = 16, AV_EV_AUDIO_TRACK_END = 32, AV_EV_VIDEO_TRACK_END = 64 };

static SceAvPlayerHandle s_h = -1;
// sceAvPlayerInit returns the handle on success, < 0 on error — but the handle is a pointer and the Vita
// heap lives at 0x8x..., so a valid handle is negative as int32 (real Vita: 0x82a2b600; Vita3K's emulated
// module does the same). Only real SCE errors (0x806Axxxx, the avplayer module range) mean failure.
static int av_h_ok(int h) { return h >= 0 || (h & 0xFFFF0000) != 0x806A0000; }
static int s_fd_slot = -1;                  // the file replacement stores its descriptor here
static volatile int s_av_end;               // PLAYER_END event seen
static double s_dur;                        // duration, seconds
static int s_vw, s_vh;                      // video size
static unsigned long long s_vframes;        // frames pulled since Start
static unsigned long long s_vframes_total;  // frames pulled since AddSource — never zeroed (EOF guard)
static int s_autostart;                     // init.autoStart was 1: AddSource starts playback, never call Start
static int s_feed;                          // SH3_MV_FEED: keep the guest renderer fed with black frames
static uint8_t *s_feed_buf;                 // the black DIB (allocated once)

// ---- file replacement: the player reads the mp4 itself through these ----
static int avf_open(void *p, const char *fn) {
    *(int *)p = sceIoOpen(fn, SCE_O_RDONLY, 0);
    rt_log("[movie] avf_open %s -> fd %d\n", fn, *(int *)p);
    return *(int *)p < 0 ? -1 : 0;
}
static int avf_close(void *p) { rt_log("[movie] avf_close\n"); if (*(int *)p >= 0) sceIoClose(*(int *)p); *(int *)p = -1; return 0; }
static int avf_read(void *p, uint8_t *buf, uint64_t pos, uint32_t len) {
    static int logged;
    int fd = *(int *)p;
    if (sceIoLseek(fd, (SceOff)pos, SCE_SEEK_SET) < 0) { rt_log("[movie] avf_read seek %llu failed\n", (unsigned long long)pos); return -1; }
    uint32_t n = 0;
    while (n < len) { ssize_t r = sceIoRead(fd, buf + n, len - n); if (r <= 0) break; n += (uint32_t)r; }
    if (logged < 40) { logged++; rt_log("[movie] avf_read %d: pos %llu len %u -> %u\n", logged, (unsigned long long)pos, len, n); }
    return (int)n;
}
static uint64_t avf_size(void *p) {
    uint64_t s = (uint64_t)sceIoLseek(*(int *)p, 0, SCE_SEEK_END);
    static int logged;
    if (!logged) { logged = 1; rt_log("[movie] avf_size -> %llu\n", (unsigned long long)s); }
    return s;
}

static void *av_alloc(void *a, uint32_t align, uint32_t size) {
    void *p = memalign(align < 16 ? 16 : align, size);
    static int n;
    if (!p) rt_log("[movie] avplayer: allocate %u B align %u FAILED (newlib heap)\n", size, align);
    else if (n++ < 12) rt_log("[movie] avplayer: allocate %u B align %u ok\n", size, align);
    return p;
}
static void av_free(void *a, void *p) { free(p); }
// The video frame buffers must be physically contiguous and GPU-mapped: the hardware H.264 decoder writes its
// NV12 output there, and plain main-RAM buffers make the module fail with an error event right after the first
// reads (real Vita, 2026-11-14). Two recipes, switched by SH3_MV_TEX:
//  "phy" (default): vitaGL's video_playback sample — USER_MAIN_PHYCONT_NC_RW memblock, size aligned to 1 MB.
//  "cd": OpenFMV's gpu_alloc (hardware-confirmed by the PSVita-Dev-Wiki author: GetVideoData never yields a
//        frame unless the buffer is a raw USER_CDRAM_RW block + sceGxmMapMemory RW): attr 0x4, align 0x40000.
// Needs free memory of that kind: main.c leaves SH4_PHYCONT_MB / SH4_CDRAM_KEEP_MB free after vitaGL.
// The chosen recipe is tried first, then the other contiguous pool, then plain main RAM: returning NULL from
// allocateTexture kills the player with an error event (real Vita, run 12: an 8 MB request failed with only
// 4 MB of CDRAM free; OpenFMV leaves 32 MB).
static int av_tex_cd;                        // 1: prefer the CDRAM recipe (set from SH3_MV_TEX=cd in av_open)
#define AV_TEX_MAX 24
static struct { void *base; SceUID blk; } s_tex[AV_TEX_MAX];  // blk < 0: plain malloc block
static void av_tex_track(void *base, SceUID blk) {
    for (int i = 0; i < AV_TEX_MAX; i++) if (!s_tex[i].base) { s_tex[i].base = base; s_tex[i].blk = blk; return; }
    rt_log("[movie] avplayer: tex table overflow %p\n", base);
}
static void *av_alloc_tex(void *a, uint32_t align, uint32_t size) {
    (void)a;
    struct texkind { int type; uint32_t attr; const char *name; } kinds[2] = {
        { SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, 0, "phycont" },
        { SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x00000004u, "cdram" },
    };
    int order[3] = { av_tex_cd, 1 - av_tex_cd, -1 };   // chosen recipe, the other pool, main RAM
    for (int i = 0; i < 3; i++) {
        uint32_t sz = size;
        if (order[i] < 0) {                                  // main RAM: the decoder may not use it, but better than NULL
            void *p = memalign(align < 16 ? 16 : align, size);
            if (p) { av_tex_track(p, -1); rt_log("[movie] avplayer: tex %u B -> main RAM\n", size); return p; }
            continue;
        }
        struct texkind *k = &kinds[order[i]];
        SceUID blk;
        if (order[i] == 1) {                                 // cdram: OpenFMV's gpu_alloc (attr 0x4, align >= 0x40000)
            if (align < 0x40000) align = 0x40000;
            sz = (size + align - 1) & ~(align - 1);
            SceKernelAllocMemBlockOpt opt;
            memset(&opt, 0, sizeof opt);
            opt.size = sizeof opt;
            opt.attr = k->attr;
            opt.alignment = align;
            blk = sceKernelAllocMemBlock("mv_tex", k->type, sz, &opt);
        } else {                                             // phycont: sample's alloc_for_gpu (1 MB alignment)
            sz = (size + 0xFFFFFu) & ~0xFFFFFu;
            blk = sceKernelAllocMemBlock("mv_tex", k->type, sz, NULL);
        }
        if (blk < 0) { rt_log("[movie] avplayer: alloc_tex %u B %s -> memblock %08x\n", sz, k->name, (unsigned)blk); continue; }
        void *base = NULL;
        int r = sceKernelGetMemBlockBase(blk, &base);
        if (r < 0 || !base) { rt_log("[movie] avplayer: alloc_tex %u B %s -> base %08x\n", sz, k->name, (unsigned)r); sceKernelFreeMemBlock(blk); continue; }
        r = sceGxmMapMemory(base, sz, SCE_GXM_MEMORY_ATTRIB_RW);
        if (r < 0) { rt_log("[movie] avplayer: alloc_tex %u B %s -> gxmMapMemory %08x\n", sz, k->name, (unsigned)r); sceKernelFreeMemBlock(blk); continue; }
        av_tex_track(base, blk);
        rt_log("[movie] avplayer: tex %u B %s ok\n", sz, k->name);
        return base;
    }
    rt_log("[movie] avplayer: alloc_tex %u B all pools FAILED\n", size);
    return NULL;
}
static void *s_tex_dead[AV_TEX_MAX]; static SceUID s_tex_dead_blk[AV_TEX_MAX];  // freed by the module thread, reaped on the game thread
static void av_free_tex(void *a, void *p) {
    (void)a;
    if (!p) return;
    // called from the avplayer module thread: GL calls are illegal here (run 13: "GL call from another thread").
    // unlink now, unmap/free on the next Present (game thread), like the queue's other cross-thread handoffs.
    for (int i = 0; i < AV_TEX_MAX; i++) if (s_tex[i].base == p) {
        SceUID blk = s_tex[i].blk;
        s_tex[i].base = NULL;
        if (blk < 0) { free(p); return; }
        for (int j = 0; j < AV_TEX_MAX; j++) if (!s_tex_dead[j]) { s_tex_dead[j] = p; s_tex_dead_blk[j] = blk; return; }
        rt_log("[movie] avplayer: free_tex %p dead list full\n", p);
        return;
    }
    rt_log("[movie] avplayer: free_tex %p not in table\n", p);
}
static void av_reap_tex(void) {   // game thread (Present): q_sync + glFinish are legal here
    for (int j = 0; j < AV_TEX_MAX; j++) {
        void *p = s_tex_dead[j];
        if (!p) continue;
        s_tex_dead[j] = NULL;
        q_sync("av_free_tex");
        glFinish();                                                 // GPU idle before unmapping (sample's free_for_gpu)
        int r1 = sceGxmUnmapMemory(p);
        int r2 = s_tex_dead_blk[j] >= 0 ? sceKernelFreeMemBlock(s_tex_dead_blk[j]) : (int)s_tex_dead_blk[j];
        if (r1 < 0 || r2 < 0) rt_log("[movie] avplayer: reap_tex %p unmap %08x free %08x\n", p, (unsigned)r1, (unsigned)r2);
    }
}
static void av_event(void *p, int32_t id, int32_t src, void *data) {
    // every event, once: the id sequence shows how far the player got (2 = stream created, 4 = player end, ...)
    rt_log("[movie] avplayer: event id %d src %d data %08x\n", id, src, data ? *(uint32_t *)data : 0u);
    if (id == AV_EV_PLAYER_END) s_av_end = 1;
}

// ---- video frames: the pull thread stages NV12 copies; the prep thread uploads the newest one ----
// The staging buffer uses vitaGL's planar layout: the Y plane padded to power-of-2 dimensions (nearest_po2),
// UV at offset padw*padh. GXM expects the UV plane at the tight w*h offset of the UPLOADED size, and vitaGL
// pads the plane before copying (gpu_alloc_planar_texture), so a tight 512x384 frame uploaded as 512x384 puts
// its UV where GXM reads garbage (wrong colors + green, real Vita 2026-11-14). Uploading as 512x512 padded
// makes both layouts agree; the quad samples only the used 0..vw/padw, 0..vh/padh region.
#define AVQ 3
static uint8_t *s_av_buf[AVQ];
static int s_padw, s_padh;                    // padded texture dimensions (po2 of s_vw/s_vh), set in av_create_vtex
// CSC1 (vitaGL's "BT709" mapping) is the matrix the hardware-confirmed OpenFMV uses for sceAvPlayer output;
// SH3_MV_CSC=0 switches to CSC0 ("BT601") to compare tints without rebuilding.
static GLenum s_yuv_fmt = VGL_YUV420P_NV12_BT709;
static volatile int s_av_busy[AVQ];         // 1: owned by the consumer (queued or being uploaded)
static int s_av_q[AVQ];
static volatile unsigned s_av_qh, s_av_qt;
static unsigned s_av_seq;                  // frames staged
static volatile int s_av_pick;             // 1 from Present entry until this Presented frame consumed its staged frame
static volatile unsigned s_av_late;        // staged frames dropped by catch-up (consumer behind)
static SDL_mutex *s_av_mx;
static double s_av_up_ms;
static unsigned s_av_up_n;

static void av_stage(SceAvPlayerFrameInfo *vi) {
    uint32_t w = vi->details.video.width, h = vi->details.video.height;
    if ((int)w != s_vw || (int)h != s_vh) return;                       // unexpected frame geometry: drop
    size_t ysz = (size_t)w * h;
    size_t uvoff = (size_t)s_padw * s_padh, psz = uvoff * 3 / 2;
    int k = -1;
    SDL_LockMutex(s_av_mx);
    for (int i = 0; i < AVQ; i++) {
        int qd = 0;
        for (unsigned n = s_av_qh; n != s_av_qt; n++) if (s_av_q[n % AVQ] == i) { qd = 1; break; }
        if (!s_av_busy[i] && !qd) { k = i; break; }
    }
    if (k >= 0) s_av_busy[k] = 1;
    SDL_UnlockMutex(s_av_mx);
    if (k < 0) return;                                                  // the consumer is behind: drop
    if (!s_av_buf[k] && !(s_av_buf[k] = malloc(psz))) { s_av_busy[k] = 0; return; }
    const uint8_t *src = (const uint8_t *)vi->pData;                    // NV12: Y plane then interleaved UV
    if ((int)w == s_padw) {                                             // our case: only the height needs padding
        memcpy(s_av_buf[k], src, ysz);
        memcpy(s_av_buf[k] + uvoff, src + ysz, ysz / 2);
    } else {                                                            // rows must be re-padded one by one
        for (uint32_t y = 0; y < h; y++) memcpy(s_av_buf[k] + (size_t)y * s_padw, src + (size_t)y * w, w);
        for (uint32_t y = 0; y < h / 2; y++)
            memcpy(s_av_buf[k] + uvoff + (size_t)y * s_padw, src + ysz + (size_t)y * w, w);
    }
    SDL_LockMutex(s_av_mx);
    s_av_q[s_av_qt % AVQ] = k; s_av_qt++; s_av_seq++;
    SDL_UnlockMutex(s_av_mx);
}

// prep thread (through tex_gl): upload the newest staged frame into the texture, as vitaGL YUV (hardware CSC).
GLuint movie_yuv_tex(Texture *t) {
    if (!s_av_mx) return t->gl;
    if (!t->gl) {
        glGenTextures(1, &t->gl);
        glBindTexture(GL_TEXTURE_2D, t->gl);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    // Exactly one staged frame per Presented frame, in decode order (FIFO): every decoded frame is
    // shown once and only once, so pacing is even. tex_gl can be called several times per frame, so
    // consumption is gated by the Present tick (s_av_pick). If more than one frame is queued the
    // consumer fell behind: drop the older ones and catch up (the old "show newest" skipped frames
    // whenever the decoder phase drifted -> the irregular pacing seen 2026-11-14).
    int k = -1;
    SDL_LockMutex(s_av_mx);
    if (s_av_pick) {
        s_av_pick = 0;
        if (s_av_qh != s_av_qt) {
            while (s_av_qt - s_av_qh > 1) { s_av_busy[s_av_q[s_av_qh % AVQ]] = 0; s_av_qh++; s_av_late++; }
            k = s_av_q[s_av_qh % AVQ]; s_av_qh++;
        }
    }
    SDL_UnlockMutex(s_av_mx);
    if (k >= 0) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        static int yt_n;
        if (!yt_n++) rt_log("[movie] yuv_tex: gl %u first upload %dx%d buf %p\n", t->gl, s_vw, s_vh, s_av_buf[k]);
        glBindTexture(GL_TEXTURE_2D, t->gl);
        q_tex_yuv((GLsizei)s_padw, (GLsizei)s_padh, s_av_buf[k], (size_t)s_padw * s_padh * 3 / 2, &s_av_busy[k], s_yuv_fmt);
        s_av_up_ms += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        if (!(++s_av_up_n % 75)) { rt_log("[movie] avplayer: YUV upload queued %.1f ms/frame, %u late drops\n", s_av_up_ms / 75, s_av_late); s_av_up_ms = 0; }
    }
    return t->gl;
}

// keep the guest renderer's state machine fed (SH3_MV_FEED=1): one black sample per video frame.
// In this path the guest renderer never calls SetProperties on our allocator (no source media type was
// negotiated), so the sample slots must be created here, exactly like M(al, SetProperties) does.
static void av_feed_alloc(Movie *m) {
    static uint32_t sbuf[NSAMP], scap[NSAMP];
    int w = m->w ? m->w : s_vw, h = m->h ? m->h : s_vh;   // m->w is still 0 at av_open: the stream info is async
    uint32_t sz = (uint32_t)w * (uint32_t)h * 4;
    if (!sz) return;
    Samp *ns[NSAMP] = {0};
    for (int i = 0; i < NSAMP; i++) if (!m->samp[i]) {
        ns[i] = calloc(1, sizeof(Samp));
        if (!ns[i]) { for (int j = 0; j < i; j++) free(ns[j]); return; }
    }
    if (!movie_samples_reserve(sbuf, scap, NSAMP, sz)) {
        for (int i = 0; i < NSAMP; i++) free(ns[i]);
        static int rl; if (!rl++) rt_log("[movie] avplayer feed: guest heap has no room for %u x %u B\n", NSAMP, sz);
        return;
    }
    m->buf_size = sz;
    for (int i = 0; i < NSAMP; i++) {
        if (!m->samp[i]) {
            m->samp[i] = ns[i];
            m->samp[i]->m = m;
            com_wrap(&m->samp[i]->com, MK_SAMP, sm_vt, 19, "IMediaSample");
        }
        m->samp[i]->buf = sbuf[i];
        m->samp[i]->size = sz;
    }
    rt_log("[movie] avplayer feed: %u sample slots of %u B ready\n", NSAMP, sz);
}
static void av_feed_dummy(Movie *m, CPU *c) {
    static unsigned s_feed_n, s_feed_skip;
    if (!m->samp[0]) av_feed_alloc(m);
    Samp *sp = NULL;
    for (int i = 0; i < NSAMP && !sp; i++) if (m->samp[i] && !m->samp[i]->busy) sp = m->samp[i];
    if (!sp || !m->mem_in) {
        if (s_feed_skip++ < 10) rt_log("[movie] feed skip: sp=%p mem_in=%08x samp %08x %08x %08x busy %d%d%d\n",
            (void *)sp, m->mem_in, m->samp[0], m->samp[1], m->samp[2],
            m->samp[0] ? m->samp[0]->busy : -1, m->samp[1] ? m->samp[1]->busy : -1, m->samp[2] ? m->samp[2]->busy : -1);
        return;
    }
    uint32_t size = (uint32_t)m->w * (uint32_t)m->h * 4;
    if (!s_feed_buf && !(s_feed_buf = calloc(1, size))) return;
    sp->busy = 1; sp->com.refs = 1;
    sp->len = size;
    sp->t0 = (int64_t)(sceAvPlayerCurrentTime(s_h) * 1e4); sp->t1 = sp->t0 + 333667;
    sp->have_time = 1; sp->sync = 1;
    uint32_t rhr = gcall(c, m->mem_in, 6, 1, sp->com.guest);            // the sample buffer stays black
    sp->com.refs = 0; sp->busy = 0;
    if ((int32_t)rhr < 0) rt_log("[movie] avplayer feed: Receive -> %08x\n", rhr);
    else if ((++s_feed_n % 30) == 0) rt_log("[movie] avplayer feed: %u samples sent, last Receive %08x\n", s_feed_n, rhr);
}

// 1 once the video stream info is available (fills s_vw/s_vh/s_dur; stream creation is async on Vita3K
// and on the Vita — the vitasdk sample queries it in the STREAM_CREATED event, not right after AddSource)
static int av_read_stream_info(void) {
    SceAvPlayerStreamInfo si;
    for (int i = 0; i < 2; i++) {
        if (sceAvPlayerGetStreamInfo(s_h, (uint32_t)i, &si) != 0) continue;
        if (si.type == (uint32_t)SCE_AVPLAYER_VIDEO && !s_vw) { s_vw = (int)si.details.video.width; s_vh = (int)si.details.video.height; }
        if (si.duration > (uint64_t)(s_dur * 1000)) s_dur = (double)si.duration / 1000.0;
    }
    return s_vw && s_vh;
}
static int movie_pot(int v);
static void av_create_vtex(void);

static int av_thread(void *arg) {
    Movie *m = arg;
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
    const uint32_t cap = sizeof g_ring / sizeof g_ring[0];
    uint64_t hz = SDL_GetPerformanceFrequency(), st0 = SDL_GetPerformanceCounter();
    const uint64_t tstart = st0;            // for the startup grace before IsActive may be trusted (see EOF below)
    int saw_active = 0;
    double aud_acc = 0;
    unsigned long long run0 = 0; uint32_t pre0 = 0;
    int eof = 0;
    float *pcm = NULL; size_t pcmcap = 0;
    while (!m->stop) {
        if (m->state == 2 && s_h != -1) {
            if (!s_vw && av_read_stream_info()) {      // async stream creation: the size can arrive only after Start
                g_movie_w = movie_pot(s_vw); g_movie_h = movie_pot(s_vh);
                m->w = s_vw; m->h = s_vh;              // the feed sizes its black samples from these
                av_create_vtex();
                rt_log("[movie] avplayer: stream info %dx%d %.2f s\n", s_vw, s_vh, s_dur);
            }
            uint64_t a0 = SDL_GetPerformanceCounter();
            int npull = 0;
            while (npull++ < 16 && (g_ring_w - g_ring_r + cap) % cap < 48000) {   // keep ~0.5 s of audio ahead
                SceAvPlayerFrameInfo fi;
                if (!sceAvPlayerGetAudioData(s_h, &fi)) break;
                int ch = fi.details.audio.channelCount ? (int)fi.details.audio.channelCount : 2;
                int frames = (int)fi.details.audio.size / (2 * ch);
                if (frames > 0) {
                    if (pcmcap < (size_t)frames * 2) { free(pcm); pcmcap = (size_t)frames * 2; pcm = malloc(pcmcap * sizeof(float)); }
                    if (pcm) {
                        const int16_t *s16 = (const int16_t *)fi.pData;
                        for (int i = 0; i < frames * 2; i++) pcm[i] = s16[i] * (1.0f / 32768.0f);
                        movie_audio_push(pcm, frames, (int)fi.details.audio.sampleRate);
                    }
                }
            }
            aud_acc += (double)(SDL_GetPerformanceCounter() - a0) * 1000.0 / (double)hz;
            SceAvPlayerFrameInfo vi;
            while (sceAvPlayerGetVideoData(s_h, &vi)) {
                if (!s_vframes_total) rt_log("[movie] avplayer: premiere frame %ux%u\n", vi.details.video.width, vi.details.video.height);
                s_vframes++; s_vframes_total++; av_stage(&vi);
            }
            // The game's loop rate follows the feed rate (run 20/21), and the decoder's arrival jitters (demuxer
            // reads in 64 KB chunks), so pace the feed on the display instead: one sample per 2 vblanks = exactly
            // 30/s, phase-locked. The sample carries no pixels (the video shows through the staged texture), so
            // pacing it independently of frame arrival is safe; gating per arriving frame dropped early frames (20 fps, run 22).
            if (s_feed) {
                static uint32_t s_feed_v;
                uint32_t v = sceDisplayGetVcount();
                if ((int32_t)(v - s_feed_v) >= 2) { s_feed_v = v; av_feed_dummy(m, m->dec_cpu); }
            }
            // IsActive is false while the stream is still being created (~1 s after AddSource): both the vitaGL
            // sample and OpenFMV only treat it as end-of-file once a frame was decoded. Treat it as EOF before
            // that only after a 10 s grace, so a dead player still ends the movie instead of hanging.
            int active = sceAvPlayerIsActive(s_h);
            if (active && !saw_active) { saw_active = 1; rt_log("[movie] avplayer: player actif\n"); }
            if (!eof && (s_av_end || (!active && (s_vframes_total || SDL_GetPerformanceCounter() - tstart > 10 * hz)))) {
                eof = 1;
                rt_log("[movie] avplayer: fin de flux (%llu frames pulled)\n", s_vframes);
                if (m->rpin) gcall(m->dec_cpu, m->rpin, 14, 0);          // EndOfStream -> the renderer notifies EC_COMPLETE
                for (int i = 0; i < 20 && !m->nev && !m->nsink; i++) SDL_Delay(50);
                if (!m->nev && !m->nsink) { rt_log("[movie] avplayer: EC_COMPLETE de secours\n"); rpush(NULL, 1, 0, 0); }
                m->ended = 1;
                break;
            }
        }
        uint64_t now = SDL_GetPerformanceCounter();
        if (now - st0 > 2 * hz) {
            double sec = (double)(now - st0) / (double)hz;
            double cpu_ms = 0; unsigned pre = 0;
            SceKernelThreadInfo ti = {.size = sizeof ti};
            if (!sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti)) {
                cpu_ms = (double)(ti.runClocks - run0) / 1000.0 / sec; pre = ti.threadPreemptCount - pre0;
                run0 = ti.runClocks; pre0 = ti.threadPreemptCount;
            }
            rt_log("[movie] avplayer t=%.1f s: %.1f frames/s pulled, audio %.0f ms/s, ring %.2f s; thread: %.0f ms CPU/s, preemptions %u\n",
                   sceAvPlayerCurrentTime(s_h) / 1000.0, s_vframes / sec, aud_acc / sec,
                   (double)((g_ring_w - g_ring_r + cap) % cap) / 2.0 / 48000.0, cpu_ms, pre);
            s_vframes = 0; aud_acc = 0; st0 = now;
        }
        SDL_Delay(4);
    }
    free(pcm);
    return 0;
}

static int movie_pot(int v) { int p = 1; while (p < v) p <<= 1; return p; }

// open <host with .mp4> if it exists; 1 on success (s_h, s_vw/s_vh, s_dur set)
static int av_open(const char *host) {
    char mp4[700];
    size_t n = strlen(host);
    if (n <= 4 || n > sizeof mp4 - 4) return 0;
    memcpy(mp4, host, n - 4); mp4[n - 4] = 0;
    strncat(mp4, ".mp4", sizeof mp4 - strlen(mp4) - 1);
    int fd = sceIoOpen(mp4, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoClose(fd);
    static int loaded;
    if (!loaded) { sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER); loaded = 1; }
    static SceAvPlayerInitData init;
    memset(&init, 0, sizeof init);
    { const char *tx = getenv("SH3_MV_TEX"); av_tex_cd = !(tx && !strcmp(tx, "phy")); }   // default cd (console-tested); SH3_MV_TEX=phy: phycont
    { const char *cc = getenv("SH3_MV_CSC"); if (cc && !strcmp(cc, "0")) s_yuv_fmt = VGL_YUV420P_NV12_BT601; }
    // sceAppUtilInit before using the player, as OpenFMV does (on by default: the console-tested setup had it;
    // runs 10-11 showed the player works without it too). SH3_MV_APPUTIL=0 skips it.
    if (!(getenv("SH3_MV_APPUTIL") && !strcmp(getenv("SH3_MV_APPUTIL"), "0"))) {
        static int apputil_done;
        if (!apputil_done) {
            apputil_done = 1;
            int ar = sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
            rt_log("[movie] avplayer: sceAppUtilInit -> %08x\n", (unsigned)ar);
        }
    }
    {   // what the module will find when it allocates: the free pools at init time
        SceKernelFreeMemorySizeInfo fi = {.size = sizeof fi};
        sceKernelGetFreeMemorySize(&fi);
        rt_log("[movie] avplayer: init with tex=%s, free user %d KB cdram %d KB phycont %d KB\n",
               av_tex_cd ? "cd" : "phy", fi.size_user >> 10, fi.size_cdram >> 10, fi.size_phycont >> 10);
    }
    init.memoryReplacement = (SceAvPlayerMemReplacement){NULL, av_alloc, av_free, av_alloc_tex, av_free_tex};
    // SH3_MV_NOREP=1: exactly the vitaGL sample setup — no file replacement (the module opens the path itself)
    // and autoStart, to separate a file-callback problem from a decoder problem without rebuilding.
    const char *nr = getenv("SH3_MV_NOREP");
    int no_rep = nr && !strcmp(nr, "1");
    if (!no_rep)
        init.fileReplacement = (SceAvPlayerFileReplacement){&s_fd_slot, avf_open, avf_close, avf_read, avf_size};
    init.eventReplacement = (SceAvPlayerEventReplacement){NULL, av_event};
    {   // SH3_MV_DBG: the player's own log level (the Sony module prints why a source fails; emulator diagnosis)
        const char *dg = getenv("SH3_MV_DBG");
        init.debugLevel = dg ? (uint32_t)atoi(dg) : 0;
    }
    const char *pr = getenv("SH3_MV_PRIO");
    init.basePriority = pr ? (uint32_t)strtoul(pr, NULL, 0) : 0xA0;   // a real thread priority (0-255): the player creates its demux thread at this priority (OpenFMV uses 0xA0; 0x200000 makes sceKernelCreateThread fail — seen on Vita3K as "Demux thread creation failed")
    init.numOutputVideoFrameBuffers = 5;   // OpenFMV's value: 5 NV12 buffers of 512x384 fit the memory left free (cd: 5 x 512 KB of CDRAM, phy: 5 x 1 MB of phycont)
    // Both working references (vitaGL sample, OpenFMV) use autoStart=1 and never call Start: calling Start before
    // the stream exists returns 0x806A0002, which we used to misread as the player dying. SH3_MV_AUTOSTART=0 reverts.
    { const char *as = getenv("SH3_MV_AUTOSTART"); s_autostart = !(as && !strcmp(as, "0")); init.autoStart = s_autostart; }
    // defaultLanguage left zero like OpenFMV's proven InitData (a tag with no matching stream may make the module reject the source)
    s_h = sceAvPlayerInit(&init);
    if (!av_h_ok(s_h)) { rt_log("[movie] avplayer: init %s -> %08x\n", mp4, (unsigned)s_h); return 0; }
    if (sceAvPlayerAddSource(s_h, mp4) < 0) {
        rt_log("[movie] avplayer: AddSource %s failed\n", mp4);
        sceAvPlayerClose(s_h); s_h = -1; return 0;
    }
    s_dur = 0; s_vw = s_vh = 0;
    av_read_stream_info();
    s_av_end = 0; s_vframes = 0; s_vframes_total = 0;
    rt_log("[movie] avplayer: %s %dx%d %.2f s (prio %x%s)%s\n", mp4, s_vw, s_vh, s_dur,
           (unsigned)init.basePriority, no_rep ? ", no file replacement" : "", s_autostart ? "" : ", manual Start");
    return 1;
}

static void av_close(void) {
    if (s_h != -1) { sceAvPlayerStop(s_h); sceAvPlayerClose(s_h); s_h = -1; }
    g_movie_avplayer = 0;
    if (s_av_mx) {   // the staging ring belongs to the closed movie
        SDL_LockMutex(s_av_mx);
        s_av_qh = s_av_qt = 0; s_av_seq = 0; s_av_pick = 0; s_av_late = 0;
        for (int i = 0; i < AVQ; i++) s_av_busy[i] = 0;
        SDL_UnlockMutex(s_av_mx);
    }
}

// ---- drawing the video: replace the game's movie quad at record time (keeps the whrlays on top) ----
typedef struct { float x, y, z, rhw; uint32_t color; float u, v; } AvVertex;   // same layout as console_ui
static Texture *s_vtex;                       // host-owned YUV texture (created once, kept)
static uint32_t s_av_vb;                      // its vertex buffer, guest memory
static volatile int s_av_in_record;           // reentrancy guard (av_record_quad calls prep_draw)
static volatile unsigned s_av_drawn;          // intercepted quad draws since the last Present
static unsigned s_av_hit_frames, s_av_miss_frames;
static uint64_t s_av_stat_t;

static void av_create_vtex(void) {
    if (!s_vw || !s_vh) return;                        // stream info still pending
    if (!s_av_mx) s_av_mx = SDL_CreateMutex();
    if (!s_vtex) {
        s_vtex = calloc(1, sizeof *s_vtex);
        s_vtex->com.kind = K_TEXTURE; s_vtex->com.refs = 1;
        s_vtex->w = (uint32_t)s_vw; s_vtex->h = (uint32_t)s_vh;
        s_vtex->levels = 1; s_vtex->fmt = FMT_A8R8G8B8; s_vtex->pool = 1;
        s_vtex->yuv = 1; s_vtex->keep = 1;
        s_padw = movie_pot(s_vw); s_padh = movie_pot(s_vh);   // movie_pot is defined below
    }
    if (!s_av_vb) s_av_vb = gheap_alloc(sizeof(AvVertex) * 6);   // optional: the quad falls back to the host buffer below
}

// fullscreen opaque quad over the current viewport, fixed-function pipeline (the console_ui recipe)
static void av_record_quad(Device *d) {
    static AvVertex s_av_quad[6];   // host fallback: the guest heap is nearly full during movies (heap 31/32 MB)
    static int ar_n;
    if (!s_vtex || s_av_in_record) {
        if (ar_n++ < 3) rt_log("[movie] quad skip: vtex %p inrec %d\n", (void *)s_vtex, s_av_in_record);
        return;
    }
    if (ar_n++ < 3) rt_log("[movie] quad rec: vp %d,%d %dx%d gl %u vb %08x\n", (int)d->vp.x, (int)d->vp.y, (int)d->vp.w, (int)d->vp.h, s_vtex->gl, s_av_vb);
    float x = d->vp.x - .5f, y = d->vp.y - .5f, w = (float)d->vp.w, h = (float)d->vp.h;
    const uint32_t white = 0xffffffffu;
    float ue = s_padw ? (float)s_vw / s_padw : 1.0f, ve = s_padh ? (float)s_vh / s_padh : 1.0f;   // padded texture: sample only the used region
    AvVertex q[6] = {
        {x, y, 0, 1, white, 0, 0}, {x + w, y, 0, 1, white, ue, 0}, {x + w, y + h, 0, 1, white, ue, ve},
        {x, y, 0, 1, white, 0, 0}, {x + w, y + h, 0, 1, white, ue, ve}, {x, y + h, 0, 1, white, 0, ve},
    };
    void *vbuf = s_av_vb ? g_mem + s_av_vb : (void *)s_av_quad;
    memcpy(vbuf, q, sizeof q);
    // The game's own render state at Present time culls the quad away (run 14: recorded, uploaded, invisible).
    // Apply console_ui's proven full state (it zeroes rs/tss, then the six values its overlay needs), save/restore.
    static uint32_t rs0[256], tss0[MAX_STAGES][32];
    memcpy(rs0, d->rs, sizeof rs0); memcpy(tss0, d->tss, sizeof tss0);
    uint32_t rs[256] = {0}, tss[MAX_STAGES][32] = {{0}};
    rs[19] = 5; rs[20] = 6; rs[22] = 1; rs[27] = 1; rs[168] = 15; rs[171] = 1;
    tss[0][1] = 4; tss[0][2] = 2; tss[0][3] = 0; tss[0][4] = 4; tss[0][5] = 2; tss[0][6] = 0;   // MODULATE(TEXTURE, CURRENT)
    tss[0][13] = tss[0][14] = 3; tss[0][16] = tss[0][17] = 2;
    for (unsigned i = 1; i < MAX_STAGES; i++) tss[i][1] = 1;             // other stages off
    Texture *t0 = d->tex[0];
    uint32_t vs0 = d->vs, ps0 = d->ps;
    Texture *vt = s_vtex;
    prep_set(&d->tex[0], &vt, sizeof vt);
    prep_set(d->rs, rs, sizeof rs);
    prep_set(d->tss, tss, sizeof tss);
    PSET(vs, 0x144); PSET(ps, 0);
    s_av_in_record = 1;
    prep_draw(d, 4, 0, 6, 2, NULL, 0, 0, vbuf, sizeof(AvVertex));
    s_av_in_record = 0;
    prep_set(&d->tex[0], &t0, sizeof t0);
    prep_set(d->rs, rs0, sizeof rs0);
    prep_set(d->tss, tss0, sizeof tss0);
    PSET(vs, vs0); PSET(ps, ps0);
}

// prep_draw entry: 1 if this draw is the game's movie quad and was replaced by the video quad
int movie_vita_override(Device *d, uint32_t prim, uint32_t nverts, uint32_t nprims) {
    (void)prim; (void)nverts;
    if (!g_movie_avplayer || !s_vtex || s_av_in_record) return 0;
    if (nprims > 2) return 0;                                           // the movie quad is two triangles
    for (int s = 0; s < MAX_STAGES; s++) {
        Texture *t = d->tex[s];
        if (t && t != s_vtex && t->levels == 1 && !(t->usage & 1) &&
            t->w == (uint32_t)g_movie_w && t->h == (uint32_t)g_movie_h) {
            s_av_drawn++;
            av_record_quad(d);
            return 1;
        }
    }
    return 0;
}

// Present entry: if the game never drew its movie quad this frame, show the video at Present instead
// (the whrlays then end up under it: the console log tells which case the game is in).
void movie_vita_present(Device *d) {
    av_reap_tex();                                    // unmap blocks the module freed from its own thread
    if (!g_movie_avplayer || !s_vtex) return;
    s_av_pick = 1;                                    // this Presented frame may consume one staged frame (FIFO)
    if (s_av_drawn) s_av_hit_frames++;
    else if (g_mv && !g_mv->ended && g_mv->state) { av_record_quad(d); s_av_miss_frames++; }
    s_av_drawn = 0;
    uint64_t now = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency();
    if (!s_av_stat_t) s_av_stat_t = now;
    if (now - s_av_stat_t > 2 * hz) {
        rt_log("[movie] avplayer draw: %u frames via the game quad, %u at Present\n", s_av_hit_frames, s_av_miss_frames);
        s_av_hit_frames = s_av_miss_frames = 0; s_av_stat_t = now;
    }
}
#endif /* __vita__ (sceAvPlayer path) */

static plm_frame_t *g_pending;
// Video decode time: gap between two plm_decode callbacks (g_cb_t reset at the start of each plm_decode).
// Audio: measured in its own thread.
static uint64_t g_cb_t;
static double g_st_vid;
static volatile double g_st_aud_rate;   // ms/s of MP2 decode, measured in the audio thread's own window
static double cb_ms(void) { uint64_t n = SDL_GetPerformanceCounter(); double d = (double)(n - g_cb_t) * 1000.0 / (double)SDL_GetPerformanceFrequency(); g_cb_t = n; return d; }
static int g_handed;
static void hand_frame(Movie *m, plm_frame_t *f);
static void on_video(plm_t *p, plm_frame_t *f, void *u) {
    g_st_vid += cb_ms();
    Movie *m = u;
    if (p->video_decoder->skipped) { m->st_nskip++; return; }   // B frame skipped (late)
    m->st_ndec++;
    if (m->yuv[0]) { hand_frame(m, f); g_handed = 1; }   // every decoded frame goes to the output FIFO, in display order
    else g_pending = f;
}

// SH4_MOVIE_BENCH=s: at startup, decodes s seconds of m_00 alone (nothing else running): raw cost of pl_mpeg.
void movie_bench(const char *dir, int secs) {
    char p[300];
    SDL_snprintf(p, sizeof p, "%s/movie/m_00.pac", dir);
    plm_t *plm = plm_create_with_filename(p);
    uint8_t *rgb = malloc(640 * 480 * 4);
    if (!plm || !rgb) { rt_log("[bench] %s: cannot open\n", p); return; }
    double ms = 1000.0 / (double)SDL_GetPerformanceFrequency(), dec = 0, conv = 0, aud = 0;
    plm_set_audio_enabled(plm, 0);
    int n = 0, na = 0;
    uint32_t hash = 2166136261u;
    for (; n < secs * 30; n++) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        plm_frame_t *f = plm_decode_video(plm);
        uint64_t t1 = SDL_GetPerformanceCounter();
        if (!f) break;
        frame_to_bgra(f, rgb, (int)f->width * 4);
        dec += (double)(t1 - t0) * ms; conv += (double)(SDL_GetPerformanceCounter() - t1) * ms;
        // FNV-1a hash of the YUV planes: must equal that of test/plm_vbench.c (plain C) for the same number of frames
        plm_plane_t *pl[3] = { &f->y, &f->cb, &f->cr };
        for (int k = 0; k < 3; k++)
            for (size_t i = 0, e = (size_t)pl[k]->width * pl[k]->height; i < e; i++) hash = (hash ^ pl[k]->data[i]) * 16777619u;
        if (n % 30 == 0) rt_log("[bench] frame %d: hash %08x\n", n, (unsigned)hash);
        if (n == 90) {   // check: max difference against the reference conversion
            uint8_t *ref = malloc(640 * 480 * 4);
            int dmax = 0;
            if (ref) {
                plm_frame_to_bgra(f, ref, (int)f->width * 4);
                for (int i = 0; i < 640 * 480 * 4; i++) { int d = abs(ref[i] - rgb[i]); if ((i & 3) != 3 && d > dmax) dmax = d; }
                free(ref);
            }
            rt_log("[bench] max conversion error = %d\n", dmax);
        }
    }
    plm_destroy(plm);
    plm = plm_create_with_filename(p);
    if (plm) {
        plm_set_video_enabled(plm, 0);
        uint64_t t0 = SDL_GetPerformanceCounter();
        plm_samples_t *a;
        for (; (a = plm_decode_audio(plm)) && a->time < secs; na++) {}
        aud = (double)(SDL_GetPerformanceCounter() - t0) * ms;
    }
    rt_log("[bench] %d frames: decode %.1f ms/frame, BGRA conversion %.1f ms/frame; audio: %.0f ms for %d frames; hash %08x\n",
           n, n ? dec / n : 0, n ? conv / n : 0, aud, na, (unsigned)hash);
    free(rgb);
    if (plm) plm_destroy(plm);
}

// SH4_MOVIE_BENCH=s[,wait,core,core...]: with a wait, the bench runs during the game in a pinned thread
// successively on each given core (0-2): measures the contention suffered by movie decoding.
static char g_bench_dir[300];
static int g_bench_arg[8], g_bench_n;
static int bench_thread(void *u) {
    (void)u;
    SDL_Delay((uint32_t)g_bench_arg[1] * 1000);
    for (int i = 2; i < g_bench_n; i++) {
#ifdef __vita__
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 << g_bench_arg[i]);
#endif
        rt_log("[bench] core %d\n", g_bench_arg[i]);
        movie_bench(g_bench_dir, g_bench_arg[0]);
    }
    return 0;
}
void movie_bench_start(const char *dir, const char *spec) {
    SDL_strlcpy(g_bench_dir, dir, sizeof g_bench_dir);
    for (g_bench_n = 0; g_bench_n < 8 && *spec; g_bench_n++) {
        g_bench_arg[g_bench_n] = atoi(spec);
        spec += strcspn(spec, ",");
        if (*spec) spec++;
    }
    if (g_bench_n < 3) movie_bench(dir, g_bench_arg[0]);
    else SDL_CreateThread(bench_thread, "bench", NULL);
}
// newlib fills a FILE in BUFSIZ blocks (1 KB): unbuffered stream = one read per pl_mpeg refill (<= 128 KB).
static plm_buffer_t *movie_file(const char *host) {
    FILE *f = fopen(host, "rb");
    if (!f) return NULL;
    setvbuf(f, NULL, _IONBF, 0);
    return plm_buffer_create_with_file(f, 1);
}
// SH3 .000: MPEG-PS obfuscated by an LCG keystream (see codec/movie000.h); plaintext starts at file offset 0x8000.
#include "codec/movie000.c"
static char g_movie_note[600];                    // last .000 file the game opened (its own source filter reads it)
void movie_note_file(const char *host) { snprintf(g_movie_note, sizeof g_movie_note, "%s", host); }
// .000 reader. Each 4 KB period is decrypted from the same LCG base, so periods can be read in any grouping.
// It used to fseek + fread one 4 KB period at a time; newlib's fseek drops the FILE buffer, so on the Vita every
// 4 KB became its own seek + read on the memory card, for two readers (video and audio demux the whole file):
// ~250 card accesses per second for op.000 (97 MB / 192 s). Now a span of 64 periods (256 KB) per read, with a seek
// only when the reader jumps; the bytes handed to pl_mpeg are the same.
#define MOVIE_DEC_SPAN (64 * MOVIE000_PERIOD)
typedef struct { FILE *f; size_t size, pos, fpos; uint32_t base; uint8_t *chunk; long chunk_idx; } MovieDec;
static void movie_dec_load(plm_buffer_t *buffer, void *user) {
    MovieDec *d = user;
    if (buffer->discard_read_bytes) plm_buffer_discard_read_bytes(buffer);
    while (buffer->length < buffer->capacity && d->pos < d->size) {
        long idx = (long)(d->pos / MOVIE_DEC_SPAN);
        size_t off = d->pos % MOVIE_DEC_SPAN, want = MOVIE_DEC_SPAN;
        if (idx != d->chunk_idx) {
            size_t n = 0, at = MOVIE000_HEADER_SIZE + (size_t)idx * MOVIE_DEC_SPAN;
            // read()/lseek() on the descriptor, not fread: newlib splits a buffered fread into reads the size of the
            // FILE buffer, so even 256 KB came off the card in small sceIoRead calls (console: ~750 ms of reads per
            // second per reader while op.000 needs ~0.5 MB/s).
            int fd = fileno(d->f);
            if (d->fpos == at || lseek(fd, (off_t)at, SEEK_SET) == (off_t)at) {
                uint64_t t = SDL_GetPerformanceCounter();
                while (n < want) { ssize_t r = read(fd, d->chunk + n, want - n); if (r <= 0) break; n += (size_t)r; }
                g_st_io += (double)(SDL_GetPerformanceCounter() - t) * 1000.0 / (double)SDL_GetPerformanceFrequency();
            }
            d->fpos = n == want ? at + n : (size_t)-1;
            if (n < want) memset(d->chunk + n, 0, want - n);
            if (n >= 4) movie000_decrypt(d->chunk, n & ~(size_t)3, d->base);
            d->chunk_idx = idx;
        }
        size_t take = MOVIE_DEC_SPAN - off;
        if (take > buffer->capacity - buffer->length) take = buffer->capacity - buffer->length;
        if (take > d->size - d->pos) take = d->size - d->pos;
        memcpy(buffer->bytes + buffer->length, d->chunk + off, take);
        buffer->length += take; d->pos += take;
    }
    if (d->pos >= d->size) buffer->has_ended = TRUE;
}
static void movie_dec_seek(plm_buffer_t *buffer, size_t pos, void *user) { (void)buffer; ((MovieDec *)user)->pos = pos; }
static size_t movie_dec_tell(plm_buffer_t *buffer, void *user) { (void)buffer; return ((MovieDec *)user)->pos; }
static void movie_dec_free(void *p) {
    MovieDec *d = p;
    if (!d) return;
    if (d->f) fclose(d->f);
    free(d->chunk);
    free(d);
}
static plm_buffer_t *movie_dec_file(const char *host, void **out) {
    FILE *f = fopen(host, "rb");
    if (!f) return NULL;
    uint8_t *hdr = malloc(MOVIE000_HEADER_SIZE);
    MovieDec *d = calloc(1, sizeof *d);
    if (!hdr || !d || fseek(f, 0, SEEK_END) || ftell(f) <= MOVIE000_HEADER_SIZE || fseek(f, 0, SEEK_SET) ||
        fread(hdr, 1, MOVIE000_HEADER_SIZE, f) != MOVIE000_HEADER_SIZE) { free(hdr); free(d); fclose(f); return NULL; }
    d->f = f; d->chunk_idx = -1; d->fpos = (size_t)-1;
    d->chunk = malloc(MOVIE_DEC_SPAN);
    if (!d->chunk) { free(hdr); movie_dec_free(d); return NULL; }
    d->base = movie000_base_state(hdr);
    free(hdr);
    fseek(f, 0, SEEK_END);
    d->size = (size_t)ftell(f) - MOVIE000_HEADER_SIZE;
    plm_buffer_t *b = plm_buffer_create_with_callbacks(movie_dec_load, movie_dec_seek, movie_dec_tell, d->size, d);
    if (!b) { movie_dec_free(d); return NULL; }
    *out = d;   // freed by movie_free_decoders (pl_mpeg does not own callback state)
    return b;
}
static void movie_cached_load(plm_buffer_t *buffer, void *user) {
    if (buffer->discard_read_bytes) plm_buffer_discard_read_bytes(buffer);
    size_t n = movie_stream_read(user, buffer->bytes + buffer->length, buffer->capacity - buffer->length);
    buffer->length += n;
    if (!n) buffer->has_ended = TRUE;
}
static void movie_cached_seek(plm_buffer_t *buffer, size_t pos, void *user) {
    (void)buffer; ((MovieCursor *)user)->pos = pos;
}
static size_t movie_cached_tell(plm_buffer_t *buffer, void *user) {
    (void)buffer; return ((MovieCursor *)user)->pos;
}
static plm_buffer_t *movie_input(Movie *m, const char *host, int index) {
    { const char *e = strrchr(host, '.');
      if (e && !strcasecmp(e, ".000")) { int k = index & 1; movie_dec_free(m->dec[k]); m->dec[k] = NULL; return movie_dec_file(host, &m->dec[k]); } }
    if (!m->stream) return movie_file(host);
    m->cursor[index].source = m->stream; m->cursor[index].pos = 0;
    return plm_buffer_create_with_callbacks(movie_cached_load, movie_cached_seek,
                                          movie_cached_tell, m->stream->size, &m->cursor[index]);
}
// Audio buffer refill: demuxes up to the next audio packet (video packets are skipped).
static void apkt_load(plm_buffer_t *ab, void *u) {
    plm_packet_t *p;
    while ((p = plm_demux_decode(u)))
        if (p->type == PLM_DEMUX_PACKET_AUDIO_1) { plm_buffer_write(ab, p->data, p->length); return; }
    plm_buffer_signal_end(ab);
}
// Movie audio on core 2, ~0.25 s ahead of the mixer: video decoding keeps core 1 to itself.
static int audio_thread(void *arg) {
    Movie *m = arg;
#ifdef __vita__
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
#endif
    const uint32_t cap = sizeof g_ring / sizeof g_ring[0];
    double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
    uint64_t hz = SDL_GetPerformanceFrequency(), aw0 = SDL_GetPerformanceCounter();
    double aud_acc = 0;
    while (!m->stop && !m->ended) {
        plm_samples_t *s = NULL;
        if (m->state == 2 && (g_ring_w - g_ring_r + cap) % cap < 48000 * 2 / 4) {
            uint64_t t = SDL_GetPerformanceCounter();
            s = plm_audio_decode(m->aud);
            aud_acc += (double)(SDL_GetPerformanceCounter() - t) * ms;
            if (s) movie_audio_push(s->interleaved, (int)s->count, plm_audio_get_samplerate(m->aud));
        }
        // own 2 s window: the shared counter was reset by the decode thread while this thread kept adding to it,
        // which is how "audio 2553 ms/s" (impossible) appeared in the console logs
        uint64_t now = SDL_GetPerformanceCounter();
        if (now - aw0 > 2 * hz) { g_st_aud_rate = aud_acc * (double)hz / (double)(now - aw0); /* ms per second */ aud_acc = 0; aw0 = now; }
        if (!s) SDL_Delay(5);
    }
    return 0;
}
// Caller must join every decoder/output/audio thread first. Queued graphics
// uploads own their separate RGB565 staging buffers, not these YUV slots.
static void movie_free_output(Movie *m) {
    for (int i = 0; i < OQ; i++) { free(m->yuv[i]); m->yuv[i] = NULL; }
    rt_free_cpu(m->dec_cpu);
    m->dec_cpu = NULL;
    m->ohead = m->otail = 0;
    m->obusy = -1;
}
static void movie_free_decoders(Movie *m) {
    if (m->plm) plm_destroy(m->plm);
    if (m->aud) plm_audio_destroy(m->aud);
    if (m->adm) plm_demux_destroy(m->adm);
    m->plm = NULL; m->aud = NULL; m->adm = NULL;
    for (int k = 0; k < 2; k++) { movie_dec_free(m->dec[k]); m->dec[k] = NULL; }   // after the buffers that read through them
    movie_stream_close(m->stream); m->stream = NULL;
    movie_free_output(m);
}
// Decode -> output: YUV copy (~0.5 MB) into the next FIFO slot; if the output thread is too far behind, the oldest waiting frame is dropped.
static void hand_frame(Movie *m, plm_frame_t *f) {
    SDL_LockMutex(m->lock);
    while (m->otail - m->ohead >= OQ - 2) { m->ohead++; m->odropped++; }
    if (m->ohead == m->otail) m->ohead = m->otail = 0;
    int w;
    for (w = 0; w < OQ; w++) {
        if (w == m->obusy) continue;
        unsigned n = m->ohead;
        while (n != m->otail && m->oq[n % OQ] != w) n++;
        if (n == m->otail) break;
    }
    if (w == OQ) { m->odropped++; SDL_UnlockMutex(m->lock); return; }
    SDL_UnlockMutex(m->lock);
    size_t ys = (size_t)f->y.width * f->y.height, cs = (size_t)f->cb.width * f->cb.height;
    plm_frame_t *o = &m->of[w];
    *o = *f;
    o->y.data = m->yuv[w]; o->cb.data = o->y.data + ys; o->cr.data = o->cb.data + cs;
    memcpy(o->y.data, f->y.data, ys); memcpy(o->cb.data, f->cb.data, cs); memcpy(o->cr.data, f->cr.data, cs);
    SDL_LockMutex(m->lock);
    m->oq[m->otail % OQ] = w;
    m->otail++;
    SDL_UnlockMutex(m->lock);
    SDL_SemPost(m->osem);
}
// BGRA conversion + guest Receive on core 2 (core 1 only decodes), then EndOfStream at the end of the stream.
static int out_thread(void *arg) {
    Movie *m = arg;
    rt_log("[movie] output thread started\n");
#ifdef __vita__
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
#endif
    while (!m->stop) {
        SDL_SemWaitTimeout(m->osem, 50);
        int done = m->dec_done;
        SDL_LockMutex(m->lock);
        int r = -1;
        if (m->ohead != m->otail) { r = m->oq[m->ohead % OQ]; m->ohead++; }
        m->obusy = r;
        SDL_UnlockMutex(m->lock);
        if (r >= 0) push_frame(m, m->dec_cpu, &m->of[r]);
        SDL_LockMutex(m->lock);
        m->obusy = -1;
        SDL_UnlockMutex(m->lock);
        if (r < 0 && done) {
            if (!m->stop && m->rpin) {
                rt_log("[movie] fin de flux\n");
                gcall(m->dec_cpu, m->rpin, 14, 0); // EndOfStream -> the renderer notifies EC_COMPLETE to the graph
            }
            m->ended = 1;
            break;
        }
    }
    return 0;
}
static int decode_thread(void *arg) {
    Movie *m = arg;
#ifdef __vita__
    const char *choice = getenv("SH4_MOVIE_CORE");
    int core = choice ? atoi(choice) : 0;
    if (core < 0 || core > 2) core = 0;
    int aff = sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 << core);
    const char *low = getenv("SH4_MOVIE_LOW_PRIORITY");
    int priority = (!low || strcmp(low, "0")) ? SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW) : 0;
    rt_log("[movie] decode thread on core %d -> %08x, low priority -> %08x\n", core, aff, priority);
#endif
    uint64_t last = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency(), st0 = last;
    while (!m->stop && !plm_has_ended(m->plm)) {
        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)hz;
        last = now;
        if (dt > 0.25) dt = 0.25;
        int got = 0;
        if (m->state == 2) {
            g_pending = NULL; g_handed = 0;
            uint64_t d0 = SDL_GetPerformanceCounter();
            g_cb_t = d0;
            plm_decode(m->plm, dt);
            m->st_dec += (double)(SDL_GetPerformanceCounter() - d0) * 1000.0 / (double)hz;
            got = g_handed;
            if (g_pending) { got = 1; push_frame(m, m->dec_cpu, g_pending); }   // no memory for the output buffers: the last frame only
        }
        if (now - st0 > 2 * hz) {
            double sec = (double)(now - st0) / (double)hz;
            rt_log("[movie] t=%.1f s: %.1f frames decoded/s (%.1f B skipped), %.1f delivered/s; per second: decode %.0f ms (video %.0f, audio %.0f, file read %.0f), conversion %.0f ms, Receive %.0f ms\n",
                   plm_get_time(m->plm), m->st_ndec / sec, m->st_nskip / sec, m->st_npush / sec, m->st_dec / sec, g_st_vid / sec, g_st_aud_rate, g_st_io / sec, m->st_conv / sec, m->st_recv / sec);
#ifdef __vita__
            // real CPU time of the thread (runClocks, us) versus elapsed time: tells preemption from raw slowness
            static uint64_t run0; static uint32_t pre0, ipre0;
            SceKernelThreadInfo ti = {.size = sizeof ti};
            if (!sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti)) {
                rt_log("[movie] thread: %.0f ms CPU/s, preemptions %u (interrupts %u), core %d, priority %x\n", (double)(ti.runClocks - run0) / 1000.0 / sec,
                       ti.threadPreemptCount - pre0, ti.intrPreemptCount - ipre0, ti.currentCpuId, ti.currentPriority);
                run0 = ti.runClocks; pre0 = ti.threadPreemptCount; ipre0 = ti.intrPreemptCount;
            }
#endif
            m->st_dec = m->st_conv = m->st_recv = 0; g_st_vid = g_st_io = 0; m->st_ndec = m->st_npush = m->st_nskip = 0; st0 = now;
        }
        if (!got) SDL_Delay(4);
    }
    m->dec_done = 1;
    SDL_SemPost(m->osem);
    return 0;
}

// ---------- IMediaControl / IMediaPosition / IMediaEvent / IBasicAudio / IMediaEventSink ----------
M(ctl, Run) {
    HOSTFN; MV;
    if (m->state != 2) {
        if (m->renderer) gcall(c, m->renderer, 6, 2, 0u, 0u); // Run(tStart=0)
        m->state = 2;
#ifdef __vita__
        if (g_movie_avplayer) {
            if (s_h != -1) {
                if (!m->thread) {
                    m->dec_cpu = rt_new_cpu(1u << 20);
                    av_create_vtex();
                    s_av_end = 0;
                    int sr = s_autostart ? 0 : sceAvPlayerStart(s_h);   // autoStart: AddSource already started the stream; an early Start only returns 0x806A0002
                    m->thread = SDL_CreateThread(av_thread, "movie_av", m);
                    rt_log("[movie] avplayer: start %s-> %08x, feed=%d\n", s_autostart ? "(skipped, autoStart) " : "", (unsigned)sr, s_feed);
                } else sceAvPlayerResume(s_h);
            }
            STDRET(1, HR_OK);
        }
#endif
        if (!m->thread && !m->ended) {
            m->dec_cpu = rt_new_cpu(1u << 20);
            size_t fs = (size_t)((m->w + 15) & ~15) * ((m->h + 15) & ~15) * 3 / 2;
            int ok = 1;
            for (int i = 0; i < OQ; i++) { if (!m->yuv[i]) m->yuv[i] = malloc(fs); if (!m->yuv[i]) ok = 0; }
            if (!ok) { for (int i = 0; i < OQ; i++) { free(m->yuv[i]); m->yuv[i] = NULL; } rt_log("[movie] output buffers: not enough memory\n"); }
            if (!m->osem) m->osem = SDL_CreateSemaphore(0);
            m->ohead = m->otail = 0; m->obusy = -1; m->dec_done = 0;
            m->odropped = 0; s_movie_serial++; m565_begin();
            g_st_vid = g_st_io = 0; g_st_aud_rate = 0;
            m->thread = SDL_CreateThread(decode_thread, "movie", m);
            m->othread = SDL_CreateThread(out_thread, "movie_out", m);
            if (m->aud) m->athread = SDL_CreateThread(audio_thread, "movie_audio", m);
            if (!m->thread || !m->othread || (m->aud && !m->athread)) rt_log("[movie] thread creation failed : dec=%p out=%p aud=%p (%s)\n", (void *)m->thread, (void *)m->othread, (void *)m->athread, SDL_GetError());
#ifdef __vita__
            if (!m->othread) { SceKernelFreeMemorySizeInfo fi = {.size = sizeof fi}; sceKernelGetFreeMemorySize(&fi); rt_log("[movie] free user %d KB\n", fi.size_user >> 10); }
#endif
        }
    }
    STDRET(1, HR_OK);
}
M(ctl, Pause) { HOSTFN; MV;
#ifdef __vita__
    if (g_movie_avplayer && m->state == 2 && s_h != -1) sceAvPlayerPause(s_h);
#endif
    m->state = 1; if (m->renderer) gcall(c, m->renderer, 5, 0); STDRET(1, HR_OK); }
M(ctl, Stop) {
    HOSTFN; MV;
    { extern unsigned g_frame; rt_log("[movie-stop] frame=%u\n", g_frame); }
    m->stop = 1;
    if (m->thread) { SDL_WaitThread(m->thread, NULL); m->thread = NULL; }
    if (m->othread) { SDL_WaitThread(m->othread, NULL); m->othread = NULL; }
    if (m->athread) { SDL_WaitThread(m->athread, NULL); m->athread = NULL; }
#ifdef __vita__
    if (g_movie_avplayer) av_close();     // the hardware player belongs to this movie: release it
#endif
    if (m->state && m->renderer) gcall(c, m->renderer, 4, 0);  // Stop
    m->state = 0;
    g_movie_w = g_movie_h = 0;
    movie_audio_flush();
    if (m->stream) rt_log("[movie-source] requests=%llu disk_reads=%llu disk_kb=%llu cache_hit_kb=%llu io_ms=%.1f output_dropped=%u\n",
                         (unsigned long long)m->stream->requests, (unsigned long long)m->stream->reads,
                         (unsigned long long)(m->stream->read_bytes / 1024),
                         (unsigned long long)(m->stream->hit_bytes / 1024),
                         m->stream->io_ticks * 1000.0 / SDL_GetPerformanceFrequency(), m->odropped);
    // The output/EOF callbacks have ended. Their private x86 CPU owns a 1 MiB
    // guest stack and a TIB; keeping it after Stop leaks one set per movie.
    movie_free_output(m);
    STDRET(1, HR_OK);
}
M(ctl, GetState) { HOSTFN; MV; W32(ARG(3), (uint32_t)m->state); STDRET(3, HR_OK); }
M(ctl, NI) { STDRET(2, E_NOTIMPL_); }
M(ctl, Disp) { STDRET(2, E_NOTIMPL_); }
static const gfn ctl_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Run,
    ctl_Pause, ctl_Stop, ctl_GetState, ctl_NI, ctl_NI, ctl_NI, ctl_NI, ctl_Pause};

M(pos, Duration) {
    HOSTFN; MV;
    double dur = 0;
#ifdef __vita__
    if (g_movie_avplayer) dur = s_dur; else
#endif
    if (m->plm) dur = plm_get_duration(m->plm);
    W64(ARG(2), rt_u64f(dur)); STDRET(2, HR_OK);
}
// ponytail: audio only follows a rewind to the start outside playback (the only expected case); a real seek would need plm_demux_seek.
M(pos, PutCur) {
    HOSTFN; MV;
    double t = rt_f64(((uint64_t)ARG(3) << 32) | ARG(2));
    rt_log("[movie] PutCurrentPosition(%.2f)\n", t);
#ifdef __vita__
    if (g_movie_avplayer) {
        if (s_h != -1) sceAvPlayerJumpToTime(s_h, (uint64_t)(t * 1000));
        movie_audio_flush();
        STDRET(3, HR_OK);
    }
#endif
    if (m->plm) plm_seek(m->plm, t, 0);
    if (m->aud && !m->athread && t == 0) { plm_demux_rewind(m->adm); plm_audio_rewind(m->aud); movie_audio_flush(); }
    STDRET(3, HR_OK);
}
M(pos, GetCur) {
    HOSTFN; MV;
    double t = 0;
#ifdef __vita__
    if (g_movie_avplayer) t = s_h != -1 ? (double)sceAvPlayerCurrentTime(s_h) / 1000.0 : 0; else
#endif
    if (m->plm) t = plm_get_time(m->plm);
    W64(ARG(2), rt_u64f(t)); STDRET(2, HR_OK);
}
M(pos, Gen2) { STDRET(2, HR_OK); }
M(pos, Gen3) { STDRET(3, HR_OK); }
static const gfn pos_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, pos_Duration,
    pos_PutCur, pos_GetCur, pos_Gen2, pos_Gen3, pos_Gen2, pos_Gen3, pos_Gen3, pos_Gen2, pos_Gen2, pos_Gen2};

M(ev, GetHandle) { HOSTFN; W32(ARG(2), 0); STDRET(2, HR_OK); }
M(ev, GetEvent) {
    HOSTFN; MV;
    uint32_t hr = VFW_E_TIMEOUT;
    SDL_LockMutex(m->lock);
    if (m->nev) {
        W32(ARG(2), m->ev[0].code); W32(ARG(3), m->ev[0].p1); W32(ARG(4), m->ev[0].p2);
        memmove(&m->ev[0], &m->ev[1], (size_t)(--m->nev) * sizeof m->ev[0]);
        hr = HR_OK;
    }
    SDL_UnlockMutex(m->lock);
    STDRET(5, hr);
}
M(ev, WaitComp) { HOSTFN; W32(ARG(3), 0); STDRET(3, VFW_E_TIMEOUT); }
M(ev, Free) { STDRET(4, HR_OK); }
// IMediaEventEx tail (SH3 queries 56a868c0 and calls SetNotifyWindow): SetNotifyWindow(this,hwnd,msg,data),
// SetNotifyFlags(this,flags), GetNotifyFlags(this,&flags)
M(ev, SetNotifyWindow) {
    HOSTFN; MV;
    rt_log("[movie-gr] SetNotifyWindow(hwnd=%08x msg=%08x data=%08x)\n", ARG(2), ARG(3), ARG(4));
    SDL_LockMutex(m->lock);
    m->notify_hwnd = ARG(2); m->notify_msg = ARG(3); m->notify_data = ARG(4);
    SDL_UnlockMutex(m->lock);
    STDRET(4, HR_OK);
}
M(ev, GetNotifyFlags) { HOSTFN; if (ARG(2)) W32(ARG(2), 0); STDRET(2, HR_OK); }
static const gfn ev_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, ev_GetHandle,
    ev_GetEvent, ev_WaitComp, pos_Gen2, pos_Gen2, ev_Free, ev_SetNotifyWindow, pos_Gen2 /*SetNotifyFlags*/, ev_GetNotifyFlags};

M(au, PutVol) {
    HOSTFN; MV;
    m->vol = (int32_t)ARG(2);
    g_audio_gain_q8 = m->vol <= -10000 ? 0 : (int)(256.0f * powf(10.0f, (float)m->vol / 2000.0f));
    STDRET(2, HR_OK);
}
M(au, GetVol) { HOSTFN; MV; W32(ARG(2), (uint32_t)m->vol); STDRET(2, HR_OK); }
static const gfn au_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, au_PutVol,
    au_GetVol, pos_Gen2, au_GetVol};

M(sink, Notify) { HOSTFN; rt_log("[movie] event %u (%08x, %08x)\n", ARG(2), ARG(3), ARG(4)); if (g_mv) g_mv->nsink++; rpush(c, ARG(2), ARG(3), ARG(4)); STDRET(4, HR_OK); }
static const gfn sink_vt[] = {any_QI, any_AddRef, any_Release, sink_Notify};

// ---------- IGraphBuilder ----------
static uint32_t guid_d1(CPU *c, uint32_t p) { HOSTFN; return R32(p); }

M(gr, QI) {
    HOSTFN; MV;
    uint32_t d1 = guid_d1(c, ARG(2)), out = ARG(3), r = 0;
    switch (d1) {
    case 0x56a868b1: r = m->ctl_g; break;
    case 0x56a868b2: r = m->pos_g; break;
    case 0x56a868b6: case 0x56a868c0: r = m->ev_g; break;
    case 0x56a868b3: r = m->aud_g; break;
    case 0x56a868a2: r = m->sink_g; break;
    case 0x56a868a9: case 0x56a8689f: case 0: r = m->graph_g; break;
    default: rt_log("[movie] graph: unsupported QueryInterface IID %08x\n", d1); break;
    }
    W32(out, r);
    STDRET(3, r ? HR_OK : E_NOINTERFACE_);
}
// ---- guest filter / pin discovery (the game builds its own source and renderer filters) ----
// First pin of a guest IBaseFilter with the wanted direction (0 input, 1 output); 0 if none.
static uint32_t guest_find_pin(CPU *c, uint32_t filt, int want_dir) {
    HOSTFN;
    uint32_t tmp = guest_tmp() + 224, found = 0;
    W32(tmp, 0);
    if ((int32_t)gcall(c, filt, 10, 1, tmp) < 0 || !R32(tmp)) return 0;   // IBaseFilter::EnumPins
    uint32_t en = R32(tmp);
    for (int i = 0; i < 8 && !found; i++) {
        W32(tmp + 4, 0); W32(tmp + 8, 0);
        gcall(c, en, 3, 3, 1u, tmp + 4, tmp + 8);                            // IEnumPins::Next(1, &pin, &fetched)
        uint32_t pin = R32(tmp + 4);
        if (!pin || R32(tmp + 8) != 1) break;
        W32(tmp + 12, 99);
        gcall(c, pin, 9, 1, tmp + 12);                                       // IPin::QueryDirection
        rt_log("[movie-gr]   filter %08x pin %08x dir=%u\n", filt, pin, R32(tmp + 12));
        if ((int)R32(tmp + 12) == want_dir) found = pin;
    }
    gcall(c, en, 2, 0);                                                      // Release
    return found;
}
M(gr, AddFilter) {
    HOSTFN; MV;
    uint32_t f = ARG(2);
    rt_log("[movie-gr] AddFilter(%08x host=%d name=%08x)\n", f, com_get(f) != NULL, ARG(3));
    if (!f) STDRET(3, HR_OK);
    if (!com_get(f)) { // guest filter (renderer or SH3's own source filter): it joins the graph
        if (m->nguest < 4) m->guest[m->nguest++] = f;
        gcall(c, f, 13, 2, m->graph_g, ARG(3));
        rt_log("[movie] guest filter %08x added to the graph\n", f);
    }
    STDRET(3, HR_OK);
}
// host output pin (video codec) -> guest input pin (renderer): format negotiation, allocator, delivery setup
static uint32_t connect_host_guest(CPU *c, Movie *m, HObj *po, uint32_t out, uint32_t in) {
    HOSTFN;
    uint32_t tmp = guest_tmp();
    uint8_t g_vid[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}, g_rgb[8] = {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}, /* RGB32 */
            g_vi[8] = {0xBF, 0x01, 0x00, 0xAA, 0x00, 0x55, 0x59, 0x5A};
    uint32_t mt = tmp, fmt = tmp + 80, w = (uint32_t)m->w, h = (uint32_t)m->h;
    memset(mb + tmp, 0, 256);
    put_guid(c, mt, 0x73646976, 0x0000, 0x0010, g_vid);
    put_guid(c, mt + 16, 0xe436eb7e, 0x524f, 0x11ce, g_rgb); // MEDIASUBTYPE_RGB32: the only one the game's renderer accepts
    W32(mt + 32, 1); W32(mt + 40, w * h * 4);
    put_guid(c, mt + 44, 0x05589f80, 0xc356, 0x11ce, g_vi);
    W32(mt + 64, 88); W32(mt + 68, fmt);
    W32(fmt + 16, w); W32(fmt + 20, h); W32(fmt + 24, w); W32(fmt + 28, h);        // rcSource/rcTarget
    W32(fmt + 40, 333667); W32(fmt + 44, 0);                                          // AvgTimePerFrame
    W32(fmt + 48, 40); W32(fmt + 52, w); W32(fmt + 56, h);                            // BITMAPINFOHEADER
    W16(fmt + 60, 1); W16(fmt + 62, 32); W32(fmt + 68, w * h * 4);
    uint32_t hr = gcall(c, in, 4, 2, out, mt); // IPin::ReceiveConnection
    rt_log("[movie] ReceiveConnection -> %08x\n", hr);
    if ((int32_t)hr < 0) return hr;
    m->rpin = in;
    uint32_t pm = tmp + 160;
    W32(pm, 0);
    uint8_t g_mi[8] = {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70};
    put_guid(c, tmp + 176, 0x56a8689d, 0x0ad4, 0x11ce, g_mi);
    hr = gcall(c, in, 0, 2, tmp + 176, pm);   // QueryInterface(IID_IMemInputPin)
    m->mem_in = R32(pm);
    if ((int32_t)hr < 0 || !m->mem_in) return hr ? hr : E_NOINTERFACE_;
    W32(pm, 0);
    hr = gcall(c, m->mem_in, 3, 1, pm);       // GetAllocator (CoCreateInstance(CLSID_MemoryAllocator) -> host)
    m->alloc = R32(pm);
    rt_log("[movie] GetAllocator -> %08x (%08x)\n", hr, m->alloc);
    if ((int32_t)hr < 0 || !m->alloc) return hr ? hr : HR_FAIL;
    uint32_t req = tmp + 192;
    W32(req, 2); W32(req + 4, w * h * 4); W32(req + 8, 1); W32(req + 12, 0);
    hr = gcall(c, m->alloc, 3, 2, req, req + 16);  // SetProperties
    rt_log("[movie] SetProperties -> %08x\n", hr);
    hr = gcall(c, m->mem_in, 4, 2, m->alloc, 0u);  // NotifyAllocator
    rt_log("[movie] NotifyAllocator -> %08x\n", hr);
    po->peer = in;
    return hr;
}
M(gr, Connect) {
    HOSTFN; MV;
    uint32_t out = ARG(2), in = ARG(3);
    rt_log("[movie-gr] Connect(out=%08x in=%08x)\n", out, in);
    HObj *po = (HObj *)com_get(out), *pi = (HObj *)com_get(in);
    if (po && pi) { po->peer = in; pi->peer = out; STDRET(3, HR_OK); }
    if (!po || pi) STDRET(3, 0x80040217u);
    STDRET(3, connect_host_guest(c, m, po, out, in));
}
static uint32_t movie_open_source(Movie *m, const char *host);
// IGraphBuilder::Render(pin): SH3 hands over its own source filter's output pin and expects the graph to
// build the rest. The host decodes the file itself (pl_mpeg + demangler), so the guest source filter is not run.
M(gr, Render) {
    HOSTFN; MV;
    uint32_t pin = ARG(2);
    rt_log("[movie-gr] Render(pin=%08x) file=\"%s\"\n", pin, g_movie_note);
    uint32_t rin = 0;
    for (int i = 0; i < m->nguest && !rin; i++) {
        uint32_t p = guest_find_pin(c, m->guest[i], 0);
        if (p) { rin = p; m->renderer = m->guest[i]; }
    }
    if (!rin) { rt_log("[movie] Render: no guest renderer input pin\n"); STDRET(2, 0x80040217u); }
    uint32_t hr = movie_open_source(m, g_movie_note);
    if ((int32_t)hr < 0) STDRET(2, hr);
    HObj *vo = NULL;
    for (int k = 0; k < 10; k++)
        if (m->pins[k] && m->pins[k]->role == F_VCODEC && m->pins[k]->dir == 1) vo = m->pins[k];
    if (!vo) STDRET(2, HR_FAIL);
    hr = connect_host_guest(c, m, vo, vo->com.guest, rin);
    rt_log("[movie] Render -> %08x (renderer %08x pin %08x)\n", hr, m->renderer, rin);
    STDRET(2, hr);
}
static uint32_t movie_open_source(Movie *m, const char *host) {
#ifdef __vita__
    {   // preconverted MP4 + hardware decoder (tools/convert_movies.py); SH3_MV_MP4=0 disables it
        const char *off = getenv("SH3_MV_MP4"), *e = strrchr(host, '.');
        if ((!off || strcmp(off, "0")) && e && !strcasecmp(e, ".000") && av_open(host)) {
            m->w = s_vw; m->h = s_vh;
            g_movie_w = s_vw ? movie_pot(s_vw) : 0; g_movie_h = s_vh ? movie_pot(s_vh) : 0;   // the game's own texture size (512x512 for 512x384); 0 until the async stream info arrives
            g_movie_avplayer = 1;
            const char *feed = getenv("SH3_MV_FEED");
            s_feed = !(feed && !strcmp(feed, "0"));   // default on: without samples the game's movie loop runs at 10 fps
            m->nsink = 0;
            rt_log("[movie-mode] avplayer (mp4), feed=%d, game texture %dx%d\n", s_feed, g_movie_w, g_movie_h);
            return HR_OK;
        }
    }
#endif
    const char *cache = getenv("SH4_MOVIE_CACHE"), *all = getenv("SH4_MOVIE_ALL_FRAMES");
    int use_cache = cache && !strcmp(cache, "1");
    const char *ext = strrchr(host, '.');
    if (ext && !strcasecmp(ext, ".000")) use_cache = 0;   // the stream cache reads raw bytes; .000 needs the demangler
#ifdef __vita__
    if (!cache && !(ext && !strcasecmp(ext, ".000"))) use_cache = 1;
#endif
    if (use_cache) m->stream = movie_stream_open(host);
    g_movie_all_frames = all ? !strcmp(all, "1") : m->stream != NULL;
    rt_log("[movie-mode] shared_cache=%d all_frames=%d\n", m->stream != NULL, g_movie_all_frames);
    plm_buffer_t *fb = movie_input(m, host, 0);
    m->plm = fb ? plm_create_with_buffer(fb, TRUE) : NULL;
    rt_log("[movie] source \"%s\" -> %s\n", host, m->plm ? "ok" : "FAILED");
#ifdef SH3_PORT
    { extern unsigned g_frame; uint8_t *mb = g_mem;   // SH3: which table entry (0x710c48, stride 8) the game asked for
      rt_log("[movie-game] frame=%u index=%d state=%u\n", g_frame, (int)(int32_t)R32(0xbc93b0), R32(0xbc93b4)); }
#endif
    if (!m->plm) return VFW_E_NOT_FOUND;
    plm_set_loop(m->plm, 0);
    plm_set_audio_enabled(m->plm, 0);
    plm_set_video_decode_callback(m->plm, on_video, m);
    if (plm_get_num_audio_streams(m->plm) && (fb = movie_input(m, host, 1))) {
        m->adm = plm_demux_create(fb, TRUE);
        plm_buffer_t *ab = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
        plm_buffer_set_load_callback(ab, apkt_load, m->adm);
        m->aud = plm_audio_create_with_buffer(ab, TRUE);
    }
    m->w = plm_get_width(m->plm);
    m->h = plm_get_height(m->plm);
    g_movie_w = m->w; g_movie_h = m->h;
    rt_log("[movie] %dx%d, %.2f i/s, %.1f s, audio=%d\n", m->w, m->h, plm_get_framerate(m->plm), plm_get_duration(m->plm),
           plm_get_num_audio_streams(m->plm));
    return HR_OK;
}
M(gr, AddSourceFilter) {
    HOSTFN; MV;
    char path[600], host[700];
    uint32_t w = ARG(2);
    size_t i = 0;
    for (; i < sizeof path - 1 && R16(w + 2 * i); i++) path[i] = (char)R16(w + 2 * i);
    path[i] = 0;
    w32_host_path(path, host, sizeof host);
    uint32_t hr = movie_open_source(m, host);
    if ((int32_t)hr < 0) { W32(ARG(4), 0); STDRET(4, hr); }
    W32(ARG(4), m->filters[F_SRC]->com.guest);
    STDRET(4, HR_OK);
}
#define GRSTUB(nm, n, hr) M(gr, nm) { HOSTFN; rt_log("[movie-gr] " #nm "(%08x,%08x,%08x)\n", ARG(2), ARG(3), ARG(4)); STDRET(n, hr); }
GRSTUB(RemoveFilter, 2, HR_OK)
GRSTUB(EnumFilters, 2, E_NOTIMPL_)
GRSTUB(FindFilterByName, 3, E_NOTIMPL_)
GRSTUB(ConnectDirect, 4, HR_OK)
GRSTUB(Reconnect, 2, HR_OK)
GRSTUB(Disconnect, 2, HR_OK)
GRSTUB(SetDefaultSyncSource, 1, HR_OK)
GRSTUB(RenderFile, 3, E_NOTIMPL_)
GRSTUB(SetLogFile, 2, HR_OK)
GRSTUB(Abort, 1, HR_OK)
GRSTUB(ShouldOperationContinue, 1, HR_OK)
static const gfn gr_vt[] = {gr_QI, any_AddRef, any_Release, gr_AddFilter, gr_RemoveFilter, gr_EnumFilters,
    gr_FindFilterByName, gr_ConnectDirect, gr_Reconnect, gr_Disconnect,
    gr_SetDefaultSyncSource, gr_Connect, gr_Render, gr_RenderFile, gr_AddSourceFilter,
    gr_SetLogFile, gr_Abort, gr_ShouldOperationContinue};

// ---------- entry points for CoCreateInstance ----------
static Movie *movie_new(void) {
    Movie *m = calloc(1, sizeof *m);
    m->lock = SDL_CreateMutex();
    m->vol = -10000;
    m->graph_g = mkobj(m, MK_GRAPH, gr_vt, 18, "IGraphBuilder", &m->graph);
    m->ctl_g = mkobj(m, MK_CTL, ctl_vt, 16, "IMediaControl", NULL);
    m->pos_g = mkobj(m, MK_POS, pos_vt, 18, "IMediaPosition", NULL);
    m->ev_g = mkobj(m, MK_EV, ev_vt, 16, "IMediaEventEx", NULL);
    m->aud_g = mkobj(m, MK_AUD, au_vt, 11, "IBasicAudio", NULL);
    m->sink_g = mkobj(m, MK_SINK, sink_vt, 4, "IMediaEventSink", NULL);
    const char *none[2] = {0, 0}, *vs[2] = {"Audio", "Video"}, *one[2] = {"Output", 0};
    make_filter(m, F_SRC, 0, 1, NULL, one);
    make_filter(m, F_SPLIT, 1, 2, "Input", vs);
    make_filter(m, F_ACODEC, 1, 1, "Input", one);
    make_filter(m, F_VCODEC, 1, 1, "Input", one);
    make_filter(m, F_DSR, 1, 0, "Input", none);
    return m;
}

// Returns 1 if the CLSID is handled (object written to *out), 0 otherwise.
int movie_cocreate(CPU *c, uint32_t clsid, uint32_t riid, uint32_t out, uint32_t *hr) {
    HOSTFN;
    (void)riid;
    uint32_t d1 = R32(clsid);
    int which = d1 == 0x336475d0 ? F_SPLIT : d1 == 0x4a2286e0 ? F_ACODEC : d1 == 0xfeb50740 ? F_VCODEC : d1 == 0x79376820 ? F_DSR : -1;
    if (d1 == 0xe436ebb3) { // CLSID_FilterGraph : nouveau film
        if (g_mv) {                                             // movie finished without Stop: without a join, dead threads keep their stack and creating the next one fails
            g_mv->stop = 1;
            if (g_mv->thread) { SDL_WaitThread(g_mv->thread, NULL); g_mv->thread = NULL; }
            if (g_mv->othread) { SDL_WaitThread(g_mv->othread, NULL); g_mv->othread = NULL; }
            if (g_mv->athread) { SDL_WaitThread(g_mv->athread, NULL); g_mv->athread = NULL; }
#ifdef __vita__
            if (g_movie_avplayer) av_close();
#endif
            movie_free_decoders(g_mv);
        }
        // ponytail: the rest of the previous movie (COM objects, ~1 KB) leaks
        g_mv = movie_new();
        W32(out, g_mv->graph_g);
        *hr = HR_OK;
        return 1;
    }
    if (d1 == 0x1e651cc0 && g_mv) { // CLSID_MemoryAllocator
        if (!g_mv->alloc) g_mv->alloc = mkobj(g_mv, MK_ALLOC, al_vt, 9, "IMemAllocator", NULL);
        W32(out, g_mv->alloc);
        *hr = HR_OK;
        return 1;
    }
    if (which < 0 || !g_mv) return 0;
    W32(out, g_mv->filters[which]->com.guest);
    *hr = HR_OK;
    return 1;
}
