// Case Zero additive campaign runtime for Dead Rising 2 PC.
//
// Loaded as a dinput8.dll proxy (DR2 imports only DirectInput8Create from it),
// so installing it replaces no vanilla file. With the vanilla campaign selected
// the DLL installs nothing beyond the DirectInput forwarder.
//
// With the case_zero campaign selected it:
//   * resolves data\<path> to data\case_zero\<path> when that overlay file exists;
//   * enables DR2's retained prologue mode (enable_prolog_experience, byte 0xDDCB1A),
//     which makes SetupNewGameLevel start in PROLOGUE_SAFEHOUSE;
//   * renames the Steam Remote Storage save from DR2SAVE.DR2S to CZ0SAVE.DR2S.
//
// Every code patch is signature-checked against the unpacked retail image and
// applied in process memory only; deadrising2.exe is never modified on disk.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <unknwn.h>
#include <stdio.h>
#include <wchar.h>
#include "save_namespace.h"
#include "local_save_storage.h"
#include "harness_keyboard.h"
#include "harness_mouse.h"

#pragma comment(linker, "/EXPORT:DirectInput8Create=_CZ_DirectInput8Create@20")

// 4-player co-op module (4-player-coop/runtime/coop_net.cpp); build.ps1 compiles it in, or the stub below.
namespace coop {
void Initialize(const wchar_t* root);
bool IsHarness();
bool CampaignActivationPending();
void ActivateCampaignActorsOnCurrentThread();
bool ConnectionMeshInitPending();
void InitializeConnectionMeshOnCurrentThread();
bool ConnectionListenerRearmPending();
void RearmConnectionListenerOnCurrentThread();
bool ClientTransitionPending();
void CompleteClientTransitionOnCurrentThread();
bool ClientDataTransferRequestPending();
void RequestClientDataTransferOnCurrentThread();
bool HostStateTransferPending();
void StartHostStateTransferOnCurrentThread();
bool HostFlow7FinalizePending();
void FinalizeHostFlow7OnCurrentThread();
bool ClothingVariantsPending();
void ApplyClothingVariantsOnCurrentThread();
int Instance();
void TraceInput(int code, bool down);
}

namespace {

enum class Campaign { Vanilla, CaseZero };

Campaign g_campaign = Campaign::Vanilla;
int g_logLevel = 1;  // 0 off, 1 redirects/patches, 2 every data access
DWORD g_worldHeapExtraMiB = 64;    // [Memory] WorldHeapExtraMiB
DWORD g_textureHeapExtraMiB = 32;  // [Memory] TextureHeapExtraMiB
DWORD g_copiedDataHeapExtraMiB = 32;  // [Memory] CopiedDataHeapExtraMiB
bool g_prologMode = true;              // [Diagnostics] PrologMode=0 leaves enable_prolog_experience off
int g_forceNewGameLevel = -1;          // [Diagnostics] ForceNewGameLevel=<enum> rewrites SetupNewGameLevel's map
int g_startLevel = 53;                 // [Campaign] StartLevel: route hosting PROLOGUE_SAFEHOUSE (X_GAMEPLAY_ZOO)
float g_trackSpawn[3] = {0, 0, 0};    // [Diagnostics] TrackSpawn=x,y,z: where the position tracker looks for the player
bool g_trackSpawnSet = false;
wchar_t g_root[MAX_PATH];        // game directory with trailing backslash
wchar_t g_dataPrefix[MAX_PATH];  // "<root>data\" with trailing backslash
wchar_t g_overlay[MAX_PATH];     // "<root>data\case_zero\" with trailing backslash
// Vanilla launcher mode: DR2 boots as vanilla Dead Rising 2 and the runtime serves only data\case_zero_launcher\
// (DR2's title menu with a CASE: ZERO button, build-case-zero-launcher.mjs) plus that button's action.
bool g_launcher = false;
wchar_t g_traceArchives[512] = {};  // [Diagnostics] TraceArchives (see TraceOpen)
wchar_t g_launcherOverlay[MAX_PATH];  // "<root>data\case_zero_launcher\"
CRITICAL_SECTION g_logLock;
FILE* g_log = nullptr;
HMODULE g_realDinput = nullptr;

// DR2 PC keeps every save slot (including prologue slots) in one Steam Cloud file. In Case Zero
// mode that file is transparently swapped for a separate cloud file.
using dr2_save::kDr2SaveName;
using dr2_save::kCaseZeroSaveName;
using dr2_save::kHarnessSaveNames;

using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using GetFileAttributesA_t = DWORD(WINAPI*)(LPCSTR);
using GetFileAttributesExW_t = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
using FindFirstFileW_t = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);

CreateFileW_t Real_CreateFileW = CreateFileW;
CreateFileA_t Real_CreateFileA = CreateFileA;
GetFileAttributesA_t Real_GetFileAttributesA = GetFileAttributesA;
GetFileAttributesExW_t Real_GetFileAttributesExW = GetFileAttributesExW;
FindFirstFileW_t Real_FindFirstFileW = FindFirstFileW;

