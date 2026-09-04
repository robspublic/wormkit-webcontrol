#include "W2App.h"

#include "Hooks.h"
#include "Log.h"

void W2App::install() {
    // TODO(offsets): hook the game's app/GameGlobal construction to capture
    // addrGameGlobal and addrDdMain, mirroring the reference W2App::install().
    // Until wired, the getters below return 0 and dependent code must guard.
    Log::info("W2App::install (offsets not yet wired)");
}

DWORD W2App::getAddrGameGlobal() { return addrGameGlobal; }
DWORD W2App::getAddrDdMain()     { return addrDdMain; }

DWORD W2App::getAddrTurnGame() {
    // TODO(offsets): resolve the CTaskTurnGame pointer from GameGlobal.
    // In the reference this walks a known offset off GameGlobal.
    return 0;
}
