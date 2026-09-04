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
| `scripts/`  | Helpers to fetch CI build artifacts and deploy the DLL | bash + `gh` |
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

## Building and deploying the DLL

The Windows CI (`.github/workflows/build-dll.yml`) builds `wkWebControl.dll` on
every push touching `dll/**` and uploads it as an artifact. Two helper scripts
in `scripts/` cover the round trip from a pushed build to the running game.

### 1. Fetch the built DLL — `scripts/fetch-artifact.sh`

Downloads the latest CI build artifact into `gitHubArtifacts/` (gitignored).
Artifacts live in GitHub Actions storage, not git, so this uses the GitHub CLI
(`gh`), not the SSH remote.

One-time setup: install `gh` and authenticate.

```
sudo apt install gh      # or: sudo snap install gh
gh auth login
```

Then, after pushing a change:

```
scripts/fetch-artifact.sh --wait     # wait for the build to finish, then download
scripts/fetch-artifact.sh            # grab the latest completed run's artifact
scripts/fetch-artifact.sh --run <id> # download from a specific run id
```

### 2. Deploy into Worms Armageddon — `scripts/deploy-artifact.sh`

Copies `gitHubArtifacts/wkWebControl.dll` into the WA install dir, backing up any
currently deployed DLL to `wkWebControl.dll.YYYYMMDD.bak` first (a `-HHMMSS`
suffix is added if a same-day backup already exists). It verifies the target
really is the WA folder (checks for `WA.exe`) before writing, and installs the
example ini as `wkWebControl.ini` only if no config exists yet.

```
scripts/deploy-artifact.sh                    # deploy to the default WA dir
scripts/deploy-artifact.sh --wa-dir <path>    # deploy to a different install
```

Default WA dir: `/mnt/linux-hdd/steam/steamapps/common/Worms Armageddon`.

Typical loop: push a change → `fetch-artifact.sh --wait` → `deploy-artifact.sh`
→ launch WA (with `DevConsole=1` in the ini to see the module's log).

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
