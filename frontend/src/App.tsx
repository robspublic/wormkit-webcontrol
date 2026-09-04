import { useCallback, useEffect, useState } from "react";
import { Link, Outlet } from "react-router-dom";

import { api, type Me } from "./api";
import { useWakeLock } from "./useWakeLock";

export interface AppContext {
  me: Me | null;
  refreshMe: () => void;
}

export default function App() {
  const [me, setMe] = useState<Me | null>(null);
  const [error, setError] = useState<string | null>(null);

  // Keep the screen awake on every page (it's a game controller).
  useWakeLock();

  const refreshMe = useCallback(() => {
    api
      .me()
      .then(setMe)
      .catch((e) => setError(String(e)));
  }, []);

  useEffect(refreshMe, [refreshMe]);

  return (
    <div className="app">
      <header className="topbar">
        <span className="brand">Worms Web Control</span>
        <nav>
          <Link to="/">Play</Link>
          {me?.is_admin && <Link to="/monitor">Monitor</Link>}
        </nav>
        <span className="who">{me ? me.email : "…"}</span>
      </header>

      {error && <div className="banner error">Not signed in: {error}</div>}

      <main>
        <Outlet context={{ me, refreshMe } satisfies AppContext} />
      </main>
    </div>
  );
}
