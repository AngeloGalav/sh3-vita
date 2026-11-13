// ADVAPI32 (emulated registry), GDI32, IMM32, WINMM, OLE32.
#include "win32.h"
#include <stdlib.h>

// ---------- registry: small in-memory table ----------
typedef struct { char key[128], name[64]; uint32_t type, size; uint8_t data[260]; } RegVal;
#define MAX_REG 64
static RegVal reg[MAX_REG];
static int reg_n;
static const char *SH4KEY = "SOFTWARE\\Konami\\SILENT HILL 4\\1.00.000";
static const char *SH3KEY = "SOFTWARE\\KONAMI\\SILENT HILL 3";

static RegVal *reg_find(const char *key, const char *name) {
    for (int i = 0; i < reg_n; i++)
        if (!SDL_strcasecmp(reg[i].key, key) && !SDL_strcasecmp(reg[i].name, name)) return &reg[i];
    return NULL;
}
static void reg_put(const char *key, const char *name, uint32_t type, const void *data, uint32_t size) {
    RegVal *v = reg_find(key, name);
    if (!v) {
        if (reg_n == MAX_REG) return;
        v = &reg[reg_n++];
        SDL_strlcpy(v->key, key, sizeof v->key);
        SDL_strlcpy(v->name, name, sizeof v->name);
    }
    v->type = type;
    v->size = size > sizeof v->data ? sizeof v->data : size;
    memcpy(v->data, data, v->size);
}
static void reg_defaults(void) {
    static int done;
    if (done) return;
    done = 1;
    const char *lang = getenv("SH4_LANG") ? getenv("SH4_LANG") : "English";
    reg_put(SH4KEY, "Install Language", 1, lang, (uint32_t)strlen(lang) + 1);
    reg_put(SH4KEY, "Movie Install", 1, GUEST_ROOT, (uint32_t)strlen(GUEST_ROOT) + 1);
    // SH3: install root for data / movie / save (the game appends "data/...", "data/movie/...")
    reg_put(SH3KEY, "data", 1, GUEST_ROOT, (uint32_t)strlen(GUEST_ROOT) + 1);
    reg_put(SH3KEY, "movie", 1, GUEST_ROOT, (uint32_t)strlen(GUEST_ROOT) + 1);
    reg_put(SH3KEY, "save", 1, GUEST_ROOT, (uint32_t)strlen(GUEST_ROOT) + 1);
    reg_put(SH3KEY, "sound", 1, GUEST_ROOT, (uint32_t)strlen(GUEST_ROOT) + 1);
}

typedef struct { char path[128]; } RegKey;
#define A(name) void imp_ADVAPI32_##name(CPU *c)
A(RegOpenKeyA) {
    HOSTFN;
    reg_defaults();
    RegKey *k = calloc(1, sizeof *k);
    SDL_strlcpy(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "", sizeof k->path);
    W32(ARG(3), h_new(H_REGKEY, k));
    STDRET(3, 0);
}
A(RegOpenKeyExA) {   // (hKey, subkey, options, sam, out)
    HOSTFN;
    reg_defaults();
    RegKey *k = calloc(1, sizeof *k);
    SDL_strlcpy(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "", sizeof k->path);
    W32(ARG(5), h_new(H_REGKEY, k));
    STDRET(5, 0);
}
A(RegCloseKey) { HOSTFN; free(h_get(ARG(1), H_REGKEY)); h_close(ARG(1)); STDRET(1, 0); }
A(RegQueryValueExA) {
    HOSTFN;
    RegKey *k = h_get(ARG(1), H_REGKEY);
    RegVal *v = k ? reg_find(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "") : NULL;
    if (!v) STDRET(6, 2); // ERROR_FILE_NOT_FOUND
    if (ARG(4)) W32(ARG(4), v->type);
    uint32_t cap = ARG(6) ? R32(ARG(6)) : 0;
    if (ARG(6)) W32(ARG(6), v->size);
    if (ARG(5)) {
        if (cap < v->size) STDRET(6, 234); // ERROR_MORE_DATA
        memcpy(mb + ARG(5), v->data, v->size);
    }
    STDRET(6, 0);
}
A(RegSetValueExA) {
    HOSTFN;
    RegKey *k = h_get(ARG(1), H_REGKEY);
    if (k) reg_put(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "", ARG(4), mb + ARG(5), ARG(6));
    STDRET(6, 0);
}
A(RegDeleteValueA) {
    HOSTFN;
    RegKey *k = h_get(ARG(1), H_REGKEY);
    RegVal *v = k ? reg_find(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "") : NULL;
    if (v) *v = reg[--reg_n];
    STDRET(2, v ? 0 : 2);
}

// ---------- GDI32 / IMM32 ----------
void imp_GDI32_DeleteObject(CPU *c) { STDRET(1, 1); }
void imp_IMM32_ImmAssociateContextEx(CPU *c) { STDRET(3, 1); }

