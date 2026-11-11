# SH3 import list — J0 deliverable

Generated with `python3 re/pe_imports.py game/sh3.exe` against the oracle
`sh3.exe` SHA-256 `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`
(2003 release; no Steam/GOG release exists). Compare with the SH4 ledger
(`re/LEDGER.md`, Q1) and the study §2 (`PORTAGE-SH3-2026-11-11.md`).

## Summary vs the SH4 shim

| DLL | SH3 | SH4 | Gap for the port |
|---|---|---|---|
| KERNEL32 | 144 | 97 | GDI/resources, `MapViewOfFile`/`CreateFileMappingA`, `SetThreadAffinityMask`, `PulseEvent`, `GetOverlappedResult`, `CreateFileW`, `FindResourceW`, `SetPriorityClass`, `SetThreadPriorityBoost` |
| USER32 | 54 | 26 | `ChangeDisplaySettingsA`, `EnumDisplaySettingsA`, `RegisterDeviceNotificationA`/`UnregisterDeviceNotification`, cursors (`SetCursor`, `GetCursorPos`, `SetCursorPos`, `ClipCursor`), `DrawTextA/W`, `LoadIconA`, `wvsprintfA`/`wsprintfA` |
| GDI32 | 11 | 1 | **new surface**: `CreateFontIndirectA`, `CreateDIBSection`, `CreateCompatibleDC`, `DeleteDC`, `GetObjectA`, `SetTextColor`, `SetBkColor`, `SetBkMode`, `SelectObject`, `GetStockObject` |
| WINMM | 16 | timers only | `mmio*` family (WAV/RIFF reading by the game): `mmioOpenA/Read/Seek/Close/Advance/Ascend/Descend/GetInfo/SetInfo/CreateChunk/Write` |
| DSOUND | `#11` (DirectSoundCreate8) | idem | reusable |
| DINPUT8 | `DirectInput8Create` | idem | reusable |
| d3d8 | `Direct3DCreate8` | idem | reusable |
| ADVAPI32 | 4 (registry) | idem | reusable |
| ole32 | 6 | 6 | `CoTaskMemAlloc/Free` already covered |
| OLEAUT32 | `#6`, `#2` | – | two ordinals to identify (J2) |
| IMM32 | 2 | 2 | reusable |

No DirectShow, no Bink: all video/audio decoding is statically linked into
`sh3.exe` (study §4). The shim base `port/src/{win32,dx,d3d8}` stays valid;
the delta is the list above, not an overhaul.

## Full list

- **IMM32.dll (2)**: ImmAssociateContext, ImmGetContext
- **WINMM.dll (16)**: mmioAdvance, mmioClose, timeKillEvent, timeEndPeriod,
  timeGetTime, timeSetEvent, mmioAscend, mmioRead, mmioDescend, mmioGetInfo,
  mmioCreateChunk, mmioSeek, mmioSetInfo, mmioOpenA, timeBeginPeriod, mmioWrite