void Log(int level, const char* format, ...) {
  if (!g_log || level > g_logLevel) return;
  EnterCriticalSection(&g_logLock);
  SYSTEMTIME now;
  GetLocalTime(&now);
  fprintf(g_log, "%02u:%02u:%02u.%03u [%lu] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId());
  va_list args;
  va_start(args, format);
  vfprintf(g_log, format, args);
  va_end(args);
  fputc('\n', g_log);
  fflush(g_log);
  LeaveCriticalSection(&g_logLock);
}

bool HasArgument(const wchar_t* commandLine, const wchar_t* argument) {
  const size_t length = wcslen(argument);
  for (const wchar_t* cursor = commandLine; (cursor = wcsstr(cursor, argument)) != nullptr; cursor += length) {
    const bool startsToken = cursor == commandLine || cursor[-1] == L' ' || cursor[-1] == L'"';
    const wchar_t next = cursor[length];
    if (startsToken && (next == 0 || next == L' ' || next == L'"')) return true;
  }
  return false;
}

void LoadConfiguration() {
  GetModuleFileNameW(nullptr, g_root, MAX_PATH);
  wchar_t* slash = wcsrchr(g_root, L'\\');
  if (slash) slash[1] = 0;
  swprintf(g_dataPrefix, MAX_PATH, L"%sdata\\", g_root);
  swprintf(g_overlay, MAX_PATH, L"%sdata\\case_zero\\", g_root);
  swprintf(g_launcherOverlay, MAX_PATH, L"%sdata\\case_zero_launcher\\", g_root);

  wchar_t ini[MAX_PATH];
  swprintf(ini, MAX_PATH, L"%scase_zero_campaign.ini", g_root);
  wchar_t campaign[64];
  GetPrivateProfileStringW(L"Campaign", L"Active", L"vanilla_dr2", campaign, 64, ini);
  g_logLevel = GetPrivateProfileIntW(L"Campaign", L"Log", 1, ini);
  g_worldHeapExtraMiB = GetPrivateProfileIntW(L"Memory", L"WorldHeapExtraMiB", g_worldHeapExtraMiB, ini);
  g_textureHeapExtraMiB = GetPrivateProfileIntW(L"Memory", L"TextureHeapExtraMiB", g_textureHeapExtraMiB, ini);
  g_copiedDataHeapExtraMiB = GetPrivateProfileIntW(L"Memory", L"CopiedDataHeapExtraMiB", g_copiedDataHeapExtraMiB, ini);
  g_prologMode = GetPrivateProfileIntW(L"Diagnostics", L"PrologMode", 1, ini) != 0;
  g_forceNewGameLevel = GetPrivateProfileIntW(L"Diagnostics", L"ForceNewGameLevel", -1, ini);
  {
    wchar_t spawn[128] = {};
    GetPrivateProfileStringW(L"Diagnostics", L"TrackSpawn", L"", spawn, 128, ini);
    g_trackSpawnSet = swscanf_s(spawn, L"%f,%f,%f", &g_trackSpawn[0], &g_trackSpawn[1], &g_trackSpawn[2]) == 3;
  }
  g_startLevel = GetPrivateProfileIntW(L"Campaign", L"StartLevel", g_startLevel, ini);
  GetPrivateProfileStringW(L"Diagnostics", L"TraceArchives", L"", g_traceArchives, _countof(g_traceArchives), ini);
  if (_wcsicmp(campaign, L"case_zero") == 0) g_campaign = Campaign::CaseZero;
  // One-shot selection written by the CASE: ZERO menu button: it applies to this launch only (and only when the
  // previous session confirmed it by quitting cleanly), so the next ordinary boot is vanilla Dead Rising 2 again.
  {
    wchar_t launch[64] = {};
    GetPrivateProfileStringW(L"Campaign", L"Launch", L"", launch, 64, ini);
    const bool confirmed = GetPrivateProfileIntW(L"Campaign", L"LaunchConfirmed", 0, ini) != 0;
    if (launch[0] && confirmed && _wcsicmp(launch, L"case_zero") == 0) g_campaign = Campaign::CaseZero;
    WritePrivateProfileStringW(L"Campaign", L"Launch", nullptr, ini);
    WritePrivateProfileStringW(L"Campaign", L"LaunchConfirmed", nullptr, ini);
  }

  // Command-line selection wins over the ini so Steam launch options can pick a campaign.
  const wchar_t* commandLine = GetCommandLineW();
  if (HasArgument(commandLine, L"-campaign=case_zero")) g_campaign = Campaign::CaseZero;
  if (HasArgument(commandLine, L"-campaign=vanilla_dr2")) g_campaign = Campaign::Vanilla;

  // A campaign without its content pack must never activate.
  if (g_campaign == Campaign::CaseZero) {
    wchar_t manifest[MAX_PATH];
    swprintf(manifest, MAX_PATH, L"%scase-zero-pack.json", g_overlay);
    if (GetFileAttributesW(manifest) == INVALID_FILE_ATTRIBUTES) g_campaign = Campaign::Vanilla;
  }
  if (g_campaign == Campaign::Vanilla) {
    wchar_t menu[MAX_PATH], manifest[MAX_PATH];
    swprintf(menu, MAX_PATH, L"%sfrontend\\mainmenu.big", g_launcherOverlay);
    swprintf(manifest, MAX_PATH, L"%scase-zero-pack.json", g_overlay);
    g_launcher = GetFileAttributesW(menu) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(manifest) != INVALID_FILE_ATTRIBUTES;
  }

  if (g_logLevel > 0) {
    wchar_t logPath[MAX_PATH];
    const wchar_t* coopInstance = wcsstr(GetCommandLineW(), L"-coopinstance=");
    if (coopInstance) swprintf(logPath, MAX_PATH, L"%scase_zero_runtime.%d.log", g_root,
                               _wtoi(coopInstance + wcslen(L"-coopinstance=")));
    else swprintf(logPath, MAX_PATH, L"%scase_zero_runtime.log", g_root);
    // Keep the previous session's log: a DEAD RISING 2 switch relaunches the game immediately.
    wchar_t previousPath[MAX_PATH];
    swprintf(previousPath, MAX_PATH, L"%scase_zero_runtime.previous.log", g_root);
    MoveFileExW(logPath, previousPath, MOVEFILE_REPLACE_EXISTING);
    g_log = _wfsopen(logPath, L"w", _SH_DENYWR);
  }
}

// data\case_zero\aliases.txt maps a data-relative path the engine asks for in Case Zero mode to
// another data-relative path ("frontend/fecmnp.big = frontend/fecmn.big"). The target is taken
// from the overlay when present there, otherwise from vanilla data. Lines starting with ';' are comments.
struct Alias {
  wchar_t from[MAX_PATH];
  wchar_t to[MAX_PATH];
};
Alias* g_aliases = nullptr;
int g_aliasCount = 0;

void Trim(wchar_t* text) {
  wchar_t* start = text;
  while (*start == L' ' || *start == L'\t') start++;
  memmove(text, start, (wcslen(start) + 1) * sizeof(wchar_t));
  for (size_t length = wcslen(text); length && wcschr(L" \t\r\n", text[length - 1]); length--) text[length - 1] = 0;
  for (wchar_t* cursor = text; *cursor; cursor++)
    if (*cursor == L'/') *cursor = L'\\';
}

void LoadAliases() {
  wchar_t path[MAX_PATH];
  swprintf(path, MAX_PATH, L"%saliases.txt", g_overlay);
  FILE* file = _wfopen(path, L"rt, ccs=UTF-8");
  if (!file) return;
  wchar_t line[MAX_PATH * 2 + 8];
  int capacity = 0;
  while (fgetws(line, _countof(line), file)) {
    wchar_t* separator = wcschr(line, L'=');
    if (line[0] == L';' || !separator) continue;
    *separator = 0;
    if (g_aliasCount == capacity) {
      capacity = capacity ? capacity * 2 : 32;
      g_aliases = static_cast<Alias*>(realloc(g_aliases, capacity * sizeof(Alias)));
    }
    Alias& alias = g_aliases[g_aliasCount];
    wcsncpy_s(alias.from, line, _TRUNCATE);
    wcsncpy_s(alias.to, separator + 1, _TRUNCATE);
    Trim(alias.from);
    Trim(alias.to);
    if (alias.from[0] && alias.to[0]) g_aliasCount++;
  }
  fclose(file);
  Log(1, "loaded %d path aliases", g_aliasCount);
}

bool Exists(const wchar_t* path) {
  return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// Returns true and fills |out| when |path| is under data\ and the overlay (directly or through an
// alias) supplies a replacement.
bool ResolveOverlay(const wchar_t* path, wchar_t* out, DWORD outCount) {
  if (!path || (g_campaign != Campaign::CaseZero && !g_launcher)) return false;
  wchar_t full[MAX_PATH * 2];
  const DWORD length = GetFullPathNameW(path, MAX_PATH * 2, full, nullptr);
  if (length == 0 || length >= MAX_PATH * 2) return false;
  const size_t prefixLength = wcslen(g_dataPrefix);
  if (_wcsnicmp(full, g_dataPrefix, prefixLength) != 0) return false;
  const wchar_t* relative = full + prefixLength;
  if (_wcsnicmp(relative, L"case_zero\\", 10) == 0 || _wcsnicmp(relative, L"case_zero_launcher\\", 19) == 0) return false;
  if (g_launcher) {
    if (swprintf(out, outCount, L"%s%s", g_launcherOverlay, relative) >= 0 && Exists(out)) {
      Log(1, "launcher %ls", full);
      return true;
    }
    return false;
  }
  if (swprintf(out, outCount, L"%s%s", g_overlay, relative) >= 0 && Exists(out)) {
    Log(1, "overlay %ls", full);
    return true;
  }
  for (int index = 0; index < g_aliasCount; index++) {
    if (_wcsicmp(relative, g_aliases[index].from) != 0) continue;
    if (swprintf(out, outCount, L"%s%s", g_overlay, g_aliases[index].to) >= 0 && Exists(out)) {
      Log(1, "alias %ls -> overlay %ls", full, g_aliases[index].to);
      return true;
    }
    if (swprintf(out, outCount, L"%s%s", g_dataPrefix, g_aliases[index].to) >= 0 && Exists(out)) {
      Log(1, "alias %ls -> vanilla %ls", full, g_aliases[index].to);
      return true;
    }
    Log(1, "alias target missing for %ls", full);
    return false;
  }
  Log(2, "vanilla %ls", full);
  return false;
}

bool Widen(LPCSTR path, wchar_t* out, int outCount) {
  return path && MultiByteToWideChar(CP_ACP, 0, path, -1, out, outCount) > 0;
}

// Reports data files the game asked for but that exist in neither the overlay nor vanilla data.
void LogIfMissing(HANDLE result, const wchar_t* path) {
  if (result != INVALID_HANDLE_VALUE || g_campaign != Campaign::CaseZero || !path) return;
  const DWORD error = GetLastError();
  wchar_t full[MAX_PATH * 2];
  if (GetFullPathNameW(path, MAX_PATH * 2, full, nullptr) && _wcsnicmp(full, g_dataPrefix, wcslen(g_dataPrefix)) == 0)
    Log(1, "missing %ls (error %lu)", full, error);
  SetLastError(error);
}

// ---- Diagnostics: [Diagnostics] TraceArchives=npcs.big;cine_props.big logs the first read of every entry of the
// named archives ("archive npcs.big: head_young_chuck.big"), so asset selection can be observed at runtime.
struct TracedEntry {
  DWORD start, end;
  char name[80];
  bool seen;
};
struct TracedArchive {
  HANDLE handle;
  char label[48];
  TracedEntry* entries;
  int count;
};
TracedArchive g_traced[32] = {};
SRWLOCK g_traceLock = SRWLOCK_INIT;
using ReadFile_t = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using ReadFileEx_t = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);
ReadFile_t Real_ReadFile = nullptr;
ReadFileEx_t Real_ReadFileEx = nullptr;

void TraceOpen(HANDLE handle, const wchar_t* path) {
  if (!g_traceArchives[0] || handle == INVALID_HANDLE_VALUE || !path) return;
  AcquireSRWLockExclusive(&g_traceLock);
  for (TracedArchive& archive : g_traced)
    if (archive.handle == handle) archive.handle = nullptr;  // handle value reused by another file
  ReleaseSRWLockExclusive(&g_traceLock);
  const wchar_t* base = wcsrchr(path, L'\\');
  base = base ? base + 1 : path;
  wchar_t list[512];
  wcsncpy_s(list, g_traceArchives, _TRUNCATE);
  bool wanted = false;
  wchar_t* context = nullptr;
  for (wchar_t* token = wcstok_s(list, L";,", &context); token; token = wcstok_s(nullptr, L";,", &context))
    if (_wcsicmp(token, base) == 0) wanted = true;
  if (!wanted) return;
  HANDLE file = Real_CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD header[6] = {}, got = 0;
  if (!Real_ReadFile(file, header, sizeof(header), &got, nullptr) || got != sizeof(header) || header[0] != 0x03040506) {
    CloseHandle(file);
    return;
  }
  const DWORD tableSize = header[1], count = header[3], info = header[4];
  BYTE* table = static_cast<BYTE*>(malloc(tableSize));
  SetFilePointer(file, 0, nullptr, FILE_BEGIN);
  const bool ok = table && Real_ReadFile(file, table, tableSize, &got, nullptr) && got == tableSize && info + count * 28 <= tableSize;
  CloseHandle(file);
  if (!ok) {
    free(table);
    return;
  }
  TracedEntry* entries = static_cast<TracedEntry*>(calloc(count, sizeof(TracedEntry)));
  for (DWORD index = 0; index < count; index++) {
    const DWORD* record = reinterpret_cast<const DWORD*>(table + info + index * 28);
    entries[index].start = record[4];
    entries[index].end = record[4] + record[2];
    if (record[0] < tableSize) strncpy_s(entries[index].name, reinterpret_cast<const char*>(table + record[0]), _TRUNCATE);
  }
  free(table);
  AcquireSRWLockExclusive(&g_traceLock);
  for (TracedArchive& archive : g_traced) {
    if (archive.handle) continue;
    free(archive.entries);
    archive.handle = handle;
    archive.entries = entries;
    archive.count = static_cast<int>(count);
    WideCharToMultiByte(CP_ACP, 0, base, -1, archive.label, sizeof(archive.label), nullptr, nullptr);
    entries = nullptr;
    break;
  }
  ReleaseSRWLockExclusive(&g_traceLock);
  free(entries);
}

void TraceRead(HANDLE handle, ULONGLONG offset, DWORD size) {
  if (!g_traceArchives[0]) return;
  AcquireSRWLockExclusive(&g_traceLock);
  for (TracedArchive& archive : g_traced) {
    if (archive.handle != handle) continue;
    for (int index = 0; index < archive.count; index++) {
      TracedEntry& entry = archive.entries[index];
      if (entry.seen || entry.end <= offset || entry.start >= offset + size) continue;
      entry.seen = true;
      Log(1, "archive %s: %s", archive.label, entry.name);
    }
    break;
  }
  ReleaseSRWLockExclusive(&g_traceLock);
}

BOOL WINAPI Hook_ReadFile(HANDLE handle, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped) {
  if (g_traceArchives[0]) {
    ULONGLONG offset;
    if (overlapped) {
      offset = overlapped->Offset | (static_cast<ULONGLONG>(overlapped->OffsetHigh) << 32);
    } else {
      LONG high = 0;
      const DWORD low = SetFilePointer(handle, 0, &high, FILE_CURRENT);
      offset = low | (static_cast<ULONGLONG>(static_cast<DWORD>(high)) << 32);
    }
    TraceRead(handle, offset, size);
  }
  return Real_ReadFile(handle, buffer, size, read, overlapped);
}

BOOL WINAPI Hook_ReadFileEx(HANDLE handle, LPVOID buffer, DWORD size, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE routine) {
  if (g_traceArchives[0] && overlapped) TraceRead(handle, overlapped->Offset | (static_cast<ULONGLONG>(overlapped->OffsetHigh) << 32), size);
  return Real_ReadFileEx(handle, buffer, size, overlapped, routine);
}

HANDLE WINAPI Hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile) {
  wchar_t redirected[MAX_PATH * 2];
  if (ResolveOverlay(name, redirected, MAX_PATH * 2)) name = redirected;
  HANDLE result = Real_CreateFileW(name, access, share, security, disposition, flags, templateFile);
  LogIfMissing(result, name);
  TraceOpen(result, name);
  return result;
}

HANDLE WINAPI Hook_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile) {
  wchar_t wide[MAX_PATH * 2], redirected[MAX_PATH * 2];
  if (Widen(name, wide, MAX_PATH * 2) && ResolveOverlay(wide, redirected, MAX_PATH * 2)) {
    HANDLE result = Real_CreateFileW(redirected, access, share, security, disposition, flags, templateFile);
    LogIfMissing(result, redirected);
    TraceOpen(result, redirected);
    return result;
  }
  HANDLE result = Real_CreateFileA(name, access, share, security, disposition, flags, templateFile);
  if (Widen(name, wide, MAX_PATH * 2)) {
    if (result == INVALID_HANDLE_VALUE) LogIfMissing(result, wide);
    else TraceOpen(result, wide);
  }
  return result;
}

DWORD WINAPI Hook_GetFileAttributesA(LPCSTR name) {
  wchar_t wide[MAX_PATH * 2], redirected[MAX_PATH * 2];
  if (Widen(name, wide, MAX_PATH * 2) && ResolveOverlay(wide, redirected, MAX_PATH * 2)) return GetFileAttributesW(redirected);
  return Real_GetFileAttributesA(name);
}

BOOL WINAPI Hook_GetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
  wchar_t redirected[MAX_PATH * 2];
  if (ResolveOverlay(name, redirected, MAX_PATH * 2)) name = redirected;
  return Real_GetFileAttributesExW(name, level, info);
}

// Directory searches use the overlay only when it has a match, so vanilla listings are untouched.
HANDLE WINAPI Hook_FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW data) {
  wchar_t redirected[MAX_PATH * 2];
  if (g_campaign == Campaign::CaseZero && pattern) {
    wchar_t full[MAX_PATH * 2];
    const size_t prefixLength = wcslen(g_dataPrefix);
    const DWORD length = GetFullPathNameW(pattern, MAX_PATH * 2, full, nullptr);
    if (length && length < MAX_PATH * 2 && _wcsnicmp(full, g_dataPrefix, prefixLength) == 0 &&
        _wcsnicmp(full + prefixLength, L"case_zero\\", 10) != 0 &&
        swprintf(redirected, MAX_PATH * 2, L"%s%s", g_overlay, full + prefixLength) >= 0) {
      HANDLE overlay = Real_FindFirstFileW(redirected, data);
      if (overlay != INVALID_HANDLE_VALUE) {
        Log(1, "overlay-find %ls", full);
        return overlay;
      }
    }
  }
  return Real_FindFirstFileW(pattern, data);
}

// The Primary heap is sized at the top of WinMain, so code patches must land before the game's
// own startup runs. These CRT-startup imports are the first game calls after SteamStub unpacks.
void TryApplyCodePatches();

using GetSystemTimeAsFileTime_t = void(WINAPI*)(LPFILETIME);
using HeapCreate_t = HANDLE(WINAPI*)(DWORD, SIZE_T, SIZE_T);
using GetVersionExA_t = BOOL(WINAPI*)(LPOSVERSIONINFOA);
using GetCommandLineA_t = LPSTR(WINAPI*)();
using GetStartupInfoA_t = void(WINAPI*)(LPSTARTUPINFOA);
GetSystemTimeAsFileTime_t Real_GetSystemTimeAsFileTime = GetSystemTimeAsFileTime;
HeapCreate_t Real_HeapCreate = HeapCreate;
GetVersionExA_t Real_GetVersionExA = reinterpret_cast<GetVersionExA_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetVersionExA"));
GetCommandLineA_t Real_GetCommandLineA = GetCommandLineA;
GetStartupInfoA_t Real_GetStartupInfoA = GetStartupInfoA;

void WINAPI Hook_GetSystemTimeAsFileTime(LPFILETIME time) {
  TryApplyCodePatches();
  Real_GetSystemTimeAsFileTime(time);
}
HANDLE WINAPI Hook_HeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum) {
  TryApplyCodePatches();
  return Real_HeapCreate(options, initial, maximum);
}
BOOL WINAPI Hook_GetVersionExA(LPOSVERSIONINFOA info) {
  TryApplyCodePatches();
  return Real_GetVersionExA(info);
}
LPSTR WINAPI Hook_GetCommandLineA() {
  TryApplyCodePatches();
  return Real_GetCommandLineA();
}
void WINAPI Hook_GetStartupInfoA(LPSTARTUPINFOA info) {
  TryApplyCodePatches();
  Real_GetStartupInfoA(info);
}

// ---- Steam Cloud save namespace (ISteamRemoteStorage, STEAMREMOTESTORAGE_INTERFACE_VERSION012).
// Typed wrappers and namespace policy are shared with the offline x86 regression test.
using dr2_save::kRemoteStorageSlots;
void* g_realRemoteVtable[kRemoteStorageSlots];
void* g_remoteVtable[kRemoteStorageSlots];
void* g_wrappedRemoteStorage = nullptr;
SRWLOCK g_remoteWrapLock = SRWLOCK_INIT;
dr2_save::LocalStorage g_harnessSaveStorage;

