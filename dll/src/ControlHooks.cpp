#include "ControlHooks.h"

#include <mutex>

#include "ControlState.h"
#include "W2App.h"
#include "Hooks.h"
#include "Log.h"
#include "entities/CTaskWorm.h"
#include "entities/CTaskTurnGame.h"

namespace {

// Latest turn/worm snapshot, published by the game thread each frame and read
// by the IPC thread answering "turn" queries. Guarded so the two threads never
// tear a read/write.
std::mutex g_snapshotMutex;
Protocol::TurnSnapshot g_snapshot;

void publishSnapshot(const Protocol::TurnSnapshot& snap) {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = snap;
}

// Apply a single command to the given (turn-holding) worm by writing its
// fields. All writes happen on the game thread from the frame hook.
void applyCommand(CTaskWorm* worm, const ControlCommand& cmd) {
    switch (cmd.action) {
        case ControlAction::MoveRight:
            worm->facing_direction_dword1A8 = +1; // TODO(offsets): confirm sign
            // TODO: to actually walk, drive the game's movement input for this
            // frame rather than only setting facing.
            break;
        case ControlAction::MoveLeft:
            worm->facing_direction_dword1A8 = -1; // TODO(offsets): confirm sign
            break;
        case ControlAction::AimUp:
            worm->shooting_angle_dword270 += cmd.value; // TODO: clamp to range
            break;
        case ControlAction::AimDown:
            worm->shooting_angle_dword270 -= cmd.value;
            break;
        case ControlAction::SelectWeapon:
            worm->selected_weapon_unknown170 = cmd.value;
            worm->selected_weapon_entry_ptr36C = 0; // force re-resolve next frame
            // TODO: prefer invoking the game's weapon-select routine so side
            // effects (ammo checks, panel state) stay consistent.
            break;
        case ControlAction::Fire:
            // TODO: drive the fire input; direct field pokes are insufficient.
            break;
    }
}

// Called once per game frame (from the installed hook). Publishes the current
// turn snapshot, then drains queued commands and applies them to the current
// turn-holder's active worm.
void onFrame() {
    Protocol::TurnSnapshot snap;

    // TODO(offsets): resolve the current turn-holding worm from the turn game /
    // task tree. Sketch:
    //   CTaskTurnGame* tg = (CTaskTurnGame*)W2App::getAddrTurnGame();
    //   if (!tg) { publishSnapshot(snap); return; }
    //   int team = tg->currentTurnTeamNumber();
    //   if (team < 0) { ControlState::clear(); publishSnapshot(snap); return; }
    //   CTaskWorm* worm = <active worm of that team>;
    //   if (worm) {
    //       snap.round_active = true;
    //       snap.turn_team = <team name>;
    //       snap.pos_x = worm->posX;
    //       snap.pos_y = worm->posY;
    //       snap.weapon = worm->selected_weapon_unknown170;
    //
    //       while (auto cmd = ControlState::pop()) {
    //           if (cmd->team != *snap.turn_team) continue; // turn gate
    //           applyCommand(worm, *cmd);
    //       }
    //   }
    (void)&applyCommand; // silence unused-until-wired warning

    publishSnapshot(snap);
}

} // namespace

void ControlHooks::install() {
    // TODO(offsets): scan for and hook the per-frame dispatch point, calling
    // onFrame() each frame. In the reference this rides the worm/turn-game
    // FrameFinish message.
    (void)&onFrame;
    Log::info("ControlHooks::install (stub)");
}

Protocol::TurnSnapshot ControlHooks::snapshot_turn() {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    return g_snapshot;
}