// ---------- WINMM ----------
#define W(name) void imp_WINMM_##name(CPU *c)
W(timeBeginPeriod) { STDRET(1, 0); }
W(timeEndPeriod) { STDRET(1, 0); }
W(timeGetTime) { STDRET(0, w32_tick_ms()); }

typedef struct { SDL_TimerID id; CPU *c; uint32_t cb, user, periodic, handle; SDL_mutex *m; } MMTimer;
// SH3 arms a one-shot timer about twice per frame. Each one used to get a fresh guest CPU (256 KB stack + TIB
// on the guest heap) that was freed after the callback: constant churn of the guest heap. Idle timer CPUs now
// wait in a small pool and are reused; arming is logged only for the first timers, then counted.
#define MM_POOL 8
static CPU *mm_pool[MM_POOL];
static int mm_pool_n;
static SDL_mutex *mm_pool_m;
static unsigned mm_armed, mm_killed, mm_reused;
static CPU *mm_cpu_get(void) {
    CPU *c = NULL;
    if (!mm_pool_m) mm_pool_m = SDL_CreateMutex();
    SDL_LockMutex(mm_pool_m);
    if (mm_pool_n) c = mm_pool[--mm_pool_n], mm_reused++;
    SDL_UnlockMutex(mm_pool_m);
    return c ? c : rt_new_cpu(0x40000);
}
static void mm_cpu_put(CPU *c) {
    c->fcw = 0x27f;   // the callback's x87 control word does not carry over to the next timer
    SDL_LockMutex(mm_pool_m);
    if (mm_pool_n < MM_POOL) { mm_pool[mm_pool_n++] = c; c = NULL; }
    SDL_UnlockMutex(mm_pool_m);
    if (c) rt_free_cpu(c);
}
static Uint32 mm_tick(Uint32 interval, void *p) {
    MMTimer *t = p;
    SDL_LockMutex(t->m);
    if (t->c) rt_guest_call(t->c, t->cb, 5, t->handle, 0u, t->user, 0u, 0u);
    if (!t->periodic && t->c) { mm_cpu_put(t->c); t->c = NULL; }   // one-shot: back to the pool for the next arm
    SDL_UnlockMutex(t->m);
    return t->periodic ? interval : 0;
}
W(timeSetEvent) {
    HOSTFN;
    MMTimer *t = calloc(1, sizeof *t);
    t->c = mm_cpu_get();
    t->cb = ARG(3);
    t->user = ARG(4);
    t->periodic = ARG(5) & 1;
    t->m = SDL_CreateMutex();
    t->handle = h_new(H_TIMER, t);
    t->id = SDL_AddTimer(ARG(1) ? ARG(1) : 1, mm_tick, t);
    if (++mm_armed <= 4) rt_log("timeSetEvent(%u ms, cb=%08x, periodic=%u)\n", ARG(1), t->cb, t->periodic);
    else if (!(mm_armed % 5000)) rt_log("[winmm] timers armed %u, killed %u, CPUs reused %u\n", mm_armed, mm_killed, mm_reused);
    STDRET(5, t->handle);
}
W(timeKillEvent) {
    HOSTFN;
    MMTimer *t = h_get(ARG(1), H_TIMER);
    if (t) { mm_killed++; SDL_RemoveTimer(t->id); SDL_LockMutex(t->m); if (t->c && t->c != c) { mm_cpu_put(t->c); t->c = NULL; } SDL_UnlockMutex(t->m); h_close(ARG(1)); }   /* a callback killing its own timer: mm_tick frees the CPU once the callback returns */
    STDRET(1, 0);
}

// ---------- OLE32 ----------
#define O(name) void imp_OLE32_##name(CPU *c)
O(CoInitialize) { STDRET(1, 0); }
O(CoUninitialize) { STDRET(0, 0); }
O(CoFreeUnusedLibraries) { STDRET(0, 0); }
O(CoTaskMemAlloc) { HOSTFN; STDRET(1, gheap_alloc(ARG(1))); }
O(CoTaskMemFree) { HOSTFN; gheap_free(ARG(1)); STDRET(1, 0); }
int movie_cocreate(CPU *c, uint32_t clsid, uint32_t riid, uint32_t out, uint32_t *hr);
O(CoCreateInstance) {
    HOSTFN;
    uint32_t g = ARG(1), mhr;
    if (!getenv("SH3_NO_MOVIE") && movie_cocreate(c, g, ARG(4), ARG(5), &mhr)) STDRET(5, mhr);   // SH3_NO_MOVIE=1: movies unavailable (the .000 files are encrypted, J5)
    rt_log("CoCreateInstance({%08x-...}) -> REGDB_E_CLASSNOTREG\n", R32(g));
    W32(ARG(5), 0);
    STDRET(5, 0x80040154u);
}
