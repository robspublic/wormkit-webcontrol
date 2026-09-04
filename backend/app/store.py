"""Persistence for the team-name <-> user-email mapping (SQLite)."""

from __future__ import annotations

import sqlite3
import threading


class MappingStore:
    """Thread-safe store mapping in-game team names to user emails.

    A team name maps to exactly one email (the player who controls it). An
    email may own several teams.
    """

    def __init__(self, db_path: str) -> None:
        self._lock = threading.RLock()
        # check_same_thread=False: guarded by our own lock, used from the
        # request threads and the WebSocket loop.
        self._conn = sqlite3.connect(db_path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self) -> None:
        with self._lock:
            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS team_mapping (
                    team_name TEXT PRIMARY KEY,
                    email     TEXT NOT NULL
                )
                """
            )
            self._conn.commit()

    def set_mapping(self, team_name: str, email: str) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT INTO team_mapping(team_name, email) VALUES(?, ?) "
                "ON CONFLICT(team_name) DO UPDATE SET email = excluded.email",
                (team_name, email.lower()),
            )
            self._conn.commit()

    def delete_mapping(self, team_name: str) -> None:
        with self._lock:
            self._conn.execute(
                "DELETE FROM team_mapping WHERE team_name = ?", (team_name,)
            )
            self._conn.commit()

    def all_mappings(self) -> dict[str, str]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT team_name, email FROM team_mapping"
            ).fetchall()
        return {row["team_name"]: row["email"] for row in rows}

    def email_for_team(self, team_name: str) -> str | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT email FROM team_mapping WHERE team_name = ?", (team_name,)
            ).fetchone()
        return row["email"] if row else None

    def teams_for_email(self, email: str) -> list[str]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT team_name FROM team_mapping WHERE email = ?",
                (email.lower(),),
            ).fetchall()
        return [row["team_name"] for row in rows]

    def close(self) -> None:
        with self._lock:
            self._conn.close()
