#include "CTaskWorm.h"

#include "CTaskTeam.h"
#include "../Hooks.h"
#include "../Log.h"

// Original CTaskWorm::HandleMessage, filled in by the detour installer.
int(__fastcall* origWormHandleMessage)(CTaskWorm* This, int EDX, CTask* sender,
                                        Constants::TaskMessage mtype, size_t size,
                                        void* data) = nullptr;

// Hook on the worm's per-frame message handler. This is the injection point:
// on our custom TaskMessage_WebControlApply (dispatched from the frame hook in
// ControlHooks), we apply queued web input to this worm.
int __fastcall hookWormHandleMessage(CTaskWorm* This, int EDX, CTask* sender,
                                     Constants::TaskMessage mtype, size_t size,
                                     void* data) {
    // TODO: handle Constants::TaskMessage_WebControlApply here (or apply from
    // ControlHooks directly). Placeholder passes everything through.
    return origWormHandleMessage(This, EDX, sender, mtype, size, data);
}

void CTaskWorm::install() {
    // Read-only monitor reaches worms by traversing the task tree, so no hook
    // is needed yet. The WormHandleMessage detour below is wired later when we
    // implement control (the write path).
    //
    // TODO(control): scan CTaskWorm::HandleMessage and _HookDefault it:
    //   DWORD addrWormHandleMessage = _ScanPattern("CTaskWormHandleMessage",
    //       "\x55\x8B\xEC\x83\xE4\xF8...", "??????xx...");
    //   _HookDefault(WormHandleMessage);
    (void)&hookWormHandleMessage; // silence unused-until-control warning
    Log::info("CTaskWorm::install (read-only, no hook yet)");
}

bool CTaskWorm::isOwnedByMe() {
    CTaskTeam* team = (CTaskTeam*)this->parent;
    return team ? team->isOwnedByMe() : true;
}

bool CTaskWorm::isTurnHolder() {
    CTaskTeam* team = (CTaskTeam*)this->parent;
    return team ? team->isTurnHolder() : false;
}
