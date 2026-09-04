#include <windows.h>

#include <ctime>
#include <stdexcept>
#include <string>

#include "version.h"
#include "Config.h"
#include "Log.h"
#include "Hooks.h"
#include "W2App.h"
#include "entities/CTaskTeam.h"
#include "entities/CTaskTurnGame.h"
#include "entities/CTaskWorm.h"
#include "ControlHooks.h"
#include "IpcServer.h"

// Wire up all subsystems. Order matters: game-state accessors and entity hooks
// first, then the control bridge, then the IPC server that feeds it.
static void install() {
    srand((unsigned)(time(nullptr) * GetCurrentProcessId()));

    W2App::install();
    CTaskTeam::install();
    CTaskTurnGame::install();
    CTaskWorm::install();

    ControlHooks::install();
    IpcServer::install();
}

// Single-instance guard (per-process), following StepS's pattern from the
// reference modules.
static bool lockCurrentInstance(const char* name) {
    if (!Config::isMutexEnabled()) return true;
    char mutexName[MAX_PATH];
    _snprintf_s(mutexName, _TRUNCATE, "P%u/%s", GetCurrentProcessId(), name);
    if (!CreateMutexA(nullptr, FALSE, mutexName)) return false;
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            try {
                Config::readConfig();
                if (Config::isModuleEnabled() &&
                    Config::waVersionCheck() &&
                    lockCurrentInstance(PROJECT_NAME)) {
                    Log::install(Config::isDevConsoleEnabled());
                    Log::info(std::string(PROJECT_NAME) + " " + PROJECT_VERSION +
                              " (target WA " WA_TARGET_VERSION ")");
                    Hooks::loadOffsets();
                    install();
                    Hooks::saveOffsets();
                }
            } catch (const std::exception& e) {
                MessageBoxA(nullptr, e.what(), Config::getFullStr().c_str(), MB_ICONERROR);
                Hooks::cleanup();
            }
            break;
        }
        case DLL_PROCESS_DETACH:
            IpcServer::stop();
            Hooks::cleanup();
            break;
        default:
            break;
    }
    return TRUE;
}
