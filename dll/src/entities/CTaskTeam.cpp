#include "CTaskTeam.h"

#include "../W2App.h"
#include "../Hooks.h"
#include "../Log.h"

void CTaskTeam::install() {
    // TODO(offsets): if we need to hook team message handling (e.g. to observe
    // turn changes), scan + hook it here. Not required for read-only turn/owner
    // checks, which just read fields.
    Log::info("CTaskTeam::install (stub)");
}

bool CTaskTeam::isOwnedByMe() {
    // Ownership: compare this team's owner id with the local machine id stored
    // in DdMain. For local couch co-op there is a single machine, so all teams
    // are "owned by me" — but we keep the check faithful to the reference for
    // correctness and future-proofing.
    DWORD ddmain = W2App::getAddrDdMain();
    if (ddmain == 0) return true; // offsets not wired yet; assume local
    // TODO(offsets): char mymachine = *(char*)(ddmain + 0xD9DC + 0x40);
    //                return owner_byte40 == mymachine;
    return true;
}

bool CTaskTeam::isTurnHolder() {
    // TODO(offsets): compare this team against CTaskTurnGame's current-team
    // fields (see CTaskTurnGame::currentTurnTeamNumber()).
    return false;
}

const char* CTaskTeam::getName() const {
    // TODO(offsets): locate the team-name string within the team struct.
    return "";
}
