import { useCallback, useEffect, useRef, useState } from "react";
import { useOutletContext } from "react-router-dom";

import {
  api,
  openControlSocket,
  type ControlAction,
  type ControlPhase,
} from "../api";
import { useGameState } from "../useGameState";
import { useTurnVibration } from "../useTurnVibration";
import {
  WEAPON_SLOTS,
  PANEL_WIDTH,
  PANEL_HEIGHT,
  TILE_SIZE,
  TILE_PITCH,
  TILE_ORIGIN,
  type WeaponSlot,
} from "../weapons";
import weaponPanelUrl from "../assets/weapon-panel.png";
import type { AppContext } from "../App";

export default function ControlPage() {
  const { me, refreshMe } = useOutletContext<AppContext>();
  // Live game state via WebSocket; on a new game, re-fetch my claimed team.
  const { snapshot: snap } = useGameState(refreshMe);
  const [connected, setConnected] = useState(false);
  const [lastReply, setLastReply] = useState<string>("");
  const [claimError, setClaimError] = useState<string | null>(null);
  const [showWeapons, setShowWeapons] = useState(false);
  const socketRef = useRef<WebSocket | null>(null);

  // Actions the user is currently holding, so we can force-release them if the
  // turn is lost or the component unmounts (otherwise the worm keeps moving).
  const heldRef = useRef<Set<ControlAction>>(new Set());

  const send = useCallback(
    (action: ControlAction, phase: ControlPhase = "press", value = 0) => {
      const ws = socketRef.current;
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ action, phase, value }));
      }
    },
    [],
  );

  // Press/release a held input, tracking it so we can auto-release later.
  const press = useCallback(
    (action: ControlAction) => {
      if (heldRef.current.has(action)) return;
      heldRef.current.add(action);
      send(action, "press");
    },
    [send],
  );
  const release = useCallback(
    (action: ControlAction) => {
      if (!heldRef.current.has(action)) return;
      heldRef.current.delete(action);
      send(action, "release");
    },
    [send],
  );
  const releaseAll = useCallback(() => {
    for (const action of [...heldRef.current]) release(action);
  }, [release]);

  // Maintain the control WebSocket.
  useEffect(() => {
    const ws = openControlSocket();
    socketRef.current = ws;
    ws.onopen = () => setConnected(true);
    ws.onclose = () => setConnected(false);
    ws.onmessage = (ev) => setLastReply(ev.data);
    return () => {
      releaseAll();
      ws.close();
    };
  }, [releaseAll]);

  const myTeam = me?.team ?? null;
  const turnTeam = snap?.turn_team ?? null;
  const roundActive = snap?.round_active ?? false;

  // It's my turn when I've claimed the team that currently holds the turn.
  const isMyTurn = myTeam !== null && turnTeam !== null && myTeam === turnTeam;

  // Buzz the phone when it becomes my turn (no-op on unsupported browsers).
  useTurnVibration(isMyTurn);

  // The weapon my active worm currently has selected (for highlighting the
  // palette). Best-effort: the turn-holder team's current/active worm.
  const myTeamInfo = snap?.teams.find((t) => t.team_number === myTeam) ?? null;
  const selectedWeaponId =
    myTeamInfo?.worms.find((w) => w.active)?.weapon ??
    myTeamInfo?.worms[0]?.weapon ??
    null;

  // Per-weapon availability for the turn team, keyed by weapon id.
  const ammoById = new Map<number, { ammo: number; delay: number }>(
    (snap?.weapons ?? []).map((w) => [w.id, { ammo: w.ammo, delay: w.delay }]),
  );

  // If the turn is lost (or the round ends) while holding a button, release it
  // so a worm doesn't keep walking/charging after control is gone.
  useEffect(() => {
    if (!isMyTurn) releaseAll();
  }, [isMyTurn, releaseAll]);

  const canClaim = myTeam === null && roundActive && turnTeam !== null;

  async function onClaim() {
    setClaimError(null);
    try {
      await api.claim();
      refreshMe();
    } catch (e) {
      setClaimError(String(e));
    }
  }

  return (
    <div className="control">
      <div className={`turn-banner ${isMyTurn ? "active" : "waiting"}`}>
        {!roundActive
          ? "Waiting for game connection"
          : turnTeam === null
            ? "Between turns…"
            : myTeam === null
              ? `Team ${turnTeam} is up — claim it if it's yours`
              : isMyTurn
                ? `Your turn — controlling team ${myTeam}`
                : `Team ${turnTeam}'s turn — you have team ${myTeam}, please wait`}
      </div>

      {canClaim && (
        <div className="claim">
          <button className="btn claim-btn" onClick={onClaim}>
            This is my team (Team {turnTeam})
          </button>
          {claimError && <div className="banner error">{claimError}</div>}
        </div>
      )}

      <fieldset disabled={!isMyTurn} className="pad">
        <div className="row">
          <HoldButton className="btn move" action="move_left" press={press} release={release}>
            ◀ Left
          </HoldButton>
          <HoldButton className="btn move" action="move_right" press={press} release={release}>
            Right ▶
          </HoldButton>
        </div>

        <div className="row">
          <HoldButton className="btn aim" action="aim_down" press={press} release={release}>
            ▼ Aim down
          </HoldButton>
          <HoldButton className="btn aim" action="aim_up" press={press} release={release}>
            ▲ Aim up
          </HoldButton>
        </div>

        <div className="row">
          {/* Weapon select and jump are one-shots, not held inputs. */}
          <button
            className="btn weapon"
            onClick={() => setShowWeapons((s) => !s)}
            aria-expanded={showWeapons}
          >
            {!showWeapons && <WeaponIcon weaponId={selectedWeaponId} />}
            {showWeapons ? "Close weapons" : "Weapons"}
          </button>
          <button className="btn jump" onClick={() => send("jump", "press")}>
            ⤒ Jump
          </button>
        </div>

        {showWeapons && (
          <WeaponPalette
            selectedWeaponId={selectedWeaponId}
            ammoById={ammoById}
            round={snap?.round ?? 0}
            onSelect={(weaponId) => {
              send("select_weapon", "press", weaponId);
              setShowWeapons(false);
            }}
          />
        )}

        <div className="row">
          <HoldButton className="btn fire" action="fire" press={press} release={release}>
            🔥 Fire (hold)
          </HoldButton>
        </div>
      </fieldset>

      <div className="status">
        <span>socket: {connected ? "connected" : "disconnected"}</span>
        {lastReply && <span className="reply">{lastReply}</span>}
      </div>
    </div>
  );
}

