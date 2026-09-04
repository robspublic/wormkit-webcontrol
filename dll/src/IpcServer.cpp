#include "IpcServer.h"

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

    // Read from the connected pipe, splitting the byte stream on '\n' into
    // messages. Loops until the client disconnects or the server stops.
    void servePipe(HANDLE pipe) {
        std::string buffer;
        char chunk[1024];
        while (g_running.load()) {
            DWORD read = 0;
            if (!ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) || read == 0) {
                break; // client disconnected or error
            }
            buffer.append(chunk, read);

            // Process every complete line currently in the buffer.
            size_t nl;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                if (line.empty()) continue;

                auto response = handleMessage(line);
                if (response) {
                    DWORD written = 0;
                    WriteFile(pipe, response->data(),
                              (DWORD)response->size(), &written, nullptr);
                }
            }
        }
    }

    void serverLoop(std::string pipeName) {
        Log::info("IpcServer listening on " + pipeName);
        while (g_running.load()) {
            HANDLE pipe = CreateNamedPipeA(
                pipeName.c_str(),
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,           // single instance: one local backend
                4096, 4096,  // out/in buffer sizes
                0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                Log::error("CreateNamedPipe failed");
                Sleep(500);
                continue;
            }

            // Block until the backend connects.
            BOOL connected = ConnectNamedPipe(pipe, nullptr)
                                 ? TRUE
                                 : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (connected) {
                servePipe(pipe);
            }

            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
        Log::info("IpcServer stopped");
    }
}

void IpcServer::install() {
    if (g_running.exchange(true)) return; // already running
    g_thread = std::thread(serverLoop, Config::getPipeName());
}

void IpcServer::stop() {
    if (!g_running.exchange(false)) return;
    // Nudge the accept by opening/closing a client handle so ConnectNamedPipe
    // returns; then join.
    HANDLE nudge = CreateFileA(Config::getPipeName().c_str(), GENERIC_READ,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (nudge != INVALID_HANDLE_VALUE) CloseHandle(nudge);
    if (g_thread.joinable()) g_thread.join();
}
