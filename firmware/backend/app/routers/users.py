from fastapi import APIRouter, HTTPException

from ..config import DEFAULT_STARTING_BALANCE
from ..database import db, row_to_dict, rows_to_dicts
from ..schemas import UserCreate

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


@router.get("/{user_id}")
def get_user(user_id: int) -> dict:
    with db() as conn:
        user = conn.execute("SELECT * FROM users WHERE id = ?", (user_id,)).fetchone()
        if not user:
            raise HTTPException(404, "User not found")
        return row_to_dict(user)

