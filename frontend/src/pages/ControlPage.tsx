import { useEffect, useRef, useState } from "react";
import { useOutletContext } from "react-router-dom";

import {
  api,
  openControlSocket,
  type ControlAction,
  type GameSnapshot,
} from "../api";
import type { AppContext } from "../App";

// How often to refresh the game snapshot (whose turn it is).
const POLL_MS = 1000;

export default function ControlPage() {
  const { me, refreshMe } = useOutletContext<AppContext>();
  const [snap, setSnap] = useState<GameSnapshot | null>(null);
  const [connected, setConnected] = useState(false);
  const [lastReply, setLastReply] = useState<string>("");
  const [claimError, setClaimError] = useState<string | null>(null);
  const socketRef = useRef<WebSocket | null>(null);

  // Poll the game snapshot.
  useEffect(() => {
    let active = true;
    const tick = () => {
      api
        .monitor()
        .then((s) => active && setSnap(s))
        .catch(() => active && setSnap(null));
    };
    tick();
    const id = setInterval(tick, POLL_MS);
    return () => {
      active = false;
      clearInterval(id);
    };
  }, []);

  // Maintain the control WebSocket.
  useEffect(() => {
    const ws = openControlSocket();
    socketRef.current = ws;
    ws.onopen = () => setConnected(true);
    ws.onclose = () => setConnected(false);
    ws.onmessage = (ev) => setLastReply(ev.data);
    return () => ws.close();
  }, []);

  const myTeam = me?.team ?? null;
  const turnTeam = snap?.turn_team ?? null;
  const roundActive = snap?.round_active ?? false;

  // It's my turn when I've claimed the team that currently holds the turn.
  const isMyTurn = myTeam !== null && turnTeam !== null && myTeam === turnTeam;

  // Show the claim button when I have no team yet and a team is currently
  // taking its turn (that's the team I'd be claiming).
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

  function send(action: ControlAction, value = 0) {
    const ws = socketRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ action, value }));
    }
  }

  return (
    <div className="control">
      <div className={`turn-banner ${isMyTurn ? "active" : "waiting"}`}>
        {!roundActive
          ? "Waiting for a game to start…"
          : myTeam === null
            ? turnTeam !== null
              ? `Team ${turnTeam} is up — claim it if it's yours`
              : "Waiting for a turn…"
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
          <button className="btn move" onClick={() => send("move_left")}>
            ◀ Left
          </button>
          <button className="btn move" onClick={() => send("move_right")}>
            Right ▶
          </button>
        </div>

        <div className="row">
          <button className="btn aim" onClick={() => send("aim_up", 1)}>
            ▲ Aim up
          </button>
          <button className="btn aim" onClick={() => send("aim_down", 1)}>
            ▼ Aim down
          </button>
        </div>

        <div className="row">
          <button className="btn weapon" onClick={() => send("select_weapon", 0)}>
            Change weapon
          </button>
          <button className="btn fire" onClick={() => send("fire")}>
            🔥 Fire
          </button>
        </div>
      </fieldset>

      <div className="status">
        <span>socket: {connected ? "connected" : "disconnected"}</span>
        {lastReply && <span className="reply">{lastReply}</span>}
      </div>
    </div>
  );
}
