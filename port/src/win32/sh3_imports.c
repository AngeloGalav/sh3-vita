// SH3 additions to the emulated Win32 layer: the imports that sh3.exe uses and the SH4 shim lacks (J2).
// Convention (see rt.h): host function receives the guest CPU, esp -> return address, stdcall args at ARG(1..n);
// STDRET(n, v) pops n args. Behaviour: real where the startup path needs it, otherwise a logged stub.
#include "win32.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#define K(name) void imp_KERNEL32_##name(CPU *c)
#define U(name) void imp_USER32_##name(CPU *c)
#define G(name) void imp_GDI32_##name(CPU *c)
#define STUB(tag, name, n, v) tag(name) { STDRET(n, v); }
#define LSTUB(tag, dll, name, n, v) tag(name) { rt_log("[stub] %s!%s\n", dll, #name); STDRET(n, v); }
static int s_synclog;
#define SLOG(...) do { if (getenv("SH3_SYNCLOG") && s_synclog++ < 400) rt_log(__VA_ARGS__); } while (0)
#define CDECLRET(v) do { c->eax = (uint32_t)(v); c->esp += 4u; return; } while (0)

// ---------- KERNEL32: atomics, sync ----------
K(InterlockedExchange) { HOSTFN; STDRET(2, __atomic_exchange_n((uint32_t *)(mb + ARG(1)), ARG(2), __ATOMIC_SEQ_CST)); }
K(InterlockedExchangeAdd) { HOSTFN; STDRET(2, __atomic_fetch_add((uint32_t *)(mb + ARG(1)), ARG(2), __ATOMIC_SEQ_CST)); }
K(InterlockedCompareExchange) {
    HOSTFN;
    uint32_t exp = ARG(3);
    __atomic_compare_exchange_n((uint32_t *)(mb + ARG(1)), &exp, ARG(2), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    STDRET(3, exp);   // returns the initial destination value
}
K(WaitForSingleObject) { HOSTFN; uint32_t h = ARG(1); SLOG("[sync] cpu=%p WaitForSingleObject(%x,%u)\n", (void *)c, h, ARG(2)); uint32_t r = w32_wait(1, &h, 0, ARG(2)); SLOG("[sync] cpu=%p Wait(%x) -> %u\n", (void *)c, h, r); STDRET(2, r); }
K(ReleaseMutex) { STDRET(1, 1); }
K(PulseEvent) { HOSTFN; SLOG("[sync] cpu=%p PulseEvent(%x)\n", (void *)c, ARG(1)); w32_event_pulse(ARG(1)); STDRET(1, 1); }
K(CreateSemaphoreA) {   // modelled as an auto-reset event signalled while the count is non-zero (enough for the game's use)
    HOSTFN; Event *e = calloc(1, sizeof *e); e->manual = 0; e->state = ARG(2) > 0;
    w32_set_last_error(c, 0); STDRET(4, h_new(H_EVENT, e));
}
K(ReleaseSemaphore) { HOSTFN; w32_event_set(ARG(1), 1); if (ARG(3)) W32(ARG(3), 0); STDRET(3, 1); }
K(DuplicateHandle) { HOSTFN; if (ARG(4)) W32(ARG(4), ARG(2)); STDRET(7, 1); }
static Thread *thread_of(const CPU *c, uint32_t h) { return h == 0xfffffffeu ? w32_thread_of_cpu(c) : h_get(h, H_THREAD); }
K(SuspendThread) {   // a thread suspending itself (the CRI workers idle this way) blocks until another thread resumes it
    HOSTFN; Thread *t = thread_of(c, ARG(1));
    SLOG("[sync] cpu=%p SuspendThread(%x) self=%d\n", (void *)c, ARG(1), t && t->c == c);
    if (!t) STDRET(1, 0xffffffffu);
    SDL_LockMutex(g_sync);
    int prev = t->susp++;
    if (t->c == c) while (t->susp > 0) w32_cv_wait(SDL_MUTEX_MAXWAIT);
    SDL_UnlockMutex(g_sync);
    STDRET(1, prev);   // suspending ANOTHER thread only counts: guest threads cannot be stopped asynchronously here
}
K(ResumeThread) {
    HOSTFN; Thread *t = thread_of(c, ARG(1));
    SLOG("[sync] cpu=%p ResumeThread(%x)\n", (void *)c, ARG(1));
    if (!t) STDRET(1, 0xffffffffu);
    SDL_LockMutex(g_sync);
    int prev = t->susp;
    if (prev > 0) t->susp--;
    w32_cv_broadcast();
    SDL_UnlockMutex(g_sync);
    STDRET(1, prev);
}
K(ExitThread) { HOSTFN; rt_log("ExitThread(%u)\n", ARG(1)); SDL_Delay(1); for (;;) SDL_Delay(1000); }
K(SetThreadAffinityMask) { STDRET(2, 1); }
K(SetThreadPriorityBoost) { STDRET(2, 1); }
K(SetConsoleCtrlHandler) { STDRET(2, 1); }
K(SetErrorMode) { STDRET(1, 0); }
K(OutputDebugStringA) { HOSTFN; rt_log("[dbg] %s", GSTR(ARG(1)) ? GSTR(ARG(1)) : ""); STDRET(1, 0); }
K(FatalAppExitA) { HOSTFN; rt_fatal("FatalAppExit: %s", GSTR(ARG(2)) ? GSTR(ARG(2)) : ""); }
K(GetProcessHeap) { STDRET(0, 0x00a00000u); }
K(lstrlenA) { HOSTFN; STDRET(1, GSTR(ARG(1)) ? strlen(GSTR(ARG(1))) : 0); }
K(SetEnvironmentVariableA) { STDRET(2, 1); }

// ---------- KERNEL32: files, directories, mappings ----------
K(CreateFileW) {   // path narrowed to latin-1, then forwarded to CreateFileA (same 7 arguments)
    HOSTFN;
    uint32_t wp = ARG(1); char tmp[600]; unsigned i = 0;
    if (wp) for (; i < sizeof tmp - 1 && R16(wp + 2 * i); i++) tmp[i] = (char)R16(wp + 2 * i);
    tmp[i] = 0;
    uint32_t g = gstrdup(tmp);
    W32(c->esp + 4, g);
    extern void imp_KERNEL32_CreateFileA(CPU *);
    imp_KERNEL32_CreateFileA(c);
    gheap_free(g);
}
K(DeleteFileA) { HOSTFN; char hp[600]; w32_host_path_w(GSTR(ARG(1)), hp, sizeof hp); STDRET(1, unlink(hp) == 0); }
K(SetEndOfFile) { STDRET(1, 1); }
// FindFirstFileA / FindNextFileA / FindClose over the host folder (w32_host_path: the save-dir overlay first).
// They used to find nothing, always. After writing data.sys at every boot, SH3 lists the save folder to confirm the
// save (0x6016b0: FindFirstFileA through IAT 0x68a1e4, failure -> 0x60192c), and MSVC's _stat (0x664aa8) is built on
// FindFirstFileA too: so every boot ended in "Error occurred during system data saving. Data not saved." although
// the file had been written. Patterns: '*' and '?', case-insensitive; a name without wildcards finds that entry.
typedef struct { DIR *d; char dir[600], pat[260]; int single; } Find;
static int find_match(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (*p == '*') { for (;; s++) { if (find_match(p + 1, s)) return 1; if (!*s) return 0; } }
        if (!*s || (*p != '?' && SDL_tolower((unsigned char)*p) != SDL_tolower((unsigned char)*s))) return 0;
    }
    return !*s;
}
static void find_fill(CPU *c, uint32_t fd, const char *dir, const char *name) {
    HOSTFN; (void)c;
    char hp[900]; struct stat st;
    SDL_snprintf(hp, sizeof hp, "%s/%s", dir, name);
    int ok = !stat(hp, &st);
    for (unsigned i = 0; i < 320; i += 4) W32(fd + i, 0);
    W32(fd, ok && S_ISDIR(st.st_mode) ? 0x10u : 0x20u);   // FILE_ATTRIBUTE_DIRECTORY / ARCHIVE
    uint64_t ft = ((uint64_t)(ok ? st.st_mtime : 0) + 11644473600ull) * 10000000ull;   // FILETIME, 100 ns since 1601
    for (unsigned k = 0; k < 3; k++) { W32(fd + 4 + 8 * k, (uint32_t)ft); W32(fd + 8 + 8 * k, (uint32_t)(ft >> 32)); }
    W32(fd + 0x20, ok && !S_ISDIR(st.st_mode) ? (uint32_t)st.st_size : 0);
    size_t n = strlen(name); if (n > 259) n = 259;
    memcpy(mb + fd + 0x2c, name, n); mb[fd + 0x2c + n] = 0;
}
static int find_next(CPU *c, Find *f, uint32_t fd) {
    if (f->single) { if (f->single++ > 1) return 0; find_fill(c, fd, f->dir, f->pat); return 1; }
    for (struct dirent *e; f->d && (e = readdir(f->d));)
        if (find_match(f->pat, e->d_name)) { find_fill(c, fd, f->dir, e->d_name); return 1; }
    return 0;
}
K(FindFirstFileA) {
    HOSTFN;
    const char *g = GSTR(ARG(1)); uint32_t fd = ARG(2);
    char gd[600]; SDL_snprintf(gd, sizeof gd, "%s", g ? g : "");
    char *sep = strrchr(gd, '\\'); char *sep2 = strrchr(gd, '/'); if (sep2 > sep) sep = sep2;
    Find *f = calloc(1, sizeof *f);
    if (!f || !g) { free(f); w32_set_last_error(c, 2); STDRET(2, 0xffffffffu); }
    SDL_snprintf(f->pat, sizeof f->pat, "%s", sep ? sep + 1 : gd);
    if (sep) sep[1] = 0; else SDL_snprintf(gd, sizeof gd, ".");   // keep the separator: "C:\\SH3\\" maps to the game folder
    if (!f->pat[0]) SDL_snprintf(f->pat, sizeof f->pat, ".");      // "dir\\" names the folder itself
    w32_host_path(gd, f->dir, sizeof f->dir);
    if (!strpbrk(f->pat, "*?")) {
        char hp[900]; struct stat st; SDL_snprintf(hp, sizeof hp, "%s/%s", f->dir, f->pat);
        if (stat(hp, &st)) { w32_host_path(g, hp, sizeof hp); if (stat(hp, &st)) { free(f); w32_set_last_error(c, 2); STDRET(2, 0xffffffffu); } }
        f->single = 1;
    } else f->d = opendir(f->dir);
    if (!find_next(c, f, fd)) { if (f->d) closedir(f->d); free(f); w32_set_last_error(c, 2); rt_log("FindFirstFileA(\"%s\") -> none\n", g); STDRET(2, 0xffffffffu); }
    uint32_t h = h_new(H_FIND, f);
    rt_log("FindFirstFileA(\"%s\") -> %x \"%s\"\n", g, h, (const char *)(mb + fd + 0x2c));
    STDRET(2, h);
}
K(FindNextFileA) {
    HOSTFN;
    Find *f = h_get(ARG(1), H_FIND);
    if (f && find_next(c, f, ARG(2))) STDRET(2, 1);
    w32_set_last_error(c, 18);   // ERROR_NO_MORE_FILES
    STDRET(2, 0);
}
K(FindClose) {
    HOSTFN;
    Find *f = h_get(ARG(1), H_FIND);
    if (!f) STDRET(1, 0);
    if (f->d) closedir(f->d);
    free(f);
    h_close(ARG(1));
    STDRET(1, 1);
}
K(GetCurrentDirectoryA) {
    HOSTFN; const char *s = "C:\\SH3"; uint32_t n = ARG(1), p = ARG(2), l = (uint32_t)strlen(s);
    if (p && n > l) { memcpy(mb + p, s, l + 1); STDRET(2, l); }
    STDRET(2, l + 1);
}
K(SetCurrentDirectoryA) { STDRET(1, 1); }
K(GetFullPathNameA) {
    HOSTFN; const char *in = GSTR(ARG(1)); uint32_t n = ARG(2), p = ARG(3), l = (uint32_t)strlen(in);
    if (n > l) { memcpy(mb + p, in, l + 1); if (ARG(4)) { const char *b = strrchr(in, '\\'); W32(ARG(4), p + (b ? (uint32_t)(b + 1 - in) : 0)); } STDRET(4, l); }
    STDRET(4, l + 1);
}
K(GetLogicalDrives) { STDRET(0, 1u << 2); }   // C:
// ---------- KERNEL32: time, locale ----------
static void fill_systemtime(uint32_t p, const struct tm *t, uint32_t ms) {
    HOSTFN;
    W16(p, t->tm_year + 1900); W16(p + 2, t->tm_mon + 1); W16(p + 4, t->tm_wday); W16(p + 6, t->tm_mday);
    W16(p + 8, t->tm_hour); W16(p + 10, t->tm_min); W16(p + 12, t->tm_sec); W16(p + 14, ms);
}
K(GetLocalTime) { HOSTFN; time_t tt = time(NULL); struct tm t; localtime_r(&tt, &t); fill_systemtime(ARG(1), &t, 0); STDRET(1, 0); }
K(FileTimeToLocalFileTime) { HOSTFN; W64(ARG(2), R64(ARG(1))); STDRET(2, 1); }
K(FileTimeToSystemTime) {
    HOSTFN; uint64_t ft = R64(ARG(1)); time_t tt = (time_t)(ft / 10000000ull - 11644473600ull); struct tm t; gmtime_r(&tt, &t);
    fill_systemtime(ARG(2), &t, (uint32_t)((ft / 10000ull) % 1000)); STDRET(2, 1);
}
K(GetTimeZoneInformation) { HOSTFN; memset(mb + ARG(1), 0, 172); STDRET(1, 0); }
K(GetDateFormatA) { HOSTFN; if (ARG(5) && ARG(6) > 1) { W8(ARG(5), 0); } STDRET(6, 1); }
K(GetTimeFormatA) { HOSTFN; if (ARG(5) && ARG(6) > 1) { W8(ARG(5), 0); } STDRET(6, 1); }
K(GetUserDefaultLCID) { STDRET(0, 0x409); }
K(GetUserDefaultLangID) { STDRET(0, 0x409); }
K(GetThreadLocale) { STDRET(0, 0x409); }
K(IsValidLocale) { STDRET(2, 1); }
K(IsValidCodePage) { STDRET(1, 1); }
K(EnumSystemLocalesA) { STDRET(2, 1); }
K(GetLocaleInfoW) {   // empty string answer; the CRT only probes
    HOSTFN; if (ARG(3) && ARG(4) > 0) W16(ARG(3), 0); STDRET(4, 1);
}
static uint32_t cmp_generic(CPU *c, int wide) {
    HOSTFN; uint32_t a = ARG(3), la = ARG(4), b = ARG(5), lb = ARG(6), flags = ARG(2);
    int ci = (flags & 1) != 0;
    uint32_t i = 0;
    for (;; i++) {
        int ea = (int)la >= 0 ? i >= la : 0, eb = (int)lb >= 0 ? i >= lb : 0;
        uint32_t x = ea ? 0 : wide ? R16(a + 2 * i) : R8(a + i), y = eb ? 0 : wide ? R16(b + 2 * i) : R8(b + i);
        if (!ea && (int)la < 0 && x == 0) ea = 1;
        if (!eb && (int)lb < 0 && y == 0) eb = 1;
        if (ea || eb) return ea && eb ? 2 : ea ? 1 : 3;
        if (ci) { if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; }
        if (x != y) return x < y ? 1 : 3;
    }
}
K(CompareStringA) { STDRET(6, cmp_generic(c, 0)); }
K(CompareStringW) { STDRET(6, cmp_generic(c, 1)); }

