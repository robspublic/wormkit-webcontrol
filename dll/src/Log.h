#ifndef WKWEBCONTROL_LOG_H
#define WKWEBCONTROL_LOG_H

#include <string>

// Minimal logging facility for the module. When the dev console is enabled in
// config, messages are written to an allocated console; otherwise they are
// discarded (or optionally appended to a log file). Kept intentionally tiny so
// it can be called from any thread (pipe thread + game thread).
namespace Log {
    void install(bool enableConsole);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
}

#endif // WKWEBCONTROL_LOG_H
