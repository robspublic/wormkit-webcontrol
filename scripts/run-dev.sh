#!/usr/bin/env bash
#
# run-dev.sh - start everything needed for local frontend development:
#   1. the FastAPI backend  (uvicorn, port 8000)
#   2. the Vite dev server  (which proxies /api and /ws to the backend)
#
# The frontend is useless without the backend it proxies to, so this launches
# both. The backend runs in the background; the frontend runs in the foreground.
# Ctrl-C stops the frontend and this script then tears the backend down too.
#
# On Linux the backend uses the mock IPC transport by default (no game / no
# Windows needed). Override with WKWC_USE_MOCK_IPC=0 in the environment.
#
# Usage:
#   scripts/run-dev.sh [--host] [--frontend-port <n>] [--backend-port <n>]
#
#   --host                Expose the Vite dev server on the LAN (phones can connect).
#   --frontend-port <n>   Vite port (default: Vite's 5173).
#   --backend-port <n>    uvicorn port (default: 8000). The Vite proxy target is
#                         kept in sync automatically (via WKWC_BACKEND_PORT).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BACKEND_DIR="${REPO_ROOT}/backend"
FRONTEND_DIR="${REPO_ROOT}/frontend"

# ---- args --------------------------------------------------------------------
BACKEND_PORT=8000
VITE_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) VITE_ARGS+=("--host"); shift ;;
    --frontend-port) VITE_ARGS+=("--port" "${2:?--frontend-port needs a value}"); shift 2 ;;
    --backend-port)  BACKEND_PORT="${2:?--backend-port needs a value}"; shift 2 ;;
    -h|--help)
      grep '^#' "$0" | sed 's/^# \{0,1\}//' | sed '1d'
      exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 2 ;;
  esac
done

# ---- preflight ---------------------------------------------------------------
command -v python3 >/dev/null 2>&1 || { echo "error: python3 not found." >&2; exit 1; }
command -v npm >/dev/null 2>&1 || { echo "error: npm not found (install Node.js)." >&2; exit 1; }

# ---- backend setup -----------------------------------------------------------
cd "${BACKEND_DIR}"
if [[ ! -x .venv/bin/uvicorn ]]; then
  echo "Backend venv missing or incomplete; creating and installing…"
  python3 -m venv .venv
  .venv/bin/pip install --quiet --upgrade pip
  .venv/bin/pip install --quiet -e ".[dev]"
fi

# Default to the mock transport on non-Windows unless the caller overrode it.
export WKWC_USE_MOCK_IPC="${WKWC_USE_MOCK_IPC:-1}"

# ---- frontend setup ----------------------------------------------------------
if [[ ! -d "${FRONTEND_DIR}/node_modules" ]]; then
  echo "Frontend node_modules missing; installing…"
  (cd "${FRONTEND_DIR}" && npm install)
fi

# ---- launch backend (background) --------------------------------------------
echo "Starting backend on http://localhost:${BACKEND_PORT} (WKWC_USE_MOCK_IPC=${WKWC_USE_MOCK_IPC})…"
.venv/bin/uvicorn app.main:app --port "${BACKEND_PORT}" &
BACKEND_PID=$!

# Tear the backend down whenever this script exits (Ctrl-C, error, or normal).
cleanup() {
  if kill -0 "${BACKEND_PID}" 2>/dev/null; then
    echo
    echo "Stopping backend (pid ${BACKEND_PID})…"
    kill "${BACKEND_PID}" 2>/dev/null || true
    wait "${BACKEND_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

# Give uvicorn a moment and confirm it actually came up.
sleep 1
if ! kill -0 "${BACKEND_PID}" 2>/dev/null; then
  echo "error: backend failed to start (see output above)." >&2
  exit 1
fi

# ---- launch frontend (foreground) -------------------------------------------
echo "Starting Vite dev server (proxying /api and /ws to the backend)…"
cd "${FRONTEND_DIR}"
# Keep the Vite proxy target in sync with the backend port we chose.
export WKWC_BACKEND_PORT="${BACKEND_PORT}"
# Expand the args array safely even when empty (set -u compatible).
npm run dev -- ${VITE_ARGS[@]+"${VITE_ARGS[@]}"}
