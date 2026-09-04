#!/usr/bin/env bash
#
# fetch-artifact.sh - download the latest wkWebControl.dll build artifact from
# GitHub Actions into gitHubArtifacts/.
#
# Artifacts live in GitHub's Actions storage (not git), so this uses the
# GitHub CLI (`gh`) rather than the git/SSH remote.
#
# Usage:
#   scripts/fetch-artifact.sh [--wait] [--run <run-id>]
#
#   --wait          Wait for the latest run to finish before downloading.
#   --run <id>      Download from a specific run id (default: latest run).
#
# Requires: gh (https://cli.github.com), authenticated via `gh auth login`.

set -euo pipefail

# ---- config ------------------------------------------------------------------
REPO="robspublic/wormkit-webcontrol"
ARTIFACT_NAME="wkWebControl"
WORKFLOW="build-dll.yml"
# Files the artifact is expected to contain (cleared before re-download, since
# `gh run download` won't overwrite existing files).
DLL_NAME="wkWebControl.dll"
INI_EXAMPLE="wkWebControl.ini.example"

# Resolve the output dir relative to the repo root (this script lives in scripts/).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${REPO_ROOT}/gitHubArtifacts"

# ---- args --------------------------------------------------------------------
WAIT=0
RUN_ID=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wait) WAIT=1; shift ;;
    --run)  RUN_ID="${2:-}"; shift 2 ;;
    -h|--help)
      grep '^#' "$0" | sed 's/^# \{0,1\}//' | sed '1d'
      exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 2 ;;
  esac
done

# ---- preflight ---------------------------------------------------------------
if ! command -v gh >/dev/null 2>&1; then
  echo "error: 'gh' (GitHub CLI) is not installed." >&2
  echo "       Install it (e.g. 'sudo apt install gh') then run 'gh auth login'." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "error: gh is not authenticated. Run: gh auth login" >&2
  exit 1
fi

# ---- resolve the run ---------------------------------------------------------
if [[ -z "${RUN_ID}" ]]; then
  # Latest run of our workflow (any status).
  RUN_ID="$(gh run list --repo "${REPO}" --workflow "${WORKFLOW}" \
              --limit 1 --json databaseId --jq '.[0].databaseId')"
  if [[ -z "${RUN_ID}" || "${RUN_ID}" == "null" ]]; then
    echo "error: no runs found for workflow ${WORKFLOW} in ${REPO}." >&2
    exit 1
  fi
fi
echo "Latest run: ${RUN_ID}"

# ---- optionally wait for completion -----------------------------------------
if [[ "${WAIT}" -eq 1 ]]; then
  echo "Waiting for run ${RUN_ID} to finish…"
  # `gh run watch` exits non-zero if the run concludes in failure; we surface it.
  gh run watch "${RUN_ID}" --repo "${REPO}" --exit-status || {
    echo "error: run ${RUN_ID} did not succeed; not downloading." >&2
    exit 1
  }
fi

# ---- download ----------------------------------------------------------------
mkdir -p "${OUT_DIR}"

# `gh run download` refuses to overwrite existing files, so clear any prior copy
# of this artifact's contents first. We remove the specific files we know the
# artifact contains rather than wiping the whole dir.
for f in "${DLL_NAME}" "${INI_EXAMPLE}"; do
  if [[ -e "${OUT_DIR}/${f}" ]]; then
    rm -f -- "${OUT_DIR}/${f}"
  fi
done

echo "Downloading '${ARTIFACT_NAME}' from run ${RUN_ID} into ${OUT_DIR}…"
# gh extracts the artifact zip; --dir places files directly under OUT_DIR.
gh run download "${RUN_ID}" --repo "${REPO}" --name "${ARTIFACT_NAME}" --dir "${OUT_DIR}"

echo
echo "Done. Contents of ${OUT_DIR}:"
ls -la "${OUT_DIR}"
