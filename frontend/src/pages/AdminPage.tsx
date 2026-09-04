import { useEffect, useState } from "react";

import { api, type Mappings } from "../api";

export default function AdminPage() {
  const [mappings, setMappings] = useState<Mappings>({});
  const [team, setTeam] = useState("");
  const [email, setEmail] = useState("");
  const [error, setError] = useState<string | null>(null);

  function refresh() {
    api
      .listMappings()
      .then(setMappings)
      .catch((e) => setError(String(e)));
  }

  useEffect(refresh, []);

  async function onAdd(e: React.FormEvent) {
    e.preventDefault();
    setError(null);
    if (!team.trim() || !email.trim()) return;
    try {
      await api.setMapping(team.trim(), email.trim());
      setTeam("");
      setEmail("");
      refresh();
    } catch (err) {
      setError(String(err));
    }
  }

  async function onDelete(t: string) {
    setError(null);
    try {
      await api.deleteMapping(t);
      refresh();
    } catch (err) {
      setError(String(err));
    }
  }

  const entries = Object.entries(mappings);

  return (
    <div className="admin">
      <h2>Team → player mapping</h2>
      <p className="hint">
        Map each in-game team name to the email of the player who controls it.
      </p>

      {error && <div className="banner error">{error}</div>}

      <form className="mapping-form" onSubmit={onAdd}>
        <input
          placeholder="Team name (as in Worms)"
          value={team}
          onChange={(e) => setTeam(e.target.value)}
        />
        <input
          placeholder="player@example.com"
          type="email"
          value={email}
          onChange={(e) => setEmail(e.target.value)}
        />
        <button type="submit" className="btn">
          Save
        </button>
      </form>

      <table className="mapping-table">
        <thead>
          <tr>
            <th>Team</th>
            <th>Email</th>
            <th />
          </tr>
        </thead>
        <tbody>
          {entries.length === 0 && (
            <tr>
              <td colSpan={3} className="empty">
                No mappings yet.
              </td>
            </tr>
          )}
          {entries.map(([t, e]) => (
            <tr key={t}>
              <td>{t}</td>
              <td>{e}</td>
              <td>
                <button className="btn danger" onClick={() => onDelete(t)}>
                  Remove
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
