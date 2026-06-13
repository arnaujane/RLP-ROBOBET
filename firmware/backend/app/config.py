import os
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
FRONTEND_DIR = ROOT_DIR / "frontend"
DATA_DIR = ROOT_DIR / "data"
DB_PATH = DATA_DIR / "robobet.sqlite3"

DEFAULT_STARTING_BALANCE = 1000
TWITCH_VALIDATE_URL = "https://id.twitch.tv/oauth2/validate"
TWITCH_HELIX_USERS_URL = "https://api.twitch.tv/helix/users"
TWITCH_HTTP_TIMEOUT_SECONDS = float(os.getenv("ROBOBET_TWITCH_TIMEOUT_SECONDS", "5"))
TWITCH_CHAT_SHARED_SECRET = os.getenv("ROBOBET_TWITCH_CHAT_SECRET", "").strip()
