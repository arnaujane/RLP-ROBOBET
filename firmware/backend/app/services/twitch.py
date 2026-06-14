from __future__ import annotations

import json
import shlex
from sqlite3 import Connection
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from ..config import TWITCH_CHAT_SHARED_SECRET, TWITCH_HELIX_USERS_URL, TWITCH_HTTP_TIMEOUT_SECONDS, TWITCH_VALIDATE_URL
from ..database import rows_to_dicts
from ..realtime import hub
from .betting import odds_for
from .users import upsert_twitch_user


class TwitchAuthError(Exception):
    pass


class TwitchUpstreamError(Exception):
    pass


def _fetch_json(url: str, headers: dict[str, str]) -> dict[str, Any]:
    request = Request(url, headers=headers)
    try:
        with urlopen(request, timeout=TWITCH_HTTP_TIMEOUT_SECONDS) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        if error.code in (401, 403):
            raise TwitchAuthError(detail or "Unauthorized Twitch request") from error
        raise TwitchUpstreamError(detail or f"Twitch HTTP {error.code}") from error
    except URLError as error:
        raise TwitchUpstreamError(str(error.reason)) from error


def authenticate_twitch_user(conn: Connection, access_token: str) -> tuple[dict, dict]:
    token = access_token.strip()
    if not token:
        raise TwitchAuthError("Access token is required")

    validation = _fetch_json(TWITCH_VALIDATE_URL, {"Authorization": f"OAuth {token}"})
    client_id = str(validation.get("client_id") or "").strip()
    login = str(validation.get("login") or "").strip()
    user_id = str(validation.get("user_id") or "").strip()
    if not client_id or not login or not user_id:
        raise TwitchAuthError("Twitch token validation returned incomplete identity")

    profile_payload = _fetch_json(
        TWITCH_HELIX_USERS_URL,
        {
            "Authorization": f"Bearer {token}",
            "Client-Id": client_id,
        },
    )
    profile = (profile_payload.get("data") or [None])[0]
    if not profile:
        raise TwitchAuthError("Twitch profile not found for validated token")

    user = upsert_twitch_user(
        conn,
        twitch_id=profile.get("id") or user_id,
        login=profile.get("login") or login,
        display_name=profile.get("display_name") or login,
        profile_image_url=profile.get("profile_image_url") or "",
    )
    return user, profile


def check_chat_secret(secret: str | None) -> bool:
    expected = TWITCH_CHAT_SHARED_SECRET
    if not expected:
        return True
    return bool(secret) and secret == expected


def _normalize_bet_kind(raw_kind: str) -> str | None:
    value = raw_kind.strip().lower()
    return {
        "finish": "finish",
        "final": "finish",
        "llega": "finish",
        "time": "time_range",
        "tiempo": "time_range",
        "time_range": "time_range",
        "obstacles": "obstacles",
        "obstaculos": "obstacles",
        "obstaculo": "obstacles",
        "algorithm": "algorithm",
        "algoritmo": "algorithm",
    }.get(value)


def _normalize_bet_choice(kind: str, raw_choice: str) -> str | None:
    value = raw_choice.strip().lower()
    by_kind = {
        "finish": {
            "si": "yes",
            "sí": "yes",
            "yes": "yes",
            "no": "no",
        },
        "time_range": {
            "fast": "fast",
            "rapido": "fast",
            "rápido": "fast",
            "medium": "medium",
            "medio": "medium",
            "slow": "slow",
            "lento": "slow",
        },
        "obstacles": {
            "0": "0",
            "1": "1",
            "2+": "2+",
            "2": "2+",
        },
        "algorithm": {
            "dfs": "DFS",
            "bfs": "BFS",
        },
    }
    return by_kind.get(kind, {}).get(value)


def _poll_results(conn: Connection, poll_id: int, options: list[str]) -> list[dict]:
    rows = conn.execute(
        "SELECT choice, COUNT(*) AS votes FROM votes WHERE poll_id = ? GROUP BY choice",
        (poll_id,),
    ).fetchall()
    counts = {row["choice"]: row["votes"] for row in rows}
    return [{"choice": option, "votes": int(counts.get(option, 0))} for option in options]


