from fastapi import APIRouter, HTTPException

from ..config import DEFAULT_STARTING_BALANCE
from ..database import db, row_to_dict, rows_to_dicts
from ..schemas import TwitchUserCreate, UserCreate
from ..services.users import upsert_twitch_user

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

@router.post("/twitch")
def create_or_update_twitch_user(payload: TwitchUserCreate) -> dict:
    with db() as conn:
        try:
            return upsert_twitch_user(
                conn,
                twitch_id=payload.twitch_id,
                login=payload.login,
                display_name=payload.display_name,
                profile_image_url=payload.profile_image_url,
            )
        except ValueError as error:
            raise HTTPException(400, str(error)) from None


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

