#ifndef WKWEBCONTROL_CONSTANTS_H
#define WKWEBCONTROL_CONSTANTS_H

typedef unsigned long DWORD;

namespace Constants {

    // Message types dispatched to CTask::HandleMessage each frame by the game.
    // Values match WA 3.8.1's internal task-message enum (from the wkRealTime
    // reference, same game build). Only the ones we use are named here.
    enum TaskMessage : DWORD {
        TaskMessage_None         = 0,
        TaskMessage_FrameStart   = 1,
        TaskMessage_FrameFinish  = 2,
        TaskMessage_RenderScene  = 3,
        TaskMessage_ProcessInput = 4,  // per-frame: drain input FIFO to worms

        // Input/control messages. The game's keyboard handler enqueues these
        // into the input FIFO (*(ddGame+0x40)); ProcessInput then applies them
        // to the active worm. We enqueue the same messages to drive the worm
        // from the web UI. Movement/aim are level-triggered: re-sent each frame
        // while the button is held. Fire is charge/release: FireWeapon starts
        // charging, ReleaseWeapon launches. Values from WA 3.8.1 (reference).
        TaskMessage_MoveLeft      = 30,
        TaskMessage_MoveRight     = 31,
        TaskMessage_MoveUp        = 32,  // aim up
        TaskMessage_MoveDown      = 33,  // aim down
        TaskMessage_Jump          = 36,  // jump (one-shot; JumpUp=37 = backflip)
        TaskMessage_FireWeapon    = 38,
        TaskMessage_ReleaseWeapon = 39,
        TaskMessage_SelectWeapon  = 51,

        // Turn lifecycle. StartTurn/TurnStarted payload's first DWORD is the
        // active team number; FinishTurn/TurnFinished mark end of a turn.
        TaskMessage_StartTurn    = 52,
        TaskMessage_FinishTurn   = 55,
        TaskMessage_TurnStarted  = 56,
        TaskMessage_TurnFinished = 57,
    };

    // Class-type tag stored at CTask+0x20, identifying the concrete task type.
    // Sequential from 0, matching WA 3.8.1 (wkRealTime Constants.h). Only the
    // types we care about are named; the numeric values must stay exact.
    enum ClassType : DWORD {
        ClassType_None     = 0,
        ClassType_TurnGame = 6,
        ClassType_Team     = 10,
        ClassType_Worm     = 17,
    };

    // Weapon ids (subset). Full table is large; we only need identifiers the
    // web UI exposes. TODO(offsets): confirm ids against WA 3.8.1 weapon table.
    enum Weapon : DWORD {
        Weapon_Bazooka  = 0, // TODO: confirm
    };

} // namespace Constants

#endif // WKWEBCONTROL_CONSTANTS_H