bool UsesPrivateSaveNamespace() { return coop::IsHarness() || g_campaign == Campaign::CaseZero; }
const char* PrivateSaveName() {
  return coop::IsHarness() ? kHarnessSaveNames[coop::Instance()] : kCaseZeroSaveName;
}

struct SaveNamespaceContext {
  static void** Table() { return g_realRemoteVtable; }
  static dr2_save::LocalStorage& Store() { return g_harnessSaveStorage; }
  static dr2_save::Policy CurrentPolicy() {
    return {UsesPrivateSaveNamespace() ? PrivateSaveName() : nullptr, coop::IsHarness()};
  }
  static void Trace(int slot, const char* name, const char* mapped) {
    Log(1, "save namespace: slot=%d name=%s target=%s", slot,
        name ? name : "<null>", mapped ? mapped : "<blocked>");
  }
};

void WrapRemoteStorageUnlocked(void* storage) {
  if (!storage || storage == g_wrappedRemoteStorage) return;
  void** vtable = *reinterpret_cast<void***>(storage);
  if (vtable == g_remoteVtable) return;
  for (int slot = 0; slot < kRemoteStorageSlots; slot++) {
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(&vtable[slot], &info, sizeof(info)) || info.State != MEM_COMMIT) break;
    g_realRemoteVtable[slot] = g_remoteVtable[slot] = vtable[slot];
  }
  if (coop::IsHarness()) {
    dr2_save::LocalBackend<SaveNamespaceContext>::Install(g_realRemoteVtable);
    dr2_save::LocalBackend<SaveNamespaceContext>::Install(g_remoteVtable);
  }
  dr2_save::Adapter<SaveNamespaceContext>::Install(g_remoteVtable);
  DWORD oldProtect;
  if (VirtualProtect(storage, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
    *reinterpret_cast<void***>(storage) = g_remoteVtable;
    VirtualProtect(storage, sizeof(void*), oldProtect, &oldProtect);
    g_wrappedRemoteStorage = storage;
    Log(1, "save namespace: backend=%s active (%s appears as %s)",
        coop::IsHarness() ? "local-only" : "steam", PrivateSaveName(), kDr2SaveName);
  }
}

void WrapRemoteStorage(void* storage) {
  AcquireSRWLockExclusive(&g_remoteWrapLock);
  WrapRemoteStorageUnlocked(storage);
  ReleaseSRWLockExclusive(&g_remoteWrapLock);
}

using SteamRemoteStorage_t = void*(__cdecl*)();
SteamRemoteStorage_t Real_SteamRemoteStorage = nullptr;
void* __cdecl Hook_SteamRemoteStorage() {
  void* storage = Real_SteamRemoteStorage ? Real_SteamRemoteStorage() : nullptr;
  if (UsesPrivateSaveNamespace()) WrapRemoteStorage(storage);
  return storage;
}

template <int Slot, typename Result, typename... Args>
Result SaveProbeCall(void* storage, Args... args) {
  using Fn = Result(__thiscall*)(void*, Args...);
  return reinterpret_cast<Fn>((*static_cast<void***>(storage))[Slot])(storage, args...);
}

DWORD WINAPI LocalSaveProbeThread(LPVOID) {
  Sleep(10000);
  if (!g_harnessSaveStorage.Name()) {
    Log(1, "local save probe: FAILED instance=%d reason=unconfigured-local-root", coop::Instance());
    return 0;
  }
  void* storage = nullptr;
  const DWORD storageDeadline = GetTickCount() + 120000;
  do {
    storage = Hook_SteamRemoteStorage();
    if (storage && *static_cast<void***>(storage) == g_remoteVtable) break;
    Sleep(250);
  } while (static_cast<LONG>(GetTickCount() - storageDeadline) < 0);
  if (!storage || *static_cast<void***>(storage) != g_remoteVtable) {
    Log(1, "local save probe: FAILED instance=%d reason=storage-not-wrapped", coop::Instance());
    return 0;
  }
  using namespace dr2_save;
  if (SaveProbeCall<kGetFileCount, int>(storage) != 0) {
    Log(1, "local save probe: FAILED instance=%d reason=nonempty-fixture-refusing-write", coop::Instance());
    return 0;
  }
  const DWORD payload[] = {0x434F4F50, static_cast<DWORD>(coop::Instance()), GetCurrentProcessId(), 0x53415645};
  DWORD readback[4]{};
  const bool written = SaveProbeCall<kFileWrite, bool>(storage, kDr2SaveName, static_cast<const void*>(payload), 16);
  const bool roundtrip = written && SaveProbeCall<kFileRead, int>(storage, kDr2SaveName,
      static_cast<void*>(readback), 16) == 16 && memcmp(readback, payload, sizeof(payload)) == 0;
  int size = -1;
  const char* visible = SaveProbeCall<kGetFileNameAndSize, const char*>(storage, 0, &size);
  bool passed = roundtrip && size == 16 && SameName(visible, kDr2SaveName) &&
      SaveProbeCall<kGetFileCount, int>(storage) == 1 &&
      SaveProbeCall<kGetFileTimestamp, std::int64_t>(storage, kDr2SaveName) > 0 &&
      !SaveProbeCall<kFileExists, bool>(storage, kCaseZeroSaveName) &&
      !SaveProbeCall<kFileWrite, bool>(storage, kCaseZeroSaveName, static_cast<const void*>(payload), 16) &&
      SaveProbeCall<kFileShare, std::uint64_t>(storage, kDr2SaveName) == 0 &&
      SaveProbeCall<kFileWriteStreamOpen, std::uint64_t>(storage, kDr2SaveName) == UINT64_MAX;
  for (int index = 0; index < 4; index++) {
    if (index == coop::Instance()) continue;
    passed = !SaveProbeCall<kFileExists, bool>(storage, kHarnessSaveNames[index]) && passed;
    passed = !SaveProbeCall<kFileDelete, bool>(storage, kHarnessSaveNames[index]) && passed;
  }
  // Only remove our own sentinel after reading back exactly what this probe wrote.
  const bool cleaned = roundtrip && SaveProbeCall<kFileDelete, bool>(storage, kDr2SaveName) &&
      SaveProbeCall<kGetFileCount, int>(storage) == 0;
  Log(1, "local save probe: %s instance=%d roundtrip=%d cleaned=%d cloudWrites=0",
      passed && cleaned ? "PASSED" : "FAILED", coop::Instance(), roundtrip, cleaned);
  return 0;
}

// ---- Direct3D 9 diagnostics: log vertex declarations the device rejects (Case Zero-converted meshes
// that DR2's remapper would otherwise turn into an empty slot and a crash at 0x00AB3143).
struct VertexElement {
  WORD stream, offset;
  BYTE type, method, usage, usageIndex;
};
using CreateVertexDeclaration_t = HRESULT(__stdcall*)(void*, const VertexElement*, void**);
using CreateDevice_t = HRESULT(__stdcall*)(void*, UINT, DWORD, HWND, DWORD, void*, void**);
using Present_t = HRESULT(__stdcall*)(void*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Direct3DCreate9_t = void*(__stdcall*)(UINT);
CreateVertexDeclaration_t Real_CreateVertexDeclaration = nullptr;
CreateDevice_t Real_CreateDevice = nullptr;
Present_t Real_Present = nullptr;
Direct3DCreate9_t Real_Direct3DCreate9 = nullptr;
DWORD g_harnessFrameMs = 0;
DWORD g_nextHarnessPresent = 0;
DWORD g_lastHarnessActivation = 0;
volatile LONG g_harnessNeedsActivation = 1;
int g_harnessCaptureIndex = 0;
wchar_t g_harnessCaptureRoot[MAX_PATH]{};
HWND g_harnessWindow = nullptr;
WNDPROC g_harnessWndProc = nullptr;
PVOID volatile g_harnessRatingLogoScreen = nullptr;
volatile DWORD g_harnessRatingAdvanceAt = 0;
constexpr UINT kHarnessAdvanceRating = WM_APP + 0x434;
constexpr UINT kHarnessActivate = WM_APP + 0x435;
constexpr UINT kHarnessActivateCampaign = WM_APP + 0x436;

bool HarnessGameInactive() {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  __try {
    return base[0x00E5F619 - 0x00400000] == 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return true;
  }
}

void ForceHarnessGameActive() {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  __try {
    base[0x00E5F619 - 0x00400000] = 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

LRESULT CALLBACK HarnessWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == kHarnessActivate) {
    if (g_harnessWndProc) CallWindowProcW(g_harnessWndProc, window, WM_ACTIVATE, WA_ACTIVE, 0);
    ForceHarnessGameActive();
    InterlockedExchange(&g_harnessNeedsActivation, 0);
    return 0;
  }
  if (message == kHarnessAdvanceRating) {
    void* screen = InterlockedExchangePointer(&g_harnessRatingLogoScreen, nullptr);
    if (screen) {
      // This transition owns frontend state. Never invoke it on the render worker in Present.
      using AdvanceFrontend_t = void(__thiscall*)(void*);
      auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
      reinterpret_cast<AdvanceFrontend_t>(base + (0x007E87A0 - 0x00400000))(screen);
      Log(1, "harness: rating-logo screen advanced on window thread through native animation_done path");
    }
    return 0;
  }
  if (message == kHarnessActivateCampaign) {
    coop::ActivateCampaignActorsOnCurrentThread();
    coop::InitializeConnectionMeshOnCurrentThread();
    coop::RearmConnectionListenerOnCurrentThread();
    coop::CompleteClientTransitionOnCurrentThread();
    coop::RequestClientDataTransferOnCurrentThread();
    coop::StartHostStateTransferOnCurrentThread();
    coop::FinalizeHostFlow7OnCurrentThread();
    coop::ApplyClothingVariantsOnCurrentThread();
    return 0;
  }
  // DR2 PC's WndProc calls SuspendAllViewports for WM_ACTIVATE(WA_INACTIVE). Keep only the
  // harness simulation active; this does not alter Windows' real foreground window.
  if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)
    wParam = (wParam & 0xFFFF0000u) | WA_ACTIVE;
  return g_harnessWndProc ? CallWindowProcW(g_harnessWndProc, window, message, wParam, lParam)
                          : DefWindowProcW(window, message, wParam, lParam);
}

void TryCaptureHarnessFrame(void* device) {
  if (!coop::IsHarness() || !g_harnessCaptureRoot[0]) return;
  static SRWLOCK captureLock = SRWLOCK_INIT;
  if (!TryAcquireSRWLockExclusive(&captureLock)) return;
  struct Unlock { SRWLOCK* lock; ~Unlock() { ReleaseSRWLockExclusive(lock); } } unlock{&captureLock};
  wchar_t request[MAX_PATH];
  swprintf(request, MAX_PATH, L"%scoop_capture.%d.flag", g_root, coop::Instance());
  if (GetFileAttributesW(request) == INVALID_FILE_ATTRIBUTES) return;
  DeleteFileW(request);
  const DWORD captureStarted = GetTickCount();

  using GetBackBuffer_t = HRESULT(__stdcall*)(void*, UINT, UINT, int, void**);
  using Release_t = ULONG(__stdcall*)(void*);
  using SaveSurface_t = HRESULT(WINAPI*)(LPCWSTR, int, void*, const PALETTEENTRY*, const RECT*);
  void** deviceVtable = *reinterpret_cast<void***>(device);
  void* surface = nullptr;
  HRESULT result = reinterpret_cast<GetBackBuffer_t>(deviceVtable[18])(device, 0, 0, 0, &surface);
  if (SUCCEEDED(result) && surface) {
    static SaveSurface_t saveSurface = nullptr;
    if (!saveSurface) {
      HMODULE d3dx = LoadLibraryW(L"d3dx9_43.dll");
      if (d3dx) saveSurface = reinterpret_cast<SaveSurface_t>(GetProcAddress(d3dx, "D3DXSaveSurfaceToFileW"));
    }
    wchar_t output[MAX_PATH];
    swprintf(output, MAX_PATH, L"%scoop_capture.%d.%d.png", g_harnessCaptureRoot, coop::Instance(), g_harnessCaptureIndex++);
    const bool fresh = GetFileAttributesW(output) == INVALID_FILE_ATTRIBUTES;
    result = fresh && saveSurface ? saveSurface(output, 3 /* D3DXIFF_PNG */, surface, nullptr, nullptr) : E_FAIL;
    if (fresh && FAILED(result)) DeleteFileW(output);
    void** surfaceVtable = *reinterpret_cast<void***>(surface);
    reinterpret_cast<Release_t>(surfaceVtable[2])(surface);
    Log(1, "d3d9: hidden frame capture %ls result=%08lX elapsedMs=%lu", output,
        static_cast<DWORD>(result), GetTickCount() - captureStarted);
  } else {
    if (result == static_cast<HRESULT>(0x88760868)) InterlockedExchange(&g_harnessNeedsActivation, 1);
    Log(1, "d3d9: GetBackBuffer for hidden capture failed result=%08lX", static_cast<DWORD>(result));
  }
}

