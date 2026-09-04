# wkWebControl frontend (Vite + React + TS)

Two pages:

- **Play** (`/`) — the player control pad. Polls `/api/monitor` for the current
  turn. If the signed-in user hasn't claimed a team yet and a team is taking its
  turn, a **"This is my team"** button claims it (`POST /api/claim`). The
  move/aim/weapon/fire buttons are enabled only when the user's claimed team is
  the one currently taking its turn.
- **Monitor** (`/monitor`, admin-only) — read-only view of game state: whether a
  game is running, teams (turn-holder highlighted, local flag, who claimed each),
  and per-worm position/weapon/facing. Includes a **Clear team claims** button.

Teams are identified by **number** (the DLL can't read names). Identity is
provided by the fronting oauth-proxy/nginx via `X-Auth-Email`; the frontend never
sets it. `/api/me` reports the user's email, claimed team number, and admin flag.

## Setup

```
npm install
```

## Dev

```
npm run dev
```

Vite proxies `/api` and `/ws` to the FastAPI backend at `http://localhost:8000`
(see `vite.config.ts`), so run the backend alongside it.

## Build

```
npm run build      # tsc type-check + vite production build -> dist/
npm run preview    # serve the built dist/ locally
```

In production, serve `dist/` behind the same proxy that fronts the API so the
single-origin `/api` and `/ws` paths resolve and the auth header is injected.