def _latest_open_poll(conn: Connection) -> dict | None:
    row = conn.execute("SELECT * FROM polls WHERE status = 'open' ORDER BY id DESC LIMIT 1").fetchone()
    return dict(row) if row else None


def _latest_open_poll_by_kind(conn: Connection, kind: str) -> dict | None:
    row = conn.execute(
        "SELECT * FROM polls WHERE status = 'open' AND lower(kind) = ? ORDER BY id DESC LIMIT 1",
        (kind.lower(),),
    ).fetchone()
    return dict(row) if row else None


def _open_poll_by_id(conn: Connection, poll_id: int) -> dict | None:
    row = conn.execute("SELECT * FROM polls WHERE id = ? AND status = 'open'", (poll_id,)).fetchone()
    return dict(row) if row else None


def _normalize_poll_target(raw_target: str) -> str | None:
    value = raw_target.strip().lower().replace("-", "").replace("_", "")
    return {
        "algoritmo": "algorithm",
        "algorithm": "algorithm",
        "dfsbfs": "algorithm",
        "obstaculo1": "obstacle_1",
        "obstacle1": "obstacle_1",
        "obs1": "obstacle_1",
        "o1": "obstacle_1",
        "obstaculo2": "obstacle_2",
        "obstacle2": "obstacle_2",
        "obs2": "obstacle_2",
        "o2": "obstacle_2",
        "obstaculo3": "obstacle_3",
        "obstacle3": "obstacle_3",
        "obs3": "obstacle_3",
        "o3": "obstacle_3",
    }.get(value)


def _match_poll_option(raw_choice: str, options: list[str]) -> str | None:
    normalized = raw_choice.strip().lower()
    for option in options:
        if option.strip().lower() == normalized:
            return option
    return None


def _run_state_text(conn: Connection) -> str:
    row = conn.execute("SELECT * FROM runs ORDER BY id DESC LIMIT 1").fetchone()
    current_run = dict(row) if row else {}
    robot_state = hub.robot_state or {}
    run_status = current_run.get("status") or "idle"
    algorithm = current_run.get("algorithm") or robot_state.get("algorithm") or "DFS"
    robot_mode = robot_state.get("state") or "offline"
    obstacles = robot_state.get("obstacle_count", 0)
    current_node = robot_state.get("current_node", "-")
    return f"estado={run_status} algoritmo={algorithm} robot={robot_mode} nodo={current_node} obstaculos={obstacles}"


def _log_chat_event(conn: Connection, event_type: str, payload: dict[str, Any]) -> None:
    conn.execute(
        "INSERT INTO events(source, type, payload) VALUES (?, ?, ?)",
        ("twitch", event_type, json.dumps(payload, ensure_ascii=True)),
    )


