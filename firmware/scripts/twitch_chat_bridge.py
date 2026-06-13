from __future__ import annotations

import argparse
import asyncio
import json
import os
import signal
import sys
from collections import deque
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

import websockets


TWITCH_VALIDATE_URL = "https://id.twitch.tv/oauth2/validate"
TWITCH_USERS_URL = "https://api.twitch.tv/helix/users"
TWITCH_EVENTSUB_SUBSCRIPTIONS_URL = "https://api.twitch.tv/helix/eventsub/subscriptions"
TWITCH_EVENTSUB_WS_URL = "wss://eventsub.wss.twitch.tv/ws"


class BridgeError(Exception):
    pass


def env(name: str, default: str | None = None) -> str | None:
    value = os.getenv(name)
    if value is None or not value.strip():
        return default
    return value.strip()


def http_json(
    url: str,
    *,
    method: str = "GET",
    headers: dict[str, str] | None = None,
    body: dict[str, Any] | None = None,
    timeout: float = 10.0,
) -> dict[str, Any]:
    payload = None
    request_headers = {"Accept": "application/json", **(headers or {})}
    if body is not None:
        payload = json.dumps(body).encode("utf-8")
        request_headers["Content-Type"] = "application/json"

    request = Request(url, data=payload, headers=request_headers, method=method)
    try:
        with urlopen(request, timeout=timeout) as response:
            raw = response.read().decode("utf-8")
            return json.loads(raw) if raw else {}
    except HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace").strip()
        raise BridgeError(f"HTTP {error.code} en {url}: {detail or error.reason}") from error
    except URLError as error:
        raise BridgeError(f"No se pudo contactar con {url}: {error.reason}") from error
    except json.JSONDecodeError as error:
        raise BridgeError(f"Respuesta JSON invalida en {url}") from error


def validate_token(access_token: str) -> dict[str, Any]:
    return http_json(
        TWITCH_VALIDATE_URL,
        headers={"Authorization": f"OAuth {access_token}"},
        timeout=10.0,
    )


def helix_headers(client_id: str, access_token: str) -> dict[str, str]:
    return {
        "Client-Id": client_id,
        "Authorization": f"Bearer {access_token}",
    }


def resolve_user(
    *,
    client_id: str,
    access_token: str,
    login: str | None = None,
    user_id: str | None = None,
) -> dict[str, Any]:
    if not login and not user_id:
        raise BridgeError("Hace falta broadcaster login o broadcaster user id.")

    params = {}
    if user_id:
        params["id"] = user_id
    else:
        params["login"] = login

    url = f"{TWITCH_USERS_URL}?{urlencode(params)}"
    payload = http_json(url, headers=helix_headers(client_id, access_token), timeout=10.0)
    user = (payload.get("data") or [None])[0]
    if not user:
        who = user_id or login or "desconocido"
        raise BridgeError(f"No se encontro el usuario de Twitch '{who}'.")
    return user


