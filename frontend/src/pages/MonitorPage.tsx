import { useEffect, useState } from "react";

import { api, fmtPos, type Mappings } from "../api";
import { useGameState } from "../useGameState";

// Team claims change rarely; poll them on a slow timer (state comes via WS).
const MAPPINGS_POLL_MS = 2000;

export default function MonitorPage() {
  const { snapshot: snap, offline } = useGameState();
  const [mappings, setMappings] = useState<Mappings>({});
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;
    const tick = () => {
      api
        .listMappings()
        .then((m) => active && setMappings(m))
        .catch(() => {});
    };
    tick();
    const id = setInterval(tick, MAPPINGS_POLL_MS);
    return () => {
      active = false;
      clearInterval(id);
    };
  }, []);

  async function onClear() {
    setError(null);
    if (!confirm("Clear all team claims? Everyone will need to re-claim.")) return;
    try {
      await api.clearMappings();
    } catch (e) {
      setError(String(e));
    }
  }

  const gameState = !snap
    ? "…"
    : !snap.round_active
      ? "No game running"
      : snap.before_round_start
        ? "Placing worms…"
        : "Round active";

  return (
    <div className="monitor">
      <div className="monitor-head">
        <h2>Game monitor</h2>
        <button className="btn danger" onClick={onClear}>
          Clear team claims
        </button>
      </div>

      {offline && <div className="banner waiting-banner">Waiting for game connection</div>}
      {error && <div className="banner error">{error}</div>}

      <dl className="game-stats">
        <div>
          <dt>State</dt>
          <dd>{gameState}</dd>
        </div>
        <div>
          <dt>Teams</dt>
          <dd>{snap?.num_teams ?? "—"}</dd>
        </div>
        <div>
          <dt>Turn team</dt>
          <dd>{snap?.turn_team ?? "—"}</dd>
        </div>
        <div>
          <dt>Turn time</dt>
          <dd>{snap?.turn_time_ms != null ? `${Math.round(snap.turn_time_ms / 1000)}s` : "—"}</dd>
        </div>
      </dl>

      {snap?.teams?.length ? (
        snap.teams.map((t) => (
          <div
            key={t.team_number}
            className={`team-card ${t.is_turn_holder ? "turn" : ""}`}
          >
            <div className="team-head">
              <strong>Team {t.team_number}</strong>
              {t.is_turn_holder && <span className="tag turn-tag">TURN</span>}
              {t.is_local && <span className="tag">local</span>}
              <span className="claimed">
                {mappings[String(t.team_number)]
                  ? `claimed by ${mappings[String(t.team_number)]}`
                  : "unclaimed"}
              </span>
            </div>
            <table className="worm-table">
              <thead>
                <tr>
                  <th>Worm</th>
                  <th>Active</th>
                  <th>Pos (x, y)</th>
                  <th>Weapon</th>
                  <th>Facing</th>
                </tr>
              </thead>
              <tbody>
                {t.worms.map((w) => (
                  <tr key={w.worm} className={w.worm === t.current_worm ? "current" : ""}>
                    <td>{w.worm}</td>
                    <td>{w.active ? "●" : ""}</td>
                    <td>
                      {fmtPos(w.pos_x)}, {fmtPos(w.pos_y)}
                    </td>
                    <td>{w.weapon}</td>
                    <td>{w.facing > 0 ? "▶" : w.facing < 0 ? "◀" : "—"}</td>
                  </tr>
                ))}
                {t.worms.length === 0 && (
                  <tr>
                    <td colSpan={5} className="empty">
                      no worms
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        ))
      ) : (
        <p className="hint">No teams to show.</p>
      )}
    </div>
  );
}
