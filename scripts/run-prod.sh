#!/usr/bin/env bash
#
# run-prod.sh - run the production web app on this machine (the one running WA).
#
# Builds the frontend, then starts the FastAPI backend on port 8000 serving both
# the API/WebSocket and the built SPA (single origin). nginx
# (worms.operimentum.com) proxies to http://<this-host>:8000 and injects the
# X-Auth-Email header.
#
# Unlike run-dev.sh, this uses the REAL named-pipe IPC transport (mock OFF), so
# the backend talks to the wkWebControl DLL inside WA.exe. That means it is only
# meaningful on the Windows machine running the game with the DLL loaded.
#
# Usage:
#   scripts/run-prod.sh [--port <n>] [--host <addr>]
#
#   --port <n>    Backend/app port (default: 8000; must match the nginx upstream).
#   --host <addr> Bind address (default: 0.0.0.0 so nginx on another host can reach it).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BACKEND_DIR="${REPO_ROOT}/backend"
FRONTEND_DIR="${REPO_ROOT}/frontend"

# ---- args --------------------------------------------------------------------
PORT=8000
BIND="0.0.0.0"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="${2:?--port needs a value}"; shift 2 ;;
    --host) BIND="${2:?--host needs a value}"; shift 2 ;;
    -h|--help)
      grep '^#' "$0" | sed 's/^# \{0,1\}//' | sed '1d'
      exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 2 ;;
  esac
done

# ---- preflight ---------------------------------------------------------------
command -v python3 >/dev/null 2>&1 || { echo "error: python3 not found." >&2; exit 1; }
command -v npm >/dev/null 2>&1 || { echo "error: npm not found (install Node.js)." >&2; exit 1; }

# ---- build the frontend ------------------------------------------------------
echo "Building frontend (frontend/dist)…"
cd "${FRONTEND_DIR}"
if [[ ! -d node_modules ]]; then
  npm install
fi
npm run build

# ---- backend venv ------------------------------------------------------------
cd "${BACKEND_DIR}"
if [[ ! -x .venv/bin/uvicorn ]]; then
  echo "Backend venv missing or incomplete; creating and installing…"
  python3 -m venv .venv
  .venv/bin/pip install --quiet --upgrade pip
  .venv/bin/pip install --quiet -e ".[dev]"
fi

# ---- run ---------------------------------------------------------------------
# Real named-pipe transport to the DLL (mock OFF). On non-Windows the transport
# factory still falls back to the mock, so this is only fully live on the game
# host; elsewhere it runs but can't reach the DLL.
export WKWC_USE_MOCK_IPC="${WKWC_USE_MOCK_IPC:-0}"

echo "Starting production app on http://${BIND}:${PORT} (WKWC_USE_MOCK_IPC=${WKWC_USE_MOCK_IPC})…"
echo "nginx (worms.operimentum.com) should proxy to this host:${PORT}."
exec .venv/bin/uvicorn app.main:app --host "${BIND}" --port "${PORT}"
