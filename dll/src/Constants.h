#ifndef WKWEBCONTROL_CONSTANTS_H
#define WKWEBCONTROL_CONSTANTS_H

typedef unsigned long DWORD;

namespace Constants {

    // Message types dispatched to CTask::HandleMessage each frame by the game.
    //
    // NOTE: these numeric values must match WA 3.8.1's internal task-message
    // ids. The two we rely on (FrameFinish, RenderScene) and any others should
    // be confirmed against the game / reference RE notes before trusting them.
    // TODO(offsets): verify TaskMessage values against WA 3.8.1.
    enum TaskMessage : DWORD {
        TaskMessage_FrameFinish = 0,   // TODO: confirm id
        TaskMessage_RenderScene = 0,   // TODO: confirm id
        // Custom message ids for this module start high to avoid colliding with
        // the game's own message space. These are internal to wkWebControl and
        // are only ever dispatched by us.
        TaskMessage_WebControlApply = 0x7000, // apply queued web input to worm
    };

    // Class-type tag stored at CTask+0x20. Used to identify what kind of task a
    // pointer refers to when traversing the task tree.
    // TODO(offsets): verify ClassType values against WA 3.8.1.
    enum ClassType : DWORD {
        ClassType_Unknown  = 0,
        ClassType_Worm     = 0, // TODO: confirm
        ClassType_Team     = 0, // TODO: confirm
        ClassType_TurnGame = 0, // TODO: confirm
    };

    // Weapon ids (subset). Full table is large; we only need identifiers the
    // web UI exposes. TODO(offsets): confirm ids against WA 3.8.1 weapon table.
    enum Weapon : DWORD {
        Weapon_Bazooka  = 0, // TODO: confirm
    };

} // namespace Constants

#endif // WKWEBCONTROL_CONSTANTS_H
