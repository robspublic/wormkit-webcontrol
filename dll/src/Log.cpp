#include "Log.h"

#include <windows.h>
#include <cstdio>
#include <mutex>

#include "version.h"

namespace {
    bool g_consoleEnabled = false;
    std::mutex g_mutex;

    void write(const char* level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_consoleEnabled) {
            std::printf("[%s] %s\n", level, msg.c_str());
            std::fflush(stdout);
        }
    }
}

namespace Log {

    void install(bool enableConsole) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_consoleEnabled = enableConsole;
        if (enableConsole) {
            AllocConsole();
            // Redirect stdio to the new console.
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
            SetConsoleTitleA(PROJECT_NAME " dev console");
        }
    }

    void info(const std::string& msg)  { write("INFO", msg); }
    void warn(const std::string& msg)  { write("WARN", msg); }
    void error(const std::string& msg) { write("ERROR", msg); }

} // namespace Log
