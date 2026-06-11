from __future__ import annotations

import asyncio
from typing import Any

from fastapi import WebSocket


class RealtimeHub:
    def __init__(self) -> None:
        self.ui_clients: set[WebSocket] = set()
        self.robot: WebSocket | None = None
        self.pending_robot_commands: list[dict[str, Any]] = []
        self.robot_state: dict[str, Any] = {
            "connected": False,
            "state": "offline",
            "algorithm": "DFS",
            "speed_limit": 100,
            "current_node": None,
            "last_event": None,
            "obstacle_count": 0,
            "line_position": None,
            "camera_sign": "NO_SIGN",
            "camera_transport": None,
            "battery": None,
        }
        self._lock = asyncio.Lock()

    async def register_ui(self, ws: WebSocket) -> None:
        await ws.accept()
        async with self._lock:
            self.ui_clients.add(ws)
        await ws.send_json({"type": "snapshot", "robot": self.robot_state})

    async def unregister_ui(self, ws: WebSocket) -> None:
        async with self._lock:
            self.ui_clients.discard(ws)

    async def register_robot(self, ws: WebSocket) -> None:
        await ws.accept()
        async with self._lock:
            self.robot = ws
            self.robot_state["connected"] = True
            self.robot_state["state"] = "connected"
            pending = list(self.pending_robot_commands)
            self.pending_robot_commands.clear()
        await self.broadcast({"type": "robot_connection", "connected": True})
        for command in pending:
            await self.send_robot_command(command)

    async def unregister_robot(self, ws: WebSocket) -> None:
        async with self._lock:
            if self.robot is ws:
                self.robot = None
                self.robot_state["connected"] = False
                self.robot_state["state"] = "offline"
        await self.broadcast({"type": "robot_connection", "connected": False})

    async def broadcast(self, message: dict[str, Any]) -> None:
        dead: list[WebSocket] = []
        async with self._lock:
            clients = list(self.ui_clients)
        for client in clients:
            try:
                await client.send_json(message)
            except Exception:
                dead.append(client)
        if dead:
            async with self._lock:
                for client in dead:
                    self.ui_clients.discard(client)

    async def send_robot_command(self, command: dict[str, Any]) -> None:
        async with self._lock:
            robot = self.robot
            if robot is None:
                self.pending_robot_commands.append(command)
                return
        await robot.send_json(command)
        await self.broadcast({"type": "command_sent", "command": command})

    async def update_robot_state(self, payload: dict[str, Any]) -> None:
        async with self._lock:
            self.robot_state.update(payload)
            if payload.get("event"):
                self.robot_state["last_event"] = payload["event"]
        await self.broadcast({"type": "robot_telemetry", "robot": self.robot_state, "payload": payload})


hub = RealtimeHub()