HRESULT __stdcall Hook_Present(void* device, const RECT* source, const RECT* destination, HWND overrideWindow, const RGNDATA* dirtyRegion) {
  if (g_harnessFrameMs) {
    const DWORD now = GetTickCount();
    if (g_nextHarnessPresent && static_cast<LONG>(g_nextHarnessPresent - now) > 0) Sleep(g_nextHarnessPresent - now);
    g_nextHarnessPresent = GetTickCount() + g_harnessFrameMs;
  }
  TryCaptureHarnessFrame(device);
  return Real_Present(device, source, destination, overrideWindow, dirtyRegion);
}

HRESULT __stdcall Hook_CreateVertexDeclaration(void* device, const VertexElement* elements, void** declaration) {
  const HRESULT result = Real_CreateVertexDeclaration(device, elements, declaration);
  if (FAILED(result)) {
    char text[1024];
    size_t used = 0;
    for (int index = 0; index < 32 && elements[index].stream != 0xFF && used < sizeof(text) - 48; index++)
      used += _snprintf_s(text + used, sizeof(text) - used, _TRUNCATE, "[s%u o%u t%u m%u u%u i%u] ", elements[index].stream, elements[index].offset,
                          elements[index].type, elements[index].method, elements[index].usage, elements[index].usageIndex);
    Log(1, "d3d9: CreateVertexDeclaration failed %08lX: %s", static_cast<DWORD>(result), used ? text : "(empty)");
    // The game omits frame pointers: scan the raw stack for game-image return addresses (after a call).
    char stack[768];
    size_t length = 0;
    const DWORD* slot = reinterpret_cast<const DWORD*>(_AddressOfReturnAddress());
    for (int index = 0; index < 1024 && length < sizeof(stack) - 12; index++) {
      const DWORD value = slot[index];
      if (value < 0x401005 || value >= 0xC00000) continue;
      const BYTE* code = reinterpret_cast<const BYTE*>(value);
      if (code[-5] == 0xE8 || (code[-2] == 0xFF && (code[-1] & 0x38) == 0x10) || (code[-3] == 0xFF && (code[-2] & 0x38) == 0x10) ||
          (code[-6] == 0xFF && (code[-5] & 0x38) == 0x10))
        length += _snprintf_s(stack + length, sizeof(stack) - length, _TRUNCATE, "+%X:%08lX ", index * 4, value);
    }
    // Resource names formatted into the callers' stack buffers identify the mesh being built.
    const char* scan = reinterpret_cast<const char*>(slot);
    for (int offset = 0; offset < 0x1000;) {
      int run = 0;
      while (offset + run < 0x1000 && scan[offset + run] >= 0x20 && scan[offset + run] < 0x7F) run++;
      if (run >= 6 && scan[offset + run] == 0) Log(1, "d3d9:   stack string +%X: %.*s", offset, run, scan + offset);
      offset += run + 1;
    }
    const BYTE* header = reinterpret_cast<const BYTE*>(elements) - 16;
    Log(1, "d3d9:   elements=%p stack %s", elements, stack);
    Log(1, "d3d9:   bytes before elements %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X", header[0], header[1], header[2], header[3],
        header[4], header[5], header[6], header[7], header[8], header[9], header[10], header[11], header[12], header[13], header[14], header[15]);
  }
  return result;
}

bool WriteProtected(void* address, const void* bytes, size_t size);

void PatchVtableSlot(void* object, int slot, void* hook, void** real) {
  void** vtable = *reinterpret_cast<void***>(object);
  if (vtable[slot] == hook) return;
  *real = vtable[slot];
  WriteProtected(&vtable[slot], &hook, sizeof(hook));
}

HRESULT __stdcall Hook_CreateDevice(void* d3d, UINT adapter, DWORD type, HWND window, DWORD flags, void* parameters, void** device) {
  D3DPRESENT_PARAMETERS harnessParameters = {};
  void* effectiveParameters = parameters;
  if (parameters && wcsstr(GetCommandLineW(), L"-coopinstance=") != nullptr) {
    const wchar_t* fpsArgument = wcsstr(GetCommandLineW(), L"-coopfps=");
    const int fps = fpsArgument ? _wtoi(fpsArgument + 9) : 0;
    g_harnessFrameMs = fps > 0 ? static_cast<DWORD>(1000 / fps) : 0;
    harnessParameters = *reinterpret_cast<D3DPRESENT_PARAMETERS*>(parameters);
    harnessParameters.Windowed = TRUE;
    harnessParameters.BackBufferWidth = 640;
    harnessParameters.BackBufferHeight = 360;
    harnessParameters.BackBufferFormat = D3DFMT_UNKNOWN;
    harnessParameters.FullScreen_RefreshRateInHz = 0;
    harnessParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    harnessParameters.hDeviceWindow = window;
    // CreateDevice's parameters are in/out. Keep the engine's retained copy windowed too.
    *reinterpret_cast<D3DPRESENT_PARAMETERS*>(parameters) = harnessParameters;
    if (window) {
      const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
      SetWindowLongPtrW(window, GWL_EXSTYLE, (style | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW);
      SetWindowPos(window, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 64,
          GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) + 64, 640, 360,
          SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
      ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    Log(1, "d3d9: harness forcing windowed 640x360 adapter=%u type=%lu flags=%08lX fps=%d", adapter, type, flags, fps);
  }
  HRESULT result = Real_CreateDevice(d3d, adapter, type, window, flags, effectiveParameters, device);
  if (coop::IsHarness() && result == static_cast<HRESULT>(0x88760868)) {
    for (int attempt = 1; attempt <= 12 && result == static_cast<HRESULT>(0x88760868); attempt++) {
      Sleep(500);
      ForceHarnessGameActive();
      result = Real_CreateDevice(d3d, adapter, type, window, flags, effectiveParameters, device);
      Log(1, "d3d9: harness CreateDevice retry=%d result=%08lX device=%p", attempt,
          static_cast<DWORD>(result), device ? *device : nullptr);
    }
    if (result == static_cast<HRESULT>(0x88760868)) {
      if (HasArgument(GetCommandLineW(), L"-coophardwarerequired")) {
        Log(1, "harness renderer: hardware unavailable after retries; refusing NULLREF for frontend test");
        ExitProcess(ERROR_NOT_SUPPORTED);
      }
      constexpr DWORD kHardwareVertexProcessing = 0x40;
      constexpr DWORD kMixedVertexProcessing = 0x80;
      constexpr DWORD kSoftwareVertexProcessing = 0x20;
      constexpr DWORD kNullReferenceDevice = 4;
      const DWORD softwareFlags = (flags & ~(kHardwareVertexProcessing | kMixedVertexProcessing)) |
                                  kSoftwareVertexProcessing;
      result = Real_CreateDevice(d3d, 0, kNullReferenceDevice, window, softwareFlags,
                                 effectiveParameters, device);
      Log(1, "d3d9: harness NULLREF fallback result=%08lX device=%p flags=%08lX",
          static_cast<DWORD>(result), device ? *device : nullptr, softwareFlags);
    }
  }
  Log(1, "d3d9: CreateDevice result=%08lX device=%p", static_cast<DWORD>(result), device ? *device : nullptr);
  if (SUCCEEDED(result) && device && *device) {
    if (coop::IsHarness() && window) {
      const bool isolatedDesktop = HasArgument(GetCommandLineW(), L"-coopdesktop");
      g_harnessWindow = window;
      g_harnessWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
          window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HarnessWndProc)));
      if (g_harnessWndProc) {
        CallWindowProcW(g_harnessWndProc, window, WM_ACTIVATE, WA_ACTIVE, 0);
        ForceHarnessGameActive();
        InterlockedExchange(&g_harnessNeedsActivation, 0);
      }
      const int harnessX = isolatedDesktop ? 0 : GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 64;
      const int harnessY = isolatedDesktop ? 0 : GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) + 64;
      const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
      SetWindowLongPtrW(window, GWL_EXSTYLE, (style | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW);
      SetWindowPos(window, nullptr, harnessX, harnessY, 640, 360,
                   SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
      ShowWindow(window, SW_SHOWNOACTIVATE);
      Log(1, "d3d9: harness window virtualized hwnd=%p wndproc=%p visible=%d", window, g_harnessWndProc,
          IsWindowVisible(window));
      Log(1, "harness window: x=%d y=%d width=640 height=360 noactivate=1 toolwindow=1", harnessX, harnessY);
    }
    if (g_harnessFrameMs) PatchVtableSlot(*device, 17, reinterpret_cast<void*>(&Hook_Present), reinterpret_cast<void**>(&Real_Present));
    PatchVtableSlot(*device, 86, reinterpret_cast<void*>(&Hook_CreateVertexDeclaration), reinterpret_cast<void**>(&Real_CreateVertexDeclaration));
    Log(1, "d3d9: device created; vertex-declaration diagnostics active");
  }
  return result;
}

void* __stdcall Hook_Direct3DCreate9(UINT version) {
  void* d3d = Real_Direct3DCreate9(version);
  if (d3d) PatchVtableSlot(d3d, 16, reinterpret_cast<void*>(&Hook_CreateDevice), reinterpret_cast<void**>(&Real_CreateDevice));
  return d3d;
}

struct ImportHook {
  const char* dll;
  const char* name;
  void* hook;
  void** real;
};

ImportHook g_hooks[] = {
    {"d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(Hook_Direct3DCreate9), reinterpret_cast<void**>(&Real_Direct3DCreate9)},
    {"steam_api.dll", "SteamRemoteStorage", reinterpret_cast<void*>(Hook_SteamRemoteStorage), reinterpret_cast<void**>(&Real_SteamRemoteStorage)},
    {"KERNEL32.dll", "GetSystemTimeAsFileTime", reinterpret_cast<void*>(Hook_GetSystemTimeAsFileTime), reinterpret_cast<void**>(&Real_GetSystemTimeAsFileTime)},
    {"KERNEL32.dll", "HeapCreate", reinterpret_cast<void*>(Hook_HeapCreate), reinterpret_cast<void**>(&Real_HeapCreate)},
    {"KERNEL32.dll", "GetVersionExA", reinterpret_cast<void*>(Hook_GetVersionExA), reinterpret_cast<void**>(&Real_GetVersionExA)},
    {"KERNEL32.dll", "GetCommandLineA", reinterpret_cast<void*>(Hook_GetCommandLineA), reinterpret_cast<void**>(&Real_GetCommandLineA)},
    {"KERNEL32.dll", "GetStartupInfoA", reinterpret_cast<void*>(Hook_GetStartupInfoA), reinterpret_cast<void**>(&Real_GetStartupInfoA)},
    {"KERNEL32.dll", "CreateFileW", reinterpret_cast<void*>(Hook_CreateFileW), reinterpret_cast<void**>(&Real_CreateFileW)},
    {"KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(Hook_CreateFileA), reinterpret_cast<void**>(&Real_CreateFileA)},
    {"KERNEL32.dll", "GetFileAttributesA", reinterpret_cast<void*>(Hook_GetFileAttributesA), reinterpret_cast<void**>(&Real_GetFileAttributesA)},
    {"KERNEL32.dll", "GetFileAttributesExW", reinterpret_cast<void*>(Hook_GetFileAttributesExW), reinterpret_cast<void**>(&Real_GetFileAttributesExW)},
    {"KERNEL32.dll", "FindFirstFileW", reinterpret_cast<void*>(Hook_FindFirstFileW), reinterpret_cast<void**>(&Real_FindFirstFileW)},
    {"KERNEL32.dll", "ReadFile", reinterpret_cast<void*>(Hook_ReadFile), reinterpret_cast<void**>(&Real_ReadFile)},
    {"KERNEL32.dll", "ReadFileEx", reinterpret_cast<void*>(Hook_ReadFileEx), reinterpret_cast<void**>(&Real_ReadFileEx)},
};

bool WriteProtected(void* address, const void* bytes, size_t size) {
  DWORD oldProtect;
  if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
  memcpy(address, bytes, size);
  VirtualProtect(address, size, oldProtect, &oldProtect);
  FlushInstructionCache(GetCurrentProcess(), address, size);
  return true;
}

// Patches (or re-patches) the hooked IAT slots of the game module. Returns the number of slots now hooked.
int PatchImports() {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress) return 0;
  int hooked = 0;
  for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); descriptor->Name; descriptor++) {
    const char* dllName = reinterpret_cast<char*>(base + descriptor->Name);
    if (!descriptor->OriginalFirstThunk) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
    for (; names->u1.AddressOfData; names++, slots++) {
      if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
      const char* importName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
      for (auto& hook : g_hooks) {
        if (_stricmp(dllName, hook.dll) != 0 || strcmp(importName, hook.name) != 0) continue;
        void* current = reinterpret_cast<void*>(static_cast<ULONG_PTR>(slots->u1.Function));
        if (current == hook.hook) {
          hooked++;
          continue;
        }
        *hook.real = current;
        const DWORD replacement = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(hook.hook));
        if (WriteProtected(&slots->u1.Function, &replacement, sizeof(replacement))) {
          hooked++;
          Log(1, "hooked import %s", importName);
        }
      }
    }
  }
  return hooked;
}

// Retail addresses (image base 0x00400000, recovered from the unpacked runtime image).
constexpr DWORD kPrologFlagVa = 0x00DDCB1A;          // enable_prolog_experience
constexpr DWORD kPrologFlagStoreVa = 0x007BCF72;     // mov byte ptr [0xDDCB1A], al
constexpr DWORD kSetupNewGameCheckVa = 0x007B56CA;   // mov al, byte ptr [0xDDCB1A]

