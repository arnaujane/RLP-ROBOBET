# Protocolo

## Servidor A Robot

Los comandos via WebSocket `/ws/robot` son JSON:

```json
{ "type": "START_RUN", "run_id": 1 }
{ "type": "STOP_RUN" }
{ "type": "RESET_RUN" }
{ "type": "CALIBRATE_QTR" }
{ "type": "SET_ALGORITHM", "algorithm": "DFS" }
{ "type": "SET_SPEED_LIMIT", "percent": 80 }
{ "type": "SET_OBSTACLE_EDGE", "edge_id": "edge_A" }
```

## Robot A Servidor

El robot envia telemetria y eventos por WebSocket:

```json
{
  "event": "EDGE_SELECTED",
  "state": "SELECTING_EDGE",
  "algorithm": "DFS",
  "speed_limit": 100,
  "current_node": 3,
  "obstacle_count": 1,
  "line_position": 3520,
  "camera_sign": "GREEN_SIGN",
  "elapsed_ms": 42130,
  "turn": "LEFT"
}
```

Eventos principales:

- `ROBOT_READY`
- `CALIBRATION_STARTED`
- `CALIBRATION_DONE`
- `RUN_STARTED`
- `EDGE_SELECTED`
- `LINE_LOST`
- `OBSTACLE_PATCH_DETECTED`
- `OBSTACLE_ALLOWED`
- `OBSTACLE_BLOCKED`
- `BACKTRACK_STARTED`
- `BACKTRACK_DONE`
- `FINISH_DETECTED`
- `RUN_STOPPED`
- `RUN_RESET`
- `GRAPH_FULL`

## Robot A ESP32-CAM

El robot solo consulta la camara cuando detecta una marca negra completa con los QTR. Hay dos transportes disponibles:

- HTTP/IP para pruebas sin soldar UART.
- UART para la version final cableada.

### HTTP

El robot llama:

```text
GET http://192.168.4.2/detect
```

La camara responde:

```json
{ "sign": "GREEN_SIGN", "red_pixels": 0, "green_pixels": 120, "black_pixels": 0 }
```

### UART

El robot envia:

```text
DETECT
```

La camara responde una linea:

```text
GREEN_SIGN
RED_SIGN
BLACK_SIGN
NO_SIGN
```

El robot interpreta `NO_SIGN` o timeout igual que `RED_SIGN`: media vuelta. `BLACK_SIGN` significa final de recorrido.

