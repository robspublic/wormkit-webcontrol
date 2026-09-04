# wkWebControl (WormKit module)

A WormKit plugin for **Worms Armageddon 3.8.1** that receives web-based control
commands over TCP loopback and applies them to the current turn-holding worm.

## Layout

```
dll/
├── CMakeLists.txt          # 32-bit MSVC, C++23, /MT, PolyHook2 + patternscanner
├── cmake/version.h.in      # generated PROJECT_NAME / version macros
├── wkWebControl.ini.example# copy to WA dir as wkWebControl.ini
├── lib/                    # git submodules (see below)
└── src/
    ├── dllmain.cpp         # lifecycle: config -> hooks -> install -> ipc
    ├── Config.*            # reads wkWebControl.ini
    ├── Log.*               # dev-console logging
    ├── Hooks.*             # PolyHook2 detours + pattern scan + offset cache
    ├── PatternScanner.*    # vendored byte-signature scanner over WA.exe
    ├── Constants.h         # TaskMessage / ClassType / Weapon enums
    ├── W2App.*             # game globals (GameGlobal, DdMain, TurnGame)
    ├── entities/           # CTask, CGameTask, CTaskTeam, CTaskTurnGame, CTaskWorm
    ├── Protocol.*          # shared IPC wire contract (actions, GameSnapshot)
    ├── ControlState.*      # thread-safe command queue (IPC -> game thread)
    ├── ControlHooks.*      # per-frame hook: drain queue -> apply to worm
    └── IpcServer.*         # TCP server thread (backend <-> DLL)
```

## IPC wire contract

`src/Protocol.h` defines the constants shared with the backend
(`backend/app/protocol.py`). Transport is newline-delimited JSON over TCP:

- Backend → DLL command: `{"type":"cmd","team":"Red","action":"move_left","value":0}`
- Backend → DLL query:   `{"type":"query","what":"turn"}`
- DLL → Backend reply:   `{"turn_team":"Red","pos_x":1234,"pos_y":567,"weapon":3,"round_active":true}`

Action strings and `TurnState` field names must stay in sync between the two
sides; `backend/tests/test_protocol.py` pins the exact shape.

## Dependencies

PolyHook 2.0 (x86 detours) is a git submodule. Byte-signature scanning is
vendored directly (`src/PatternScanner.*`) rather than pulled from an external
submodule, so the build stays reproducible on public CI.

```
git submodule add https://github.com/stevemk14ebr/PolyHook_2_0.git \
    dll/lib/PolyHook_2_0
git submodule update --init --recursive
```

## Build (MSVC, 32-bit)

```
cmake -B build -A Win32
cmake --build build --config Release
```

Output: `build/Release/wkWebControl.dll`. Debug builds are unsupported (the
hooked game functions use custom calling conventions).

## Install into the game

1. Copy `wkWebControl.dll` into the WA install dir (next to `WA.exe`).
2. Copy `wkWebControl.ini.example` there as `wkWebControl.ini`.
3. Enable "Load WormKit modules" in WA Advanced Settings (3.7.0.0+).

## Offset verification status

This scaffold compiles against the reference-derived structure but does **not**
yet contain verified offsets/signatures for the local `WA.exe`. Every place
requiring reverse-engineering is marked `TODO(offsets)`:

- `Hooks::scanPattern` — implemented (vendored `PatternScanner`); needs real
  signatures supplied by the entity `install()` functions.
- `W2App` — capture GameGlobal / DdMain during app init.
- `Constants.h` — real TaskMessage / ClassType / Weapon ids.
- `entities/*` — confirm field offsets and vtable slot indices.
- `ControlHooks` — resolve the active turn-holder worm; drive real input.
  (The IPC read/write loop, JSON, and turn-snapshot plumbing are done; only the
  game-memory `TODO(offsets)` inside `onFrame()` / `applyCommand()` remain.)

`IpcServer` (TCP read loop + JSON parse/serialize) is complete.

These are bounded tasks: because the local game is exactly WA 3.8.1, the
reference module signatures are expected to resolve directly.
