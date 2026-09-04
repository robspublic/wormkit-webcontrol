#include "ControlHooks.h"

#include <mutex>

#include "ControlState.h"
#include "W2App.h"
#include "Hooks.h"
#include "IpcServer.h"
#include "Log.h"
#include "TaskMessageFifo.h"
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

// Enqueue one input message into the game's input FIFO (*(ddGame+0x40)). The
// turn-game's ProcessInput then applies it to the active worm on this frame.
// A size-0 payload matches how the game queues discrete directional/fire input
// events. No-op (safe) if we're not in a game.
void sendInput(Constants::TaskMessage mtype) {
    DWORD ddGame = W2App::getAddrDdGame();
    if (ddGame == 0) return;
    auto* inputFifo = *(TaskMessageFifo**)(ddGame + 0x40);
    if (inputFifo == nullptr) return;
    TaskMessageFifo::callTaskMessageSend(inputFifo, 0, mtype, nullptr);
}

// Was the fire button held on the previous frame? Used to emit FireWeapon /
// ReleaseWeapon exactly once on the press / release edge. Game-thread only.
bool g_wasFiring = false;

// Translate the current held-input state into per-frame WA input messages.
// Movement/aim are level-triggered (re-sent every frame while held); fire is
// edge-triggered (FireWeapon on press, ReleaseWeapon on release). Runs on the
// game thread from onFrame().
void applyHeldInput() {
    ControlState::Snapshot in = ControlState::read();

    if (in.select_weapon >= 0) {
        // TODO(offsets): SelectWeapon likely needs the weapon id as payload;
        // size-0 select may be a no-op. Left minimal until confirmed in-game.
        sendInput(Constants::TaskMessage_SelectWeapon);
    }

    // Movement + aim: assert each held direction this frame.
    if (in.move_left)  sendInput(Constants::TaskMessage_MoveLeft);
    if (in.move_right) sendInput(Constants::TaskMessage_MoveRight);
    if (in.aim_up)     sendInput(Constants::TaskMessage_MoveUp);
    if (in.aim_down)   sendInput(Constants::TaskMessage_MoveDown);

    // Fire: charge on the press edge, launch on the release edge.
    if (in.firing && !g_wasFiring) {
        sendInput(Constants::TaskMessage_FireWeapon);
    } else if (!in.firing && g_wasFiring) {
        sendInput(Constants::TaskMessage_ReleaseWeapon);
    }
    g_wasFiring = in.firing;
}

} // namespace

// Runs on the game thread (turn-game FrameFinish). Traverse the live tree,
// publish a copied snapshot, push it to the connected backend, then apply any
// held web input for this frame.
void ControlHooks::onFrame() {
    Protocol::GameSnapshot snap = buildSnapshot();
    publishSnapshot(snap);
    IpcServer::pushState(snap);

    // Only drive input while a game is live. Turn-gating is enforced by the
    // backend (claim + is-users-turn) before commands ever reach us, and the
    // game's ProcessInput naturally routes input to the current turn-holder.
    if (snap.round_active) {
        applyHeldInput();
    }
}

void ControlHooks::install() {
    // onFrame() is invoked from CTaskTurnGame's FrameFinish hook (game thread).
    Log::info("ControlHooks::install (snapshot builder + input ready)");
}

void ControlHooks::onGameTornDown() {
    CTaskTurnGame::clearTurn();
    ControlState::clear();  // drop any held input from the finished game
    g_wasFiring = false;
    // Clear the published snapshot so a between-games query reports "no game"
    // rather than the last game's stale data.
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = Protocol::GameSnapshot{};
}