// Streaming heap size getters. DR2 sizes these for Fortune City's streaming; Still Creek keeps
// more of its world resident at once, so Case Zero mode enlarges them before they are created.
constexpr DWORD kWorldHeapAddVa = 0x007A1CB3;      // add eax, 0x01B3D400 (Streaming world heap base size)
constexpr DWORD kTextureHeapSizeAVa = 0x007A1CC7;  // mov eax, 0x02800000 (SW Texture heap)
constexpr DWORD kTextureHeapSizeBVa = 0x007A1CCE;  // mov eax, 0x0290E400 (SW Texture heap, alternate)
constexpr DWORD kPrimaryHeapSizeVa = 0x007ACFCE;  // mov esi, 0x16300000 (Primary heap, carved first)
const BYTE kPrimaryHeapSize[] = {0xBE, 0x00, 0x00, 0x30, 0x16};
const BYTE kWorldHeapAdd[] = {0x05, 0x00, 0xD4, 0xB3, 0x01};
const BYTE kTextureHeapSizeA[] = {0xB8, 0x00, 0x00, 0x80, 0x02};
const BYTE kTextureHeapSizeB[] = {0xB8, 0x00, 0xE4, 0x90, 0x02};
constexpr DWORD kCopiedDataHeapSizeVa = 0x007A1CE0;  // mov eax, 0x0053D400 (SW copied data heap)
const BYTE kCopiedDataHeapSize[] = {0xB8, 0x00, 0xD4, 0x53, 0x00};
const BYTE kPrologFlagStore[] = {0xA2, 0x1A, 0xCB, 0xDD, 0x00};
const BYTE kSetupNewGameCheck[] = {0xA0, 0x1A, 0xCB, 0xDD, 0x00};

BYTE* g_prologStub = nullptr;

BYTE* Va(DWORD va) {
  return reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)) + (va - 0x00400000);
}

