#include "IpcServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <optional>
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

    // Serialize the current turn snapshot to a JSON line for a "turn" query.
    // Field names must match the Python TurnState model.
    std::string turnResponse() {
        Protocol::TurnSnapshot s = ControlHooks::snapshot_turn();
        json j;
        j["turn_team"] = s.turn_team ? json(*s.turn_team) : json(nullptr);
        j["pos_x"] = s.pos_x ? json(*s.pos_x) : json(nullptr);
        j["pos_y"] = s.pos_y ? json(*s.pos_y) : json(nullptr);
        j["weapon"] = s.weapon ? json(*s.weapon) : json(nullptr);
        j["round_active"] = s.round_active;
        return j.dump() + "\n";
    }

    // Serialize the full game snapshot to a JSON line for a "state" query.
    // Field names must match the Python GameSnapshot / TeamInfo / WormInfo
    // models exactly.
    std::string stateResponse() {
        Protocol::GameSnapshot s = ControlHooks::snapshot_game();
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

        // Diagnostic: log what the DLL actually built for this query, so we can
        // tell live-DLL-data from backend/frontend caching.
        Log::info("state: round_active=" + std::to_string(s.round_active) +
                  " num_teams=" + std::to_string(s.num_teams) +
                  " turn_team=" + (s.turn_team ? std::to_string(*s.turn_team) : "none"));
        return j.dump() + "\n";
    }

    // Handle one newline-delimited JSON message. Returns an optional response
    // line to write back (present only for queries).
    std::optional<std::string> handleMessage(const std::string& line) {
        json j;
        try {
            j = json::parse(line);
        } catch (const std::exception& e) {
            Log::warn(std::string("bad JSON from backend: ") + e.what());
            return std::nullopt;
        }

        const std::string type = j.value("type", "");

        if (type == Protocol::kTypeCommand) {
            auto action = Protocol::parse_action(j.value("action", ""));
            if (!action) {
                Log::warn("unknown action: " + j.value("action", ""));
                return std::nullopt;
            }
            ControlCommand cmd;
            cmd.team = j.value("team", "");
            cmd.action = *action;
            cmd.value = j.value("value", 0);
            ControlState::push(cmd);
            return std::nullopt;
        }

        if (type == Protocol::kTypeQuery) {
            const std::string what = j.value("what", "");
            if (what == "turn") return turnResponse();
            if (what == "state") return stateResponse();
            return std::nullopt;
        }

        Log::warn("unknown message type: " + type);
        return std::nullopt;
    }

    // The listening socket, so stop() can unblock accept() from another thread.
    SOCKET g_listenSock = INVALID_SOCKET;

    // Serve one connected client: read bytes, split on '\n', respond per line.
    // Returns when the client disconnects or the server stops.
    void serveClient(SOCKET client) {
        std::string buffer;
        char chunk[2048];
        while (g_running.load()) {
            int n = recv(client, chunk, sizeof(chunk), 0);
            if (n <= 0) break; // client disconnected or error

            buffer.append(chunk, n);
            size_t nl;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                if (line.empty()) continue;

                auto response = handleMessage(line);
                if (response) {
                    // send() may not write it all at once; loop until done.
                    const char* p = response->data();
                    int remaining = (int)response->size();
                    while (remaining > 0) {
                        int sent = send(client, p, remaining, 0);
                        if (sent <= 0) return;
                        p += sent;
                        remaining -= sent;
                    }
                }
            }
        }
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

        Log::info("IpcServer listening on 127.0.0.1:" + std::to_string(port));
        while (g_running.load()) {
            SOCKET client = accept(g_listenSock, nullptr, nullptr);
            if (client == INVALID_SOCKET) break; // listen socket closed by stop()
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
