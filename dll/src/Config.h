#ifndef WKWEBCONTROL_CONFIG_H
#define WKWEBCONTROL_CONFIG_H

#include <string>

// Reads wkWebControl.ini from the Worms Armageddon install directory (next to
// WA.exe). Modeled on the config pattern used by the reference modules.
//
// Example wkWebControl.ini:
//   [wkWebControl]
//   Enabled=1
//   DevConsole=0
//   CheckWaVersion=1
//   Port=27099
class Config {
public:
    static void readConfig();

    static bool isModuleEnabled();
    static bool isDevConsoleEnabled();
    static bool isMutexEnabled();

    // Returns true if the running WA.exe matches the version this module was
    // built against (WA_TARGET_VERSION). When CheckWaVersion=0, always true.
    static bool waVersionCheck();

    // TCP port the IPC server listens on (127.0.0.1). The backend connects here.
    // A named pipe can't be used: WA runs under Wine/Proton and its pipe
    // namespace isn't reachable by the native-Linux backend; Wine maps Winsock
    // TCP onto the host stack, so loopback TCP works across the boundary.
    static int getPort();

    // "wkWebControl 0.1.0" style label for message boxes.
    static std::string getFullStr();

private:
    static inline bool enabled = true;
    static inline bool devConsole = false;
    static inline bool checkWaVersion = true;
    static inline bool mutexEnabled = true;
    static inline int port = 27099;
};

#endif // WKWEBCONTROL_CONFIG_H
