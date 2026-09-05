# Worms Armageddon 3.8.1 — Reverse-Engineering Notes

Everything we've learned about interacting with `WA.exe` (Worms Armageddon
**3.8.1**) from the wkWebControl WormKit DLL: how it's hooked, how game memory is
laid out, how to read state, and how to inject input. Written so a future session
can pick up without re-deriving any of this.

> Scope: WA **3.8.1** only. Offsets/signatures are build-specific. The
> `reference/` directory (gitignored) contains the WormKit modules these were
> derived from — primarily **wkRealTime** (entity layouts, hooks, task-message
> plumbing) and **wkTerrainSync** (scheme/ammo table). `reference/WormKit` and
> `reference/wkBindKeys` are also present.

---

## 1. Environment & build

- **WA.exe** is a 32-bit PE (x86). The DLL must be built **32-bit with MSVC**.
  `dll/CMakeLists.txt` has a hard guard (`FATAL_ERROR`) if the toolchain isn't
  MSVC — this is why a Linux `cmake -B ... -S .` *always* "fails" at that guard.
  Reaching the guard = the CMake/source parse is clean. It does **not** compile.
- **The DLL only truly compiles on Windows CI** (`.github/workflows/build-dll.yml`).
  The local CMake parse cannot catch C++ errors like missing declarations,
  out-of-order function definitions, or unused-symbol issues. **This is a real
  blind spot** — a build broke once on `C3861 'dumpWeaponTable': identifier not
  found` because a function was called before its definition with no forward
  declaration. When adding helper functions, either define-before-use or add a
  forward declaration; the CMake parse will not save you.
- The game runs on **Linux via Steam Proton/Wine**. Consequences:
  - Named pipes don't work for IPC. We use **TCP loopback 127.0.0.1:27099**
    (Wine maps Winsock TCP to the host).
  - The Wine filesystem maps the Unix root `/` to the **`Z:` drive**. To write a
    file the host can read directly, use e.g.
    `Z:\home\rfisher\git\wormkit-webcontrol\tmp\wkwc_dump.txt`. `fopen` in the
    DLL with that path lands in the repo `tmp/`.
  - `ERRORLOG.TXT` is written by WA into its install dir on a crash — the primary
    crash-diagnostic artifact (see §9).
- WA install dir (this machine):
  `/mnt/linux-hdd/steam/steamapps/common/Worms Armageddon/`.

---

## 2. Hooking & pattern scanning

The DLL uses **PolyHook 2.0** (x86 detours) plus a vendored byte-signature
scanner (`dll/src/PatternScanner.*`). `dll/src/Hooks.*` wraps both.

- `_ScanPattern(name, pattern, mask)` → resolves an address by byte signature.
  - `pattern` is a byte string (may contain `\x00`); `mask` is `'x'` (must match)
    / `'?'` (wildcard). **Mask length defines the pattern length**, not
    `strlen(pattern)` — so embedded nulls in the pattern are fine.
  - Resolved addresses are **cached to disk** (`wkWebControl.cache`) so scanning
    is paid once per build. Cache is keyed by `name`.
  - **On failure it throws** `std::runtime_error`. In `DllMain` that's caught and
    shown in a message box, aborting init. If you scan from a place where a throw
    would be bad (e.g. the game thread), wrap it in try/catch (we do this for the
    ammo-table scan so a miss just disables a feature).
- `_HookDefault(Name)` wires `addrName -> hookName -> origName` by naming
  convention. Detour installs a trampoline into `orig*`.
- **Detour trampoline gotcha (x86):** when storing the original address, use a
  local `uint64_t trampoline` then assign `*ppOriginal = (DWORD)trampoline`.
  Passing a `DWORD*` where a `uint64_t*` is expected writes 8 bytes into a 4-byte
  slot → corrupts the next field → `EIP=0` crash. (Learned the hard way.)
- Prefer **not adding scans you don't use**: an unused scan still runs at startup
  and can abort init if the signature doesn't resolve in this exact binary
  (`FifoMakeSpace` did exactly this and had to be removed).

### Calling game functions directly (inline asm shims)

WA functions often use non-standard calling conventions. The reference calls them
via `__asm` shims, e.g. `TaskMessageFifo::callTaskMessageSend`:
```
_asm mov eax, fifo      ; 'this'/first arg in a register
_asm mov ecx, msize
_asm push data
_asm push mtype
_asm call addrTaskMessageSend
_asm mov retv, eax
```
This pattern (args split between registers and stack) is common; match the
reference's shim exactly per function.

