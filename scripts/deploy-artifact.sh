#!/usr/bin/env bash
#
# deploy-artifact.sh - install the built wkWebControl.dll into the Worms
# Armageddon folder, backing up any currently deployed DLL first.
#
# The existing wkWebControl.dll (if present) is moved to
#   wkWebControl.dll.YYYYMMDD.bak
# (a time suffix is appended if a same-day backup already exists, so an earlier
# deploy on the same day is never clobbered).
#
# The example ini is installed as wkWebControl.ini only if no ini exists yet -
# an already-present config you've tuned is left untouched.
#
# Usage:
#   scripts/deploy-artifact.sh [--wa-dir <path>]
#
#   --wa-dir <path>   Override the Worms Armageddon install dir.
#
# Source of the DLL is gitHubArtifacts/ (populate it with fetch-artifact.sh).

set -euo pipefail

# ---- config ------------------------------------------------------------------
DLL_NAME="wkWebControl.dll"
INI_NAME="wkWebControl.ini"
INI_EXAMPLE="wkWebControl.ini.example"
DEFAULT_WA_DIR="/mnt/linux-hdd/steam/steamapps/common/Worms Armageddon"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${REPO_ROOT}/gitHubArtifacts"

# ---- args --------------------------------------------------------------------
WA_DIR="${DEFAULT_WA_DIR}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wa-dir) WA_DIR="${2:-}"; shift 2 ;;
    -h|--help)
      grep '^#' "$0" | sed 's/^# \{0,1\}//' | sed '1d'
      exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 2 ;;
  esac
done

SRC_DLL="${ARTIFACT_DIR}/${DLL_NAME}"

# ---- preflight ---------------------------------------------------------------
if [[ ! -f "${SRC_DLL}" ]]; then
  echo "error: ${SRC_DLL} not found." >&2
  echo "       Fetch a build first: scripts/fetch-artifact.sh" >&2
  exit 1
fi

if [[ ! -d "${WA_DIR}" ]]; then
  echo "error: Worms Armageddon dir not found: ${WA_DIR}" >&2
  echo "       Pass the correct path with --wa-dir <path>." >&2
  exit 1
fi

# Sanity: make sure this really looks like the WA install.
if [[ ! -f "${WA_DIR}/WA.exe" ]]; then
  echo "error: ${WA_DIR} does not contain WA.exe; refusing to deploy there." >&2
  echo "       Pass the correct path with --wa-dir <path>." >&2
  exit 1
fi

DEST_DLL="${WA_DIR}/${DLL_NAME}"

# ---- back up the currently deployed DLL --------------------------------------
if [[ -f "${DEST_DLL}" ]]; then
  backup="${DEST_DLL}.$(date +%Y%m%d).bak"
  # Avoid clobbering an earlier same-day backup: append HHMMSS if it exists.
  if [[ -e "${backup}" ]]; then
    backup="${DEST_DLL}.$(date +%Y%m%d-%H%M%S).bak"
  fi
  mv -- "${DEST_DLL}" "${backup}"
  echo "Backed up existing DLL -> ${backup}"
else
  echo "No existing ${DLL_NAME} to back up."
fi

# ---- deploy ------------------------------------------------------------------
cp -- "${SRC_DLL}" "${DEST_DLL}"
echo "Deployed ${DLL_NAME} -> ${DEST_DLL}"

# Install the example ini only if there's no config already.
DEST_INI="${WA_DIR}/${INI_NAME}"
SRC_INI="${ARTIFACT_DIR}/${INI_EXAMPLE}"
if [[ ! -f "${DEST_INI}" ]]; then
  if [[ -f "${SRC_INI}" ]]; then
    cp -- "${SRC_INI}" "${DEST_INI}"
    echo "Installed default config -> ${DEST_INI}"
  else
    echo "note: no ${INI_EXAMPLE} in ${ARTIFACT_DIR}; skipping ini install." >&2
  fi
else
  echo "Existing ${INI_NAME} left untouched."
fi

echo
echo "Deploy complete. wk* files in the WA dir:"
ls -la "${WA_DIR}"/wk* 2>/dev/null || true
