"""Test isolation: make the suite hermetic regardless of a local backend/.env.

A developer/prod `backend/.env` (e.g. with WKWC_ADMIN_EMAILS or
WKWC_USE_MOCK_IPC=0) must not change test behavior. We force the mock transport,
an isolated DB, and an empty admin list (so the tests' admin@ user is treated as
admin) before the app/config modules load.
"""

from __future__ import annotations

import os

# Set before importing app.config (Settings reads env at construction time).
os.environ["WKWC_USE_MOCK_IPC"] = "1"
os.environ["WKWC_ADMIN_EMAILS"] = ""
os.environ.setdefault("WKWC_DB_PATH", "/tmp/wkwc-test.db")
# Ignore any ambient .env file so it can't leak prod settings into tests.
os.environ["WKWC_ENV_FILE_IGNORE"] = "1"
