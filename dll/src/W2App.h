#ifndef WKWEBCONTROL_W2APP_H
#define WKWEBCONTROL_W2APP_H

typedef unsigned long DWORD;

// Provides access to key global objects inside WA.exe that the entity code
// needs: the GameGlobal object (root of the task tree, weapon table, colors)
// and DdMain (holds the local machine id used for ownership checks).
//
// These addresses are captured by hooking the game's app-initialization
// routines (as the reference modules do), rather than hardcoding pointers.
class W2App {
public:
    static void install();

    static DWORD getAddrGameGlobal();
    static DWORD getAddrDdMain();

    // Convenience: root turn-game task, resolved from GameGlobal.
    static DWORD getAddrTurnGame();

private:
    static inline DWORD addrGameGlobal = 0;
    static inline DWORD addrDdMain = 0;

    // TODO(offsets): hooks that capture the above during app init.
};

#endif // WKWEBCONTROL_W2APP_H
