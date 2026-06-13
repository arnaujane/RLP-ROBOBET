from __future__ import annotations

from sqlite3 import Connection

from ..config import DEFAULT_STARTING_BALANCE
from ..database import row_to_dict


def available_user_name(conn: Connection, preferred: str, login: str) -> str:
    base = (preferred or login).strip()[:32] or login[:32]
    candidate = base
    suffix = 2
    while conn.execute("SELECT id FROM users WHERE name = ?", (candidate,)).fetchone():
        tail = f"_{suffix}"
        candidate = f"{base[:32 - len(tail)]}{tail}"
        suffix += 1
    return candidate


def upsert_twitch_user(
    conn: Connection,
    *,
    twitch_id: str | None,
    login: str,
    display_name: str | None,
    profile_image_url: str | None,
) -> dict:
    normalized_login = login.strip().lower()
    normalized_display_name = (display_name or login).strip()
    if not normalized_login:
        raise ValueError("Twitch login cannot be empty")

    existing = conn.execute("SELECT * FROM users WHERE twitch_login = ?", (normalized_login,)).fetchone()
    if existing:
        conn.execute(
            """
            UPDATE users
            SET twitch_id = ?, twitch_display_name = ?, profile_image_url = ?
            WHERE id = ?
            """,
            (twitch_id, normalized_display_name, profile_image_url, existing["id"]),
        )
        return row_to_dict(conn.execute("SELECT * FROM users WHERE id = ?", (existing["id"],)).fetchone())

    name = available_user_name(conn, normalized_display_name, normalized_login)
    cur = conn.execute(
        """
        INSERT INTO users(name, twitch_id, twitch_login, twitch_display_name, profile_image_url, balance)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (name, twitch_id, normalized_login, normalized_display_name, profile_image_url, DEFAULT_STARTING_BALANCE),
    )
    return row_to_dict(conn.execute("SELECT * FROM users WHERE id = ?", (cur.lastrowid,)).fetchone())
