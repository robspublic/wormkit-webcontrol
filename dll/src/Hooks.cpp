#include "Hooks.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <cstring>

#include "Log.h"
#include "PatternScanner.h"

void Hooks::hook(std::string name, DWORD pTarget, DWORD* pDetour,
                 DWORD* ppOriginal, const char* line) {
    if (pTarget == 0) {
        throw std::runtime_error("Hook '" + name + "' has null target address (pattern scan failed?) at " +
                                 (line ? line : "?"));
    }
    auto detour = std::make_unique<PLH::x86Detour>(
        (uint64_t)pTarget, (uint64_t)pDetour, (uint64_t*)ppOriginal);
    if (!detour->hook()) {
        throw std::runtime_error("Failed to install hook '" + name + "' at " + (line ? line : "?"));
    }
    detours.push_back(std::move(detour));
    Log::info("hooked " + name);
}

DWORD Hooks::scanPattern(const char* name, const char* pattern,
                         const char* mask, const char* line) {
    // Return cached address if we already resolved (or loaded) this name.
    auto it = scanNameToAddr.find(name);
    if (it != scanNameToAddr.end() && it->second != 0) {
        return it->second;
    }

    // Byte-signature scan over WA.exe's mapped image. The mask length defines
    // the pattern length ('x' = must match, '?' = wildcard).
    const size_t len = std::strlen(mask);
    DWORD addr = PatternScanner::findInMainModule(
        reinterpret_cast<const unsigned char*>(pattern), mask, len);
    if (addr == 0) {
        throw std::runtime_error(std::string("Pattern scan failed for '") + name +
                                 "' at " + (line ? line : "?") +
                                 " (signature not found in WA.exe)");
    }
    scanNameToAddr[name] = addr;
    foundNewOffsets = true;
    Log::info(std::string("scanned ") + name);
    return addr;
}

void Hooks::loadOffsets() {
    std::ifstream in(cacheFile);
    if (!in) return;
    std::string name;
    DWORD addr;
    while (in >> name >> std::hex >> addr) {
        scanNameToAddr[name] = addr;
    }
    Log::info("loaded " + std::to_string(scanNameToAddr.size()) + " cached offsets");
}

void Hooks::saveOffsets() {
    if (!foundNewOffsets) return;
    std::ofstream out(cacheFile, std::ios::trunc);
    for (auto& [name, addr] : scanNameToAddr) {
        out << name << " " << std::hex << addr << "\n";
    }
}

void Hooks::cleanup() {
    for (auto& d : detours) {
        if (d) d->unHook();
    }
    detours.clear();
}
