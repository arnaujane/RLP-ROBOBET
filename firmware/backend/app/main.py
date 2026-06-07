import json
from typing import Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from .config import FRONTEND_DIR
from .database import db, init_db
from .realtime import hub
from .routers import bets, operator, polls, run, streams, users

app = FastAPI(title="RoboBet", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
def startup() -> None:
    init_db()


app.include_router(users.router)
app.include_router(bets.router)
app.include_router(polls.router)
app.include_router(run.router)
app.include_router(operator.router)
app.include_router(streams.router)


@app.websocket("/ws/ui")
async def ws_ui(ws: WebSocket) -> None:
    await hub.register_ui(ws)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        await hub.unregister_ui(ws)


@app.websocket("/ws/robot")
async def ws_robot(ws: WebSocket) -> None:
    await hub.register_robot(ws)
    try:
        while True:
            raw = await ws.receive_text()
            try:
                payload: dict[str, Any] = json.loads(raw)
            except json.JSONDecodeError:
                payload = {"event": "MALFORMED_JSON", "raw": raw}
            await hub.update_robot_state(payload)
            event = payload.get("event") or payload.get("type")
            if event:
                with db() as conn:
                    conn.execute(
                        "INSERT INTO events(source, type, payload) VALUES (?, ?, ?)",
                        ("robot", str(event), json.dumps(payload)),
                    )
            if event == "FINISH_DETECTED":
                elapsed_ms = int(payload.get("elapsed_ms", 0))
                obstacle_count = int(payload.get("obstacle_count", hub.robot_state.get("obstacle_count", 0) or 0))
                await run.finish_run(
                    run.RunFinish(reached_finish=True, elapsed_ms=elapsed_ms, obstacle_count=obstacle_count)
                )
    except WebSocketDisconnect:
        await hub.unregister_robot(ws)


if FRONTEND_DIR.exists():
    app.mount("/", StaticFiles(directory=FRONTEND_DIR, html=True), name="frontend")

