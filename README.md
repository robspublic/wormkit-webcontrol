# wormkit-webcontrol

Web-based controls for local couch/co-op play of **Worms Armageddon 3.8.1**.

When it's a player's turn, they can move their worm, aim, and change weapon from
a phone or laptop browser. A small web app talks to a WormKit plugin running
inside the game and relays the input to the currently active worm.

Target game: **Worms Armageddon v3.8.1** (32-bit `WA.exe`), Steam edition at
`/mnt/linux-hdd/steam/steamapps/common/Worms Armageddon`.

> Intended for **local, single-instance, offline** play only. Injecting worm
> state into an online/networked game will desync it.

## Architecture

```
Browser (React/Vite)
   |  HTTPS + X-Auth-Email header (set by your oauth-proxy / nginx)
   v
FastAPI backend            (same machine as WA.exe)
   |  named pipe  \\.\pipe\wkwebcontrol  (JSON)
   v
wkWebControl.dll           (WormKit module inside WA.exe)
   |  writes fields on the active worm / drives input, at the game's tick
   v
Worms Armageddon game state
```

Control commands never touch game memory from the pipe thread. The pipe thread
enqueues commands; the DLL drains that queue on the game's own per-frame message
hook and applies them to the turn-holding worm. This keeps the game
deterministic and avoids cross-thread crashes. The turn-holder check is enforced
inside the DLL, not just in the web layer.

## Components

| Directory   | What it is | Toolchain |
|-------------|------------|-----------|
| `dll/`      | `wkWebControl` WormKit plugin (C++, PolyHook 2.0, pattern scanning) | CMake + MSVC (Windows / CI) |
| `backend/`  | FastAPI app: admin API, control WebSocket, turn gating, pipe client | Python 3 + venv |
| `frontend/` | Vite/React: admin page + player control page | Node |
| `reference/`| Cloned open-source WormKit modules used as implementation models | (read-only) |

## Reference modules

`reference/` contains cloned sources studied while building this project:

- **wkRealTime** — turn-state manipulation, per-frame message hooks, worm/team
  structs, PolyHook + pattern scanning. Primary model.
- **wkTerrainSync** — modern CMake/MSVC build system, custom protocol, exports.
- **wkBindKeys** — in-game key binding / input injection (written in D).
- **WormKit** — CyberShadow's original module set (Packets, WormNAT2, etc.).

These are third-party projects retained locally for reference only.

## Build environment note

The DLL must be a **32-bit Windows PE built with MSVC** (the reference modules
rely on MSVC inline asm, `__fastcall`, and naked functions that do not port to
GCC/MinGW as-is). Because the host here is Linux, the DLL builds either on a
Windows machine/VM or via a GitHub Actions Windows runner. The backend and
frontend run natively on Linux.

## IPC contract

Backend and DLL speak newline-delimited JSON over the pipe. The contract is
defined on both sides (`backend/app/protocol.py`, `dll/src/Protocol.h`) and
pinned by `backend/tests/test_protocol.py`:

- command: `{"type":"cmd","team":"Red","action":"move_left","value":0}`
- query:   `{"type":"query","what":"turn"}`
- reply:   `{"turn_team":"Red","pos_x":1234,"pos_y":567,"weapon":3,"round_active":true}`

## Status

Scaffolding complete across all four components. What runs and is verified today
on Linux:

- **backend** — fully functional against a mock transport; `pytest` green
  (API + turn-gating + wire-contract tests).
- **frontend** — type-checks and builds (`npm run build`).
- **DLL** — full structure, IPC read/write loop, and JSON wired; the CMake
  project builds on the Windows CI runner. The remaining work is the
  `TODO(offsets)` game-memory reverse engineering (verified against the local
  WA 3.8.1), which can only be exercised on Windows with the game running.

See each component's `README.md` for setup and run instructions.
