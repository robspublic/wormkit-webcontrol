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
//   PipeName=\\.\pipe\wkwebcontrol
class Config {
public:
    static void readConfig();

    static bool isModuleEnabled();
    static bool isDevConsoleEnabled();
    static bool isMutexEnabled();

    // Returns true if the running WA.exe matches the version this module was
    // built against (WA_TARGET_VERSION). When CheckWaVersion=0, always true.
    static bool waVersionCheck();

    static const std::string& getPipeName();

    // "wkWebControl 0.1.0" style label for message boxes.
    static std::string getFullStr();

private:
    static inline bool enabled = true;
    static inline bool devConsole = false;
    static inline bool checkWaVersion = true;
    static inline bool mutexEnabled = true;
    static inline std::string pipeName = "\\\\.\\pipe\\wkwebcontrol";
};

#endif // WKWEBCONTROL_CONFIG_H
