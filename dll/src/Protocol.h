#ifndef WKWEBCONTROL_PROTOCOL_H
#define WKWEBCONTROL_PROTOCOL_H

#include <optional>
#include <string>
#include <vector>

#include "ControlState.h"

// Wire contract shared with the FastAPI backend (backend/app/protocol.py).
// Keep these constants in sync with the Python side:
//   - action strings  <-> ControlAction StrEnum values
//   - field names     <-> CommandMessage / QueryMessage / TurnState fields
//
// Transport: newline-delimited JSON over TCP loopback (127.0.0.1:27099).
//
//   Backend -> DLL:
//     {"type":"cmd","team":"Red","action":"move_left","value":0}
//     {"type":"query","what":"turn"}
//
//   DLL -> Backend (query response):
//     {"turn_team":"Red","pos_x":1234,"pos_y":567,"weapon":3,"round_active":true}
namespace Protocol {

    // Message type discriminator ("type" field).
    inline constexpr const char* kTypeCommand = "cmd";
    inline constexpr const char* kTypeQuery = "query";

    // Action string values (must equal Python ControlAction values).
    inline constexpr const char* kActionMoveLeft = "move_left";
    inline constexpr const char* kActionMoveRight = "move_right";
    inline constexpr const char* kActionAimUp = "aim_up";
    inline constexpr const char* kActionAimDown = "aim_down";
    inline constexpr const char* kActionSelectWeapon = "select_weapon";
    inline constexpr const char* kActionFire = "fire";

    // Map an action string to the internal enum. Returns nullopt if unknown.
    std::optional<ControlAction> parse_action(const std::string& s);

    // ----- Read-only monitor snapshot --------------------------------------
    // A point-in-time view of game state, built on the game thread and read by
    // the IPC thread. Field names mirror the Python models in protocol.py.
    //
    // Only high-confidence fields (proven readable in the reference module) are
    // included. Team name and health are intentionally absent — they have no
    // confirmed offset in WA 3.8.1 and would be guesses.

    struct WormSnapshot {
        int team = 0;      // owning team number
        int worm = 0;      // worm number within the team
        bool active = false;
        int pos_x = 0;
        int pos_y = 0;
        int weapon = 0;    // selected weapon index
        int facing = 0;    // facing direction (+/-1)
    };

    struct TeamSnapshot {
        int team_number = 0;
        int owner = 0;           // owning machine id
        int current_worm = 0;    // current worm number
        bool is_turn_holder = false;
        bool is_local = false;   // owned by the local machine
        std::vector<WormSnapshot> worms;
    };

    struct GameSnapshot {
        int game_id = 0;                 // increments each new game (0 = none yet)
        bool round_active = false;       // a game/match is loaded (GameGlobal != 0)
        bool before_round_start = false; // worms still being placed
        int num_teams = 0;
        int current_machine = -1;        // machine id currently holding the turn
        std::optional<int> turn_team;    // team_number of the turn holder, if known
        std::optional<int> turn_time_ms; // turn timer (ms), if in a game
        std::vector<TeamSnapshot> teams;
    };

    // Back-compat: the original "turn" query still answers with these fields,
    // derived from the snapshot (turn team + its active worm's position/weapon).
    struct TurnSnapshot {
        std::optional<std::string> turn_team;
        std::optional<int> pos_x;
        std::optional<int> pos_y;
        std::optional<int> weapon;
        bool round_active = false;
    };

} // namespace Protocol

#endif // WKWEBCONTROL_PROTOCOL_H