class TwitchChatBridge:
    def __init__(
        self,
        *,
        access_token: str,
        client_id: str,
        read_user_id: str,
        read_user_login: str,
        broadcaster_user_id: str,
        broadcaster_login: str,
        backend_url: str,
        backend_secret: str | None,
        eventsub_url: str,
    ) -> None:
        self.access_token = access_token
        self.client_id = client_id
        self.read_user_id = read_user_id
        self.read_user_login = read_user_login
        self.broadcaster_user_id = broadcaster_user_id
        self.broadcaster_login = broadcaster_login
        self.backend_url = backend_url
        self.backend_secret = backend_secret
        self.eventsub_url = eventsub_url
        self.seen_message_ids: deque[str] = deque(maxlen=512)
        self.running = True

    def stop(self) -> None:
        self.running = False

    def print_boot_summary(self) -> None:
        print(f"[TWITCH] Leyendo chat como: {self.read_user_login} ({self.read_user_id})")
        print(f"[TWITCH] Canal objetivo: {self.broadcaster_login} ({self.broadcaster_user_id})")
        print(f"[TWITCH] Backend destino: {self.backend_url}")
        if self.backend_secret:
            print("[TWITCH] Header X-RoboBet-Secret activo")

    def create_subscription(self, session_id: str) -> dict[str, Any]:
        body = {
            "type": "channel.chat.message",
            "version": "1",
            "condition": {
                "broadcaster_user_id": self.broadcaster_user_id,
                "user_id": self.read_user_id,
            },
            "transport": {
                "method": "websocket",
                "session_id": session_id,
            },
        }
        return http_json(
            TWITCH_EVENTSUB_SUBSCRIPTIONS_URL,
            method="POST",
            headers=helix_headers(self.client_id, self.access_token),
            body=body,
            timeout=10.0,
        )

    def send_to_backend(self, event: dict[str, Any]) -> dict[str, Any]:
        message = (((event.get("message") or {}).get("text")) or "").strip()
        chatter_login = str(event.get("chatter_user_login") or "").strip()
        payload = {
            "login": chatter_login,
            "display_name": event.get("chatter_user_name") or chatter_login,
            "twitch_id": event.get("chatter_user_id"),
            "channel": event.get("broadcaster_user_login") or self.broadcaster_login,
            "message": message,
        }
        if event.get("chatter_user_profile_image_url"):
            payload["profile_image_url"] = event["chatter_user_profile_image_url"]

        headers = {"Content-Type": "application/json", "Accept": "application/json"}
        if self.backend_secret:
            headers["X-RoboBet-Secret"] = self.backend_secret
        return http_json(self.backend_url, method="POST", headers=headers, body=payload, timeout=10.0)

    async def handle_notification(self, payload: dict[str, Any]) -> None:
        metadata = payload.get("metadata") or {}
        message_id = str(metadata.get("message_id") or "")
        if message_id and message_id in self.seen_message_ids:
            return
        if message_id:
            self.seen_message_ids.append(message_id)

        if metadata.get("subscription_type") != "channel.chat.message":
            return

        event = ((payload.get("payload") or {}).get("event")) or {}
        message = (((event.get("message") or {}).get("text")) or "").strip()
        chatter_login = str(event.get("chatter_user_login") or "").strip()
        if not message or not chatter_login:
            return

        print(f"[CHAT] #{self.broadcaster_login} <{chatter_login}> {message}")
        try:
            result = self.send_to_backend(event)
        except BridgeError as error:
            print(f"[BACKEND] Error reenviando mensaje: {error}", file=sys.stderr)
            return

        handled = bool(result.get("handled"))
        reply = str(result.get("reply") or "").strip()
        if handled:
            print(f"[BACKEND] OK -> {reply or 'comando procesado'}")

    async def listen_once(self, websocket_url: str) -> str | None:
        reconnect_url: str | None = None
        async with websockets.connect(websocket_url, ping_interval=20, ping_timeout=20) as ws:
            while self.running:
                raw = await ws.recv()
                payload = json.loads(raw)
                metadata = payload.get("metadata") or {}
                message_type = metadata.get("message_type")

                if message_type == "session_welcome":
                    session = ((payload.get("payload") or {}).get("session")) or {}
                    session_id = str(session.get("id") or "").strip()
                    if not session_id:
                        raise BridgeError("Twitch no devolvio session_id en session_welcome.")
                    created = self.create_subscription(session_id)
                    sub = (created.get("data") or [None])[0] or {}
                    print(f"[TWITCH] Suscripcion creada: {sub.get('type', 'channel.chat.message')} id={sub.get('id', '-')}")
                elif message_type == "notification":
                    await self.handle_notification(payload)
                elif message_type == "session_keepalive":
                    print("[TWITCH] keepalive")
                elif message_type == "session_reconnect":
                    session = ((payload.get("payload") or {}).get("session")) or {}
                    reconnect_url = str(session.get("reconnect_url") or "").strip() or None
                    print(f"[TWITCH] reconnect solicitado: {reconnect_url or 'sin URL'}")
                    break
                elif message_type == "revocation":
                    sub = ((payload.get("payload") or {}).get("subscription")) or {}
                    status = sub.get("status") or "unknown"
                    raise BridgeError(f"Twitch revoco la suscripcion: {status}")
        return reconnect_url

    async def run(self) -> None:
        self.print_boot_summary()
        websocket_url = self.eventsub_url
        backoff_seconds = 2
        while self.running:
            try:
                reconnect_url = await self.listen_once(websocket_url)
                websocket_url = reconnect_url or self.eventsub_url
                backoff_seconds = 2
            except (BridgeError, OSError, websockets.WebSocketException, json.JSONDecodeError) as error:
                print(f"[TWITCH] Conexion interrumpida: {error}", file=sys.stderr)
                if not self.running:
                    break
                await asyncio.sleep(backoff_seconds)
                backoff_seconds = min(backoff_seconds * 2, 30)
                websocket_url = self.eventsub_url


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Puente Twitch EventSub -> /api/twitch/chat para RoboBet.")
    parser.add_argument("--access-token", default=env("ROBOBET_TWITCH_ACCESS_TOKEN"))
    parser.add_argument("--client-id", default=env("ROBOBET_TWITCH_CLIENT_ID"))
    parser.add_argument("--broadcaster-login", default=env("ROBOBET_TWITCH_BROADCASTER_LOGIN"))
    parser.add_argument("--broadcaster-user-id", default=env("ROBOBET_TWITCH_BROADCASTER_USER_ID"))
    parser.add_argument("--backend-url", default=env("ROBOBET_TWITCH_BACKEND_URL", "http://127.0.0.1:8000/api/twitch/chat"))
    parser.add_argument("--backend-secret", default=env("ROBOBET_TWITCH_CHAT_SECRET"))
    parser.add_argument("--eventsub-url", default=env("ROBOBET_TWITCH_EVENTSUB_URL", TWITCH_EVENTSUB_WS_URL))
    return parser.parse_args()


