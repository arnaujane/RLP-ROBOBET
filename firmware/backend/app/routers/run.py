from __future__ import annotations

import json

from fastapi import APIRouter, HTTPException

from ..database import db, row_to_dict, rows_to_dicts
from ..realtime import hub
from ..schemas import RunFinish, RunStart
from ..services.betting import settle_bets

router = APIRouter(prefix="/api/run", tags=["run"])


def run_restrictions(speed_limit: int, user_obstacles: list[str] | None = None) -> dict:
    return {
        "speed_limit": speed_limit,
        "motor_limited_80": speed_limit <= 80,
        "user_obstacles": user_obstacles or [],
    }


def robot_metric(name: str, default: int = 0) -> int:
    value = hub.robot_state.get(name, default)
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def robot_crossing_count() -> int:
    explicit = robot_metric("crossing_count", -1)
    if explicit >= 0:
        return explicit
    node_count = robot_metric("node_count", -1)
    if node_count >= 0:
        return node_count
    current_node = robot_metric("current_node", -1)
    return max(0, current_node + 1)


def telemetry_summary() -> dict:
    keys = [
        "state",
        "last_event",
        "current_node",
        "node_count",
        "crossing_count",
        "obstacle_count",
        "camera_sign",
        "camera_confidence",
        "camera_reason",
        "line_position",
        "sensor_active_count",
        "qtr_threshold",
        "motor_left_percent",
        "motor_right_percent",
    ]
    return {key: hub.robot_state.get(key) for key in keys if key in hub.robot_state}


def build_result(run: dict, reached_finish: bool, elapsed_ms: int, obstacle_count: int, crossing_count: int) -> dict:
    restrictions = json.loads(run["restrictions"]) if run.get("restrictions") else run_restrictions(run["speed_limit"])
    return {
        "reached_finish": reached_finish,
        "elapsed_ms": elapsed_ms,
        "obstacle_count": obstacle_count,
        "crossing_count": crossing_count,
        "algorithm": run["algorithm"],
        "speed_limit": run["speed_limit"],
        "restrictions": restrictions,
    }


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
        restrictions = run_restrictions(payload.speed_limit)
        cur = conn.execute(
            """
            INSERT INTO runs(status, algorithm, speed_limit, restrictions, started_at)
            VALUES ('running', ?, ?, ?, CURRENT_TIMESTAMP)
            """,
            (payload.algorithm, payload.speed_limit, json.dumps(restrictions)),
        )
        run = row_to_dict(conn.execute("SELECT * FROM runs WHERE id = ?", (cur.lastrowid,)).fetchone())
    await hub.send_robot_command({"type": "SET_ALGORITHM", "algorithm": payload.algorithm})
    await hub.send_robot_command({"type": "SET_SPEED_LIMIT", "percent": payload.speed_limit})
    await hub.send_robot_command({"type": "START_RUN", "run_id": run["id"]})
    await hub.broadcast({"type": "run_started", "run": run})
    return run


@router.post("/stop")
async def stop_run() -> dict:
    stopped_run = None
    with db() as conn:
        run = conn.execute("SELECT * FROM runs WHERE status = 'running' ORDER BY id DESC LIMIT 1").fetchone()
        if run:
            run_dict = dict(run)
            elapsed_ms = robot_metric("elapsed_ms", 0)
            obstacle_count = robot_metric("obstacle_count", run_dict["obstacle_count"])
            crossing_count = robot_crossing_count()
            result = build_result(run_dict, False, elapsed_ms, obstacle_count, crossing_count)
            conn.execute(
                """
                UPDATE runs
                SET status = 'stopped', finished_at = CURRENT_TIMESTAMP, elapsed_ms = ?,
                    obstacle_count = ?, crossing_count = ?, result = ?, telemetry_summary = ?
                WHERE id = ?
                """,
                (
                    elapsed_ms,
                    obstacle_count,
                    crossing_count,
                    json.dumps(result),
                    json.dumps(telemetry_summary()),
                    run["id"],
                ),
            )
            stopped_run = row_to_dict(conn.execute("SELECT * FROM runs WHERE id = ?", (run["id"],)).fetchone())
    await hub.send_robot_command({"type": "STOP_RUN"})
    await hub.broadcast({"type": "run_stopped", "run": stopped_run})
    return {"ok": True, "run": stopped_run}


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
        run_dict = dict(run)
        result = build_result(
            run_dict,
            payload.reached_finish,
            payload.elapsed_ms,
            payload.obstacle_count,
            payload.crossing_count,
        )
        conn.execute(
            """
            UPDATE runs
            SET status = 'finished', finished_at = CURRENT_TIMESTAMP, elapsed_ms = ?,
                obstacle_count = ?, crossing_count = ?, result = ?, telemetry_summary = ?
            WHERE id = ?
            """,
            (
                payload.elapsed_ms,
                payload.obstacle_count,
                payload.crossing_count,
                json.dumps(result),
                json.dumps(telemetry_summary()),
                run["id"],
            ),
        )
        settled = settle_bets(conn, run["id"], result)
        updated_run = row_to_dict(conn.execute("SELECT * FROM runs WHERE id = ?", (run["id"],)).fetchone())
    await hub.broadcast({"type": "run_finished", "run": updated_run, "settled": settled})
    return {"run": updated_run, "settled": settled}


@router.get("/history")
def history() -> list[dict]:
    with db() as conn:
        return rows_to_dicts(conn.execute("SELECT * FROM runs ORDER BY id DESC LIMIT 20").fetchall())

