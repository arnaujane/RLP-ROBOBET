import json
from typing import Optional
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse, urlunparse
from urllib.request import Request, urlopen

from fastapi import APIRouter, HTTPException

router = APIRouter(prefix="/api/streams", tags=["streams"])

DEFAULT_FRONT_STREAM = "http://192.168.1.56/stream"
DEFAULT_OVERHEAD_STREAM = "http://127.0.0.1:8081/video"


@router.get("/defaults")
def defaults() -> dict:
    return {
        "front": DEFAULT_FRONT_STREAM,
        "overhead": DEFAULT_OVERHEAD_STREAM,
    }


def camera_endpoint(stream_url: Optional[str], endpoint: str) -> str:
    parsed = urlparse(stream_url or DEFAULT_FRONT_STREAM)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise HTTPException(400, "Invalid camera URL")
    return urlunparse((parsed.scheme, parsed.netloc, f"/{endpoint}", "", "", ""))


def get_camera_json(stream_url: Optional[str], endpoint: str, timeout: float = 4.0) -> dict:
    url = camera_endpoint(stream_url, endpoint)
    request = Request(url, headers={"Accept": "application/json"})
    try:
        with urlopen(request, timeout=timeout) as response:
            payload = response.read().decode("utf-8")
    except HTTPError as error:
        raise HTTPException(error.code, f"Camera returned HTTP {error.code}") from error
    except (TimeoutError, URLError) as error:
        message = str(error).strip() or "timeout"
        raise HTTPException(504, f"Camera unavailable: {message}") from error

    try:
        return json.loads(payload)
    except json.JSONDecodeError as error:
        raise HTTPException(502, "Camera returned invalid JSON") from error


@router.get("/camera/status")
def camera_status(stream_url: Optional[str] = None) -> dict:
    return get_camera_json(stream_url, "status")


@router.post("/camera/detect")
def camera_detect(stream_url: Optional[str] = None) -> dict:
    return get_camera_json(stream_url, "detect", timeout=6.0)