---

## 3. Global objects (W2App)

Captured by hooking app-init / game-construction routines (`dll/src/W2App.*`).

| Object | How obtained |
|---|---|
| **DdGame** | First arg to `InitializeW2App` (captured in the hook). |
| **GameGlobal** | `*(DdGame + 0x488)`, captured in `ConstructGameGlobal` hook. `0` between games. |
| **TurnGame** (`CTaskTurnGame`) | `*(DdGame + 0x8)`. `0` before a game / after teardown. Root of the task tree. |
| **DdMain** | Reference gets it from a lobby hook (`ConstructLobbyHostScreen + 0x3DE`). **We don't hook that**, so `getAddrDdMain()` returns 0 — fine for single-machine local play (see turn detection §5). |

Lifecycle hooks (scan signatures live in `W2App::install`):
- `InitializeW2App` — captures DdGame.
- `ConstructGameGlobal` — sets GameGlobal, increments a **gameId** counter (used
  by the backend to detect a new game and auto-clear team claims).
- `DestroyGameGlobal` — zeroes GameGlobal + DdGame, notifies
  `ControlHooks::onGameTornDown()` (clears turn state, held input, snapshot).

**Always re-read these pointers each frame and guard for null** — a between-games
snapshot must report "no game", not read a freed object.

---

## 4. The task tree (CTask hierarchy)

WA's game objects are **tasks** in a tree rooted at the turn-game task. Base
layout (`dll/src/entities/CTask.h`, from wkRealTime):

```
+0x00  vtable
+0x04  parent (CTask*)
+0x08  children list (CTaskList overlay):
         +0x08 max_size   (list base = CTask+0x08)
         +0x10 count      (== CTask+0x10)
         +0x14 CTask** data (== CTask+0x14)
         +0x18 hash_list
+0x1C  unknown
+0x20  classtype (ClassType enum)
+0x2C  gameglobal pointer stored on the task
```

- **vtable slot 2** = `HandleMessage(sender, TaskMessage mtype, size_t size, void* data)`.
  This is both the observation point (hook it) and the injection point (call it).
- Traverse depth-first from the turn-game task; filter by `classtype`.
- **ClassType** is sequential from 0 (wkRealTime `Constants.h`). Values we use:
  `TurnGame=6`, `Team=10`, `Worm=17`. Full enum in the reference.

**Worm enumeration caveat:** iterate a team's `children` directly and cast to
`CTaskWorm` **without** a classtype filter. The reference does this; adding a
`classtype == Worm` filter dropped all worms (a stale/wrong classtype value on
the child). Team enumeration *does* filter `classtype == Team`.

---

## 5. Turn / round tracking

### Current turn team — event-driven, NOT machine-id
The obvious approach (turn holder = team whose `owner == GameGlobal+0x726C`
machine id) **fails in single-machine local play**: every team shares one machine
id, so it's true for all teams and the turn never appears to change.

**Working approach:** hook `CTaskTurnGame::HandleMessage` and track the active
team from turn-lifecycle messages. The **first DWORD of the StartTurn /
TurnStarted payload is the team number**. Store it in an `atomic<int>`; clear to
-1 on FinishTurn / TurnFinished and on game teardown.

### Round number — GameGlobal, not the turn-game
- **Round counter = `*(int*)(GameGlobal + 0x7238)`** (1-based; `0x723c` is an
  identical mirror). Verified across 7 captures: `1,2,3,4,5,6,7`.
- **Pitfall:** `TurnGame + 0x130` looks like a round counter over two samples
  (1→2) but is actually a per-team/turn counter that **wraps** (1,2,3,1,2,3,…).
  Two data points weren't enough to distinguish it; needed ≥3 genuine rounds.
- `GameGlobal + 0x5CC` = a **frame counter** (rises ~50/sec). Useful only as a
  "are these two dumps actually different moments?" discriminator.
- `TurnGame + 0x184` = round **timer in ms, counts down** (900000 = 15:00). Not a
  round count.

