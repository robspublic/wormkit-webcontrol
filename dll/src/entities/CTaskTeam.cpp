#include "CTaskTeam.h"

#include "../W2App.h"
#include "../Log.h"

void CTaskTeam::install() {
    // Read-only monitor needs no hook here; teams are reached by traversing the
    // task tree from the turn-game task and matching classtype == Team.
    Log::info("CTaskTeam::install (read-only, no hook)");
}

bool CTaskTeam::isTurnHolder() {
    // The turn-holding machine id lives at GameGlobal+0x726C. A team is the
    // turn holder if its owner matches that machine.
    DWORD gg = W2App::getAddrGameGlobal();
    if (gg == 0) return false;
    DWORD currentMachine = *(DWORD*)(gg + 0x726C);
    return (DWORD)owner_byte40 == currentMachine;
}

bool CTaskTeam::isOwnedByMe() {
    DWORD ddmain = W2App::getAddrDdMain();
    if (ddmain == 0) return true; // DdMain not wired (single-machine local play)
    char myMachine = *(char*)(ddmain + 0xD9DC + 0x40);
    return owner_byte40 == myMachine;
}
