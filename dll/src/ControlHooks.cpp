#include "ControlHooks.h"

#include <mutex>

#include "ControlState.h"
#include "W2App.h"
#include "Hooks.h"
#include "Log.h"
#include "entities/CTask.h"
#include "entities/CTaskTeam.h"
#include "entities/CTaskTurnGame.h"
#include "entities/CTaskWorm.h"

namespace {

// Latest game snapshot, published by the game thread each frame and read by the
// IPC thread. Guarded so the two threads never tear a read/write.
std::mutex g_snapshotMutex;
Protocol::GameSnapshot g_snapshot;

void publishSnapshot(const Protocol::GameSnapshot& snap) {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = snap;
}

// Walk the task tree from the turn-game task and collect teams + worms into a
// GameSnapshot. Read-only: never mutates game memory. Returns an empty
// (round_active=false) snapshot when not in a game.
Protocol::GameSnapshot buildSnapshot() {
    Protocol::GameSnapshot snap;

    DWORD gg = W2App::getAddrGameGlobal();
    if (gg == 0) {
        return snap; // not in a game; round_active stays false
    }
    snap.round_active = true;
    snap.current_machine = (int)*(DWORD*)(gg + 0x726C);

    auto* tg = (CTaskTurnGame*)W2App::getAddrTurnGame();
    if (tg == nullptr) {
        return snap;
    }
    snap.num_teams = tg->number_of_teams_dword7C;
    snap.before_round_start = tg->its_before_round_start_dword140 != 0;
    snap.turn_time_ms = tg->turn_timer1_unknown188;

    // Traverse: for each Team task, gather its fields and its worm children.
    tg->traverse([&](CTask* obj) {
        if (obj->classtype != Constants::ClassType_Team) return;
        auto* team = (CTaskTeam*)obj;

        Protocol::TeamSnapshot ts;
        ts.team_number = team->team_number_dword38;
        ts.owner = team->owner_byte40;
        ts.current_worm = team->current_worm_number_dword48;
        ts.is_turn_holder = team->isTurnHolder();
        ts.is_local = team->isOwnedByMe();

        for (int i = 0; i < team->children.count; ++i) {
            CTask* child = team->children.data ? team->children.data[i] : nullptr;
            if (!child || child->classtype != Constants::ClassType_Worm) continue;
            auto* worm = (CTaskWorm*)child;

            Protocol::WormSnapshot ws;
            ws.team = worm->teamnumber_dwordFC;
            ws.worm = worm->wormnumber_dword100;
            ws.active = worm->active_dword104 != 0;
            ws.pos_x = worm->posX;
            ws.pos_y = worm->posY;
            ws.weapon = worm->selected_weapon_unknown170;
            ws.facing = worm->facing_direction_dword1A8;
            ts.worms.push_back(ws);
        }

        if (ts.is_turn_holder) snap.turn_team = ts.team_number;
        snap.teams.push_back(std::move(ts));
    });

    return snap;
}

// Apply a single command to the given (turn-holding) worm by writing its
// fields. All writes happen on the game thread from the frame hook.
// (Used by the control path, wired later.)
void applyCommand(CTaskWorm* worm, const ControlCommand& cmd) {
    switch (cmd.action) {
        case ControlAction::MoveRight:
            worm->facing_direction_dword1A8 = +1; // TODO(offsets): confirm sign
            break;
        case ControlAction::MoveLeft:
            worm->facing_direction_dword1A8 = -1;
            break;
        case ControlAction::AimUp:
            worm->shooting_angle_dword270 += cmd.value; // TODO: clamp
            break;
        case ControlAction::AimDown:
            worm->shooting_angle_dword270 -= cmd.value;
            break;
        case ControlAction::SelectWeapon:
            worm->selected_weapon_unknown170 = cmd.value;
            worm->selected_weapon_entry_ptr36C = 0;
            break;
        case ControlAction::Fire:
            // TODO(control): drive the fire input.
            break;
    }
}

// Called once per game frame (from the installed hook, wired later). For now it
// builds and publishes the read-only snapshot; the control-drain path is a
// sketch pending the write-side hook.
void onFrame() {
    publishSnapshot(buildSnapshot());

    // TODO(control): once the turn-holder worm is resolvable for writes, drain
    // ControlState and applyCommand() to it here (turn-gated by team).
    (void)&applyCommand;
}

} // namespace

void ControlHooks::install() {
    // TODO(control): hook the per-frame dispatch point to call onFrame() every
    // frame. Until then the snapshot is built on demand (see note below).
    (void)&onFrame;
    Log::info("ControlHooks::install (snapshot builder ready)");
}

Protocol::GameSnapshot ControlHooks::snapshot_game() {
    // Until the per-frame hook is wired, build the snapshot on demand from the
    // IPC thread. This is a read-only traversal of game memory; the game runs
    // single-threaded for logic, but a query can race a frame. That's
    // acceptable for a monitor (worst case: one field is a frame stale).
    //
    // When onFrame() is hooked, switch to returning the published g_snapshot
    // under the lock instead.
    return buildSnapshot();
}

Protocol::TurnSnapshot ControlHooks::snapshot_turn() {
    Protocol::GameSnapshot g = snapshot_game();
    Protocol::TurnSnapshot t;
    t.round_active = g.round_active;
    if (g.turn_team) {
        t.turn_team = std::to_string(*g.turn_team);
        // Report the turn-holding team's current/active worm position + weapon.
        for (const auto& team : g.teams) {
            if (team.team_number != *g.turn_team) continue;
            for (const auto& w : team.worms) {
                if (w.worm == team.current_worm || w.active) {
                    t.pos_x = w.pos_x;
                    t.pos_y = w.pos_y;
                    t.weapon = w.weapon;
                    break;
                }
            }
        }
    }
    return t;
}