// A button that sends a "press" while held and a "release" when let go. Uses
// pointer events so it works for mouse and touch, and releases on pointerleave
// / pointercancel so dragging off the button (or a canceled touch) can't leave
// an input stuck on. Long-press context menu and text selection are suppressed.
function HoldButton({
  action,
  press,
  release,
  className,
  children,
}: {
  action: ControlAction;
  press: (a: ControlAction) => void;
  release: (a: ControlAction) => void;
  className?: string;
  children: React.ReactNode;
}) {
  return (
    <button
      className={className}
      onPointerDown={(e) => {
        // Keep receiving pointerup even if the finger/cursor slides off.
        e.currentTarget.setPointerCapture(e.pointerId);
        press(action);
      }}
      onPointerUp={() => release(action)}
      onPointerCancel={() => release(action)}
      onLostPointerCapture={() => release(action)}
      onContextMenu={(e) => e.preventDefault()}
      style={{ touchAction: "none", userSelect: "none" }}
    >
      {children}
    </button>
  );
}

// A small inline icon of a single weapon, cut from the same weapon-panel sprite
// sheet as the palette. Used on the "Weapons" button to show the worm's
// currently selected weapon. Renders nothing when no weapon is known.
function WeaponIcon({ weaponId }: { weaponId: number | null }) {
  if (weaponId === null) return null;
  const slot = WEAPON_SLOTS.find((s) => s.weaponId === weaponId);
  if (!slot) return null;

  // Scale the native 28px tile down to fit comfortably beside the button label.
  const scale = 1;
  const tile = TILE_SIZE * scale;
  const pitch = TILE_PITCH * scale;
  const origin = TILE_ORIGIN * scale;
  const gridW = PANEL_WIDTH * scale;
  const gridH = PANEL_HEIGHT * scale;
  const left = origin + slot.col * pitch;
  const top = origin + slot.row * pitch;

  return (
    <span
      className="weapon-icon"
      aria-hidden="true"
      style={{
        display: "inline-block",
        width: tile,
        height: tile,
        verticalAlign: "middle",
        marginRight: 8,
        backgroundImage: `url(${weaponPanelUrl})`,
        backgroundRepeat: "no-repeat",
        backgroundSize: `${gridW}px ${gridH}px`,
        backgroundPosition: `-${left}px -${top}px`,
      }}
    />
  );
}

