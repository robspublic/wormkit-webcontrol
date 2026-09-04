#ifndef WKWEBCONTROL_CONTROLHOOKS_H
#define WKWEBCONTROL_CONTROLHOOKS_H

#include "Protocol.h"

// Bridges queued web commands into the running game.
//
// Installs a hook at the game's per-frame point (the turn-game / worm frame
// message). Each frame it:
//   1. finds the worm belonging to the current turn-holding team,
//   2. drains ControlState,
//   3. for each command, verifies the command's team == current turn-holder,
//   4. applies movement / aim / weapon by writing the worm's fields.
//
// Enforcing the turn-holder check here (not just in the web layer) means a
// misbehaving or spoofed client still can't move a worm out of turn.
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
