from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
FRONTEND_DIR = ROOT_DIR / "frontend"
DATA_DIR = ROOT_DIR / "data"
DB_PATH = DATA_DIR / "robobet.sqlite3"

DEFAULT_STARTING_BALANCE = 1000

