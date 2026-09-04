#include "PatternScanner.h"

#include <psapi.h>

namespace {

// Returns the [base, base+size) range of the main module's mapped image.
bool mainModuleRange(const unsigned char*& begin, const unsigned char*& end) {
    HMODULE mod = GetModuleHandleA(nullptr); // main EXE
    if (!mod) return false;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi))) return false;
    begin = reinterpret_cast<const unsigned char*>(mi.lpBaseOfDll);
    end = begin + mi.SizeOfImage;
    return true;
}

bool matchAt(const unsigned char* at, const unsigned char* pattern,
             const char* mask, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (mask[i] == 'x' && at[i] != pattern[i]) return false;
    }
    return true;
}

} // namespace

namespace PatternScanner {

DWORD findInMainModule(const unsigned char* pattern, const char* mask, size_t patternLen) {
    const unsigned char* begin = nullptr;
    const unsigned char* end = nullptr;
    if (!mainModuleRange(begin, end)) return 0;
    if (patternLen == 0 || (size_t)(end - begin) < patternLen) return 0;

    const unsigned char* last = end - patternLen;
    for (const unsigned char* p = begin; p <= last; ++p) {
        if (matchAt(p, pattern, mask, patternLen)) {
            return reinterpret_cast<DWORD>(p);
        }
    }
    return 0;
}

} // namespace PatternScanner
