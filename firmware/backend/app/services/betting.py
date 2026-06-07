import json
from sqlite3 import Connection
from typing import Any


def odds_for(kind: str) -> float:
    return {
        "finish": 1.5,
        "time_range": 2.5,
        "obstacles": 2.0,
        "algorithm": 1.8,
    }.get(kind, 2.0)


def resolve_choice(kind: str, choice: str, result: dict[str, Any]) -> bool:
    if kind == "finish":
        return choice == ("yes" if result.get("reached_finish") else "no")
    if kind == "algorithm":
        return choice.upper() == str(result.get("algorithm", "")).upper()
    if kind == "obstacles":
        count = int(result.get("obstacle_count", 0))
        if choice == "0":
            return count == 0
        if choice == "1":
            return count == 1
        if choice == "2+":
            return count >= 2
    if kind == "time_range":
        elapsed_s = int(result.get("elapsed_ms", 0)) / 1000
        if choice == "fast":
            return elapsed_s < 60
        if choice == "medium":
            return 60 <= elapsed_s <= 120
        if choice == "slow":
            return elapsed_s > 120
    return False


def settle_bets(conn: Connection, run_id: int, result: dict[str, Any]) -> list[dict[str, Any]]:
    rows = conn.execute(
        "SELECT * FROM bets WHERE status = 'open' AND (run_id IS NULL OR run_id = ?)",
        (run_id,),
    ).fetchall()
    settled: list[dict[str, Any]] = []
    for row in rows:
        won = resolve_choice(row["kind"], row["choice"], result)
        payout = int(row["amount"] * float(row["odds"])) if won else 0
        status = "won" if won else "lost"
        conn.execute(
            "UPDATE bets SET status = ?, payout = ?, run_id = ? WHERE id = ?",
            (status, payout, run_id, row["id"]),
        )
        if payout:
            conn.execute(
                "UPDATE users SET balance = balance + ? WHERE id = ?",
                (payout, row["user_id"]),
            )
        settled.append({**dict(row), "status": status, "payout": payout})
    conn.execute(
        "INSERT INTO events(source, type, payload) VALUES (?, ?, ?)",
        ("backend", "BETS_SETTLED", json.dumps({"run_id": run_id, "count": len(settled)})),
    )
    return settled

