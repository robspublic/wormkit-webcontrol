#ifndef WKWEBCONTROL_IPCSERVER_H
#define WKWEBCONTROL_IPCSERVER_H

// Named-pipe server that bridges the FastAPI backend to the game.
//
// Runs on its own thread. Listens on the configured pipe (default
// \\.\pipe\wkwebcontrol), reads newline-delimited JSON messages from the
// backend, parses them into ControlCommands, and enqueues them via
// ControlState. Also answers state queries (current turn team, worm position)
// so the web UI can enable/disable controls.
//
// This is the one genuinely new subsystem versus the reference modules, which
// use WA's own netcode rather than an external local IPC channel.
class IpcServer {
public:
    static void install();  // spawn the pipe server thread
    static void stop();     // signal the thread to exit (on unload)
};

#endif // WKWEBCONTROL_IPCSERVER_H
