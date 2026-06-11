from __future__ import annotations

import argparse
import asyncio
import json
import time
from typing import Any

import websockets


class RobotSimulator:
    def __init__(self, url: str, scenario: str) -> None:
        self.url = url
        self.scenario = scenario
        self.algorithm = "DFS"
        self.speed_limit = 100
        self.current_node = -1
        self.obstacle_count = 0
        self.line_position = 3500
        self.run_started_at: float | None = None
        self.run_task: asyncio.Task[None] | None = None

    def elapsed_ms(self) -> int:
        if self.run_started_at is None:
            return 0
        return int((time.monotonic() - self.run_started_at) * 1000)

    def payload(self, event: str, **extra: Any) -> dict[str, Any]:
        data: dict[str, Any] = {
            "event": event,
            "state": extra.pop("state", "FOLLOWING_LINE"),
            "algorithm": self.algorithm,
            "speed_limit": self.speed_limit,
            "current_node": self.current_node,
            "obstacle_count": self.obstacle_count,
            "line_position": self.line_position,
            "camera_sign": extra.pop("camera_sign", "NO_SIGN"),
            "threshold": 2500,
            "elapsed_ms": self.elapsed_ms(),
        }
        data.update(extra)
        return data

    async def send(self, ws: websockets.ClientConnection, event: str, **extra: Any) -> None:
        message = self.payload(event, **extra)
        await ws.send(json.dumps(message))
        print(f"robot -> backend: {event} {extra}")

    async def telemetry_loop(self, ws: websockets.ClientConnection) -> None:
        while True:
            await ws.send(
                json.dumps(
                    {
                        "type": "telemetry",
                        "state": "FOLLOWING_LINE" if self.run_started_at else "WAITING_START",
                        "algorithm": self.algorithm,
                        "speed_limit": self.speed_limit,
                        "current_node": self.current_node,
                        "obstacle_count": self.obstacle_count,
                        "line_position": self.line_position,
                        "camera_sign": "NO_SIGN",
                        "elapsed_ms": self.elapsed_ms(),
                    }
                )
            )
            await asyncio.sleep(1.0)

    async def run_green_finish(self, ws: websockets.ClientConnection) -> None:
        await self.send(ws, "RUN_STARTED")
        await asyncio.sleep(1.0)

        self.current_node = 0
        await self.send(ws, "EDGE_SELECTED", state="SELECTING_EDGE", node=0, options=3, turn="LEFT")
        await asyncio.sleep(1.2)

        self.obstacle_count = 1
        await self.send(
            ws,
            "OBSTACLE_PATCH_DETECTED",
            state="OBSTACLE_CHECK",
            camera_sign="GREEN_SIGN",
            transport="SIMULATOR",
        )
        await asyncio.sleep(0.8)
        await self.send(ws, "OBSTACLE_ALLOWED", camera_sign="GREEN_SIGN", cleared=True)
        await asyncio.sleep(1.2)

        self.current_node = 1
        await self.send(ws, "EDGE_SELECTED", state="SELECTING_EDGE", node=1, options=6, turn="STRAIGHT")
        await asyncio.sleep(1.2)

        await self.send(
            ws,
            "OBSTACLE_PATCH_DETECTED",
            state="OBSTACLE_CHECK",
            camera_sign="BLACK_SIGN",
            transport="SIMULATOR",
        )
        await asyncio.sleep(0.5)
        await self.send(ws, "FINISH_DETECTED", state="FINISHED", camera_sign="BLACK_SIGN")

    async def run_red_backtrack(self, ws: websockets.ClientConnection) -> None:
        await self.send(ws, "RUN_STARTED")
        await asyncio.sleep(1.0)

        self.current_node = 0
        await self.send(ws, "EDGE_SELECTED", state="SELECTING_EDGE", node=0, options=3, turn="LEFT")
        await asyncio.sleep(1.0)

        self.obstacle_count = 1
        await self.send(
            ws,
            "OBSTACLE_PATCH_DETECTED",
            state="OBSTACLE_CHECK",
            camera_sign="RED_SIGN",
            transport="SIMULATOR",
        )
        await asyncio.sleep(0.5)
        await self.send(ws, "OBSTACLE_BLOCKED", state="BACKTRACKING", camera_sign="RED_SIGN", blocked_turn="LEFT")
        await asyncio.sleep(0.5)
        await self.send(ws, "BACKTRACK_STARTED", state="BACKTRACKING")
        await asyncio.sleep(1.0)
        await self.send(ws, "BACKTRACK_DONE")
        await asyncio.sleep(1.2)

        self.current_node = 1
        await self.send(ws, "EDGE_SELECTED", state="SELECTING_EDGE", node=1, options=6, turn="RIGHT")
        await asyncio.sleep(1.0)
        await self.send(
            ws,
            "OBSTACLE_PATCH_DETECTED",
            state="OBSTACLE_CHECK",
            camera_sign="BLACK_SIGN",
            transport="SIMULATOR",
        )
        await asyncio.sleep(0.5)
        await self.send(ws, "FINISH_DETECTED", state="FINISHED", camera_sign="BLACK_SIGN")

    async def start_run(self, ws: websockets.ClientConnection, run_id: int) -> None:
        if self.run_task and not self.run_task.done():
            return

        self.run_started_at = time.monotonic()
        self.current_node = -1
        self.obstacle_count = 0
        print(f"backend -> robot: START_RUN run_id={run_id}")

        if self.scenario == "red-backtrack":
            self.run_task = asyncio.create_task(self.run_red_backtrack(ws))
        else:
            self.run_task = asyncio.create_task(self.run_green_finish(ws))

    async def handle_command(self, ws: websockets.ClientConnection, command: dict[str, Any]) -> None:
        command_type = command.get("type")
        print(f"backend -> robot: {command}")

        if command_type == "SET_ALGORITHM":
            self.algorithm = str(command.get("algorithm", "DFS"))
            await self.send(ws, "ALGORITHM_SET")
        elif command_type == "SET_SPEED_LIMIT":
            self.speed_limit = int(command.get("percent", 100))
            await self.send(ws, "SPEED_LIMIT_SET")
        elif command_type == "CALIBRATE_QTR":
            await self.send(ws, "CALIBRATION_STARTED", state="CALIBRATING")
            await asyncio.sleep(0.6)
            await self.send(ws, "CALIBRATION_DONE", state="WAITING_START")
        elif command_type == "START_RUN":
            await self.start_run(ws, int(command.get("run_id", 0)))
        elif command_type in {"STOP_RUN", "RESET_RUN"}:
            if self.run_task:
                self.run_task.cancel()
            self.run_started_at = None
            await self.send(ws, "RUN_STOPPED" if command_type == "STOP_RUN" else "RUN_RESET", state="WAITING_START")

    async def run(self) -> None:
        async with websockets.connect(self.url) as ws:
            await self.send(ws, "ROBOT_READY", state="WAITING_START")
            telemetry = asyncio.create_task(self.telemetry_loop(ws))
            try:
                async for raw in ws:
                    await self.handle_command(ws, json.loads(raw))
            finally:
                telemetry.cancel()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Simula el ESP32 del robot contra el backend RoboBet.")
    parser.add_argument("--url", default="ws://127.0.0.1:8000/ws/robot", help="URL WebSocket del backend.")
    parser.add_argument(
        "--scenario",
        choices=["green-finish", "red-backtrack"],
        default="green-finish",
        help="Secuencia de eventos a emitir al recibir START_RUN.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    asyncio.run(RobotSimulator(args.url, args.scenario).run())


if __name__ == "__main__":
    main()