// ---------- USER32 (window system is faked: one fixed window) ----------
static uint32_t g_wl[8];
U(GetSystemMetrics) {
    HOSTFN; switch (ARG(1)) { case 0: STDRET(1, 1920); case 1: STDRET(1, 1080); case 4: STDRET(1, 19); default: STDRET(1, 0); }
}
U(AdjustWindowRectEx) { STDRET(4, 1); }
U(AttachThreadInput) { STDRET(3, 1); }
U(BeginPaint) { HOSTFN; if (ARG(2)) memset(mb + ARG(2), 0, 64); STDRET(2, 1); }
U(EndPaint) { STDRET(2, 1); }
U(CallWindowProcA) { STDRET(5, 0); }
U(ChangeDisplaySettingsA) { STDRET(2, 0); }   // DISP_CHANGE_SUCCESSFUL
U(ClipCursor) { STDRET(1, 1); }
U(DrawTextA) { STDRET(5, 0); }
U(DrawTextW) { STDRET(5, 0); }
// The Options > Configuration "Resolution" list (FUN_005e2300) keeps only the D3D modes that EnumDisplaySettingsA
// also lists; a failure at index 0 left that list uninitialised (garbage "-1960902656x32574" on the row).
// Report the 4:3 modes of the d3d8 shim's EnumAdapterModes (960x544 is not 4:3 and the game filters it anyway).
U(EnumDisplaySettingsA) {
    HOSTFN;
    static const uint32_t m[][2] = {{640, 480}, {800, 600}, {1024, 768}};
    uint32_t i = ARG(2), p = ARG(3);
    if (i == 0xffffffffu) i = 2;   // ENUM_CURRENT_SETTINGS: the d3d8 shim's GetAdapterDisplayMode (1024x768)
    if (i >= 3 || !p) STDRET(3, 0);
    memset(mb + p + 0x26, 0, 0x9c - 0x26);   // keep dmDeviceName/dmSpecVersion/dmSize as the caller set them
    W16(p + 0x24, 0x9c);
    W32(p + 0x28, 0x00040000u | 0x00080000u | 0x00100000u | 0x00400000u);   // BITSPERPEL | PELSWIDTH | PELSHEIGHT | DISPLAYFREQUENCY
    W32(p + 0x68, 32); W32(p + 0x6c, m[i][0]); W32(p + 0x70, m[i][1]); W32(p + 0x78, 60);
    STDRET(3, 1);
}
U(FindWindowA) { STDRET(2, 0); }
U(GetClassLongA) { STDRET(2, 0); }
U(GetKeyState) { STDRET(1, 0); }
U(GetMessageA) { rt_log("[stub] USER32!GetMessageA -> WM_QUIT\n"); STDRET(4, 0); }
U(GetWindowLongA) { HOSTFN; STDRET(2, g_wl[ARG(2) & 7]); }
U(SetWindowLongA) { HOSTFN; uint32_t o = g_wl[ARG(2) & 7]; g_wl[ARG(2) & 7] = ARG(3); STDRET(3, o); }
uint32_t sh3_wndproc_subclass(void) { return g_wl[(uint32_t)-4 & 7]; }   // GWL_WNDPROC set by the game, for DispatchMessageA
U(GetWindowRect) { HOSTFN; if (ARG(2)) { W32(ARG(2), 0); W32(ARG(2) + 4, 0); W32(ARG(2) + 8, 640); W32(ARG(2) + 12, 480); } STDRET(2, 1); }
U(GetWindowThreadProcessId) { HOSTFN; if (ARG(2)) W32(ARG(2), 0x10); STDRET(2, 0x100); }
U(InvalidateRect) { STDRET(3, 1); }
U(IsIconic) { STDRET(1, 0); }
U(KillTimer) { STDRET(2, 1); }
U(RedrawWindow) { STDRET(4, 1); }
U(RegisterDeviceNotificationA) { STDRET(3, 0x1234); }
U(UnregisterDeviceNotification) { STDRET(1, 1); }
U(ScreenToClient) { STDRET(2, 1); }
U(SetCursor) { STDRET(1, 0); }
U(SetFocus) { STDRET(1, 0); }
U(SetForegroundWindow) { STDRET(1, 1); }
U(SetWindowPos) { STDRET(7, 1); }

