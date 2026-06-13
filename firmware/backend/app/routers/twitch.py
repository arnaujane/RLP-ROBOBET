from __future__ import annotations

from fastapi import APIRouter, Header, HTTPException

from ..database import db
from ..realtime import hub
from ..schemas import TwitchAuthRequest, TwitchChatMessage
from ..services.twitch import TwitchAuthError, TwitchUpstreamError, authenticate_twitch_user, check_chat_secret, process_chat_message, recent_twitch_events

router = APIRouter(prefix="/api/twitch", tags=["twitch"])


@router.post("/auth")
def authenticate(payload: TwitchAuthRequest) -> dict:
    try:
        with db() as conn:
            user, twitch_user = authenticate_twitch_user(conn, payload.access_token)
    except TwitchAuthError as error:
        raise HTTPException(401, str(error) or "Invalid Twitch token") from None
    except TwitchUpstreamError as error:
        raise HTTPException(502, f"Twitch unavailable: {error}") from None
    return {"user": user, "twitch_user": twitch_user}


@router.post("/chat")
async def chat_command(payload: TwitchChatMessage, x_robobet_secret: str | None = Header(default=None)) -> dict:
    if not check_chat_secret(x_robobet_secret):
        raise HTTPException(403, "Invalid Twitch chat secret")

    with db() as conn:
        result = process_chat_message(
            conn,
            login=payload.login,
            display_name=payload.display_name,
            twitch_id=payload.twitch_id,
            profile_image_url=payload.profile_image_url,
            channel=payload.channel,
            message=payload.message,
        )

    if result.get("handled"):
        await hub.broadcast({"type": "twitch_chat_command", "result": result})
        if "poll_id" in result and "results" in result:
            await hub.broadcast({"type": "poll_vote", "poll_id": result["poll_id"], "results": result["results"]})
    return result


@router.get("/events")
def list_events(limit: int = 20) -> list[dict]:
    with db() as conn:
        return recent_twitch_events(conn, max(1, min(limit, 100)))
