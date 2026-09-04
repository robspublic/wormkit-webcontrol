#ifndef WKWEBCONTROL_IPCSERVER_H
#define WKWEBCONTROL_IPCSERVER_H

#include "Protocol.h"

// TCP server that bridges the game to the FastAPI backend.
//
// Push model: on each game logic frame the DLL sends the latest game snapshot
// to the connected backend (newline-JSON), and it accepts fire-and-forget
// control command lines from the backend on the same socket. The backend never
// polls; it reads the stream and downsamples for its own clients.
//
// Runs its accept/read loop on its own thread. Listens on 127.0.0.1:<Port>
// (default 27099). TCP loopback rather than a named pipe: WA runs under
// Wine/Proton, whose pipe namespace isn't reachable by the native-Linux
// backend, but Wine maps Winsock TCP onto the host network stack.
class IpcServer {
public:
    static void install();  // spawn the TCP server thread
    static void stop();     // signal the thread to exit (on unload)

    // Called on the GAME THREAD once per frame (from ControlHooks::onFrame).
    // Best-effort, non-blocking send of the snapshot to the connected backend;
    // drops the frame rather than ever stalling the game.
    static void pushState(const Protocol::GameSnapshot& snapshot);
};

#endif // WKWEBCONTROL_IPCSERVER_H
