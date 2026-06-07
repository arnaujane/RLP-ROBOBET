import json
from sqlite3 import Connection


def close_poll(conn: Connection, poll_id: int) -> dict:
    poll = conn.execute("SELECT * FROM polls WHERE id = ?", (poll_id,)).fetchone()
    if poll is None:
        raise ValueError("Poll not found")
    rows = conn.execute(
        "SELECT choice, COUNT(*) AS votes FROM votes WHERE poll_id = ? GROUP BY choice ORDER BY votes DESC, choice ASC",
        (poll_id,),
    ).fetchall()
    options = json.loads(poll["options"])
    winner = rows[0]["choice"] if rows else options[0]
    conn.execute(
        "UPDATE polls SET status = 'closed', winner = ?, closed_at = CURRENT_TIMESTAMP WHERE id = ?",
        (winner, poll_id),
    )
    return {
        "id": poll_id,
        "kind": poll["kind"],
        "winner": winner,
        "results": [dict(row) for row in rows],
    }

