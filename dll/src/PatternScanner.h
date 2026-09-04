#ifndef WKWEBCONTROL_PATTERNSCANNER_H
#define WKWEBCONTROL_PATTERNSCANNER_H

#include <windows.h>

// Minimal, self-contained byte-signature scanner for the main module's code.
//
// The reference modules pull an external pattern-scanner submodule
// (nizikawa-worms/hacklib-patternscanner), which is not publicly reachable.
// To keep the build reproducible on public CI we vendor a tiny equivalent here.
//
// Signatures use the same convention as the reference:
//   pattern : raw bytes, wildcards may be any value (e.g. "\x8B\x00\x8B")
//   mask    : one char per pattern byte, 'x' = must match, '?' = wildcard
namespace PatternScanner {

    // Scan the executable image of the current process (WA.exe) for the given
    // signature. Returns the absolute address of the first match, or 0 if not
    // found. `patternLen` is the number of bytes in `pattern` (== strlen(mask)).
    DWORD findInMainModule(const unsigned char* pattern, const char* mask, size_t patternLen);

} // namespace PatternScanner

#endif // WKWEBCONTROL_PATTERNSCANNER_H
