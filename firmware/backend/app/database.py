from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from typing import Any, Iterable

from .config import DATA_DIR, DB_PATH


def _connect() -> sqlite3.Connection:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


@contextmanager
def db() -> Iterable[sqlite3.Connection]:
    conn = _connect()
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()


def row_to_dict(row: sqlite3.Row | None) -> dict[str, Any] | None:
    return dict(row) if row is not None else None


def rows_to_dicts(rows: Iterable[sqlite3.Row]) -> list[dict[str, Any]]:
    return [dict(row) for row in rows]


def _table_columns(conn: sqlite3.Connection, table: str) -> set[str]:
    return {row["name"] for row in conn.execute(f"PRAGMA table_info({table})").fetchall()}


def _ensure_column(conn: sqlite3.Connection, table: str, name: str, definition: str) -> None:
    if name not in _table_columns(conn, table):
        conn.execute(f"ALTER TABLE {table} ADD COLUMN {name} {definition}")


def init_db() -> None:
    with db() as conn:
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                twitch_id TEXT,
                twitch_login TEXT,
                twitch_display_name TEXT,
                profile_image_url TEXT,
                balance INTEGER NOT NULL DEFAULT 1000,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS runs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                status TEXT NOT NULL,
                algorithm TEXT NOT NULL DEFAULT 'DFS',
                speed_limit INTEGER NOT NULL DEFAULT 100,
                started_at TEXT,
                finished_at TEXT,
                elapsed_ms INTEGER,
                obstacle_count INTEGER NOT NULL DEFAULT 0,
                crossing_count INTEGER NOT NULL DEFAULT 0,
                restrictions TEXT,
                telemetry_summary TEXT,
                result TEXT
            );

            CREATE TABLE IF NOT EXISTS bets (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER NOT NULL,
                run_id INTEGER,
                kind TEXT NOT NULL,
                choice TEXT NOT NULL,
                amount INTEGER NOT NULL,
                odds REAL NOT NULL DEFAULT 2.0,
                status TEXT NOT NULL DEFAULT 'open',
                payout INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY(user_id) REFERENCES users(id)
            );

            CREATE TABLE IF NOT EXISTS polls (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                kind TEXT NOT NULL,
                title TEXT NOT NULL,
                options TEXT NOT NULL,
                status TEXT NOT NULL DEFAULT 'open',
                winner TEXT,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                closed_at TEXT
            );

            CREATE TABLE IF NOT EXISTS votes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                poll_id INTEGER NOT NULL,
                user_id INTEGER NOT NULL,
                choice TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(poll_id, user_id),
                FOREIGN KEY(poll_id) REFERENCES polls(id),
                FOREIGN KEY(user_id) REFERENCES users(id)
            );

            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                source TEXT NOT NULL,
                type TEXT NOT NULL,
                payload TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            """
        )
        _ensure_column(conn, "users", "twitch_id", "TEXT")
        _ensure_column(conn, "users", "twitch_login", "TEXT")
        _ensure_column(conn, "users", "twitch_display_name", "TEXT")
        _ensure_column(conn, "users", "profile_image_url", "TEXT")
        conn.execute("CREATE UNIQUE INDEX IF NOT EXISTS idx_users_twitch_login ON users(twitch_login) WHERE twitch_login IS NOT NULL")
        _ensure_column(conn, "runs", "crossing_count", "INTEGER NOT NULL DEFAULT 0")
        _ensure_column(conn, "runs", "restrictions", "TEXT")
        _ensure_column(conn, "runs", "telemetry_summary", "TEXT")

