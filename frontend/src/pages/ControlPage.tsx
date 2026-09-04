import { useEffect, useRef, useState } from "react";
import { useOutletContext } from "react-router-dom";

import {
  api,
  openControlSocket,
  type ControlAction,
  type Me,
  type TurnState,
} from "../api";

interface Ctx {
  me: Me | null;
}

// How often to refresh whose turn it is.
const TURN_POLL_MS = 1000;

export default function ControlPage() {
  const { me } = useOutletContext<Ctx>();
  const [turn, setTurn] = useState<TurnState | null>(null);
  const [connected, setConnected] = useState(false);
  const [lastReply, setLastReply] = useState<string>("");
  const socketRef = useRef<WebSocket | null>(null);

  // Poll turn state.
  useEffect(() => {
    let active = true;
    const tick = () => {
      api
        .turn()
        .then((t) => active && setTurn(t))
        .catch(() => active && setTurn(null));
    };
    tick();
    const id = setInterval(tick, TURN_POLL_MS);
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

  const myTeams = me?.teams ?? [];
  const turnTeam = turn?.turn_team ?? null;
  const isMyTurn = turnTeam !== null && myTeams.includes(turnTeam);

  function send(action: ControlAction, value = 0) {
    const ws = socketRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ action, value }));
    }
  }

  return (
    <div className="control">
      <div className={`turn-banner ${isMyTurn ? "active" : "waiting"}`}>
        {turnTeam === null
          ? "Waiting for the round to start…"
          : isMyTurn
            ? `Your turn — controlling ${turnTeam}`
            : `${turnTeam}'s turn — please wait`}
      </div>

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
