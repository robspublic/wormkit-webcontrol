#include "Protocol.h"

namespace Protocol {

std::optional<ControlAction> parse_action(const std::string& s) {
    if (s == kActionMoveLeft) return ControlAction::MoveLeft;
    if (s == kActionMoveRight) return ControlAction::MoveRight;
    if (s == kActionAimUp) return ControlAction::AimUp;
    if (s == kActionAimDown) return ControlAction::AimDown;
    if (s == kActionSelectWeapon) return ControlAction::SelectWeapon;
    if (s == kActionFire) return ControlAction::Fire;
    return std::nullopt;
}

ControlPhase parse_phase(const std::string& s) {
    if (s == kPhaseRelease) return ControlPhase::Release;
    return ControlPhase::Press;  // default: empty/unknown/"press"
}

} // namespace Protocol