// printf-style formatting for wsprintfA / wvsprintfA: args come from guest memory at `ap` (4-byte slots)
static int guest_vsprintf(uint8_t *mb, uint32_t out, uint32_t fmt, uint32_t ap) {
    uint32_t o = out;
    for (uint32_t f = fmt; R8(f); f++) {
        if (R8(f) != '%') { W8(o++, R8(f)); continue; }
        f++;
        int left = 0, zero = 0, width = 0;
        for (;; f++) { if (R8(f) == '-') left = 1; else if (R8(f) == '0') zero = 1; else break; }
        while (R8(f) >= '0' && R8(f) <= '9') width = width * 10 + (R8(f++) - '0');
        while (R8(f) == 'l' || R8(f) == 'h') f++;
        char t[64]; int n = 0;
        switch (R8(f)) {
        case 'd': case 'i': n = snprintf(t, sizeof t, "%d", (int)R32(ap)); ap += 4; break;
        case 'u': n = snprintf(t, sizeof t, "%u", R32(ap)); ap += 4; break;
        case 'x': n = snprintf(t, sizeof t, "%x", R32(ap)); ap += 4; break;
        case 'X': n = snprintf(t, sizeof t, "%X", R32(ap)); ap += 4; break;
        case 'c': t[0] = (char)R32(ap); n = 1; ap += 4; break;
        case 's': { uint32_t s = R32(ap); ap += 4; const char *p = s ? (const char *)(mb + s) : "(null)"; n = (int)strlen(p); if (n > 60) n = 60; memcpy(t, p, n); break; }
        case '%': t[0] = '%'; n = 1; break;
        default: t[0] = R8(f); n = 1; break;
        }
        if (!left) for (int k = n; k < width; k++) W8(o++, zero ? '0' : ' ');
        for (int k = 0; k < n; k++) W8(o++, (uint8_t)t[k]);
        if (left) for (int k = n; k < width; k++) W8(o++, ' ');
    }
    W8(o, 0);
    return (int)(o - out);
}
U(wsprintfA) { HOSTFN; int n = guest_vsprintf(mb, ARG(1), ARG(2), c->esp + 12); CDECLRET(n); }   // cdecl
U(wvsprintfA) { HOSTFN; STDRET(3, guest_vsprintf(mb, ARG(1), ARG(2), ARG(3))); }

