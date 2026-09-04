#ifndef WKWEBCONTROL_IPCSERVER_H
#define WKWEBCONTROL_IPCSERVER_H

// TCP server that bridges the FastAPI backend to the game.
//
// Runs on its own thread. Listens on 127.0.0.1:<Port> (default 27099), reads
// newline-delimited JSON messages from the backend, parses them into
// ControlCommands and enqueues them via ControlState, and answers state
// queries (game/turn/worm snapshot) so the web UI can reflect and gate play.
//
// TCP loopback rather than a named pipe: WA runs under Wine/Proton, whose pipe
// namespace isn't reachable by the native-Linux backend, but Wine maps Winsock
// TCP onto the host network stack.
class IpcServer {
public:
    static void install();  // spawn the TCP server thread
    static void stop();     // signal the thread to exit (on unload)
};

#endif // WKWEBCONTROL_IPCSERVER_H
