#include "W2App.h"

#include "Hooks.h"
#include "Log.h"

// Trampolines to the original game functions, filled in by Hooks::hook.
DWORD origInitializeW2App = 0;
DWORD(__stdcall* origConstructGameGlobal)(DWORD ddGame) = nullptr;
DWORD(__fastcall* origDestroyGameGlobal)(int This, int EDX) = nullptr;

// InitializeW2App receives its first real argument (the W2Wrapper "this") in
// EDI, with the DirectDraw/sound/etc objects on the stack. We capture DdGame,
// then forward all arguments (including the EDI "this") to the original.
DWORD __stdcall W2App::hookInitializeW2App(DWORD ddGame, DWORD ddDisplay,
                                           DWORD dsSound, DWORD ddKeyboard,
                                           DWORD ddMouse, DWORD wavCdRom,
                                           DWORD wsGameNet) {
    DWORD w2wrapper, retv;
    _asm mov w2wrapper, edi

    addrDdGame = ddGame;

    _asm push wsGameNet
    _asm push wavCdRom
    _asm push ddMouse
    _asm push ddKeyboard
    _asm push dsSound
    _asm push ddDisplay
    _asm push ddGame
    _asm mov edi, w2wrapper
    _asm call origInitializeW2App
    _asm mov retv, eax

    return retv;
}

DWORD __stdcall W2App::hookConstructGameGlobal(DWORD ddGame) {
    DWORD ret = origConstructGameGlobal(ddGame);
    // GameGlobal pointer is stored in the DdGame object at +0x488.
    addrGameGlobal = *(DWORD*)(ddGame + 0x488);
    Log::info("GameGlobal constructed");
    return ret;
}

DWORD __fastcall W2App::hookDestroyGameGlobal(int This, int EDX) {
    addrGameGlobal = 0;
    return origDestroyGameGlobal(This, EDX);
}

void W2App::install() {
    // WA 3.8.1 signatures, sourced from the wkRealTime reference module (same
    // game build). If any fail to resolve, scanPattern throws and DllMain
    // reports it via a message box.
    DWORD addrInitializeW2App = _ScanPattern(
        "InitializeW2App",
        "\x6A\xFF\x64\xA1\x00\x00\x00\x00\x68\x00\x00\x00\x00\x50\x64\x89\x25\x00\x00\x00\x00\x53\x8B\x5C\x24\x1C\x55\x8B\x6C\x24\x1C\x56\x8B\x74\x24\x1C\x33\xC0\x89\x86\x00\x00\x00\x00\x89\x44\x24\x14\x89\x86\x00\x00\x00\x00\x8B\xC7\xC7\x06\x00\x00\x00\x00\x89\x9E\x00\x00\x00\x00\x89\xAE\x00\x00\x00\x00\xE8\x00\x00\x00\x00",
        "????????x????xxxx????xxxxxxxxxxxxxxxxxxx????xxxxxx????xxxx????xx????xx????x????");

    DWORD addrConstructGameGlobal = _ScanPattern(
        "ConstructGameGlobal",
        "\x6A\xFF\x68\x00\x00\x00\x00\x64\xA1\x00\x00\x00\x00\x50\x64\x89\x25\x00\x00\x00\x00\x83\xEC\x24\x53\x55\x8B\x6C\x24\x3C\x8B\x85\x00\x00\x00\x00\x8B\x48\x24",
        "???????xx????xxxx????xxxxxxxxxxx????xxx");

    DWORD addrDestroyGameGlobal = _ScanPattern(
        "DestroyGameGlobal",
        "\x6A\xFF\x68\x00\x00\x00\x00\x64\xA1\x00\x00\x00\x00\x50\x64\x89\x25\x00\x00\x00\x00\x51\x56\x8B\xF1\x89\x74\x24\x04\xC7\x06\x00\x00\x00\x00\x8B\xC6\xC7\x44\x24\x00\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xA1\x00\x00\x00\x00\x83\x78\x5C\x00\x75\x07\x8B\xC6\xE8\x00\x00\x00\x00\x8B\xC6\xE8\x00\x00\x00\x00\x8B\x86\x00\x00\x00\x00\x85\xC0\x74\x09\x50\xE8\x00\x00\x00\x00\x83\xC4\x04",
        "???????xx????xxxx????xxxxxxxxxx????xxxxx?????x????x????xxxxxxxxx????xxx????xx????xxxxxx????xxx");

    _HookDefault(InitializeW2App);
    _HookDefault(ConstructGameGlobal);
    _HookDefault(DestroyGameGlobal);
    Log::info("W2App::install (hooks installed)");
}

DWORD W2App::getAddrDdGame()     { return addrDdGame; }
DWORD W2App::getAddrGameGlobal() { return addrGameGlobal; }
DWORD W2App::getAddrDdMain()     { return addrDdMain; }

void W2App::setAddrDdMain(DWORD addr) { addrDdMain = addr; }

DWORD W2App::getAddrTurnGame() {
    // TurnGame hangs off DdGame at +0x8 (0 before a game starts).
    DWORD ddGame = addrDdGame;
    return ddGame ? *(DWORD*)(ddGame + 0x8) : 0;
}
