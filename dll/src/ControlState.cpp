#include "ControlState.h"

void ControlState::apply(ControlAction action, ControlPhase phase, int value) {
    const bool held = (phase == ControlPhase::Press);
    std::lock_guard<std::mutex> lock(mutex);
    switch (action) {
        case ControlAction::MoveLeft:   state.move_left = held; break;
        case ControlAction::MoveRight:  state.move_right = held; break;
        case ControlAction::AimUp:      state.aim_up = held; break;
        case ControlAction::AimDown:    state.aim_down = held; break;
        case ControlAction::Fire:       state.firing = held; break;
        case ControlAction::SelectWeapon:
            // One-shot: only act on the press edge.
            if (held) state.select_weapon = value;
            break;
        case ControlAction::Jump:
            // One-shot: only act on the press edge.
            if (held) state.jump = true;
            break;
    }
}

ControlState::Snapshot ControlState::read() {
    std::lock_guard<std::mutex> lock(mutex);
    Snapshot copy = state;
    state.select_weapon = -1;  // consume the one-shots
    state.jump = false;
    return copy;
}

void ControlState::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    state = Snapshot{};
}