### Other turn-game fields (`CTaskTurnGame`, offsets confirmed)
| Offset | Meaning |
|---|---|
| `+0x7C` | number_of_teams |
| `+0x140` | its_before_round_start (worms still being placed) |
| `+0x188` / `+0x18C` | turn timer(s), ms |
| `+0x12C` / `+0x134` | current_team_1 / current_team_2 (reference names) |

---

## 6. Reading game state (the monitor snapshot)

Built **on the game thread** each frame (see §7), never on the IPC thread.

### CTaskTeam (`dll/src/entities/CTaskTeam.h`)
| Offset | Field |
|---|---|
| `+0x38` | team_number |
| `+0x3C` | active |
| `+0x40` | owner (machine id) |
| `+0x48` | current_worm_number |

Team **name** and **health** have **no confirmed offset** in 3.8.1 — the monitor
labels teams by number. Don't guess these.

### CTaskWorm (`dll/src/entities/CTaskWorm.h`)
| Offset | Field |
|---|---|
| `+0x84` | posX (large fixed-point; UI divides by 100000 for display) |
| `+0x88` | posY |
| `+0xFC` | teamnumber |
| `+0x100` | wormnumber |
| `+0x104` | active |
| `+0x164` | state_counter |
| `+0x170` | selected_weapon (index) |
| `+0x1A8` | facing_direction (+1 / -1) |
| `+0x270` | shooting_angle (aim) |
| `+0x36C` | selected_weapon_entry_ptr (ptr into the weapon table) |

posX/posY live in the `CGameTask` base; the rest are worm-specific. The full worm
struct is fully field-mapped in `reference/wkRealTime/src/entities/CTaskWorm.h`.

---

## 7. Threading model (critical)

- WA runs game logic on a **single game thread**. The DLL's IPC server runs on
  its **own thread**.
- **Build the snapshot on the GAME THREAD**, not the IPC thread. We hook the
  turn-game's **FrameFinish** message; in that handler we traverse the live tree,
  build a `GameSnapshot`, and publish it under a mutex. The IPC thread only ever
  reads the published copy.
- Reading the live tree from the IPC thread **races** the game thread's in-place
  updates → frozen/torn fields (worm positions "stuck"), and is unsafe.
- **All game-memory writes / input injection must happen on the game thread**
  (also from the FrameFinish path).

### IPC / data flow
- **Push model:** the DLL pushes a JSON snapshot line per FrameFinish
  (non-blocking best-effort `send`; drop if the socket buffer is full — never
  stall the game). Backend keeps the latest, fans out to `/ws/state` at 10Hz.
- Backend → DLL: fire-and-forget command lines (newline-delimited JSON) on the
  same socket. DLL reads them on the IPC thread and updates shared input state;
  the game thread applies that state on the next frame.
- Turn-gating is enforced in the backend (claim flow) AND is naturally respected
  by injecting through the turn-game (which routes to the current turn holder).

---

## 8. Injecting input (control)

**Task messages, not field writes.** WA drives worms via `TaskMessage`s, not by
poking fields. Writing `facing_direction` etc. does **not** make a worm move.

### Mechanism that works
On the game thread, dispatch a message straight to the **turn-game task's
vtable HandleMessage** with a **1024-byte zeroed payload buffer**, mirroring how
the reference sends team/turn messages:
```cpp
unsigned char buff[1024]; memset(buff, 0, sizeof(buff));
*(DWORD*)buff = <payload>;               // e.g. active team number, or weapon id
turngame->vtable8_HandleMessage(turngame, mtype, sizeof(buff), buff);
```
The turn-game routes the input to its active worm. A **size-0 payload gets
dropped** — always send the sized zeroed buffer.

### TaskMessage ids (WA 3.8.1, sequential-from-0 enum; from wkRealTime Constants.h)
| Msg | id | Notes |
|---|---|---|
| FrameStart | 1 | |
| **FrameFinish** | 2 | our per-frame hook point (build snapshot + apply input) |
| RenderScene | 3 | |
| ProcessInput | 4 | game drains its input FIFO here |
| MoveLeft | 30 | held: re-send every frame while held |
| MoveRight | 31 | held |
| **MoveUp** | 32 | **aim up** (not "FaceUp") — held |
| **MoveDown** | 33 | **aim down** — held |
| FireWeapon | 38 | press edge (start charging) |
| ReleaseWeapon | 39 | release edge (launch) |
| SkipGo | 41 | |
| SelectWeapon | 51 | see caveat below |
| StartTurn | 52 | payload[0] = team number |
| FinishTurn | 55 | |
| TurnStarted | 56 | payload[0] = team number |
| TurnFinished | 57 | |

