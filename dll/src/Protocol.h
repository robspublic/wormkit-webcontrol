#ifndef WKWEBCONTROL_PROTOCOL_H
#define WKWEBCONTROL_PROTOCOL_H

#include <optional>
#include <string>

#include "ControlState.h"

// Wire contract shared with the FastAPI backend (backend/app/protocol.py).
// Keep these constants in sync with the Python side:
//   - action strings  <-> ControlAction StrEnum values
//   - field names     <-> CommandMessage / QueryMessage / TurnState fields
//
// Transport: newline-delimited JSON over the named pipe.
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

    // Current game state reported back to the backend for a "turn" query.
    // Field names mirror the Python TurnState model.
    struct TurnSnapshot {
        std::optional<std::string> turn_team;
        std::optional<int> pos_x;
        std::optional<int> pos_y;
        std::optional<int> weapon;
        bool round_active = false;
    };

} // namespace Protocol

#endif // WKWEBCONTROL_PROTOCOL_H
