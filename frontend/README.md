# wkWebControl frontend (Vite + React + TS)

Two pages:

- **Play** (`/`) — the player control pad. Polls `/api/turn`, opens the
  `/ws/control` WebSocket, and enables the move/aim/weapon/fire buttons only
  when the current turn belongs to one of the signed-in user's teams.
- **Admin** (`/admin`) — CRUD for the team-name → player-email mapping via
  `/api/admin/mappings`.

Identity is provided by the fronting oauth-proxy/nginx via `X-Auth-Email`; the
frontend never sets it. `/api/me` reports the current user and their teams.

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
