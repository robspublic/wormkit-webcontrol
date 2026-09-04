#ifndef WKWEBCONTROL_CONTROLSTATE_H
#define WKWEBCONTROL_CONTROLSTATE_H

#include <mutex>
#include <string>

// The actions the web UI can drive. Movement and aim are HELD inputs (active
// while the button is down); fire is a charge/release (hold to build power,
// release to launch); select-weapon is a one-shot.
enum class ControlAction {
    MoveLeft,
    MoveRight,
    AimUp,
    AimDown,
    SelectWeapon,
    Fire,
    Jump,
};

// Whether a command is the start (press) or end (release) of a held input.
enum class ControlPhase {
    Press,
    Release,
};

// Shared held-input state between the IPC thread (producer, via press/release
// edges from the web UI) and the game thread (consumer, which re-asserts the
// held inputs into the game's input FIFO each frame).
//
// This is level-triggered state, not a queue of one-shots: WA movement/aim
// input must be re-sent every frame while held, so the game thread reads the
// current held set each frame rather than draining discrete events. Fire is
// tracked as an edge (press starts charging, release launches) so the game
// thread can emit exactly one FireWeapon and one ReleaseWeapon.
class ControlState {
public:
    // A copy of the held state taken by the game thread each frame.
    struct Snapshot {
        bool move_left = false;
        bool move_right = false;
        bool aim_up = false;
        bool aim_down = false;
        bool firing = false;      // fire button currently held (charging)
        int  select_weapon = -1;  // one-shot weapon id to select, or -1
        bool jump = false;        // one-shot: jump requested this read
    };

    // Apply a press/release edge from the IPC thread. `value` carries the
    // weapon id for SelectWeapon (ignored otherwise).
    static void apply(ControlAction action, ControlPhase phase, int value);

    // Read the current held state (game thread). Clears the one-shot
    // select-weapon request so it fires once.
    static Snapshot read();

    // Drop all held input (e.g. on turn change, round end, or client
    // disconnect) so a stuck key can't carry over.
    static void clear();

private:
    static inline std::mutex mutex;
    static inline Snapshot state;
};

#endif // WKWEBCONTROL_CONTROLSTATE_H
