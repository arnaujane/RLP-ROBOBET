# Integracion Del Frontend

La primera version del frontend esta preparada para mezclar datos reales y simulados. Cuando el backend, el robot o la camara no responden, la interfaz mantiene datos simulados para poder validar el diseno sin hardware.

## Pantallas

- `Administrador`: dashboard tecnico para robot, sensores, camara, motores, algoritmo, obstaculos, laberinto y operador.
- `Usuario`: vista publica con login Twitch preparado, camaras, estado del laberinto, historial, apuestas y votaciones.

## Sensores Reales Del Robot

El firmware del robot envia los sensores QTR por WebSocket en cada telemetria:

```json
{
  "sensor_values": [1200, 1300, 2800, 3000, 2900, 1400, 1200, 1100],
  "sensor_active": [false, false, true, true, true, false, false, false],
  "sensor_active_count": 3,
  "threshold": 2500,
  "qtr_threshold": 2500,
  "qtr_calibrated": false
}
```

Tambien acepta `sensorValues`, `qtr_raw`, `raw_us` o `sensors`.

`/qtr-debug` se mantiene como diagnostico local sin mover motores.

## Potencia Real De Motores

El firmware del robot envia la potencia real aplicada tras el limite de velocidad:

```json
{
  "motor_left_pwm": 28,
  "motor_right_pwm": 31,
  "motor_left_percent": 10,
  "motor_right_percent": 12
}
```

Tambien acepta `motorLeftPercent`, `motorRightPercent`, `left_motor` o `right_motor`.

## Deteccion De Camara

La interfaz usa los endpoints actuales del backend:

```text
GET  /api/streams/camera/status
POST /api/streams/camera/detect
```

La respuesta esperada es:

```json
{
  "sign": "GREEN_SIGN",
  "confidence": 92,
  "reason": "green_roi_dominant",
  "votes": {
    "green": 5,
    "red": 0,
    "black": 0,
    "none": 0
  }
}
```

## Camaras En Streaming

La URL frontal y la cenital se guardan en `localStorage`:

```text
robobet_front_url
robobet_overhead_url
```

La camara frontal actual suele ser:

```text
http://IP_ESP32_CAM/stream
```

Para camara cenital se puede usar cualquier fuente MJPEG o IP camera compatible con `<img>`.

## Twitch OAuth

El boton de Twitch esta preparado para OAuth implicito. Para activarlo:

1. Crear una aplicacion en Twitch Developer Console.
2. Configurar el redirect URI con la URL de la web, por ejemplo:

```text
http://127.0.0.1:8000/
```

3. Guardar el Client ID en el navegador:

```js
localStorage.setItem("robobet_twitch_client_id", "TU_CLIENT_ID")
```

4. Pulsar `Iniciar sesion con Twitch`.

Si no hay `robobet_twitch_client_id`, el boton crea una sesion demo para validar la UI sin depender de Twitch.

## Historial De Laberintos

La pantalla de usuario usa:

```text
GET /api/run/history
```

Campos actuales usados:

- `started_at`
- `elapsed_ms`
- `algorithm`
- `obstacle_count`
- `status`
- `speed_limit`
- `result`

Pendiente recomendado:

- Guardar numero de cruces en la tabla `runs`.
- Guardar restricciones activas como JSON.
- Guardar resultado normalizado: `finished`, `failed`, `stopped`, `interrupted`.

## WebSocket

La UI escucha:

```text
ws://HOST/ws/ui
```

Cada mensaje con `robot` actualiza el dashboard. Cada mensaje con `type` se anade al log de eventos.
