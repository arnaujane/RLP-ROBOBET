from fastapi import APIRouter, HTTPException

from ..config import DEFAULT_STARTING_BALANCE
from ..database import db, row_to_dict, rows_to_dicts
from ..schemas import TwitchUserCreate, UserCreate

router = APIRouter(prefix="/api/users", tags=["users"])


@router.get("")
def list_users() -> list[dict]:
    with db() as conn:
        return rows_to_dicts(conn.execute("SELECT * FROM users ORDER BY balance DESC, name ASC").fetchall())


@router.post("")
def create_user(payload: UserCreate) -> dict:
    name = payload.name.strip()
    if not name:
        raise HTTPException(400, "Name cannot be empty")
    with db() as conn:
        try:
            cur = conn.execute(
                "INSERT INTO users(name, balance) VALUES (?, ?)",
                (name, DEFAULT_STARTING_BALANCE),
            )
        except Exception:
            existing = conn.execute("SELECT * FROM users WHERE name = ?", (name,)).fetchone()
            if existing:
                return row_to_dict(existing)
            raise
        return row_to_dict(conn.execute("SELECT * FROM users WHERE id = ?", (cur.lastrowid,)).fetchone())


def _available_user_name(conn, preferred: str, login: str) -> str:
    base = (preferred or login).strip()[:32] or login[:32]
    candidate = base
    suffix = 2
    while conn.execute("SELECT id FROM users WHERE name = ?", (candidate,)).fetchone():
        tail = f"_{suffix}"
        candidate = f"{base[:32 - len(tail)]}{tail}"
        suffix += 1
    return candidate


@router.post("/twitch")
def create_or_update_twitch_user(payload: TwitchUserCreate) -> dict:
    login = payload.login.strip().lower()
    display_name = (payload.display_name or payload.login).strip()
    if not login:
        raise HTTPException(400, "Twitch login cannot be empty")

    with db() as conn:
        existing = conn.execute("SELECT * FROM users WHERE twitch_login = ?", (login,)).fetchone()
        if existing:
            conn.execute(
                """
                UPDATE users
                SET twitch_id = ?, twitch_display_name = ?, profile_image_url = ?
                WHERE id = ?
                """,
                (payload.twitch_id, display_name, payload.profile_image_url, existing["id"]),
            )
            return row_to_dict(conn.execute("SELECT * FROM users WHERE id = ?", (existing["id"],)).fetchone())

        name = _available_user_name(conn, display_name, login)
        cur = conn.execute(
            """
            INSERT INTO users(name, twitch_id, twitch_login, twitch_display_name, profile_image_url, balance)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (name, payload.twitch_id, login, display_name, payload.profile_image_url, DEFAULT_STARTING_BALANCE),
        )
        return row_to_dict(conn.execute("SELECT * FROM users WHERE id = ?", (cur.lastrowid,)).fetchone())


@router.get("/{user_id}")
def get_user(user_id: int) -> dict:
    with db() as conn:
        user = conn.execute("SELECT * FROM users WHERE id = ?", (user_id,)).fetchone()
        if not user:
            raise HTTPException(404, "User not found")
        payload = row_to_dict(user)
        payload["votes"] = rows_to_dicts(
            conn.execute(
                """
                SELECT votes.id, votes.poll_id, votes.choice, votes.created_at,
                       polls.kind, polls.title, polls.status, polls.winner
                FROM votes
                JOIN polls ON polls.id = votes.poll_id
                WHERE votes.user_id = ?
                ORDER BY votes.created_at DESC
                """,
                (user_id,),
            ).fetchall()
        )
        payload["bets"] = rows_to_dicts(
            conn.execute(
                "SELECT * FROM bets WHERE user_id = ? ORDER BY created_at DESC",
                (user_id,),
            ).fetchall()
        )
        return payload

