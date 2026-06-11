from __future__ import annotations

from typing import Optional

from fastapi import APIRouter, HTTPException

from ..database import db, row_to_dict, rows_to_dicts
from ..schemas import BetCreate
from ..services.betting import odds_for

router = APIRouter(prefix="/api/bets", tags=["bets"])


@router.get("")
def list_bets(user_id: Optional[int] = None) -> list[dict]:
    with db() as conn:
        if user_id is None:
            rows = conn.execute("SELECT * FROM bets ORDER BY created_at DESC").fetchall()
        else:
            rows = conn.execute("SELECT * FROM bets WHERE user_id = ? ORDER BY created_at DESC", (user_id,)).fetchall()
        return rows_to_dicts(rows)


@router.post("")
def create_bet(payload: BetCreate) -> dict:
    with db() as conn:
        run = conn.execute("SELECT * FROM runs ORDER BY id DESC LIMIT 1").fetchone()
        if run and run["status"] == "running":
            raise HTTPException(409, "Bets are closed while a run is active")
        user = conn.execute("SELECT * FROM users WHERE id = ?", (payload.user_id,)).fetchone()
        if not user:
            raise HTTPException(404, "User not found")
        if user["balance"] < payload.amount:
            raise HTTPException(400, "Not enough points")
        conn.execute("UPDATE users SET balance = balance - ? WHERE id = ?", (payload.amount, payload.user_id))
        cur = conn.execute(
            "INSERT INTO bets(user_id, kind, choice, amount, odds) VALUES (?, ?, ?, ?, ?)",
            (payload.user_id, payload.kind, payload.choice, payload.amount, odds_for(payload.kind)),
        )
        return row_to_dict(conn.execute("SELECT * FROM bets WHERE id = ?", (cur.lastrowid,)).fetchone())