// ---------- IMM32 / ADVAPI32 / OLEAUT32 ----------
void imp_IMM32_ImmGetContext(CPU *c) { STDRET(1, 0); }
void imp_IMM32_ImmAssociateContext(CPU *c) { STDRET(2, 0); }
void imp_OLEAUT32_ord2(CPU *c) {   // SysAllocString
    HOSTFN; uint32_t s = ARG(1), n = 0; if (!s) STDRET(1, 0);
    while (R16(s + 2 * n)) n++;
    uint32_t b = gheap_alloc(4 + 2 * n + 2); W32(b, 2 * n); memcpy(mb + b + 4, mb + s, 2 * n + 2); STDRET(1, b + 4);
}
void imp_OLEAUT32_ord6(CPU *c) { HOSTFN; if (ARG(1)) gheap_free(ARG(1) - 4); STDRET(1, 0); }   // SysFreeString

// ---------- GDI32: text rendering is not supported yet (logged stubs) ----------
G(CreateCompatibleDC) { rt_log("[stub] GDI32!CreateCompatibleDC\n"); STDRET(1, 0x7001); }
G(CreateDIBSection) { rt_log("[stub] GDI32!CreateDIBSection -> NULL\n"); STDRET(6, 0); }
G(CreateFontIndirectA) { rt_log("[stub] GDI32!CreateFontIndirectA\n"); STDRET(1, 0x7002); }
G(DeleteDC) { STDRET(1, 1); }
G(GetObjectA) { STDRET(3, 0); }
G(GetStockObject) { STDRET(1, 0x7003); }
G(SelectObject) { STDRET(2, 0x7004); }
G(SetBkColor) { STDRET(2, 0); }
G(SetBkMode) { STDRET(2, 0); }
G(SetTextColor) { STDRET(2, 0); }

