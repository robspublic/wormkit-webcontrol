"""Persistence for the team-number <-> user-email mapping (SQLite).

Teams are identified by their in-game team NUMBER (0-based slot), because the
DLL can read team numbers reliably but not team names. Users claim the team
that is currently taking its turn (see the claim flow in main.py), so they
never need to know the number themselves.
"""

from __future__ import annotations

import sqlite3
import threading


class MappingStore:
    """Thread-safe store mapping in-game team numbers to user emails.

    Invariants enforced here:
      - a team number maps to exactly one email (PRIMARY KEY);
      - one team per user (UNIQUE on email).
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
            # Drop any legacy-schema table (the mapping was team-name keyed in an
            # earlier version). Mappings are ephemeral per-game claims, so there
            # is nothing to migrate — recreating with the current schema is safe.
            cols = self._conn.execute(
                "SELECT name FROM pragma_table_info('team_mapping')"
            ).fetchall()
            if cols and not any(row["name"] == "team_number" for row in cols):
                self._conn.execute("DROP TABLE team_mapping")

            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS team_mapping (
                    team_number INTEGER PRIMARY KEY,
                    email       TEXT NOT NULL UNIQUE
                )
                """
            )
            self._conn.commit()

    def claim(self, team_number: int, email: str) -> bool:
        """Atomically map an unclaimed team to a user who has no team yet.

        Returns True if the claim succeeded, False if the team is already
        claimed or the user already owns a team (either violates a constraint).
        """
        email = email.lower()
        with self._lock:
            try:
                self._conn.execute(
                    "INSERT INTO team_mapping(team_number, email) VALUES(?, ?)",
                    (team_number, email),
                )
                self._conn.commit()
                return True
            except sqlite3.IntegrityError:
                # team_number already mapped, or email already owns a team.
                self._conn.rollback()
                return False

    def delete_mapping(self, team_number: int) -> None:
        with self._lock:
            self._conn.execute(
                "DELETE FROM team_mapping WHERE team_number = ?", (team_number,)
            )
            self._conn.commit()

    def clear_all(self) -> None:
        """Remove every mapping (admin action, lets everyone re-claim)."""
        with self._lock:
            self._conn.execute("DELETE FROM team_mapping")
            self._conn.commit()

    def all_mappings(self) -> dict[int, str]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT team_number, email FROM team_mapping"
            ).fetchall()
        return {row["team_number"]: row["email"] for row in rows}

    def email_for_team(self, team_number: int) -> str | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT email FROM team_mapping WHERE team_number = ?",
                (team_number,),
            ).fetchone()
        return row["email"] if row else None

    def team_for_email(self, email: str) -> int | None:
        """The team number this user has claimed, or None (one team per user)."""
        with self._lock:
            row = self._conn.execute(
                "SELECT team_number FROM team_mapping WHERE email = ?",
                (email.lower(),),
            ).fetchone()
        return row["team_number"] if row else None

    def close(self) -> None:
        with self._lock:
            self._conn.close()