// The weapon palette: a grid of tiles cut from the WA weapon-panel sprite sheet
// via CSS background-position. Tapping a tile selects that weapon. Tiles are
// scaled up from the native 36x29 cell for touch; background-size is scaled to
// match so the sprite lines up.
function WeaponPalette({
  selectedWeaponId,
  ammoById,
  round,
  onSelect,
}: {
  selectedWeaponId: number | null;
  ammoById: Map<number, { ammo: number; delay: number }>;
  round: number;
  onSelect: (weaponId: number) => void;
}) {
  // Render at 3x so 28px tiles are comfortably tappable. Tiles are absolutely
  // positioned from their exact source coords (origin + col*pitch) so the 1px
  // borders/separators in the sprite are reproduced faithfully.
  const scale = 3;
  const tile = TILE_SIZE * scale;
  const pitch = TILE_PITCH * scale;
  const origin = TILE_ORIGIN * scale;
  const gridW = PANEL_WIDTH * scale;
  const gridH = PANEL_HEIGHT * scale;

  return (
    <div className="weapon-palette">
      <div
        className="weapon-grid"
        style={{
          position: "relative",
          width: gridW,
          height: gridH,
          // Full sprite behind the tiles so the 1px borders/separator lines
          // between tiles show through the gaps (faithful panel look).
          backgroundImage: `url(${weaponPanelUrl})`,
          backgroundRepeat: "no-repeat",
          backgroundSize: `${gridW}px ${gridH}px`,
        }}
      >
        {WEAPON_SLOTS.map((slot: WeaponSlot) => {
          const selected = selectedWeaponId === slot.weaponId;
          const avail = ammoById.get(slot.weaponId);
          // If we have no availability data at all (empty snapshot), leave tiles
          // enabled so the palette still works. With data, ammo==0 = unavailable.
          const hasData = ammoById.size > 0;
          const ammo = avail?.ammo ?? -1;
          const delay = avail?.delay ?? 0;
          // `delay` is a constant "available after N rounds" threshold, not a
          // countdown, so compare it to the current round. A weapon with delay
          // N is locked until round > N (e.g. Homing Missile delay=1 unlocks on
          // round 2). It's usable once it has ammo AND the delay has elapsed.
          const deferred = delay > 0 && round <= delay;
          // ammo == 0 means the weapon isn't in this game at all -> hide it.
          // ammo == -1 means infinite. "has ammo" is therefore ammo != 0.
          const hidden = hasData && ammo === 0;
          const hasAmmo = ammo !== 0; // >0 finite, or -1 infinite
          const available = !hasData || (hasAmmo && !deferred);

          // Exact source position of this tile within the sprite (in px),
          // scaled up: left/top of the 28px tile = origin + index*pitch.
          const left = origin + slot.col * pitch;
          const top = origin + slot.row * pitch;

          // Hidden weapons render as an opaque masking cell (no icon, no badge,
          // no interaction) so the sprite behind the grid doesn't show through.
          if (hidden) {
            return (
              <div
                key={`${slot.row}-${slot.col}`}
                className="weapon-tile hidden"
                aria-hidden="true"
                style={{ position: "absolute", left, top, width: tile, height: tile }}
              />
            );
          }

          // Badge (only when the weapon has ammo; ammo==0 tiles are hidden):
          //  - deferred:          "(<rounds remaining>)" + " x<ammo>" if finite
          //                       e.g. "(5) x1" (finite) or "(5)" (infinite)
          //  - available finite:  "x<ammo>"
          //  - available infinite / no data:  nothing
          let badge = "";
          if (hasData && hasAmmo) {
            const count = ammo > 0 ? ` x${ammo}` : ""; // infinite -> no count
            if (deferred) {
              const remaining = delay - round + 1; // rounds until round > delay
              badge = `(${remaining})${count}`;
            } else {
              badge = count.trim();
            }
          }

          // Mouse-only weapons need an in-game map click to aim (homing +
          // airstrike family). We can't relay a map coordinate, so keep the
          // tile's coloring and ammo badge for reference but block selection
          // and mark it so the player knows to use the PC's mouse instead.
          const mouseOnly = slot.mouseOnly;
          const selectable = available && !mouseOnly;

          const cls =
            "weapon-tile" +
            (selected ? " selected" : "") +
            (available ? "" : " unavailable") +
            (mouseOnly ? " mouse-only" : "");

          return (
            <button
              key={`${slot.row}-${slot.col}`}
              className={cls}
              title={
                mouseOnly
                  ? `${slot.name} — use the PC mouse to aim this weapon`
                  : slot.name
              }
              aria-label={
                mouseOnly
                  ? `${slot.name}, not available from web control, use the PC mouse`
                  : slot.name
              }
              disabled={!selectable}
              onClick={() => selectable && onSelect(slot.weaponId)}
              style={{
                position: "absolute",
                left,
                top,
                width: tile,
                height: tile,
                backgroundImage: `url(${weaponPanelUrl})`,
                backgroundRepeat: "no-repeat",
                backgroundSize: `${gridW}px ${gridH}px`,
                backgroundPosition: `-${left}px -${top}px`,
              }}
            >
              {badge && <span className="weapon-badge">{badge}</span>}
              {mouseOnly && (
                <span className="weapon-mouse-badge" aria-hidden="true">
                  🖱
                </span>
              )}
            </button>
          );
        })}
      </div>
    </div>
  );
}
