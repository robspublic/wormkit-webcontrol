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

## Endpoints

| Method | Path | Auth | Purpose |
|--------|------|------|---------|
| GET | `/api/health` | none | liveness |
| GET | `/api/me` | user | email + owned teams |
| GET | `/api/turn` | user | current turn state (from DLL) |
| GET | `/api/admin/mappings` | admin | list team→email |
| PUT | `/api/admin/mappings/{team}` | admin | set mapping |
| DELETE | `/api/admin/mappings/{team}` | admin | remove mapping |
| WS | `/ws/control` | user | send control actions (turn-gated) |
```