- **KERNEL32.dll (144)**: CloseHandle, Sleep, GetFileAttributesA, GetVersionExA,
  GetThreadLocale, GetLocalTime, SetEnvironmentVariableA, CompareStringW,
  CompareStringA, GetLastError, SetEndOfFile, SetConsoleCtrlHandler,
  GetTimeZoneInformation, LCMapStringW, LCMapStringA, FlushFileBuffers,
  GetLocaleInfoA, GetACP, InterlockedExchange, CreateMutexA, GetLocaleInfoW,
  SetStdHandle, VirtualQuery, VirtualProtect, IsBadCodePtr, IsBadReadPtr,
  GetStringTypeW, GetStringTypeA, IsValidCodePage, IsValidLocale,
  EnumSystemLocalesA, GetUserDefaultLCID, GetDateFormatA, GetTimeFormatA,
  GetCurrentDirectoryA, GetCPInfo, GetOEMCP, HeapSize, WaitForSingleObject,
  ReleaseMutex, ReleaseSemaphore, CreateSemaphoreA, InitializeCriticalSection,
  DeleteCriticalSection, EnterCriticalSection, LeaveCriticalSection, ReadFile,
  GetFileSize, CreateFileA, SetFilePointer, InterlockedIncrement,
  InterlockedDecrement, CreateEventA, SetEvent, ResetEvent,
  WaitForMultipleObjects, GetSystemInfo, VirtualFree, DuplicateHandle,
  GetCurrentProcess, GetCurrentThreadId, VirtualAlloc, FreeLibrary, LoadLibraryA,
  MulDiv, GetProcAddress, GetModuleHandleA, MultiByteToWideChar,
  WideCharToMultiByte, SetThreadPriority, GetThreadPriority, GetCurrentThread,
  GetTickCount, CreateThread, SetPriorityClass, SetLastError,
  QueryPerformanceCounter, QueryPerformanceFrequency, OutputDebugStringA,
  LockResource, SizeofResource, LoadResource, FindResourceA,
  SetCurrentDirectoryA, GetFullPathNameA, GetModuleFileNameA, SetErrorMode,
  GetDriveTypeA, GetLogicalDrives, GetVolumeInformationA, GetUserDefaultLangID,
  InterlockedCompareExchange, InterlockedExchangeAdd, FileTimeToSystemTime,
  FileTimeToLocalFileTime, FindClose, FindNextFileA, FindFirstFileA, ResumeThread,
  SuspendThread, PulseEvent, SetThreadAffinityMask, SetThreadPriorityBoost,
  ExitThread, lstrlenA, GetOverlappedResult, HeapAlloc, HeapCreate, HeapDestroy,
  HeapFree, WriteFile, MapViewOfFile, CreateFileMappingA, CreateFileW,
  UnmapViewOfFile, FindResourceW, IsProcessorFeaturePresent, GetProcessHeap,
  RaiseException, RtlUnwind, GetStartupInfoA, GetCommandLineA, ExitProcess,
  CreateDirectoryA, GetSystemTimeAsFileTime, TlsSetValue, TlsGetValue, TlsFree,
  TlsAlloc, DeleteFileA, TerminateProcess, SetUnhandledExceptionFilter,
  FatalAppExitA, HeapReAlloc, IsBadWritePtr, GetStdHandle,
  UnhandledExceptionFilter, FreeEnvironmentStringsA, GetEnvironmentStrings,
  FreeEnvironmentStringsW, GetEnvironmentStringsW, SetHandleCount, GetFileType,
  GetCurrentProcessId
- **USER32.dll (54)**: MessageBoxA, LoadStringA, CreateWindowExA,
  AdjustWindowRectEx, DefWindowProcA, InvalidateRect, PostQuitMessage,
  ClipCursor, SetForegroundWindow, SetFocus, EndPaint, BeginPaint,
  RegisterClassExA, DrawTextW, FindWindowA, GetMessageA, TranslateMessage,
  DispatchMessageA, PeekMessageA, KillTimer, PostMessageA, ShowWindow,
  UpdateWindow, LoadIconA, CallWindowProcA, SetWindowLongA, PostThreadMessageA,
  GetQueueStatus, MsgWaitForMultipleObjects, wvsprintfA, wsprintfA,
  RegisterWindowMessageA, GetForegroundWindow, SystemParametersInfoA,
  AttachThreadInput, GetWindowThreadProcessId, GetClassLongA, GetWindowLongA,
  GetWindowRect, SetCursor, GetSystemMetrics, RedrawWindow, SetWindowPos,
  IsIconic, UnregisterDeviceNotification, RegisterDeviceNotificationA,
  ChangeDisplaySettingsA, EnumDisplaySettingsA, ClientToScreen, GetCursorPos,
  GetKeyState, ScreenToClient, SetCursorPos, DrawTextA
- **GDI32.dll (11)**: CreateFontIndirectA, DeleteDC, GetObjectA, SetTextColor,
  SetBkColor, SetBkMode, SelectObject, GetStockObject, DeleteObject,
  CreateCompatibleDC, CreateDIBSection
- **DSOUND.dll (1)**: `#11` (DirectSoundCreate8)
- **DINPUT8.dll (1)**: DirectInput8Create
- **d3d8.dll (1)**: Direct3DCreate8
- **ADVAPI32.dll (4)**: RegOpenKeyExA, RegQueryValueExA, RegOpenKeyA, RegCloseKey
- **ole32.dll (6)**: CoTaskMemAlloc, CoTaskMemFree, CoInitialize,
  CoFreeUnusedLibraries, CoCreateInstance, CoUninitialize
- **OLEAUT32.dll (2)**: `#6`, `#2`
