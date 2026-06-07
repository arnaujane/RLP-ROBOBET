import json

from fastapi import APIRouter, HTTPException

from ..database import db, row_to_dict, rows_to_dicts
from ..realtime import hub
from ..schemas import PollCreate, VoteCreate
from ..services.polls import close_poll

router = APIRouter(prefix="/api/polls", tags=["polls"])


@router.get("")
def list_polls(status: str | None = None) -> list[dict]:
    with db() as conn:
        if status:
            rows = conn.execute("SELECT * FROM polls WHERE status = ? ORDER BY created_at DESC", (status,)).fetchall()
        else:
            rows = conn.execute("SELECT * FROM polls ORDER BY created_at DESC").fetchall()
        polls = rows_to_dicts(rows)
        for poll in polls:
            poll["options"] = json.loads(poll["options"])
        return polls


@router.post("")
async def create_poll(payload: PollCreate) -> dict:
    options = [item.strip() for item in payload.options if item.strip()]
    if len(options) < 2:
        raise HTTPException(400, "A poll needs at least two options")
    with db() as conn:
        cur = conn.execute(
            "INSERT INTO polls(kind, title, options) VALUES (?, ?, ?)",
            (payload.kind, payload.title, json.dumps(options)),
        )
        poll = row_to_dict(conn.execute("SELECT * FROM polls WHERE id = ?", (cur.lastrowid,)).fetchone())
        poll["options"] = options
    await hub.broadcast({"type": "poll_created", "poll": poll})
    return poll


@router.post("/{poll_id}/vote")
async def vote(poll_id: int, payload: VoteCreate) -> dict:
    with db() as conn:
        poll = conn.execute("SELECT * FROM polls WHERE id = ?", (poll_id,)).fetchone()
        if not poll:
            raise HTTPException(404, "Poll not found")
        if poll["status"] != "open":
            raise HTTPException(409, "Poll is closed")
        options = json.loads(poll["options"])
        if payload.choice not in options:
            raise HTTPException(400, "Invalid option")
        user = conn.execute("SELECT id FROM users WHERE id = ?", (payload.user_id,)).fetchone()
        if not user:
            raise HTTPException(404, "User not found")
        conn.execute(
            """
            INSERT INTO votes(poll_id, user_id, choice) VALUES (?, ?, ?)
            ON CONFLICT(poll_id, user_id) DO UPDATE SET choice = excluded.choice
            """,
            (poll_id, payload.user_id, payload.choice),
        )
        rows = rows_to_dicts(
            conn.execute(
                "SELECT choice, COUNT(*) AS votes FROM votes WHERE poll_id = ? GROUP BY choice ORDER BY votes DESC",
                (poll_id,),
            ).fetchall()
        )
    await hub.broadcast({"type": "poll_vote", "poll_id": poll_id, "results": rows})
    return {"poll_id": poll_id, "results": rows}


@router.post("/{poll_id}/close")
async def close(poll_id: int) -> dict:
    with db() as conn:
        try:
            result = close_poll(conn, poll_id)
        except ValueError:
            raise HTTPException(404, "Poll not found") from None
    command = None
    if result["kind"] == "algorithm":
        command = {"type": "SET_ALGORITHM", "algorithm": result["winner"]}
    elif result["kind"] == "speed":
        command = {"type": "SET_SPEED_LIMIT", "percent": 80 if result["winner"] == "80" else 100}
    elif result["kind"] == "obstacle":
        command = {"type": "SET_OBSTACLE_EDGE", "edge_id": result["winner"]}
    if command:
        await hub.send_robot_command(command)
    await hub.broadcast({"type": "poll_closed", "result": result})
    return result

