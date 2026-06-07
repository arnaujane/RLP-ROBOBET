from fastapi import APIRouter

from ..realtime import hub
from ..schemas import CommandRequest

router = APIRouter(prefix="/api/operator", tags=["operator"])


@router.post("/command")
async def send_command(payload: CommandRequest) -> dict:
    command = {"type": payload.type, **payload.payload}
    await hub.send_robot_command(command)
    return {"queued": True, "command": command}

