import { useEffect, useState } from "react";
import { Link, Outlet } from "react-router-dom";

import { api, type Me } from "./api";

export default function App() {
  const [me, setMe] = useState<Me | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api
      .me()
      .then(setMe)
      .catch((e) => setError(String(e)));
  }, []);

  return (
    <div className="app">
      <header className="topbar">
        <span className="brand">Worms Web Control</span>
        <nav>
          <Link to="/">Play</Link>
          <Link to="/admin">Admin</Link>
        </nav>
        <span className="who">{me ? me.email : "…"}</span>
      </header>

      {error && <div className="banner error">Not signed in: {error}</div>}

      <main>
        <Outlet context={{ me }} />
      </main>
    </div>
  );
}