// ---------- WINMM mmio*: memory (FOURCC_MEM) and disk files, read-only RIFF parsing (what the game uses for WAV data) ----------
#define H_MMIO 8
typedef struct { uint8_t *data; uint32_t size, pos; int owned; uint32_t g_info; } Mmio;
#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
void imp_WINMM_mmioOpenA(CPU *c) {   // (szFile, lpmmioinfo, flags)
    HOSTFN;
    uint32_t name = ARG(1), info = ARG(2), flags = ARG(3);
    Mmio *m = calloc(1, sizeof *m);
    if (info && R32(info + 4) == FOURCC('M', 'E', 'M', ' ')) {       // memory file
        uint32_t buf = R32(info + 24), n = R32(info + 20);
        m->data = mb + buf; m->size = n; m->g_info = info;
    } else if (name && !(flags & 0x1000)) {                           // disk file, read whole into host memory
        char hp[600]; w32_host_path(GSTR(name), hp, sizeof hp);
        FILE *f = fopen(hp, "rb");
        if (!f) { rt_log("mmioOpenA(\"%s\") not found\n", GSTR(name)); free(m); STDRET(3, 0); }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        m->data = malloc(n ? n : 1); m->size = (uint32_t)n; m->owned = 1;
        if (fread(m->data, 1, n, f) != (size_t)n) { /* short read: keep what we got */ }
        fclose(f);
    } else { rt_log("[mmio] unsupported open flags=%x\n", flags); free(m); STDRET(3, 0); }
    if (info) W32(info + 12, 0);
    STDRET(3, h_new(H_MMIO, m));
}
void imp_WINMM_mmioClose(CPU *c) { HOSTFN; Mmio *m = h_get(ARG(1), H_MMIO); if (m) { if (m->owned) free(m->data); free(m); h_close(ARG(1)); } STDRET(2, 0); }
void imp_WINMM_mmioRead(CPU *c) {
    HOSTFN; Mmio *m = h_get(ARG(1), H_MMIO); uint32_t cb = ARG(3);
    if (!m) STDRET(3, 0xffffffffu);
    if (m->pos + cb > m->size) cb = m->size > m->pos ? m->size - m->pos : 0;
    memcpy(mb + ARG(2), m->data + m->pos, cb); m->pos += cb; STDRET(3, cb);
}
void imp_WINMM_mmioSeek(CPU *c) {   // (hmmio, lOffset, iOrigin)
    HOSTFN; Mmio *m = h_get(ARG(1), H_MMIO); int32_t off = (int32_t)ARG(2); uint32_t org = ARG(3);
    if (!m) STDRET(3, 0xffffffffu);
    int64_t np = org == 0 ? off : org == 1 ? (int64_t)m->pos + off : (int64_t)m->size + off;
    if (np < 0 || np > m->size) STDRET(3, 0xffffffffu);
    m->pos = (uint32_t)np; STDRET(3, m->pos);
}
void imp_WINMM_mmioDescend(CPU *c) {   // (hmmio, lpck, lpckParent, flags)
    HOSTFN; Mmio *m = h_get(ARG(1), H_MMIO); uint32_t ck = ARG(2), parent = ARG(3), flags = ARG(4);
    if (!m || !ck) STDRET(4, 0x10001);   // MMSYSERR / MMIOERR placeholder
    uint32_t endp = m->size;
    if (parent) { uint32_t pe = R32(parent + 12) + R32(parent + 4); if (pe < endp) endp = pe; }
    uint32_t want = R32(ck), wtype = R32(ck + 8);
    for (;;) {
        if (m->pos + 8 > endp) STDRET(4, 0x10002);   // MMIOERR_CHUNKNOTFOUND
        uint32_t id = *(uint32_t *)(m->data + m->pos), sz = *(uint32_t *)(m->data + m->pos + 4), type = 0;
        int list = id == FOURCC('R', 'I', 'F', 'F') || id == FOURCC('L', 'I', 'S', 'T');
        if (list && m->pos + 12 <= m->size) type = *(uint32_t *)(m->data + m->pos + 8);
        int match = 1;
        if (flags & 0x20) match = id == FOURCC('R', 'I', 'F', 'F') && type == wtype;    // MMIO_FINDRIFF
        else if (flags & 0x40) match = id == FOURCC('L', 'I', 'S', 'T') && type == wtype;  // MMIO_FINDLIST
        else if (flags & 0x10) match = id == want;                                       // MMIO_FINDCHUNK
        W32(ck, id); W32(ck + 4, sz); W32(ck + 8, type); W32(ck + 12, m->pos + 8); W32(ck + 16, 0);
        if (match) { m->pos = m->pos + 8 + (list ? 4 : 0); STDRET(4, 0); }
        m->pos += 8 + ((sz + 1) & ~1u);   // skip this chunk
    }
}
void imp_WINMM_mmioAscend(CPU *c) {   // (hmmio, lpck, wFlags): continue after the chunk
    HOSTFN; Mmio *m = h_get(ARG(1), H_MMIO); uint32_t ck = ARG(2);
    if (!m || !ck) STDRET(3, 0x10001);
    m->pos = R32(ck + 12) + ((R32(ck + 4) + 1) & ~1u);
    if (m->pos > m->size) m->pos = m->size;
    STDRET(3, 0);
}
void imp_WINMM_mmioGetInfo(CPU *c) {   // direct-buffer view of the open file (pchNext/pchEndRead)
    HOSTFN; Mmio *m = h_get(ARG(1), H_MMIO); uint32_t info = ARG(2);
    if (!m || !info) STDRET(3, 0x10001);
    memset(mb + info, 0, 72);
    uint32_t g = m->owned ? 0 : (uint32_t)(m->data - mb);
    W32(info + 20, m->size); W32(info + 24, g); W32(info + 28, g + m->pos); W32(info + 32, g + m->size); W32(info + 36, g + m->size); W32(info + 68, ARG(1));
    if (m->owned) rt_log("[mmio] GetInfo on a disk-backed file: direct buffer not exposed\n");
    STDRET(3, 0);
}
void imp_WINMM_mmioSetInfo(CPU *c) {   // adopt pchNext as the new position
    HOSTFN; Mmio *m = h_get(ARG(1), H_MMIO); uint32_t info = ARG(2);
    if (m && info && !m->owned) { uint32_t nx = R32(info + 28); m->pos = nx - (uint32_t)(m->data - mb); }
    STDRET(3, 0);
}
void imp_WINMM_mmioAdvance(CPU *c) { STDRET(3, 0); }   // memory files need no refill
void imp_WINMM_mmioCreateChunk(CPU *c) { rt_log("[stub] WINMM!mmioCreateChunk\n"); STDRET(3, 0x10001); }
void imp_WINMM_mmioWrite(CPU *c) { rt_log("[stub] WINMM!mmioWrite\n"); STDRET(3, 0xffffffffu); }
