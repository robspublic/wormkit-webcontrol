"""Backend configuration via environment variables (WKWC_ prefix)."""

from __future__ import annotations

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="WKWC_", env_file=".env")

    # Named pipe the DLL listens on (production, Windows).
    pipe_name: str = r"\\.\pipe\wkwebcontrol"

    # Force the in-process mock transport (dev on Linux, tests). When False,
    # a Windows named pipe is used only if running on win32.
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
