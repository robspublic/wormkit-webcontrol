#include "ControlState.h"

void ControlState::push(const ControlCommand& cmd) {
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(cmd);
}

std::optional<ControlCommand> ControlState::pop() {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty()) return std::nullopt;
    ControlCommand cmd = queue.front();
    queue.pop();
    return cmd;
}

void ControlState::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    std::queue<ControlCommand> empty;
    std::swap(queue, empty);
}
