#include "CTaskWorm.h"

#include "CTaskTeam.h"
#include "../Log.h"

void CTaskWorm::install() {
    // Worms are reached read-only by traversing the task tree; control input is
    // dispatched to the turn-game task (see ControlHooks), so no per-worm hook
    // is needed.
    Log::info("CTaskWorm::install (read-only)");
}

bool CTaskWorm::isOwnedByMe() {
    CTaskTeam* team = (CTaskTeam*)this->parent;
    return team ? team->isOwnedByMe() : true;
}

bool CTaskWorm::isTurnHolder() {
    CTaskTeam* team = (CTaskTeam*)this->parent;
    return team ? team->isTurnHolder() : false;
}
