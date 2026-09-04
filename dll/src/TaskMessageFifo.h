#ifndef WKWEBCONTROL_TASKMESSAGEFIFO_H
#define WKWEBCONTROL_TASKMESSAGEFIFO_H

#include "Constants.h"

// A WA task-message queue. The game reads queued TaskMessages from the input
// FIFO (at *(ddGame+0x40)) and, once per logic frame, hands them to the
// turn-game via a ProcessInput message which routes them to the active worm.
//
// This is the real input channel: to drive a worm we enqueue an input message
// (MoveLeft/MoveRight/MoveUp/MoveDown/FireWeapon/ReleaseWeapon) into that FIFO
// with callTaskMessageSend(), rather than writing worm fields directly. The
// layout and the resolved game functions come from the wkRealTime reference
// (same WA 3.8.1 build).

// One entry in a task-message FIFO. `datac` is a flexible payload.
struct TaskMessageEntry {
    DWORD totalsize_dword0;              // payload size + 4
    TaskMessageEntry* nextentry_dword4;  // next entry, or null
    Constants::TaskMessage type_dword8;  // message id
    unsigned char datac[];               // payload bytes
};

class TaskMessageFifo {
public:
    DWORD dword0;                 // backing storage
    DWORD capacity_dword4;        // max size
    DWORD end_dword8;             // current write position
    DWORD start_dwordC;           // current read position
    DWORD dword10;                // data end pointer
    DWORD data_start_dword14;     // data start pointer
    DWORD num_elements_dword18;   // number of queued elements

    // Enqueue a message of `msize` payload bytes (may be 0) of `data` into
    // `fifo`. Wraps the game's own TaskMessageSend routine (resolved by scan),
    // which grows the FIFO's backing storage as needed.
    static DWORD callTaskMessageSend(TaskMessageFifo* fifo, DWORD msize,
                                     Constants::TaskMessage mtype, void* data);

    // Resolve the game function this class wraps. Must run once at startup.
    static void install();
};

#endif // WKWEBCONTROL_TASKMESSAGEFIFO_H