def process_chat_message(
    conn: Connection,
    *,
    login: str,
    display_name: str | None,
    twitch_id: str | None,
    profile_image_url: str | None,
    channel: str | None,
    message: str,
) -> dict[str, Any]:
    text = message.strip()
    actor = upsert_twitch_user(
        conn,
        twitch_id=twitch_id,
        login=login,
        display_name=display_name,
        profile_image_url=profile_image_url or "",
    )
    result: dict[str, Any] = {
        "handled": False,
        "user": actor,
        "command": text,
        "channel": channel,
        "reply": "",
    }

    if not text.startswith("!"):
        return result

    try:
        parts = shlex.split(text)
    except ValueError:
        result["handled"] = True
        result["reply"] = "comando invalido"
        _log_chat_event(
            conn,
            "TWITCH_CHAT_COMMAND",
            {
                "login": login,
                "display_name": display_name,
                "channel": channel,
                "message": text,
                "reply": result["reply"],
            },
        )
        return result
    command = parts[0].lower()

    if command == "!estado":
        result["handled"] = True
        result["reply"] = _run_state_text(conn)
    elif command == "!apuesta":
        if len(parts) != 4:
            result["handled"] = True
            result["reply"] = "uso: !apuesta <finish|time|obstacles|algorithm> <opcion> <puntos>"
        else:
            kind = _normalize_bet_kind(parts[1])
            choice = _normalize_bet_choice(kind or "", parts[2]) if kind else None
            try:
                amount = int(parts[3])
            except ValueError:
                amount = 0
            if not kind or not choice:
                result["handled"] = True
                result["reply"] = "apuesta invalida"
            elif amount <= 0 or amount > 100000:
                result["handled"] = True
                result["reply"] = "cantidad invalida"
            else:
                run = conn.execute("SELECT * FROM runs ORDER BY id DESC LIMIT 1").fetchone()
                if run and run["status"] == "running":
                    result["handled"] = True
                    result["reply"] = "apuestas cerradas mientras corre el robot"
                else:
                    user = conn.execute("SELECT * FROM users WHERE id = ?", (actor["id"],)).fetchone()
                    if not user or user["balance"] < amount:
                        result["handled"] = True
                        result["reply"] = "no tienes puntos suficientes"
                    else:
                        conn.execute("UPDATE users SET balance = balance - ? WHERE id = ?", (amount, actor["id"]))
                        cur = conn.execute(
                            "INSERT INTO bets(user_id, kind, choice, amount, odds) VALUES (?, ?, ?, ?, ?)",
                            (actor["id"], kind, choice, amount, odds_for(kind)),
                        )
                        bet = dict(conn.execute("SELECT * FROM bets WHERE id = ?", (cur.lastrowid,)).fetchone())
                        result["handled"] = True
                        result["bet"] = bet
                        result["reply"] = f"apuesta registrada: {kind} {choice} {amount}"
    elif command == "!voto":
        if len(parts) not in (2, 3):
            result["handled"] = True
            result["reply"] = "uso: !voto <opcion>, !voto <poll_id> <opcion> o !voto <algoritmo|obstaculo1|obstaculo2|obstaculo3> <opcion>"
        else:
            poll: dict | None
            raw_choice: str
            if len(parts) == 3 and parts[1].isdigit():
                poll = _open_poll_by_id(conn, int(parts[1]))
                raw_choice = parts[2]
            elif len(parts) == 3:
                target_kind = _normalize_poll_target(parts[1])
                poll = _latest_open_poll_by_kind(conn, target_kind) if target_kind else None
                raw_choice = parts[2]
            else:
                poll = _latest_open_poll(conn)
                raw_choice = parts[1] if len(parts) == 2 else parts[2]
            if not poll:
                result["handled"] = True
                result["reply"] = "no hay votacion abierta"
            else:
                options = json.loads(poll["options"])
                choice = _match_poll_option(raw_choice, options)
                if not choice:
                    result["handled"] = True
                    result["reply"] = f"opcion invalida. opciones: {', '.join(options)}"
                else:
                    conn.execute(
                        """
                        INSERT INTO votes(poll_id, user_id, choice) VALUES (?, ?, ?)
                        ON CONFLICT(poll_id, user_id) DO UPDATE SET choice = excluded.choice
                        """,
                        (poll["id"], actor["id"], choice),
                    )
                    rows = _poll_results(conn, poll["id"], options)
                    result["handled"] = True
                    result["poll_id"] = poll["id"]
                    result["results"] = rows
                    result["reply"] = f"voto registrado en encuesta {poll['id']}: {choice}"
    else:
        result["handled"] = False
        result["reply"] = ""

    if result["handled"]:
        _log_chat_event(
            conn,
            "TWITCH_CHAT_COMMAND",
            {
                "login": login,
                "display_name": display_name,
                "channel": channel,
                "message": text,
                "reply": result["reply"],
            },
        )

    return result


def recent_twitch_events(conn: Connection, limit: int = 20) -> list[dict[str, Any]]:
    rows = conn.execute(
        "SELECT * FROM events WHERE source = 'twitch' ORDER BY id DESC LIMIT ?",
        (limit,),
    ).fetchall()
    return rows_to_dicts(rows)
