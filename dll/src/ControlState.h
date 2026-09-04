#ifndef WKWEBCONTROL_CONTROLSTATE_H
#define WKWEBCONTROL_CONTROLSTATE_H

#include <mutex>
#include <optional>
#include <queue>
#include <string>

// Thread-safe hand-off between the IPC (TCP server) thread and the game thread.
//
//   Producer: IpcServer thread   -> push()  web commands
//   Consumer: game frame hook     -> drain() and apply to the active worm
//
// The pipe thread NEVER touches game memory directly; it only enqueues here.
// The game thread NEVER blocks on I/O; it only drains here. This keeps game
// state mutations on the game's own tick and avoids cross-thread races.

enum class ControlAction {
    MoveLeft,
    MoveRight,
    AimUp,
    AimDown,
    SelectWeapon,
    Fire,
};

struct ControlCommand {
    std::string team;     // team name the command is for (turn-gated in DLL)
    ControlAction action;
    int value = 0;        // e.g. weapon id for SelectWeapon, or hold-duration
};

class ControlState {
public:
    // Called from the pipe thread.
    static void push(const ControlCommand& cmd);

    // Called from the game thread. Returns the next queued command, if any.
    static std::optional<ControlCommand> pop();

    // Drop everything (e.g. on turn change or round end).
    static void clear();

private:
    static inline std::mutex mutex;
    static inline std::queue<ControlCommand> queue;
};

#endif // WKWEBCONTROL_CONTROLSTATE_H
