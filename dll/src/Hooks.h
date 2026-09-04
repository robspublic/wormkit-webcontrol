#ifndef WKWEBCONTROL_HOOKS_H
#define WKWEBCONTROL_HOOKS_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include <polyhook2/Detour/x86Detour.hpp>

#include "version.h"

// Source-location tag attached to each hook for diagnostics.
#ifndef __CALLPOSITION__
#define STRINGIZE_DETAIL(x) #x
#define STRINGIZE(x) STRINGIZE_DETAIL(x)
#define __CALLPOSITION__ __FUNCTION__ ":" STRINGIZE(__LINE__)
#endif

// Central hooking + pattern-scanning facade, modeled on the reference modules'
// Hooks class. Wraps PolyHook 2.0 detours and a byte-signature scanner, and
// caches resolved offsets to disk so scanning is only paid once per game build.
class Hooks {
public:
    // Install a detour: redirect pTarget -> pDetour, storing a trampoline to
    // the original in *ppOriginal.
    static void hook(std::string name, DWORD pTarget, DWORD* pDetour,
                     DWORD* ppOriginal, const char* line = nullptr);

    // Scan WA.exe's code for `pattern` under `mask` ('x' = must match,
    // '?' = wildcard). Returns the resolved address, caching it under `name`.
    static DWORD scanPattern(const char* name, const char* pattern,
                             const char* mask, const char* line = nullptr);

    static void loadOffsets();  // load cached name->addr map from disk
    static void saveOffsets();  // persist any newly-scanned offsets
    static void cleanup();      // remove all detours (called on unload / error)

private:
    static inline const std::string cacheFile = PROJECT_NAME ".cache";
    static inline std::map<std::string, DWORD> scanNameToAddr;
    static inline std::vector<std::unique_ptr<PLH::x86Detour>> detours;
    static inline bool foundNewOffsets = false;
};

// Convenience macros mirroring the reference conventions.
//
//   _ScanPattern("Foo", "\\x8B...", "xx?...")  -> DWORD address
//   _HookDefault(Foo)  wires  addrFoo -> hookFoo -> origFoo  by naming.
#define _ScanPattern(name, pattern, mask) \
    Hooks::scanPattern(name, pattern, mask, __CALLPOSITION__)

#define _HookDefault(name)                                              \
    Hooks::hook(#name, addr##name, (DWORD*)&hook##name,                 \
                (DWORD*)&orig##name, __CALLPOSITION__)

#endif // WKWEBCONTROL_HOOKS_H