def validate_boot_config(args: argparse.Namespace) -> TwitchChatBridge:
    access_token = (args.access_token or "").strip()
    if not access_token:
        raise BridgeError("Falta ROBOBET_TWITCH_ACCESS_TOKEN o --access-token.")

    validation = validate_token(access_token)
    client_id = (args.client_id or validation.get("client_id") or "").strip()
    if not client_id:
        raise BridgeError("No se pudo resolver el Client ID de Twitch.")

    scopes = {scope.strip() for scope in validation.get("scopes") or [] if str(scope).strip()}
    if "user:read:chat" not in scopes:
        raise BridgeError("El token de Twitch no tiene el scope user:read:chat.")

    read_user_id = str(validation.get("user_id") or "").strip()
    read_user_login = str(validation.get("login") or "").strip()
    if not read_user_id or not read_user_login:
        raise BridgeError("El token de Twitch no devolvio user_id/login validos.")

    broadcaster = resolve_user(
        client_id=client_id,
        access_token=access_token,
        login=(args.broadcaster_login or "").strip() or None,
        user_id=(args.broadcaster_user_id or "").strip() or None,
    )

    return TwitchChatBridge(
        access_token=access_token,
        client_id=client_id,
        read_user_id=read_user_id,
        read_user_login=read_user_login,
        broadcaster_user_id=str(broadcaster.get("id") or "").strip(),
        broadcaster_login=str(broadcaster.get("login") or "").strip(),
        backend_url=str(args.backend_url or "").strip(),
        backend_secret=(args.backend_secret or "").strip() or None,
        eventsub_url=str(args.eventsub_url or TWITCH_EVENTSUB_WS_URL).strip(),
    )


async def async_main() -> int:
    args = parse_args()
    bridge = validate_boot_config(args)
    loop = asyncio.get_running_loop()

    def _stop() -> None:
        bridge.stop()

    for signame in ("SIGINT", "SIGTERM"):
        sig = getattr(signal, signame, None)
        if sig is None:
            continue
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            pass

    await bridge.run()
    return 0


def main() -> int:
    try:
        return asyncio.run(async_main())
    except KeyboardInterrupt:
        return 0
    except BridgeError as error:
        print(f"[TWITCH] Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
