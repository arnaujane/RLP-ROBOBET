import json

from fastapi import APIRouter, HTTPException

from ..database import db, row_to_dict, rows_to_dicts
from ..realtime import hub
from ..schemas import RunFinish, RunStart
from ..services.betting import settle_bets

router = APIRouter(prefix="/api/run", tags=["run"])


@router.get("/state")
def state() -> dict:
    with db() as conn:
        run = conn.execute("SELECT * FROM runs ORDER BY id DESC LIMIT 1").fetchone()
        return {"run": row_to_dict(run), "robot": hub.robot_state}


@router.post("/start")
async def start_run(payload: RunStart) -> dict:
    with db() as conn:
        active = conn.execute("SELECT * FROM runs WHERE status = 'running' ORDER BY id DESC LIMIT 1").fetchone()
        if active:
            raise HTTPException(409, "A run is already active")
        cur = conn.execute(
            "INSERT INTO runs(status, algorithm, speed_limit, started_at) VALUES ('running', ?, ?, CURRENT_TIMESTAMP)",
            (payload.algorithm, payload.speed_limit),
        )
        run = row_to_dict(conn.execute("SELECT * FROM runs WHERE id = ?", (cur.lastrowid,)).fetchone())
    await hub.send_robot_command({"type": "SET_ALGORITHM", "algorithm": payload.algorithm})
    await hub.send_robot_command({"type": "SET_SPEED_LIMIT", "percent": payload.speed_limit})
    await hub.send_robot_command({"type": "START_RUN", "run_id": run["id"]})
    await hub.broadcast({"type": "run_started", "run": run})
    return run


@router.post("/stop")
async def stop_run() -> dict:
    with db() as conn:
        run = conn.execute("SELECT * FROM runs WHERE status = 'running' ORDER BY id DESC LIMIT 1").fetchone()
        if run:
            conn.execute(
                "UPDATE runs SET status = 'stopped', finished_at = CURRENT_TIMESTAMP WHERE id = ?",
                (run["id"],),
            )
    await hub.send_robot_command({"type": "STOP_RUN"})
    await hub.broadcast({"type": "run_stopped"})
    return {"ok": True}


@router.post("/reset")
async def reset_run() -> dict:
    await hub.send_robot_command({"type": "RESET_RUN"})
    await hub.broadcast({"type": "run_reset"})
    return {"ok": True}


@router.post("/finish")
async def finish_run(payload: RunFinish) -> dict:
    with db() as conn:
        run = conn.execute("SELECT * FROM runs WHERE status = 'running' ORDER BY id DESC LIMIT 1").fetchone()
        if not run:
            raise HTTPException(404, "No active run")
        result = {
            "reached_finish": payload.reached_finish,
            "elapsed_ms": payload.elapsed_ms,
            "obstacle_count": payload.obstacle_count,
            "algorithm": run["algorithm"],
        }
        conn.execute(
            """
            UPDATE runs
            SET status = 'finished', finished_at = CURRENT_TIMESTAMP, elapsed_ms = ?,
                obstacle_count = ?, result = ?
            WHERE id = ?
            """,
            (payload.elapsed_ms, payload.obstacle_count, json.dumps(result), run["id"]),
        )
        settled = settle_bets(conn, run["id"], result)
        updated_run = row_to_dict(conn.execute("SELECT * FROM runs WHERE id = ?", (run["id"],)).fetchone())
    await hub.broadcast({"type": "run_finished", "run": updated_run, "settled": settled})
    return {"run": updated_run, "settled": settled}


@router.get("/history")
def history() -> list[dict]:
    with db() as conn:
        return rows_to_dicts(conn.execute("SELECT * FROM runs ORDER BY id DESC LIMIT 20").fetchall())

