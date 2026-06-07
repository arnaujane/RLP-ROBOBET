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
- `USER_OBSTACLE_BRIDGED`
- `OBSTACLE_DETECTED`
- `BACKTRACK_STARTED`
- `BACKTRACK_DONE`
- `FINISH_DETECTED`
- `RUN_STOPPED`
- `RUN_RESET`
- `GRAPH_FULL`

## ESP32-CAM A Robot

UART texto, una linea por evento:

```text
GREEN_SIGN
RED_SIGN
NO_SIGN
```

La ESP32-CAM usa UART0 segun el esquema. No mezclar este canal con logs de depuracion durante la demo.

