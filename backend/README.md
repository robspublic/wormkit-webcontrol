# wkWebControl backend (FastAPI)

Serves the admin + player web app and bridges browser input to the
wkWebControl DLL over a named pipe.

## Setup

```
python -m venv .venv
.venv/bin/pip install -e ".[dev]"
cp .env.example .env      # defaults to the mock transport (no game needed)
```

## Run (dev, Linux, mock transport)

```
.venv/bin/uvicorn app.main:app --reload --port 8000
```

With `WKWC_USE_MOCK_IPC=1` (default in `.env.example`) the backend runs without
the game: commands are recorded in-process and `/api/turn` returns a canned
state. On the production Windows host, set `WKWC_USE_MOCK_IPC=0` and the backend
talks to the DLL over `WKWC_PIPE_NAME`.

> The Windows named-pipe transport needs `pywin32`, which is only installed on
> Windows. It is intentionally **not** a hard dependency so the backend installs
> and runs on Linux for development.

## Test

```
.venv/bin/pytest
```

## Auth model

Identity comes only from the `X-Auth-Email` header set by your trusted
oauth-proxy / nginx. The backend must sit behind that proxy so the header can't
be spoofed by clients. `WKWC_ADMIN_EMAILS` restricts the admin API.

## Team identity and the claim flow

The DLL identifies teams by **number** (0-based slot), not name — team names
have no reliable memory offset. Users never pick a number: when the current
turn belongs to an unclaimed team, an unmapped user claims it via
`POST /api/claim` ("This is my team"). One team per user. Admins can clear all
claims (`POST /api/admin/clear-mappings`) so everyone re-claims.

## Endpoints

| Method | Path | Auth | Purpose |
|--------|------|------|---------|
| GET | `/api/health` | none | liveness |
| GET | `/api/me` | user | email, claimed team number, is_admin |
| GET | `/api/turn` | user | narrow turn state (from DLL) |
| GET | `/api/monitor` | user | full game snapshot (from DLL) |
| POST | `/api/claim` | user | claim the team currently taking its turn |
| GET | `/api/admin/mappings` | admin | list team#→email |
| DELETE | `/api/admin/mappings/{team_number}` | admin | remove one claim |
| POST | `/api/admin/clear-mappings` | admin | clear all claims |
| WS | `/ws/control` | user | send control actions (turn-gated) |

Admins are the emails in `WKWC_ADMIN_EMAILS` (empty list = every authenticated
user is admin, for dev convenience).
```
