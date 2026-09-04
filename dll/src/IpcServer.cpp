#include "IpcServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "Config.h"
#include "ControlHooks.h"
#include "ControlState.h"
#include "Log.h"
#include "Protocol.h"

using json = nlohmann::json;

namespace {
    std::thread g_thread;
    std::atomic<bool> g_running{false};
    SOCKET g_listenSock = INVALID_SOCKET;

    // The currently connected backend client. Written by the accept loop (IPC
    // thread), read by pushState() (game thread), so it's mutex-guarded.
    std::mutex g_clientMutex;
    SOCKET g_client = INVALID_SOCKET;

    // Throttle pushes so an unusually fast frame path can't flood the socket.
    // FrameFinish is ~50Hz; cap at ~1 per 15ms.
    DWORD g_lastPushTick = 0;

    void setClient(SOCKET s) {
        std::lock_guard<std::mutex> lock(g_clientMutex);
        g_client = s;
    }

    // Parse a fire-and-forget command line from the backend and enqueue it.
    void handleCommand(const std::string& line) {
        json j;
        try {
            j = json::parse(line);
        } catch (const std::exception& e) {
            Log::warn(std::string("bad JSON from backend: ") + e.what());
            return;
        }
        if (j.value("type", "") != Protocol::kTypeCommand) return;

        auto action = Protocol::parse_action(j.value("action", ""));
        if (!action) {
            Log::warn("unknown action: " + j.value("action", ""));
            return;
        }
        ControlPhase phase = Protocol::parse_phase(j.value("phase", ""));
        int value = j.value("value", 0);
        // Update the held-input state; the game thread applies it each frame.
        ControlState::apply(*action, phase, value);
    }

    // Serialize the full game snapshot to a JSON line. Field names must match
    // the Python GameSnapshot / TeamInfo / WormInfo models exactly.
    std::string stateLine(const Protocol::GameSnapshot& s) {
        json j;
        j["game_id"] = s.game_id;
        j["round_active"] = s.round_active;
        j["before_round_start"] = s.before_round_start;
        j["num_teams"] = s.num_teams;
        j["current_machine"] = s.current_machine;
        j["turn_team"] = s.turn_team ? json(*s.turn_team) : json(nullptr);
        j["turn_time_ms"] = s.turn_time_ms ? json(*s.turn_time_ms) : json(nullptr);

        json teams = json::array();
        for (const auto& t : s.teams) {
            json jt;
            jt["team_number"] = t.team_number;
            jt["owner"] = t.owner;
            jt["current_worm"] = t.current_worm;
            jt["is_turn_holder"] = t.is_turn_holder;
            jt["is_local"] = t.is_local;

            json worms = json::array();
            for (const auto& w : t.worms) {
                worms.push_back({
                    {"team", w.team},
                    {"worm", w.worm},
                    {"active", w.active},
                    {"pos_x", w.pos_x},
                    {"pos_y", w.pos_y},
                    {"weapon", w.weapon},
                    {"facing", w.facing},
                });
            }
            jt["worms"] = std::move(worms);
            teams.push_back(std::move(jt));
        }
        j["teams"] = std::move(teams);
        return j.dump() + "\n";
    }

