#include "CTaskTurnGame.h"

#include <atomic>

#include "../Hooks.h"
#include "../Log.h"

// The active team number, updated from turn messages on the game thread and
// read from the IPC thread. atomic<int> so the cross-thread read is safe.
static std::atomic<int> g_currentTurnTeam{-1};

// Original CTaskTurnGame::HandleMessage, filled in by the detour installer.
int(__fastcall* origTurnHandleMessage)(CTaskTurnGame* This, int EDX, CTask* sender,
                                       Constants::TaskMessage mtype, size_t size,
                                       void* data) = nullptr;

int __fastcall CTaskTurnGame::hookTurnHandleMessage(CTaskTurnGame* This, int EDX,
                                                    CTask* sender,
                                                    Constants::TaskMessage mtype,
                                                    size_t size, void* data) {
    // Observe turn lifecycle to track the active team. The first DWORD of the
    // StartTurn/TurnStarted payload is the team number whose turn it is.
    switch (mtype) {
        case Constants::TaskMessage_StartTurn:
        case Constants::TaskMessage_TurnStarted:
            if (data) {
                int team = (int)*(DWORD*)data;
                g_currentTurnTeam.store(team);
                Log::info("turn msg " + std::to_string((int)mtype) +
                          " -> team " + std::to_string(team));
            }
            break;
        case Constants::TaskMessage_FinishTurn:
        case Constants::TaskMessage_TurnFinished:
            g_currentTurnTeam.store(-1);
            Log::info("turn msg " + std::to_string((int)mtype) + " -> clear");
            break;
        default:
            break;
    }
    return origTurnHandleMessage(This, EDX, sender, mtype, size, data);
}

void CTaskTurnGame::install() {
    DWORD addrTurnHandleMessage = _ScanPattern(
        "CTurnGameHandleMessage",
        "\x55\x8B\xEC\x83\xE4\xF8\x81\xEC\x00\x00\x00\x00\x53\x8B\x5D\x0C\x56\x8D\x43\xFE\x83\xF8\x7B\x57\x8B\xF1\x0F\x87\x00\x00\x00\x00\x0F\xB6\x80\x00\x00\x00\x00\xFF\x24\x85\x00\x00\x00\x00\x8B\x86\x00\x00\x00\x00",
        "??????xx????xxxxxxxxxxxxxxxx????xxx????xxx????xx????");
    _HookDefault(TurnHandleMessage);
    Log::info("CTaskTurnGame::install (turn tracking hooked)");
}

int CTaskTurnGame::currentTurnTeam() { return g_currentTurnTeam.load(); }
void CTaskTurnGame::clearTurn() { g_currentTurnTeam.store(-1); }
