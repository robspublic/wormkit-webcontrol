"""Backend configuration via environment variables (WKWC_ prefix)."""

from __future__ import annotations

import os

from pydantic_settings import BaseSettings, SettingsConfigDict

# Tests set WKWC_ENV_FILE_IGNORE=1 so a local backend/.env can't leak prod
# settings (admin list, transport) into the suite. In normal runs .env is read.
_ENV_FILE = None if os.environ.get("WKWC_ENV_FILE_IGNORE") else ".env"


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="WKWC_", env_file=_ENV_FILE)

    # Host/port of the DLL's TCP IPC server. The DLL listens on 127.0.0.1 by
    # default (same host: WA runs under Wine/Proton on this Linux box).
    game_host: str = "127.0.0.1"
    game_port: int = 27099

    # Force the in-process mock transport (dev without the game running / tests).
    use_mock_ipc: bool = False

    # SQLite file for the team<->email mapping.
    db_path: str = "wkwebcontrol.db"

    # Path to the built frontend (frontend/dist). When present, the backend
    # serves the SPA at / so nginx can proxy a single origin. Relative paths
    # resolve against the backend working directory.
    frontend_dist: str = "../frontend/dist"

    # Header set by the trusted oauth-proxy / nginx layer carrying the
    # authenticated user's email. Any client-supplied copy is ignored: the
    # backend must sit behind the proxy so this header cannot be spoofed.
    auth_email_header: str = "X-Auth-Email"

    # Emails allowed to use the admin API (comma-separated). Empty = allow any
    # authenticated user (dev convenience only).
    admin_emails: str = ""

    def admin_email_set(self) -> set[str]:
        return {e.strip().lower() for e in self.admin_emails.split(",") if e.strip()}


settings = Settings()
