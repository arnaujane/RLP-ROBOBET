from fastapi import APIRouter

router = APIRouter(prefix="/api/streams", tags=["streams"])


@router.get("/defaults")
def defaults() -> dict:
    return {
        "front": "http://esp32cam.local/stream",
        "overhead": "http://127.0.0.1:8081/video",
    }

