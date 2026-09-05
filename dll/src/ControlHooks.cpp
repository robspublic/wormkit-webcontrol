#include "ControlHooks.h"

#include <cstring>
#include <mutex>
#include <string>

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

// Fill snap.weapons with the turn team's per-weapon ammo/delay (defined below).
void readTurnTeamWeapons(Protocol::GameSnapshot& snap);

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

    // Attach the turn team's weapon availability (ammo/delay) for the palette.
    readTurnTeamWeapons(snap);

    return snap;
}

// Dispatch one input message to the turn-game task, the way the reference does
// it (turngame->HandleMessage(...) directly, with a zeroed payload buffer).
// This is the game's real input routing: the turn-game applies the input to
// its active worm. No-op (safe) if we're not in a game.
//
// The payload is a 1024-byte zeroed buffer. Its leading DWORD is `payload` when
// given (>= 0) -- used to carry the weapon id for SelectWeapon -- otherwise the
// active team number, mirroring how the reference builds StartTurn/team
// messages. For simple directional/fire input the game may ignore the payload,
// but sending a correctly sized zeroed buffer matches the reference and avoids
// the "size-0 gets dropped" failure mode.
void sendInput(Constants::TaskMessage mtype, int payload = -1) {
    auto* tg = (CTaskTurnGame*)W2App::getAddrTurnGame();
    if (tg == nullptr) return;

    unsigned char buff[1024];
    memset(buff, 0, sizeof(buff));
    if (payload >= 0) {
        *(DWORD*)buff = (DWORD)payload;
    } else {
        const int activeTeam = CTaskTurnGame::currentTurnTeam();
        if (activeTeam >= 0) *(DWORD*)buff = (DWORD)activeTeam;
    }

    tg->vtable8_HandleMessage(tg, mtype, sizeof(buff), buff);
}

// Find the active worm of the team currently taking its turn, by traversing
// the live task tree. Returns nullptr if not in a game / no turn / not found.
// Game-thread only (called from onFrame). Prefers the team's current-worm; if
// that can't be matched, falls back to any worm flagged active.
CTaskWorm* findActiveTurnWorm() {
    const int activeTeam = CTaskTurnGame::currentTurnTeam();
    if (activeTeam < 0) return nullptr;
    auto* tg = (CTaskTurnGame*)W2App::getAddrTurnGame();
    if (tg == nullptr) return nullptr;

    CTaskWorm* result = nullptr;
    tg->traverse([&](CTask* obj) {
        if (result) return;
        if (obj->classtype != Constants::ClassType_Team) return;
        auto* team = (CTaskTeam*)obj;
        if (team->team_number_dword38 != activeTeam) return;

        CTaskWorm* firstActive = nullptr;
        for (int i = 0; i < team->children.count; ++i) {
            CTask* child = team->children.data ? team->children.data[i] : nullptr;
            if (!child) continue;
            auto* worm = (CTaskWorm*)child;
            if (worm->wormnumber_dword100 == team->current_worm_number_dword48) {
                result = worm;  // exact current-worm match wins
                return;
            }
            if (!firstActive && worm->active_dword104 != 0) firstActive = worm;
        }
        if (!result) result = firstActive;
    });
    return result;
}

// Select a weapon on the turn-holder's active worm by writing its state
// directly (game thread). Setting the index and zeroing the cached table-entry
// pointer makes the game recompute the entry on its next frame -- this mirrors
// the reference worm hook (selected_weapon_entry_ptr36C = weaponTable +
// 464 * selected_weapon_unknown170 when the pointer is 0). Weapon selection is
// worm STATE, not a transient input message, so a direct write is the right
// model (and why routing SelectWeapon through the turn-game did nothing).
//
// We must set BOTH the index (0x170) and the cached weapon-table entry pointer
// (0x36C) ourselves: the base game does NOT recompute the pointer when it's
// zeroed (that recompute lives in the reference module's own frame hook, which
// we don't run). Zeroing 0x36C and leaving it caused the game to dereference a
// null entry -> crash (read of [null+0x30]). So compute the entry pointer here,
// exactly like the reference: entry = *(gameGlobal+0x510) + 464 * index.
void applyWeapon(int weaponId) {
    // Guard the index to the known weapon-table range so we never point past
    // the table (an out-of-range entry would fault when the game reads it).
    if (weaponId < 1 || weaponId > 70) return;

    DWORD gg = W2App::getAddrGameGlobal();
    if (gg == 0) return;
    DWORD weaponTable = *(DWORD*)(gg + 0x510);
    if (weaponTable == 0) return;

    CTaskWorm* worm = findActiveTurnWorm();
    if (worm == nullptr) return;

    // Write the fields so the weapon DATA is always correct and crash-safe,
    // even if the input dispatch below no-ops. (This alone selects the weapon,
    // but the game doesn't refresh the held sprite/panel until the worm next
    // re-evaluates its state -- e.g. after walking.)
    worm->selected_weapon_unknown170 = weaponId;
    worm->selected_weapon_entry_ptr36C = (int)(weaponTable + 464u * (DWORD)weaponId);

    // Then route a SelectWeapon input through the SAME turn-game path that
    // movement/aim/fire use (proven to refresh visuals), so the change is
    // applied through the game's normal input handling rather than only poked
    // into memory. sendInput builds the reference's 1024-byte zeroed buffer;
    // carry the weapon id as the leading DWORD.
    sendInput(Constants::TaskMessage_SelectWeapon, weaponId);
}