    // Read command lines from the connected client until it disconnects. The
    // socket is non-blocking; recv returning WSAEWOULDBLOCK just means "no data
    // right now", so we sleep briefly and continue (pushes happen from the game
    // thread via pushState, independent of this loop).
    void serveClient(SOCKET client) {
        setClient(client);
        std::string buffer;
        char chunk[2048];
        while (g_running.load()) {
            int n = recv(client, chunk, sizeof(chunk), 0);
            if (n > 0) {
                buffer.append(chunk, n);
                size_t nl;
                while ((nl = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, nl);
                    buffer.erase(0, nl + 1);
                    if (!line.empty()) handleCommand(line);
                }
                continue;
            }
            if (n == 0) break; // client closed
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(5);  // no command data pending; yield briefly
                continue;
            }
            break; // real error
        }
        setClient(INVALID_SOCKET);
    }

    void serverLoop(unsigned short port) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            Log::error("WSAStartup failed");
            return;
        }

        g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_listenSock == INVALID_SOCKET) {
            Log::error("socket() failed");
            WSACleanup();
            return;
        }

        // Loopback only: the backend runs on the same host (native Linux side,
        // reachable because Wine maps Winsock TCP onto the host stack).
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        BOOL reuse = TRUE;
        setsockopt(g_listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

        if (bind(g_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
            listen(g_listenSock, 1) == SOCKET_ERROR) {
            Log::error("bind/listen failed on 127.0.0.1:" + std::to_string(port));
            closesocket(g_listenSock);
            g_listenSock = INVALID_SOCKET;
            WSACleanup();
            return;
        }

        Log::info("IpcServer listening on 127.0.0.1:" + std::to_string(port) +
                  " (push mode)");
        while (g_running.load()) {
            SOCKET client = accept(g_listenSock, nullptr, nullptr);
            if (client == INVALID_SOCKET) break; // listen socket closed by stop()

            // Non-blocking: recv won't stall waiting for commands, and send
            // from the game thread won't stall the game if the buffer is full.
            u_long nb = 1;
            ioctlsocket(client, FIONBIO, &nb);
            int one = 1;
            setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));

            serveClient(client);
            closesocket(client);
        }

        if (g_listenSock != INVALID_SOCKET) {
            closesocket(g_listenSock);
            g_listenSock = INVALID_SOCKET;
        }
        WSACleanup();
        Log::info("IpcServer stopped");
    }
}

void IpcServer::pushState(const Protocol::GameSnapshot& snapshot) {
    // Called on the GAME THREAD (from ControlHooks::onFrame). Best-effort,
    // non-blocking: if there's no client or the socket buffer is full, drop
    // this frame rather than ever stalling the game.
    DWORD now = GetTickCount();
    if (now - g_lastPushTick < 15) return;  // ~<=66Hz cap

    std::lock_guard<std::mutex> lock(g_clientMutex);
    if (g_client == INVALID_SOCKET) return;

    g_lastPushTick = now;
    std::string line = stateLine(snapshot);

    // Send the whole line atomically or not at all: a truncated line would
    // corrupt the newline-delimited stream. On loopback a small message is
    // normally accepted whole; if the buffer is full (backend not keeping up)
    // the first send returns WSAEWOULDBLOCK and we drop this frame. If a rare
    // partial send occurs, finish it with a short bounded spin; if that still
    // can't complete, close the socket so the backend reconnects and resyncs.
    const char* p = line.data();
    int remaining = (int)line.size();
    int spins = 0;
    while (remaining > 0) {
        int sent = send(g_client, p, remaining, 0);
        if (sent > 0) {
            p += sent;
            remaining -= sent;
            continue;
        }
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            if (p == line.data()) return;  // nothing sent yet: drop this frame
            if (++spins > 200) {           // mid-line stall: resync via reconnect
                closesocket(g_client);
                g_client = INVALID_SOCKET;
                return;
            }
            continue;  // finish the partial line
        }
        // Real error: drop the client; the read loop will also notice.
        closesocket(g_client);
        g_client = INVALID_SOCKET;
        return;
    }
}

void IpcServer::install() {
    if (g_running.exchange(true)) return; // already running
    g_thread = std::thread(serverLoop, (unsigned short)Config::getPort());
}

void IpcServer::stop() {
    if (!g_running.exchange(false)) return;
    // Closing the listen socket unblocks accept() so the thread can exit.
    if (g_listenSock != INVALID_SOCKET) {
        closesocket(g_listenSock);
        g_listenSock = INVALID_SOCKET;
    }
    if (g_thread.joinable()) g_thread.join();
}
