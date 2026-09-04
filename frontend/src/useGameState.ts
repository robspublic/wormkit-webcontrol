import { useEffect, useRef, useState } from "react";

import { openStateSocket, type GameSnapshot, type StateMessage } from "./api";

export interface GameState {
  snapshot: GameSnapshot | null; // null while offline / not yet received
  offline: boolean;
}

/**
 * Subscribe to the backend's /ws/state stream (10Hz). Returns the latest
 * snapshot and an offline flag. Reconnects automatically. Calls onNewGame when
 * the snapshot's game_id changes (used to re-fetch the user's claimed team).
 */
export function useGameState(onNewGame?: () => void): GameState {
  const [snapshot, setSnapshot] = useState<GameSnapshot | null>(null);
  const [offline, setOffline] = useState(true);
  const lastGameId = useRef<number | null>(null);
  const onNewGameRef = useRef(onNewGame);
  onNewGameRef.current = onNewGame;

  useEffect(() => {
    let closed = false;
    let ws: WebSocket | null = null;
    let retry: ReturnType<typeof setTimeout> | null = null;

    const connect = () => {
      ws = openStateSocket();
      ws.onmessage = (ev) => {
        const msg: StateMessage = JSON.parse(ev.data);
        if (msg.type === "offline") {
          setOffline(true);
          setSnapshot(null);
          return;
        }
        setOffline(false);
        setSnapshot(msg.snapshot);
        if (
          lastGameId.current !== null &&
          msg.snapshot.game_id !== lastGameId.current
        ) {
          onNewGameRef.current?.();
        }
        lastGameId.current = msg.snapshot.game_id;
      };
      ws.onclose = () => {
        if (closed) return;
        setOffline(true);
        // Reconnect after a short delay.
        retry = setTimeout(connect, 1000);
      };
      ws.onerror = () => ws?.close();
    };

    connect();
    return () => {
      closed = true;
      if (retry) clearTimeout(retry);
      ws?.close();
    };
  }, []);

  return { snapshot, offline };
}
