#include "Config.h"

#include <windows.h>
#include <string>

#include "version.h"

namespace {
    // Path to wkWebControl.ini living next to WA.exe (current working dir of
    // the game process). Using a relative name resolves against the game dir.
    const char* kIniFile = ".\\wkWebControl.ini";
    const char* kSection = "wkWebControl";

    int iniInt(const char* key, int def) {
        return (int)GetPrivateProfileIntA(kSection, key, def, kIniFile);
    }

    std::string iniStr(const char* key, const std::string& def) {
        char buf[512] = {0};
        GetPrivateProfileStringA(kSection, key, def.c_str(), buf, sizeof(buf), kIniFile);
        return std::string(buf);
    }
}

void Config::readConfig() {
    enabled        = iniInt("Enabled", 1) != 0;
    devConsole     = iniInt("DevConsole", 0) != 0;
    checkWaVersion = iniInt("CheckWaVersion", 1) != 0;
    mutexEnabled   = iniInt("Mutex", 1) != 0;
    pipeName       = iniStr("PipeName", "\\\\.\\pipe\\wkwebcontrol");
}

bool Config::isModuleEnabled()     { return enabled; }
bool Config::isDevConsoleEnabled() { return devConsole; }
bool Config::isMutexEnabled()      { return mutexEnabled; }

bool Config::waVersionCheck() {
    if (!checkWaVersion) return true;
    // TODO(offsets): read the actual version string/field from WA.exe and
    // compare against WA_TARGET_VERSION. The reference modules read a known
    // version location in the binary. For now, accept and rely on pattern
    // scanning to fail loudly if signatures don't resolve.
    return true;
}

const std::string& Config::getPipeName() { return pipeName; }

std::string Config::getFullStr() {
    return std::string(PROJECT_NAME) + " " + PROJECT_VERSION;
}