bool Matches(DWORD va, const BYTE* bytes, size_t size) {
  __try {
    return memcmp(Va(va), bytes, size) == 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// SteamStub decrypts .text progressively, so every patch site must be checked, not just one.
bool CodeUnpacked() {
  return Matches(kSetupNewGameCheckVa, kSetupNewGameCheck, sizeof(kSetupNewGameCheck)) &&
         Matches(kPrologFlagStoreVa, kPrologFlagStore, sizeof(kPrologFlagStore)) &&
         Matches(kWorldHeapAddVa, kWorldHeapAdd, sizeof(kWorldHeapAdd)) &&
         Matches(kTextureHeapSizeAVa, kTextureHeapSizeA, sizeof(kTextureHeapSizeA)) &&
         Matches(kTextureHeapSizeBVa, kTextureHeapSizeB, sizeof(kTextureHeapSizeB)) &&
         Matches(kPrimaryHeapSizeVa, kPrimaryHeapSize, sizeof(kPrimaryHeapSize)) &&
         Matches(kCopiedDataHeapSizeVa, kCopiedDataHeapSize, sizeof(kCopiedDataHeapSize));
}

// Rewrites the 32-bit immediate of a 5-byte "op eax, imm32" instruction to imm + extra MiB.
bool GrowImmediate(DWORD va, const BYTE* original, DWORD extraMiB) {
  if (!extraMiB) return true;
  BYTE patched[5];
  memcpy(patched, original, 5);
  DWORD value;
  memcpy(&value, original + 1, 4);
  value += extraMiB << 20;
  memcpy(patched + 1, &value, 4);
  return WriteProtected(Va(va), patched, 5);
}

bool ApplyCodePatches() {
  BYTE* store = Va(kPrologFlagStoreVa);

  // The settings loader stores the (retail-false) enable_prolog_experience value; divert that
  // store to a stub that always writes 1, so any later reload keeps Case Zero mode on.
  if (g_prologMode) {
    g_prologStub = static_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    const BYTE stub[] = {0xC6, 0x05, 0x1A, 0xCB, 0xDD, 0x00, 0x01, 0xC3};  // mov byte ptr [0xDDCB1A], 1; ret
    memcpy(g_prologStub, stub, sizeof(stub));
    BYTE call[5] = {0xE8};
    const LONG relative = static_cast<LONG>(reinterpret_cast<ULONG_PTR>(g_prologStub) - (reinterpret_cast<ULONG_PTR>(store) + 5));
    memcpy(call + 1, &relative, 4);
    if (!WriteProtected(store, call, sizeof(call))) return false;
  }

  // SetupNewGameLevel computes map = (prologue ? mask : 0) + 7 with "and eax, 0x0B; add eax, 7", i.e.
  // PROLOGUE_SAFEHOUSE (18). DR2 PC draws only geometry above a fixed height on its own prologue ids, so
  // Case Zero's safehouse level is hosted on X_GAMEPLAY_ZOO (53): the mask becomes StartLevel - 7.
  const BYTE bootstrap[] = {0x83, 0xE0, 0x0B, 0x83, 0xC0, 0x07};
  if (g_prologMode && g_forceNewGameLevel < 0 && g_startLevel != 18) {
    if (!Matches(0x007B56DC, bootstrap, sizeof(bootstrap)) || g_startLevel < 7 || g_startLevel > 7 + 0x7F) return false;
    const BYTE mask = static_cast<BYTE>(g_startLevel - 7);
    if (!WriteProtected(Va(0x007B56DE), &mask, 1)) return false;
    Log(1, "prologue new games start on level enum %d", g_startLevel);
  }

  // Level type table (0x00C36050, dword per level enum, low nibble 2 = exterior). Exterior levels load the
  // sky dome (data/models/environment/skybox.big, 0x006B5791) and use the exterior lighting path. Case
  // Zero's own ids are exterior (14/17/18 = 2); the host routes 52/53 are interior debug maps (0), so
  // they get Case Zero's value.
  if (g_prologMode) {
    const DWORD hostLevels[] = {52, 53};
    for (const DWORD level : hostLevels) {
      const DWORD va = 0x00C36050 + level * 4;
      const DWORD interior = 0, exterior = 2;
      if (!Matches(va, reinterpret_cast<const BYTE*>(&interior), 4) && !Matches(va, reinterpret_cast<const BYTE*>(&exterior), 4)) return false;
      if (!WriteProtected(Va(va), &exterior, 4)) return false;
    }
    Log(1, "host routes 52/53 marked exterior (sky dome, exterior lighting)");
  }

  // Diagnostic: force SetupNewGameLevel's map (Case West's proven "and eax,0xB; add eax,7" rewrite).
  if (g_forceNewGameLevel >= 0 && g_forceNewGameLevel < 128) {
    const BYTE expected[] = {0x83, 0xE0, 0x0B, 0x83, 0xC0, 0x07};
    if (!Matches(0x007B56DC, expected, sizeof(expected))) return false;
    const BYTE forced[] = {0x83, 0xE0, 0x00, 0x83, 0xC0, static_cast<BYTE>(g_forceNewGameLevel)};
    if (!WriteProtected(Va(0x007B56DC), forced, sizeof(forced))) return false;
    Log(1, "diagnostic: new game forced to level enum %d", g_forceNewGameLevel);
  }

  // The streaming heaps are carved out of the Primary heap, so it grows by their extra plus slack.
  const DWORD primaryExtraMiB = g_worldHeapExtraMiB + g_textureHeapExtraMiB + g_copiedDataHeapExtraMiB + 32;
  if (!GrowImmediate(kPrimaryHeapSizeVa, kPrimaryHeapSize, primaryExtraMiB) ||
      !GrowImmediate(kCopiedDataHeapSizeVa, kCopiedDataHeapSize, g_copiedDataHeapExtraMiB) ||
      !GrowImmediate(kWorldHeapAddVa, kWorldHeapAdd, g_worldHeapExtraMiB) ||
      !GrowImmediate(kTextureHeapSizeAVa, kTextureHeapSizeA, g_textureHeapExtraMiB) ||
      !GrowImmediate(kTextureHeapSizeBVa, kTextureHeapSizeB, g_textureHeapExtraMiB))
    return false;
  Log(1, "heaps enlarged: primary +%lu MiB, streaming world +%lu MiB, streaming texture +%lu MiB, copied data +%lu MiB",
      primaryExtraMiB, g_worldHeapExtraMiB, g_textureHeapExtraMiB, g_copiedDataHeapExtraMiB);

  // Covers the case where the settings loader already ran before the store was diverted.
  if (g_prologMode) {
    const BYTE enabled = 1;
    WriteProtected(Va(kPrologFlagVa), &enabled, 1);
  }
  Log(1, "prologue mode %s; Case Zero save namespace (%s)", g_prologMode ? "enabled" : "DISABLED (diagnostic)", kCaseZeroSaveName);
  return true;
}

// The harness also records faults outside the game image; teardown dumps can lose the faulting thread.
LONG CALLBACK CrashReporter(EXCEPTION_POINTERS* info) {
  if (!info || !info->ExceptionRecord || !info->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  const DWORD textStart = 0x00401000;
  const DWORD textEnd = 0x00C2A000;
  const CONTEXT* context = info->ContextRecord;
  if (!coop::IsHarness() && (context->Eip < textStart || context->Eip >= textEnd)) return EXCEPTION_CONTINUE_SEARCH;
  static volatile LONG reported = 0;
  if (InterlockedIncrement(&reported) > 4) return EXCEPTION_CONTINUE_SEARCH;
  Log(0 < g_logLevel ? 1 : 0, "ACCESS VIOLATION eip=%08lX %s %08lX", context->Eip,
      record->ExceptionInformation[0] ? "writing" : "reading", static_cast<DWORD>(record->ExceptionInformation[1]));
  Log(1, "  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX", context->Eax, context->Ebx,
      context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp, context->Esp);
  const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
  MEMORYSTATUSEX memory = {sizeof(memory)};
  GlobalMemoryStatusEx(&memory);
  Log(1, "  process virtual used=%llu MiB of %llu MiB", (memory.ullTotalVirtual - memory.ullAvailVirtual) >> 20, memory.ullTotalVirtual >> 20);
  // Printable strings near each register-held pointer (and one level of indirection) often name the
  // model, resource or object being processed at the fault.
  DWORD registers[] = {context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp};
  const char* registerNames[] = {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp"};
  for (int r = 0; r < 7; r++) {
    for (int level = 0; level < 2; level++) {
      DWORD pointer = registers[r];
      MEMORY_BASIC_INFORMATION info;
      if (pointer < 0x10000 || !VirtualQuery(reinterpret_cast<void*>(pointer), &info, sizeof(info)) || info.State != MEM_COMMIT ||
          (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        break;
      __try {
        const BYTE* bytes = reinterpret_cast<const BYTE*>(pointer);
        char found[256] = {};
        size_t used = 0;
        for (int offset = 0; offset < 0x200 && used < sizeof(found) - 40; offset++) {
          int length = 0;
          while (offset + length < 0x200 && bytes[offset + length] >= 0x20 && bytes[offset + length] < 0x7F && length < 32) length++;
          if (length >= 5) {
            used += _snprintf_s(found + used, sizeof(found) - used, _TRUNCATE, "+%X'%.*s' ", offset, length, bytes + offset);
            offset += length;
          }
        }
        if (used) Log(1, "  %s%s %08lX strings: %s", level ? "*" : "", registerNames[r], pointer, found);
        registers[r] = *reinterpret_cast<const DWORD*>(bytes);  // follow one level of indirection next
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        break;
      }
    }
  }

  // Vertex-declaration remapper fault: report the requested slot and the table's state.
  if (context->Eip == 0x00AB3143) {
    __try {
      const BYTE* gfx = *reinterpret_cast<BYTE* const*>(Va(0x00E5FEBC));
      const BYTE* table = *reinterpret_cast<BYTE* const*>(gfx + 4);
      const DWORD count = *reinterpret_cast<const DWORD*>(table + 0x24);
      const BYTE* entries = *reinterpret_cast<BYTE* const*>(table + 0x28);
      int nulls = 0, firstNull = -1;
      for (DWORD slot = 0; slot < count && slot < 4096; slot++) {
        if (*reinterpret_cast<const DWORD*>(entries + slot * 16 + 8) == 0) {
          if (firstNull < 0) firstNull = static_cast<int>(slot);
          nulls++;
        }
      }
      Log(1, "  vertex-declaration table: requested slot %lu, count %lu, table dwords [%08lX %08lX %08lX %08lX], empty slots %d (first %d)",
          context->Esi, count, *reinterpret_cast<const DWORD*>(table + 0x1C), *reinterpret_cast<const DWORD*>(table + 0x20),
          *reinterpret_cast<const DWORD*>(table + 0x2C), *reinterpret_cast<const DWORD*>(table + 0x30), nulls, firstNull);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }

  // DR2 heap registry: names at 0xD64340, sizes at 0xDFB770 (indexed by heap id, 128 slots).
  __try {
    for (int heap = 0; heap < 128; heap++) {
      const DWORD size = reinterpret_cast<const DWORD*>(Va(0x00DFB770))[heap];
      const char* name = reinterpret_cast<const char* const*>(Va(0x00D64340))[heap];
      if (size) Log(1, "  heap %3d size=%8lu KiB %s", heap, size >> 10, name ? name : "?");
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  int found = 0;
  __try {
    for (int row = 0; row < 0x100 / 4; row += 8)
      Log(1, "  esp+%03X: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX", row * 4, stack[row], stack[row + 1], stack[row + 2],
          stack[row + 3], stack[row + 4], stack[row + 5], stack[row + 6], stack[row + 7]);
    for (int index = 0; index < 1024 && found < 24; index++) {
      const DWORD value = stack[index];
      if (value < textStart + 5 || value >= textEnd) continue;
      const BYTE* site = reinterpret_cast<const BYTE*>(value);
      const bool afterCall = site[-5] == 0xE8 || (site[-2] == 0xFF && (site[-1] & 0x38) == 0x10) ||
                             (site[-3] == 0xFF && (site[-2] & 0x38) == 0x10) || (site[-6] == 0xFF && (site[-5] & 0x38) == 0x10);
      if (!afterCall) continue;
      Log(1, "  [esp+%03X] return %08lX", index * 4, value);
      found++;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

constexpr DWORD kMapIndexVa = 0x00DDC960;  // gMapIndex, current level enum
volatile DWORD g_clock = 0;           // game clock object, captured by Hook_ClockAdvance

// ---- Light-context loads (diagnostics): thiscall 0x008B0BA0(const char* path) loads one
// data/datafile/shader_settings/<name>.csv (or .override.csv) light context; the hook logs each path.
constexpr DWORD kLightContextLoadVa = 0x008B0BA0;
const BYTE kLightContextLoad[] = {0x81, 0xEC, 0x04, 0x01, 0x00, 0x00};  // sub esp, 0x104
DWORD g_lightContextLoadContinue = kLightContextLoadVa + 6;

void __cdecl LogLightContextLoad(const char* path) {
  __try {
    Log(1, "light context: load %s", path);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

__declspec(naked) void Hook_LightContextLoad() {
  __asm {
    push ecx
    push dword ptr [esp + 8]
    call LogLightContextLoad
    add esp, 4
    pop ecx
    sub esp, 0x104
    jmp dword ptr [g_lightContextLoadContinue]
  }
}

bool InstallJumpHook(DWORD va, const BYTE* expected, size_t size, void* hook, DWORD* resume) {
  if (!Matches(va, expected, size)) return false;
  BYTE jump[16];
  memset(jump, 0x90, sizeof(jump));
  jump[0] = 0xE9;
  const LONG relative = static_cast<LONG>(reinterpret_cast<ULONG_PTR>(hook) - (reinterpret_cast<ULONG_PTR>(Va(va)) + 5));
  memcpy(jump + 1, &relative, 4);
  if (!WriteProtected(Va(va), jump, size)) return false;
  *resume = reinterpret_cast<DWORD>(Va(va)) + static_cast<DWORD>(size);
  return true;
}

// ---- Player position tracking (diagnostics). Once a host route has loaded, writable memory is scanned
// for float triples within a few metres of the level's spawn point; those candidates (the player, its
// controller, camera targets) are then logged whenever they move, so the log records where the player went.
struct PositionCandidate {
  volatile float* xyz;
  float last[3];
};
PositionCandidate g_positions[32];
int g_positionCount = 0;

void ScanForPositions(const float spawn[3]) {
  g_positionCount = 0;
  MEMORY_BASIC_INFORMATION info;
  for (BYTE* address = reinterpret_cast<BYTE*>(0x10000); address < reinterpret_cast<BYTE*>(0x7FFE0000) && g_positionCount < 32;
       address = static_cast<BYTE*>(info.BaseAddress) + info.RegionSize) {
    if (!VirtualQuery(address, &info, sizeof(info))) break;
    // Heap objects only: skip images (the game's and this DLL's constants), thread stacks (below 16 MiB) and guard pages.
    if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || reinterpret_cast<ULONG_PTR>(info.BaseAddress) < 0x01000000 ||
        !(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (info.Protect & PAGE_GUARD)) continue;
    __try {
      const float* cursor = static_cast<const float*>(info.BaseAddress);
      const float* end = reinterpret_cast<const float*>(static_cast<BYTE*>(info.BaseAddress) + info.RegionSize) - 3;
      for (; cursor < end && g_positionCount < 32; cursor++) {
        if (!(fabsf(cursor[0] - spawn[0]) <= 3.0f && fabsf(cursor[2] - spawn[2]) <= 3.0f && fabsf(cursor[1] - spawn[1]) <= 3.0f)) continue;  // also rejects NaN
        PositionCandidate& candidate = g_positions[g_positionCount++];
        candidate.xyz = const_cast<volatile float*>(cursor);
        memcpy(candidate.last, cursor, sizeof(candidate.last));
        cursor += 3;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  Log(1, "position: %d candidates near spawn (%.1f, %.1f, %.1f)", g_positionCount, spawn[0], spawn[1], spawn[2]);
}

void LogMovedPositions() {
  for (int index = 0; index < g_positionCount; index++) {
    PositionCandidate& candidate = g_positions[index];
    float now[3];
    __try {
      now[0] = candidate.xyz[0];
      now[1] = candidate.xyz[1];
      now[2] = candidate.xyz[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      continue;
    }
    const float dx = now[0] - candidate.last[0], dz = now[2] - candidate.last[2];
    if (!(dx * dx + dz * dz >= 4.0f) || !(fabsf(now[0]) < 2000.0f && fabsf(now[1]) < 500.0f && fabsf(now[2]) < 2000.0f)) continue;
    Log(1, "position[%d] %p: %.2f, %.2f, %.2f", index, candidate.xyz, now[0], now[1], now[2]);
    memcpy(candidate.last, now, sizeof(now));
  }
}

// Case Zero's day starts at 07:00 (its opening missions run from StartTime 25200). DR2's clock starts a new game
// at 00:00 unless a mission sets it, so a host route entered at day 0 00:0x gets Case Zero's start time.
void ApplyCaseZeroStartTime() {
  if (!g_clock || !g_prologMode) return;
  int* fields = reinterpret_cast<int*>(g_clock + 0x14);
  if (fields[0] != 0 || fields[1] != 0 || fields[2] > 5) return;
  const int minute = fields[2];
  fields[1] = 7;
  fields[2] = 0;
  Log(1, "clock: new game at 00:%02d set to Case Zero's 07:00 start", minute);
}

// Reports campaign-relevant engine state as it changes, for runtime evidence.
void MonitorEngineState() {
  int lastMap = -1;
  int lastFlag = -1;
  int lastClock = -1;
  DWORD hostRouteSince = 0;
  bool scanned = false;
  for (DWORD tick = 0;; tick++) {
    const int map = static_cast<int>(*reinterpret_cast<volatile DWORD*>(Va(kMapIndexVa)));
    const int flag = *reinterpret_cast<volatile BYTE*>(Va(kPrologFlagVa));
    if (map != lastMap || flag != lastFlag) {
      Log(1, "engine state: level enum=%d enable_prolog_experience=%d", map, flag);
      if (map != lastMap) {
        hostRouteSince = (map == 52 || map == 53) ? GetTickCount() : 0;
        scanned = false;
        g_positionCount = 0;
      }
      lastMap = map;
      lastFlag = flag;
    }
    if (hostRouteSince && !scanned && GetTickCount() - hostRouteSince > 8000) {
      ApplyCaseZeroStartTime();
      // Spawn points: town "Start" (route 52) and garage "StartCase1Prologue" (route 53).
      const float town[3] = {-266.354f, 3.242f, -31.754f}, garage[3] = {-270.933f, 3.287f, -65.945f};
      ScanForPositions(g_trackSpawnSet ? g_trackSpawn : (map == 52 ? town : garage));
      scanned = true;
    }
    if (scanned && tick % 10 == 0) LogMovedPositions();
    // SteamStub decrypts code progressively; install the light-context logger once its bytes are plain.
    static bool lightHook = false;
    if (!lightHook && InstallJumpHook(kLightContextLoadVa, kLightContextLoad, sizeof(kLightContextLoad), reinterpret_cast<void*>(&Hook_LightContextLoad), &g_lightContextLoadContinue)) {
      lightHook = true;
      Log(1, "light context: load logger installed");
    }
    if (const DWORD clock = g_clock) {
      const int* fields = reinterpret_cast<const int*>(clock + 0x14);
      const int stamp = fields[0] * 100 + fields[1];
      if (stamp != lastClock) {
        Log(1, "clock: day %d %02d:%02d", fields[0], fields[1], fields[2]);
        lastClock = stamp;
      }
    }
    Sleep(100);
  }
}

// ---- Frontend actions added by the Case Zero main menu (build-case-zero-main-menu.mjs).
// DR2's frontend dispatcher (0x0080F0B0, thiscall(event, flag), event+4 = action-name hash) is entered
// through a hook that handles the Case Zero actions first:
//   ACT:CZAchievements  opens the Steam overlay's achievements page (Case Zero used the Xbox guide);
//   ACT:CZDeadRising2   switches the campaign selector to vanilla DR2 and restarts the game through Steam.
constexpr DWORD kFrontendDispatchVa = 0x0080F0B0;
constexpr DWORD kActionHashVa = 0x00A16D20;       // cdecl DWORD hash(const char* name, int length)
constexpr DWORD kPcQuitActionHashVa = 0x00DDD784;  // registered hash of "PCQuit"
constexpr DWORD kRatingLogoEventVa = 0x0080C180;
const BYTE kFrontendDispatch[] = {0x83, 0xEC, 0x20, 0x53, 0x55};  // sub esp, 0x20; push ebx; push ebp
const BYTE kRatingLogoEvent[] = {0x53, 0x57, 0x8B, 0x7C, 0x24, 0x0C};  // push ebx; push edi; mov edi,[esp+0Ch]
DWORD g_frontendDispatchContinue = kFrontendDispatchVa + 5;
DWORD g_ratingLogoEventContinue = kRatingLogoEventVa + sizeof(kRatingLogoEvent);

void __cdecl ScheduleHarnessRatingLogoAdvance(void* screen, DWORD* event) {
  if (!coop::IsHarness() || !screen || !event) return;
  using Hash_t = DWORD(__cdecl*)(const char*, int);
  auto hash = reinterpret_cast<Hash_t>(Va(kActionHashVa));
  static DWORD capcom = 0;
  if (!capcom) capcom = hash("capcom", 6);
  if (event[1] != capcom || g_harnessRatingLogoScreen) return;
  g_harnessRatingLogoScreen = screen;
  g_harnessRatingAdvanceAt = GetTickCount() + 750;
  Log(1, "harness: rating-logo screen initialized; native advance scheduled");
}

__declspec(naked) void Hook_RatingLogoEvent() {
  __asm {
    push ecx
    push dword ptr [esp + 8]  // event
    push ecx                  // screen
    call ScheduleHarnessRatingLogoAdvance
    add esp, 8
    pop ecx
    push ebx
    push edi
    mov edi, dword ptr [esp + 0x0C]
    jmp dword ptr [g_ratingLogoEventContinue]
  }
}

// Restarts DR2 through Steam once this process has exited. |launch| is the one-shot campaign for that launch
// (nullptr = vanilla Dead Rising 2); the ordinary boot selector always returns to vanilla.
void RelaunchGame(const wchar_t* launch) {
  wchar_t ini[MAX_PATH];
  swprintf(ini, MAX_PATH, L"%scase_zero_campaign.ini", g_root);
  WritePrivateProfileStringW(L"Campaign", L"Active", L"vanilla_dr2", ini);
  WritePrivateProfileStringW(L"Campaign", L"Launch", launch, ini);
  // One helper per session: after a declined switch the first helper is still waiting for this process.
  static bool helperStarted = false;
  if (helperStarted) {
    Log(1, "frontend: switch to %ls requested (helper already waiting)", launch ? launch : L"vanilla_dr2");
    return;
  }
  helperStarted = true;
  // A helper waits for this process to exit, then starts DR2 again through Steam. case_zero_switch.ps1 (shipped with
  // the pack) also covers the game window with a loading screen until the new session's title menu is up.
  wchar_t command[1024];
  wchar_t script[MAX_PATH];
  swprintf(script, MAX_PATH, L"%scase_zero_switch.ps1", g_root);
  if (Exists(script)) {
    struct Search { DWORD pid; HWND window; } search = {GetCurrentProcessId(), nullptr};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
      auto* state = reinterpret_cast<Search*>(parameter);
      DWORD pid = 0;
      GetWindowThreadProcessId(window, &pid);
      if (pid != state->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
      state->window = window;
      return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    RECT rect = {};
    if (search.window) GetWindowRect(search.window, &rect);
    swprintf(command, 1024,
             L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"%s\" -GamePid %lu "
             L"-X %ld -Y %ld -Width %ld -Height %ld",
             script, GetCurrentProcessId(), rect.left, rect.top, rect.right - rect.left,
             rect.bottom - rect.top);
  } else {
    swprintf(command, 1024,
             L"powershell.exe -NoProfile -WindowStyle Hidden -Command \"Wait-Process -Id %lu -ErrorAction SilentlyContinue; "
             L"Start-Process 'steam://rungameid/45740'\"",
             GetCurrentProcessId());
  }
  STARTUPINFOW startup = {sizeof(startup)};
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process = {};
  if (CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    Log(1, "frontend: relaunch helper started (PID %lu)", process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  } else {
    Log(1, "frontend: relaunch helper failed to start (error %lu)", GetLastError());
  }
  Log(1, "frontend: relaunching into %ls", launch ? launch : L"vanilla_dr2");
}

// The quit confirmation's text id is an immediate in the dialog setup (0x007CC223: mov [esi+0Ch], 10010).
constexpr DWORD kQuitPromptStringVa = 0x007CC226;
constexpr DWORD kQuitPromptString = 10010;        // "Are you sure you want to quit the game?"
constexpr DWORD kSwitchToCaseZeroString = 99902;  // "Switch to Case: Zero?" (build-case-zero-launcher.mjs)
constexpr DWORD kSwitchBackToDr2String = 99903;   // "Switch Back to Dead Rising 2?" (build-case-zero-main-menu.mjs)
bool g_switchPending = false;

void SetQuitPromptString(DWORD id) {
  const DWORD current = *reinterpret_cast<DWORD*>(Va(kQuitPromptStringVa));
  if (current == id) return;
  if (current != kQuitPromptString && current != kSwitchToCaseZeroString && current != kSwitchBackToDr2String) {
    Log(1, "frontend: quit prompt immediate differs (%lu); confirmation text left unchanged", current);
    return;
  }
  WriteProtected(Va(kQuitPromptStringVa), &id, sizeof(id));
}

// The player declined the switch (or chose something else): the helper finds no Launch key and does not relaunch.
void CancelPendingSwitch() {
  g_switchPending = false;
  SetQuitPromptString(kQuitPromptString);
  wchar_t ini[MAX_PATH];
  swprintf(ini, MAX_PATH, L"%scase_zero_campaign.ini", g_root);
  WritePrivateProfileStringW(L"Campaign", L"Launch", nullptr, ini);
  Log(1, "frontend: switch cancelled");
}

void OpenSteamAchievements() {
  using SteamFriends_t = void*(__cdecl*)();
  HMODULE steam = GetModuleHandleW(L"steam_api.dll");
  auto steamFriends = steam ? reinterpret_cast<SteamFriends_t>(GetProcAddress(steam, "SteamFriends")) : nullptr;
  void* friends = steamFriends ? steamFriends() : nullptr;
  if (!friends) {
    Log(1, "frontend: Steam friends interface unavailable; achievements overlay not opened");
    return;
  }
  // ISteamFriends014 slot 21: void ActivateGameOverlay(const char* dialog).
  using ActivateGameOverlay_t = void(__fastcall*)(void*, void*, const char*);
  reinterpret_cast<ActivateGameOverlay_t>((*reinterpret_cast<void***>(friends))[21])(friends, nullptr, "Achievements");
  Log(1, "frontend: opened Steam achievements overlay");
}

// Returns true when the action was fully handled here. May rewrite the event to a native action.
bool __cdecl HandleFrontendAction(DWORD* event) {
  using Hash_t = DWORD(__cdecl*)(const char*, int);
  static DWORD achievements = 0, deadRising2 = 0, caseZero = 0;
  if (!achievements) {
    auto hash = reinterpret_cast<Hash_t>(Va(kActionHashVa));
    achievements = hash("CZAchievements", 14);
    deadRising2 = hash("CZDeadRising2", 13);
    caseZero = hash("CZCaseZero", 10);
    Log(1, "frontend: action hash CZCaseZero=%08lX", caseZero);
    Log(1, "frontend: action hashes CZAchievements=%08lX CZDeadRising2=%08lX PCQuit=%08lX", achievements, deadRising2,
        *reinterpret_cast<DWORD*>(Va(kPcQuitActionHashVa)));
  }
  static int logged = 0;
  if (logged < 64) {
    logged++;
    Log(1, "frontend: event %08lX %08lX %08lX", event[0], event[1], event[2]);
  }
  if (event[1] == achievements) {
    OpenSteamAchievements();
    return true;
  }
  if (event[1] == caseZero && g_launcher) {
    // DR2's own quit confirmation asks "Switch to Case: Zero?" for this button (launcher string 99902). The waiting
    // helper relaunches only if the one-shot Launch key is still set when the game exits (the player answered YES).
    SetQuitPromptString(kSwitchToCaseZeroString);
    RelaunchGame(L"case_zero");
    g_switchPending = true;
    event[1] = *reinterpret_cast<DWORD*>(Va(kPcQuitActionHashVa));  // leave through DR2's own quit path
    return false;
  }
  if (event[1] == deadRising2 && g_campaign == Campaign::CaseZero) {
    SetQuitPromptString(kSwitchBackToDr2String);
    RelaunchGame(L"vanilla_dr2");
    g_switchPending = true;
    event[1] = *reinterpret_cast<DWORD*>(Va(kPcQuitActionHashVa));  // leave through DR2's own quit path
    return false;
  }
  // The quit confirmation answers through one more event (0x3414F9F6); 0x16EEF there is YES, which leaves the switch
  // armed for the clean exit that follows. NO, or any other menu action, keeps the player in this game.
  constexpr DWORD kDialogAnswerEvent = 0x3414F9F6, kDialogYes = 0x00016EEF;
  if (g_switchPending && !(event[1] == kDialogAnswerEvent && event[2] == kDialogYes)) CancelPendingSwitch();
  return false;
}

__declspec(naked) void Hook_FrontendDispatch() {
  __asm {
    push ecx
    push dword ptr [esp + 8]  // event
    call HandleFrontendAction
    add esp, 4
    pop ecx
    test al, al
    jz passthrough
    mov al, 1
    ret 8
  passthrough:
    sub esp, 0x20
    push ebx
    push ebp
    jmp dword ptr [g_frontendDispatchContinue]
  }
}

// ---- Game clock. The clock object (+0x14 day, +0x18 hour, +0x1C minute, +0x20 second) is advanced by
// thiscall 0x004189A0(days, hours, minutes, seconds); the hook only records `this` for monitoring.
constexpr DWORD kClockAdvanceVa = 0x004189A0;
const BYTE kClockAdvance[] = {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x54, 0x24, 0x08};  // mov eax,[esp+4]; mov edx,[esp+8]
DWORD g_clockAdvanceContinue = kClockAdvanceVa + 8;

__declspec(naked) void Hook_ClockAdvance() {
  __asm {
    mov g_clock, ecx
    mov eax, dword ptr [esp + 4]
    mov edx, dword ptr [esp + 8]
    jmp dword ptr [g_clockAdvanceContinue]
  }
}

bool InstallClockHook() {
  if (!Matches(kClockAdvanceVa, kClockAdvance, sizeof(kClockAdvance))) return false;
  BYTE jump[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
  const LONG relative = static_cast<LONG>(reinterpret_cast<ULONG_PTR>(&Hook_ClockAdvance) - (reinterpret_cast<ULONG_PTR>(Va(kClockAdvanceVa)) + 5));
  memcpy(jump + 1, &relative, 4);
  if (!WriteProtected(Va(kClockAdvanceVa), jump, sizeof(jump))) return false;
  g_clockAdvanceContinue = reinterpret_cast<DWORD>(Va(kClockAdvanceVa)) + 8;
  return true;
}

bool InstallFrontendHook() {
  if (!Matches(kFrontendDispatchVa, kFrontendDispatch, sizeof(kFrontendDispatch))) return false;
  if (g_campaign == Campaign::CaseZero && !InstallClockHook()) Log(1, "clock: advance signature differs; clock monitoring disabled");

  if (coop::IsHarness()) {
    if (!Matches(kRatingLogoEventVa, kRatingLogoEvent, sizeof(kRatingLogoEvent))) {
      Log(1, "harness: rating-logo event signature differs; startup skip disabled");
    } else {
      BYTE ratingJump[sizeof(kRatingLogoEvent)] = {0xE9, 0, 0, 0, 0, 0x90};
      const LONG ratingRelative = static_cast<LONG>(reinterpret_cast<ULONG_PTR>(&Hook_RatingLogoEvent) -
          (reinterpret_cast<ULONG_PTR>(Va(kRatingLogoEventVa)) + 5));
      memcpy(ratingJump + 1, &ratingRelative, 4);
      if (WriteProtected(Va(kRatingLogoEventVa), ratingJump, sizeof(ratingJump))) {
        g_ratingLogoEventContinue = reinterpret_cast<DWORD>(Va(kRatingLogoEventVa)) + sizeof(kRatingLogoEvent);
        Log(1, "harness: native rating-logo transition hook installed");
      } else {
        Log(1, "harness: rating-logo transition hook write failed");
      }
    }
  }

  BYTE jump[5] = {0xE9};
  const LONG relative = static_cast<LONG>(reinterpret_cast<ULONG_PTR>(&Hook_FrontendDispatch) - (reinterpret_cast<ULONG_PTR>(Va(kFrontendDispatchVa)) + 5));
  memcpy(jump + 1, &relative, 4);
  if (!WriteProtected(Va(kFrontendDispatchVa), jump, sizeof(jump))) return false;
  g_frontendDispatchContinue = reinterpret_cast<DWORD>(Va(kFrontendDispatchVa)) + 5;
  Log(1, "frontend: Case Zero menu actions installed");
  return true;
}

volatile LONG g_patchState = 0;  // 0 pending, 1 applying, 2 applied, 3 failed

void TryApplyCodePatches() {
  if (g_patchState >= 2 || !CodeUnpacked() || !Matches(kFrontendDispatchVa, kFrontendDispatch, sizeof(kFrontendDispatch))) return;
  if (InterlockedCompareExchange(&g_patchState, 1, 0) != 0) {
    while (g_patchState == 1) Sleep(0);  // another thread is applying; wait so callers see the result
    return;
  }
  // The vanilla launcher and the co-op harness only need frontend instrumentation. Every Case Zero gameplay patch
  // stays out of vanilla DR2, including unattended multiplayer clients.
  const bool applied = (g_launcher || coop::IsHarness()) ? InstallFrontendHook()
                                                         : (ApplyCodePatches() && InstallFrontendHook());
  Log(1, "code patches %s on thread %lu", applied ? "applied" : "FAILED", GetCurrentThreadId());
  InterlockedExchange(&g_patchState, applied ? 2 : 3);
}

DWORD WINAPI CampaignThread(LPVOID) {
  // SteamStub decrypts .text after the loader has run DllMain; the startup hooks normally apply the
  // patches synchronously, and this loop is the fallback.
  for (DWORD elapsed = 0; elapsed < 120000 && g_patchState < 2; elapsed++) {
    PatchImports();
    TryApplyCodePatches();
    if (g_patchState < 2) Sleep(1);
  }
  const bool patched = g_patchState == 2;
  if (!patched) {
    Log(1, "code signatures never matched; unsupported deadrising2.exe build, Case Zero patches not applied");
    return 0;
  }
  // Keep re-checking the IAT for a while in case the protector re-resolves imports.
  for (DWORD settle = 0; settle < 300; settle++) {
    PatchImports();
    Sleep(100);
  }
  if (g_logLevel > 0 && !g_launcher) MonitorEngineState();
  return 0;
}

// ---- Scripted keyboard input for unattended tests. Case Zero uses case_zero_input.txt; the four-player harness uses
// coop_input.N.txt per process. Each file lists held DIK scan codes (decimal, whitespace-separated, e.g. 28 for Enter).
const GUID kGuidSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
const GUID kGuidSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
using CreateDevice8_t = HRESULT(__stdcall*)(void*, REFGUID, void**, void*);
using GetDeviceState_t = HRESULT(__stdcall*)(void*, DWORD, void*);
using GetDeviceData_t = HRESULT(__stdcall*)(void*, DWORD, void*, DWORD*, DWORD);
CreateDevice8_t Real_CreateDevice8 = nullptr;
GetDeviceState_t Real_GetDeviceState = nullptr;
GetDeviceData_t Real_GetDeviceData = nullptr;
BYTE g_scriptKeys[256];
BYTE g_reportedKeys[256];
BYTE g_stateLoggedKeys[256];
DWORD g_scriptPolled = 0;
HarnessKeyboard g_harnessKeyboard;
HarnessMouse g_harnessMouse;
DWORD g_mouseCommandSeen = 0;
volatile LONG g_privateMouseEnabled = 0;

void PollHarnessMouse(DWORD now) {
  wchar_t path[MAX_PATH];
  swprintf(path, MAX_PATH, L"%scoop_mouse.%d.txt", g_root, coop::Instance());
  FILE* file = _wfsopen(path, L"r", _SH_DENYNO);
  if (!file) { g_harnessMouse.ReleaseButtons(now); return; }
  DWORD sequence = 0, buttons = 0;
  LONG x = 0, y = 0, z = 0;
  const int fields = fscanf_s(file, "%lu %ld %ld %ld %lu", &sequence, &x, &y, &z, &buttons);
  fclose(file);
  if (fields != 5 || !sequence) { g_harnessMouse.ReleaseButtons(now); return; }
  if (sequence == g_mouseCommandSeen) return;
  g_mouseCommandSeen = sequence;
  if (g_harnessMouse.Submit(sequence, x, y, z, buttons, now))
    Log(1, "input: mouse accepted sequence=%lu axes=%ld,%ld,%ld buttons=%lu", sequence, x, y, z, buttons);
  else {
    g_harnessMouse.ReleaseButtons(now);
    Log(1, "input: mouse rejected sequence=%lu", sequence);
  }
}

void PollScriptKeys(bool background = false) {
  if (coop::IsHarness() && !background) return;
  const DWORD now = GetTickCount();
  if (now - g_scriptPolled < 50) return;
  g_scriptPolled = now;
  if (coop::IsHarness() && InterlockedCompareExchange(&g_privateMouseEnabled, 0, 0)) PollHarnessMouse(now);
  memset(g_scriptKeys, 0, sizeof(g_scriptKeys));
  wchar_t path[MAX_PATH];
  if (coop::IsHarness()) swprintf(path, MAX_PATH, L"%scoop_input.%d.txt", g_root, coop::Instance());
  else swprintf(path, MAX_PATH, L"%scase_zero_input.txt", g_root);
  FILE* file = _wfsopen(path, L"r", _SH_DENYNO);
  if (file) {
    int code;
    while (fscanf_s(file, "%d", &code) == 1)
      if (code > 0 && code < 256) g_scriptKeys[code] = 0x80;
    fclose(file);
  }
  if (coop::IsHarness()) {
    g_harnessKeyboard.Update(g_scriptKeys, now);
    for (int key = 0; key < 256; key++) if (g_scriptKeys[key] != g_stateLoggedKeys[key]) {
      coop::TraceInput(key, g_scriptKeys[key] != 0);
      g_stateLoggedKeys[key] = g_scriptKeys[key];
    }
  }
}

DWORD WINAPI ScriptInputThread(LPVOID) {
  // Menu transitions can stop asking DirectInput until the confirm key is released. Keep the
  // synthetic state synchronized independently so a scripted key-down can never deadlock there.
  for (;;) {
    PollScriptKeys(true);
    const DWORD now = GetTickCount();
    if (g_harnessWindow && g_harnessRatingLogoScreen &&
        static_cast<LONG>(now - g_harnessRatingAdvanceAt) >= 0)
      PostMessageW(g_harnessWindow, kHarnessAdvanceRating, 0, 0);
    if (g_harnessWindow && (coop::CampaignActivationPending() || coop::ConnectionMeshInitPending() ||
                            coop::ConnectionListenerRearmPending() ||
                            coop::ClientTransitionPending() ||
                            coop::ClientDataTransferRequestPending() ||
                            coop::HostStateTransferPending() ||
                            coop::HostFlow7FinalizePending() ||
                            coop::ClothingVariantsPending()))
      PostMessageW(g_harnessWindow, kHarnessActivateCampaign, 0, 0);
    if (g_harnessWndProc && g_harnessWindow && now - g_lastHarnessActivation >= 1000 &&
        (InterlockedCompareExchange(&g_harnessNeedsActivation, 0, 0) != 0 || HarnessGameInactive())) {
      PostMessageW(g_harnessWindow, kHarnessActivate, 0, 0);
      g_lastHarnessActivation = now;
    }
    // GPU readback can block for seconds. Only Present captures frames; input must keep polling.
    Sleep(20);
  }
}

bool HasScriptKeys() {
  for (BYTE key : g_scriptKeys) if (key) return true;
  return false;
}

HRESULT __stdcall Hook_GetDeviceState(void* device, DWORD size, void* data) {
  if (coop::IsHarness()) return g_harnessKeyboard.State(size, data);
  PollScriptKeys();
  HRESULT result = Real_GetDeviceState(device, size, data);
  const bool scripted = coop::IsHarness() && HasScriptKeys();
  if (size == 256 && (SUCCEEDED(result) || scripted)) {
    if (FAILED(result)) memset(data, 0, size);
    BYTE* keys = static_cast<BYTE*>(data);
    for (int index = 0; index < 256; index++) {
      keys[index] |= g_scriptKeys[index];
      if (coop::IsHarness() && g_scriptKeys[index] != g_stateLoggedKeys[index]) {
        coop::TraceInput(index, g_scriptKeys[index] != 0);
        g_stateLoggedKeys[index] = g_scriptKeys[index];
        g_reportedKeys[index] = g_scriptKeys[index];
      }
    }
    if (scripted) result = S_OK;
  }
  return result;
}

struct DeviceObjectData {  // DIDEVICEOBJECTDATA (x86)
  DWORD offset, data, timeStamp, sequence;
  UINT_PTR appData;
};

HRESULT __stdcall Hook_GetDeviceData(void* device, DWORD objectSize, void* data, DWORD* count, DWORD flags) {
  if (coop::IsHarness()) return g_harnessKeyboard.Data(objectSize, data, count, flags);
  const DWORD capacity = count ? *count : 0;
  PollScriptKeys();
  HRESULT result = Real_GetDeviceData(device, objectSize, data, count, flags);
  const bool scripted = coop::IsHarness() && HasScriptKeys();
  if ((!count || !data || objectSize != sizeof(DeviceObjectData) || (flags & 1 /* DIGDD_PEEK */)) || (FAILED(result) && !scripted)) return result;
  if (FAILED(result)) { *count = 0; result = S_OK; }
  DeviceObjectData* events = static_cast<DeviceObjectData*>(data);
  for (int key = 1; key < 256 && *count < capacity; key++) {
    if (g_scriptKeys[key] == g_reportedKeys[key]) continue;
    DeviceObjectData& event = events[(*count)++];
    event = {static_cast<DWORD>(key), g_scriptKeys[key], GetTickCount(), 0, 0};
    if (coop::IsHarness()) coop::TraceInput(key, g_scriptKeys[key] != 0);
    g_reportedKeys[key] = g_scriptKeys[key];
    g_stateLoggedKeys[key] = g_scriptKeys[key];
  }
  return result;
}

HRESULT __stdcall HarnessMouseState(void*, DWORD size, void* data) {
  DWORD sequence = 0;
  const HRESULT result = g_harnessMouse.State(size, data, &sequence);
  if (SUCCEEDED(result)) {
    static DWORD lastSequence = 0, lastButtons = 0;
    DWORD buttons = 0;
    for (DWORD i = 0; i < size - 12; ++i) if (static_cast<BYTE*>(data)[12 + i]) buttons |= 1u << i;
    const LONG* axes = static_cast<const LONG*>(data);
    if (sequence != lastSequence || buttons != lastButtons || axes[0] || axes[1] || axes[2])
      Log(1, "input: mouse consumed sequence=%lu axes=%ld,%ld,%ld buttons=%lu", sequence, axes[0], axes[1], axes[2], buttons);
    lastSequence = sequence;
    lastButtons = buttons;
  }
  return result;
}

HRESULT __stdcall HarnessMouseData(void*, DWORD size, void* data, DWORD* count, DWORD flags) {
  return g_harnessMouse.Data(size, data, count, flags);
}

HRESULT __stdcall HarnessDeviceReady(void*) { return S_OK; }

void InstallHarnessInputDevice(void* device, bool mouse) {
  // Device8 has 32 slots in the Windows SDK. Clone per object: keyboard/mouse
  // may share a hardware vtable, and unrelated controllers must remain untouched.
  static void* tables[8][32]{};
  static LONG allocated = 0;
  void** original = *reinterpret_cast<void***>(device);
  for (auto& table : tables) if (original == table) return;
  const LONG index = InterlockedIncrement(&allocated) - 1;
  if (index >= 8) {
    Log(1, "input: private device capacity exceeded; aborting owned harness child");
    TerminateProcess(GetCurrentProcess(), 0xE043C006);
    return;
  }
  void** table = tables[index];
  memcpy(table, original, sizeof(tables[index]));
  table[7] = table[8] = table[25] = reinterpret_cast<void*>(&HarnessDeviceReady);
  table[9] = mouse ? reinterpret_cast<void*>(&HarnessMouseState) : reinterpret_cast<void*>(&Hook_GetDeviceState);
  table[10] = mouse ? reinterpret_cast<void*>(&HarnessMouseData) : reinterpret_cast<void*>(&Hook_GetDeviceData);
  InterlockedExchangePointer(reinterpret_cast<void* volatile*>(device), table);
  Log(1, "input: private %s device installed; hardware input and acquisition isolated", mouse ? "mouse" : "keyboard");
}

HRESULT __stdcall Hook_CreateDevice8(void* input, REFGUID guid, void** device, void* outer) {
  const HRESULT result = Real_CreateDevice8(input, guid, device, outer);
  if (SUCCEEDED(result) && device && *device && coop::IsHarness() && wcsstr(GetCommandLineW(), L"-coopprivatemouse") &&
      (IsEqualGUID(guid, kGuidSysKeyboard) || IsEqualGUID(guid, kGuidSysMouse))) {
    InterlockedExchange(&g_privateMouseEnabled, 1);
    InstallHarnessInputDevice(*device, IsEqualGUID(guid, kGuidSysMouse));
    return result;
  }
  if (SUCCEEDED(result) && device && *device && IsEqualGUID(guid, kGuidSysKeyboard)) {
    PatchVtableSlot(*device, 9, reinterpret_cast<void*>(&Hook_GetDeviceState), reinterpret_cast<void**>(&Real_GetDeviceState));
    PatchVtableSlot(*device, 10, reinterpret_cast<void*>(&Hook_GetDeviceData), reinterpret_cast<void**>(&Real_GetDeviceData));
    Log(1, "input: keyboard device hooked for scripted test input");
  }
  return result;
}

}  // namespace

extern "C" HRESULT WINAPI CZ_DirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
  using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
  static DirectInput8Create_t real = nullptr;
  if (!real) {
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\dinput8.dll");
    g_realDinput = LoadLibraryW(path);
    if (g_realDinput) real = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(g_realDinput, "DirectInput8Create"));
    if (!real) return E_FAIL;
  }
  const HRESULT result = real(instance, version, riid, out, outer);
  if (SUCCEEDED(result) && out && *out && (g_campaign == Campaign::CaseZero || coop::IsHarness()))
    PatchVtableSlot(*out, 3, reinterpret_cast<void*>(&Hook_CreateDevice8), reinterpret_cast<void**>(&Real_CreateDevice8));
  return result;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_DETACH) {
    // A clean exit while a switch is pending is DR2's quit path after the player answered YES; a crash or a killed
    // process never gets here, so the waiting helper does not relaunch in those cases.
    if (g_switchPending) {
      wchar_t ini[MAX_PATH];
      swprintf(ini, MAX_PATH, L"%scase_zero_campaign.ini", g_root);
      WritePrivateProfileStringW(L"Campaign", L"LaunchConfirmed", L"1", ini);
    }
    return TRUE;
  }
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  DisableThreadLibraryCalls(instance);
  InitializeCriticalSection(&g_logLock);
  LoadConfiguration();
  coop::Initialize(g_root);
  // Multiplayer test clients must exercise the stock DR2 frontend. Loading the additive Case Zero campaign selector
  // here makes startup slower and can hide vanilla lobby behavior behind unrelated replacement assets.
  if (coop::IsHarness()) g_launcher = false;
  if (coop::IsHarness()) {
    wchar_t saveRoot[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"DR2_COOP_SAVE_ROOT", saveRoot, MAX_PATH);
    const bool configured = length > 0 && length < MAX_PATH &&
        g_harnessSaveStorage.Configure(saveRoot, PrivateSaveName());
    if (configured && GetFullPathNameW(saveRoot, MAX_PATH, g_harnessCaptureRoot, nullptr) < MAX_PATH) {
      wchar_t* last = wcsrchr(g_harnessCaptureRoot, L'\\');
      if (last) last[1] = 0;
      else g_harnessCaptureRoot[0] = 0;
    }
    Log(1, "local save storage: configured=%d root=%ls; Steam save I/O disabled for harness", configured, saveRoot);
    HANDLE inputThread = CreateThread(nullptr, 0, ScriptInputThread, nullptr, 0, nullptr);
    if (inputThread) CloseHandle(inputThread);
  }
  Log(1, "Case Zero runtime loaded; campaign=%s%s root=%ls", g_campaign == Campaign::CaseZero ? "case_zero" : "vanilla_dr2",
      g_launcher ? " (launcher menu)" : "", g_root);
  if (g_launcher) {
    PatchImports();
    HANDLE thread = CreateThread(nullptr, 0, CampaignThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
  }
  if (coop::IsHarness() && !g_launcher && g_campaign != Campaign::CaseZero) {
    PatchImports();
    HANDLE thread = CreateThread(nullptr, 0, CampaignThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
  }
  if (coop::IsHarness() && g_logLevel > 0) AddVectoredExceptionHandler(1, CrashReporter);
  if (g_campaign == Campaign::CaseZero) {
    LoadAliases();
    if (!coop::IsHarness() && g_logLevel > 0) AddVectoredExceptionHandler(0, CrashReporter);
    PatchImports();
    HANDLE thread = CreateThread(nullptr, 0, CampaignThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
  }
  if (coop::IsHarness() && HasArgument(GetCommandLineW(), L"-coopsaveprobe")) {
    HANDLE thread = CreateThread(nullptr, 0, LocalSaveProbeThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
  }
  return TRUE;
}