### Held vs edge input model
- **Move / aim** are *level-triggered*: while the button is held, the message
  must be **re-sent every frame**. Shared held-state (booleans) is updated by the
  IPC thread; the game thread re-asserts each held direction each frame.
- **Fire** is *edge-triggered*: `FireWeapon` on the press edge (starts charging),
  `ReleaseWeapon` on the release edge (launches). Track previous state to emit
  exactly one of each.
- **Jump** is a one-shot (`TaskMessage_Jump = 36`).
- **Auto-release**: the backend releases any held inputs on WS disconnect / turn
  loss so a dropped phone doesn't leave a worm walking off a cliff.

### Weapon selection — special (field write, not a message)
Weapon selection is worm **state**, not transient input. Sending `SelectWeapon`
to the turn-game did **nothing**. What works: write the worm's fields on the game
thread:
```cpp
worm->selected_weapon_unknown170 = weaponId;                       // 0x170 index
DWORD weaponTable = *(DWORD*)(gameGlobal + 0x510);
worm->selected_weapon_entry_ptr36C = weaponTable + 464 * weaponId; // 0x36C entry
```
- **You MUST set both.** The base game does **not** recompute `0x36C` when it's
  zeroed (that recompute lives in the reference module's own frame hook, which we
  don't run). Zeroing `0x36C` and leaving it → game dereferences null → crash
  (access violation reading `[null+0x30]`).
- Guard: `gameGlobal != 0`, `weaponTable != 0`, and clamp `weaponId` to the valid
  table range so `weaponTable + 464*id` never points past the table (out-of-range
  entry faults the same way).
- The weapon **enum id equals the table index** (confirmed: tapped weapon matches
  in-game).
- **Known limitation (accepted):** the field write selects the correct weapon,
  but the **held-weapon sprite/panel doesn't refresh until the worm next acts**
  (e.g. walks). WA's full "select + refresh" path goes through the keyboard/
  weapon-panel input layer (F-keys), which is *not* reachable via the turn-game
  message dispatch — wkBindKeys drives it via OS-level `SendInput`, which is
  fragile under Proton (foreground-focus dependent). We chose to keep the field
  write and accept the cosmetic refresh lag rather than take on F-key replay.

---

## 9. Weapons: static table vs per-team ammo

Two **separate** structures — this distinction cost real time to discover:

### Static weapon table (definition) — `*(GameGlobal + 0x510)`
- Array of **464-byte** entries, indexed by weapon id: `entry = base + 464*id`.
- Holds the **static weapon definition** (fuse/power/damage params, sprite entry
  pointer). Same regardless of scheme.
- **Does NOT hold ammo or enabled state.** A disabled weapon's entry looks
  identical to an enabled one here. Do not look for counts in this table.

### Per-team ammo table (per-game availability) — from wkTerrainSync
The real ammo/availability. Base located via scan (same as wkTerrainSync):
```
addrReadAmmoFromWAM = _ScanPattern("ReadAmmoFromWAM",
  "\x8B\x40\x0C\x56\x8B\x74\x24\x10\x57\x8B\x7C\x24\x0C\x50\x6A\x00\x51\x57\xFF\x15\x00\x00\x00\x00\x69\xF6\x00\x00\x00\x00\x03\x74\x24\x10\x0F\xB7\x14\x75\x00\x00\x00\x00\x66\x83\xFA\xFF",
  "??????xxxxxxxxxxxxxx????xx????xxxxxxxx????xxxx");
addrAmmoTable = *(DWORD*)(addrReadAmmoFromWAM + 0x26);
```
Flat array of `short` (2 bytes), keyed by `weapon + 143*team`:
```
ammo  = *(short*)(addrAmmoTable + 2*(weapon + 143*team));
delay = *(short*)(addrAmmoTable + 2*(weapon + 143*team) + 142);
```
- **ammo**: `-1` = infinite, `0` = not in this game (hidden), `>0` = finite count.
- **delay**: a **constant "available after N rounds" threshold** (the scheme's
  weapon Delay setting), **not a countdown** — it never changes during the game.
- **Availability rule:** usable when `ammo != 0` **AND** `round > delay`
  (`round` from GameGlobal+0x7238). Homing Missile `delay=1` unlocks on round 2;
  Air Strike `delay=5` unlocks on round 6. "Rounds remaining" = `delay - round + 1`.
- Verified against a live scheme: Bazooka `ammo=-1`, Homing Missile
  `ammo=1 delay=1`, Mortar `ammo=5`, Cluster `ammo=3`, Holy Grenade
  `ammo=0 delay=4` (hidden), Skip Go `ammo=-1`. All matched.

### Weapon ids & panel layout
- `Constants::Weapon` enum (wkRealTime): `None=0, Bazooka=1, …` sequential.
  Utilities are at the **end** (JetPack=62 … Invisibility=66; MagicBullet=61).
- The in-game weapon panel is 5 cols × 13 rows = 65 slots (Util + F1..F12). The
  panel layout maps to enum ids **by name** (not 1:1 by position — the enum has a
  few weapons the panel doesn't surface, e.g. AquaSheep). See
  `frontend/src/weapons.ts` for the full name→id→row/col mapping and
  `reference/wkRealTime/WA_WeaponPanel_layout.md`.

### F-key selection cycle (documented, not implemented)
WA's own weapon selection: pressing a row's F-key selects the **first enabled**
weapon in that row; pressing it again while already on that row **advances to the
next enabled** weapon (wraps at end); **disabled/deferred weapons are skipped**.
So computing "N presses to reach weapon W" requires the enabled-weapon list per
row (now derivable from the ammo table) *and* the current cycle position. Not
implemented — see the weapon-selection limitation in §8.

---

## 10. Reverse-engineering workflow (what worked)

- **Dump to a file, not the dev console.** The dev console has limited scrollback
  and silently trimmed the *start* of large dumps (and once yielded a stale
  duplicate capture). Write to `Z:\…\tmp\wkwc_dump.txt` and read it on the host.
- **Log a discriminator** (the frame counter, GameGlobal+0x5CC) in each capture
  so you can tell whether two captures are genuinely different moments.
- **To find a counter/flag:** dump a band of candidate fields filtered to small
  plausible values across **several** captures, then diff for the field with the
  expected monotonic sequence. **Two samples are not enough** to distinguish a
  true counter from one that wraps/mirrors — get ≥3 (the round-counter hunt
  needed a full `1..7` to rule out `TurnGame+0x130`).
- **Anchor to ground truth:** compare dumps against values you can read in-game
  (ammo counts, which weapons are greyed, which round unlocks a weapon).
- Keep RE diagnostics clearly marked TEMP and remove them once decoded. Dumps and
  scratch notes live in the gitignored `tmp/`.

---

## 11. Quick reference — offsets & signatures (WA 3.8.1)

| What | Location |
|---|---|
| GameGlobal | `*(DdGame + 0x488)` |
| TurnGame | `*(DdGame + 0x8)` |
| Current machine id | `GameGlobal + 0x726C` |
| **Round number** | `GameGlobal + 0x7238` (mirror `+0x723c`) |
| Frame counter | `GameGlobal + 0x5CC` |
| Static weapon table | `*(GameGlobal + 0x510)`, 464-byte entries |
| Ammo table base | `*(ReadAmmoFromWAM_scan + 0x26)` |
| Ammo / delay | `ammoTable + 2*(weapon+143*team)` / `+142` |
| DdMain machine id (ref) | `*(char*)(DdMain + 0xD9DC + 0x40)` — DdMain not captured here |
| CTaskTeam | team# +0x38, owner +0x40, current_worm +0x48 |
| CTaskWorm | posX +0x84, posY +0x88, team +0xFC, worm +0x100, active +0x104, weapon +0x170, facing +0x1A8, aim +0x270, weapon_entry +0x36C |
| CTaskTurnGame | num_teams +0x7C, before_round_start +0x140, turn_timer +0x188 |

Scan signatures for `InitializeW2App`, `ConstructGameGlobal`, `DestroyGameGlobal`
(W2App.cpp), `CTurnGameHandleMessage` (CTaskTurnGame.cpp), and `ReadAmmoFromWAM`
(ControlHooks.cpp) are in the source. More (input FIFO send, weapon panel,
scheme, etc.) are in `reference/wkRealTime` and `reference/wkTerrainSync`.
