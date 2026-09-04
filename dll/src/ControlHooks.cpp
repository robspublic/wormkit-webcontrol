#include "ControlHooks.h"

#include <mutex>

#include "ControlState.h"
#include "W2App.h"
#include "Hooks.h"
#include "IpcServer.h"
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
    snap.game_id = W2App::getGameId();

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

    // The active team is tracked from turn messages (machine-id can't tell
    // teams apart in single-machine local play). -1 means no turn in progress.
    const int activeTeam = CTaskTurnGame::currentTurnTeam();
    if (activeTeam >= 0) snap.turn_team = activeTeam;

    // Traverse: for each Team task, gather its fields and its worm children.
    tg->traverse([&](CTask* obj) {
        if (obj->classtype != Constants::ClassType_Team) return;
        auto* team = (CTaskTeam*)obj;

        Protocol::TeamSnapshot ts;
        ts.team_number = team->team_number_dword38;
        ts.owner = team->owner_byte40;
        ts.current_worm = team->current_worm_number_dword48;
        ts.is_turn_holder = (activeTeam >= 0 && team->team_number_dword38 == activeTeam);
        ts.is_local = team->isOwnedByMe();

        // Diagnostic (temporary): how many direct children the team task has,
        // and the classtype of the first, to confirm worm enumeration. Rate-
        // limited since buildSnapshot runs every frame.
        static int diagCounter = 0;
        if ((diagCounter++ % 200) == 0) {
            Log::info("team " + std::to_string(team->team_number_dword38) +
                      " children=" + std::to_string(team->children.count) +
                      " firstType=" + std::to_string(
                          team->children.count > 0 && team->children.data
                              ? (int)team->children.data[0]->classtype : -1));
        }

        // A team's direct children are its worms. The reference casts each
        // child straight to CTaskWorm without a classtype filter, so we do the
        // same (a stale/wrong Worm classtype value would otherwise drop them
        // all -> "no worms").
        for (int i = 0; i < team->children.count; ++i) {
            CTask* child = team->children.data ? team->children.data[i] : nullptr;
            if (!child) continue;
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

} // namespace

// Runs on the game thread (turn-game FrameFinish). Traverse the live tree,
// publish a copied snapshot, and push it to the connected backend (best-effort,
// non-blocking).
void ControlHooks::onFrame() {
    Protocol::GameSnapshot snap = buildSnapshot();
    publishSnapshot(snap);
    IpcServer::pushState(snap);

    // TODO(control): once the turn-holder worm is resolvable for writes, drain
    // ControlState and applyCommand() to it here (turn-gated by team).
    (void)&applyCommand;
}

void ControlHooks::install() {
    // onFrame() is invoked from CTaskTurnGame's FrameFinish hook (game thread).
    Log::info("ControlHooks::install (snapshot builder ready)");
}

void ControlHooks::onGameTornDown() {
    CTaskTurnGame::clearTurn();
    // Clear the published snapshot so a between-games query reports "no game"
    // rather than the last game's stale data.
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = Protocol::GameSnapshot{};
}

Protocol::GameSnapshot ControlHooks::snapshot_game() {
    // Return the copy published on the game thread by onFrame(). Reading the
    // live task tree from this (IPC) thread would race the game thread's
    // in-place worm updates and yield frozen/torn values.
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    return g_snapshot;
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
