#ifndef WKWEBCONTROL_W2APP_H
#define WKWEBCONTROL_W2APP_H

typedef unsigned long DWORD;

// Provides access to key global objects inside WA.exe that the entity code
// needs:
//   - DdGame       : the app object; TurnGame hangs off it at +0x8
//   - GameGlobal   : root of the task tree / global game state
//   - DdMain       : holds the local machine id used for ownership checks
//
// Addresses are captured by hooking the game's app-initialization routines
// (as the reference modules do), rather than hardcoding pointers. Offsets and
// signatures are for WA 3.8.1.
class W2App {
public:
    static void install();

    static DWORD getAddrDdGame();
    static DWORD getAddrGameGlobal();
    static DWORD getAddrDdMain();

    // Root turn-game task, resolved from DdGame (+0x8). 0 if not in a game.
    static DWORD getAddrTurnGame();

    static void setAddrDdMain(DWORD addr);

private:
    static inline DWORD addrDdGame = 0;
    static inline DWORD addrGameGlobal = 0;
    static inline DWORD addrDdMain = 0;

    // App-init hook: captures DdGame (first argument).
    static DWORD __stdcall hookInitializeW2App(DWORD ddGame, DWORD ddDisplay,
                                               DWORD dsSound, DWORD ddKeyboard,
                                               DWORD ddMouse, DWORD wavCdRom,
                                               DWORD wsGameNet);
    // GameGlobal construction hook: captures GameGlobal from *(ddGame+0x488).
    static DWORD __stdcall hookConstructGameGlobal(DWORD ddGame);
    // GameGlobal destruction hook: clears captured pointers on teardown.
    static DWORD __fastcall hookDestroyGameGlobal(int This, int EDX);
};

#endif // WKWEBCONTROL_W2APP_H
