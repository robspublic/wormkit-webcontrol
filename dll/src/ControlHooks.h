#ifndef WKWEBCONTROL_CONTROLHOOKS_H
#define WKWEBCONTROL_CONTROLHOOKS_H

#include "Protocol.h"

// Bridges held web input into the running game.
//
// onFrame() runs on the game thread (turn-game FrameFinish). Each frame it
// builds+publishes the read-only snapshot and then, while a game is live,
// translates the current held-input state (ControlState) into real WA input
// messages enqueued in the game's input FIFO (see TaskMessageFifo):
//   - movement / aim: re-sent every frame while the button is held,
//   - fire: FireWeapon on the press edge, ReleaseWeapon on the release edge.
//
// The game's own ProcessInput routes those messages to the current turn-holder,
// so turn-gating (enforced by the backend claim flow) is respected by design.
class ControlHooks {
public:
    static void install();

    // Runs on the GAME THREAD once per frame (called from the turn-game's
    // FrameFinish hook). Builds the snapshot by traversing the live task tree
    // and publishes it under a mutex. Reading the tree here (not from the IPC
    // thread) avoids racing the game thread's in-place worm updates.
    static void onFrame();

    // Called when the game is torn down, so per-game state (turn tracking) is
    // cleared. Invoked from W2App::hookDestroyGameGlobal.
    static void onGameTornDown();
};

#endif // WKWEBCONTROL_CONTROLHOOKS_H