// Was the fire button held on the previous frame? Used to emit FireWeapon /
// ReleaseWeapon exactly once on the press / release edge. Game-thread only.
bool g_wasFiring = false;

// ---- Per-team weapon availability (WA ammo table) --------------------------
// The shared weapon table (*(gameGlobal+0x510)) holds only static weapon defs,
// NOT per-game ammo. The real per-team ammo lives in a separate flat short[]
// "ammo table" (per reference/wkTerrainSync Missions.cpp), confirmed in-game:
//   ammo  = *(short*)(addrAmmoTable + 2*(weapon + 143*team))
//   delay = *(short*)(addrAmmoTable + 2*(weapon + 143*team) + 142)
// ammo -1 = infinite, 0 = unavailable this round, >0 = finite count.
// delay = rounds until it becomes available (0 = now / n/a).

// Resolved ammo-table base (scanned once). 0 = not yet / scan failed.
DWORD g_ammoTable = 0;
bool  g_ammoScanTried = false;

// Resolve addrAmmoTable via the ReadAmmoFromWAM scan (same as wkTerrainSync).
// Wrapped so a failed scan just disables weapon availability rather than
// aborting (availability is a nice-to-have; core control must not depend on it).
DWORD resolveAmmoTable() {
    if (g_ammoScanTried) return g_ammoTable;
    g_ammoScanTried = true;
    try {
        DWORD addrReadAmmoFromWAM = _ScanPattern(
            "ReadAmmoFromWAM",
            "\x8B\x40\x0C\x56\x8B\x74\x24\x10\x57\x8B\x7C\x24\x0C\x50\x6A\x00\x51\x57\xFF\x15\x00\x00\x00\x00\x69\xF6\x00\x00\x00\x00\x03\x74\x24\x10\x0F\xB7\x14\x75\x00\x00\x00\x00\x66\x83\xFA\xFF",
            "??????xxxxxxxxxxxxxx????xx????xxxxxxxx????xxxx");
        g_ammoTable = *(DWORD*)(addrReadAmmoFromWAM + 0x26);
    } catch (...) {
        Log::warn("ammo-table scan (ReadAmmoFromWAM) failed; weapons unavailable");
        g_ammoTable = 0;
    }
    return g_ammoTable;
}

// Fill snap.weapons with ammo+delay for every weapon id (1..70) of the current
// turn team. No-op if the ammo table isn't resolvable or there's no turn team.
void readTurnTeamWeapons(Protocol::GameSnapshot& snap) {
    if (!snap.turn_team) return;
    DWORD ammoTable = resolveAmmoTable();
    if (ammoTable == 0) return;

    const int team = *snap.turn_team;
    for (int w = 1; w <= 70; ++w) {
        Protocol::WeaponAmmo wa;
        wa.id = w;
        wa.ammo  = (int)*(short*)(ammoTable + 2 * (w + 143 * team));
        wa.delay = (int)*(short*)(ammoTable + 2 * (w + 143 * team) + 142);
        snap.weapons.push_back(wa);
    }
}

// Translate the current held-input state into per-frame WA input messages.
// Movement/aim are level-triggered (re-sent every frame while held); fire is
// edge-triggered (FireWeapon on press, ReleaseWeapon on release); jump and
// weapon-select are one-shots. Runs on the game thread from onFrame().
void applyHeldInput() {
    ControlState::Snapshot in = ControlState::read();

    if (in.jump) sendInput(Constants::TaskMessage_Jump);

    if (in.select_weapon >= 0) {
        // Weapon selection is worm state: write it directly to the active worm
        // (routing a SelectWeapon message through the turn-game had no effect).
        applyWeapon(in.select_weapon);
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
