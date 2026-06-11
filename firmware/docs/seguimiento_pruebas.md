# Seguimiento De Pruebas E Incidencias

Este documento recoge los fallos encontrados durante las pruebas reales, la causa observada y las implementaciones aplicadas. La idea es que sirva como historial tecnico para no repetir diagnosticos.

## 2026-06-11 - QTR-8RC Siempre Marcaba Todo Negro

### Sintoma

- El robot daba vueltas al pulsar `Iniciar`.
- La lectura del QTR desde `/status` mostraba:

```text
3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000
```

- El mapa de linea mostraba los 8 sensores activos.
- El robot interpretaba esa lectura como mancha negra completa, es decir obstaculo/final.

### Diagnostico

- Se comparo el firmware actual con el firmware original de GitHub.
- La funcion de lectura `readQtrRaw()` era equivalente a la original.
- Los pines y el timeout eran los mismos:

```text
QTR_PINS = 13, 12, 14, 27, 26, 25, 33, 32
QTR_TIMEOUT_US = 3000
```

- Se descarto que el problema fuera la logica nueva de obstaculos/camara.
- Se comprobo que el valor `3000` en QTR-8RC significa que el pin no descarga antes del timeout.

### Implementacion Aplicada

- Se anadio el endpoint local del robot:

```text
http://IP_ROBOT/qtr-debug
```

- El endpoint no mueve motores.
- Solo se ejecuta si el robot no esta en carrera.
- Devuelve:

```text
pins
idle_levels
charged_high_levels
released_after_50us_levels
raw_us
timeout_us
```

### Resultado

- Al poner el sensor sobre superficie negra, la lectura seguia cerca de timeout.
- Al poner el sensor sobre blanco/linea normal, el QTR empezo a devolver valores reales.
- Lectura validada sobre linea centrada:

```text
2388, 2062, 3000, 3000, 3000, 2185, 1615, 2065
```

- Lectura validada con linea a la izquierda:

```text
3000, 3000, 2254, 1227, 1166, 1239, 1218, 2314
```

- Lectura validada con linea a la derecha:

```text
1220, 1107, 1019, 1059, 1156, 3000, 3000, 3000
```

### Estado

- El QTR funciona.
- El umbral actual `2500` parece razonable para distinguir linea negra normal.
- Una mancha negra ancha deberia activar 7/8 u 8/8 sensores y entrar en la logica de obstaculo/final.

## 2026-06-11 - Flasheo WROOM Fallaba Por COM3/Bootloader

### Sintoma

- PlatformIO fallaba al subir firmware por `COM3`.
- Errores observados:

```text
Failed to connect to ESP32: No serial data received
Could not open COM3, the port is busy or doesn't exist
```

### Diagnostico

- `COM3` aparecia como `Silicon Labs CP210x USB to UART Bridge`.
- Windows llego a mostrar el dispositivo en estado `Error`.
- Tras desconectar y reconectar USB, el puerto volvio a estado `OK`.

### Prueba Aplicada

- Se probo una velocidad de subida mas conservadora en `robot/platformio.ini`:

```ini
upload_speed = 115200
```

### Resultado

- Tras reconectar la WROOM, el puerto `COM3` volvio a estado `OK` y el flasheo completo funciono correctamente.
- La causa real fue el estado del puerto/bootloader, no la velocidad de subida.

### Estado

- `upload_speed = 115200` se retiro para recuperar la velocidad normal de carga.
- Si vuelve a fallar, primero comprobar estado de `COM3` antes de cambiar codigo.

## 2026-06-11 - `/status` No Actualizaba Sensores Con Motores Desactivados

### Sintoma

- `/qtr-debug` devolvia lecturas reales del QTR.
- `/status` seguia mostrando:

```text
0, 0, 0, 0, 0, 0, 0, 0
```

### Diagnostico

- El `loop()` sale antes de leer sensores cuando `motorsAllowed()` es falso.
- Esto pasa si `runActive = false` y `localMotorsEnabled = false`.
- Por tanto, `/status` puede quedarse con una lectura vieja mientras el robot esta parado.

### Implementacion Aplicada

- No se ha cambiado la logica de movimiento.
- Para diagnostico sin motores se usa `/qtr-debug`.

### Estado

- Pendiente decidir si conviene cambiar el `loop()` para leer sensores siempre, incluso con motores desactivados.
- Ese cambio es seguro en principio, pero afecta al comportamiento de telemetria y conviene hacerlo de forma consciente.

## 2026-06-11 - Primera Version Profesional Del Frontend

### Sintoma

- La web anterior funcionaba, pero tenia una estructura mas basica y mezclaba control, usuario, camaras y eventos en una sola pantalla.
- Faltaba una separacion clara entre pantalla de administrador y pantalla de usuario.

### Diagnostico

- El backend ya exponia APIs suficientes para estado, historial, usuarios, apuestas, votaciones, streams y WebSocket.
- La telemetria real del robot aun no envia todos los datos que la UI puede mostrar, especialmente sensores crudos y potencia real de motores.

### Implementacion Aplicada

- Se rehizo `frontend/index.html` con dos pantallas:
  - Administrador.
  - Usuario.
- Se rehizo `frontend/styles.css` con layout responsive, tarjetas, sensores visuales, camaras, gauges de motor y tabla de historial.
- Se rehizo `frontend/app.js` para:
  - Consumir APIs reales si estan disponibles.
  - Usar WebSocket `/ws/ui`.
  - Simular datos cuando falta backend, robot o telemetria concreta.
  - Preparar OAuth de Twitch.
  - Mantener apuestas, votaciones, usuarios e historial.
- Se documento la integracion en `docs/frontend_integracion.md`.

### Resultado

- La web se sirve desde `http://127.0.0.1:8000/`.
- `node --check frontend/app.js` pasa correctamente.
- `python -m compileall backend` pasa correctamente.

### Estado

- Cerrado como primera version visual y funcional.
- Implementado despues: el firmware ya envia `sensor_values`, `sensor_active`, `sensor_active_count`, `motor_left_percent` y `motor_right_percent`.

## 2026-06-11 - Telemetria Real De QTR Y Motores Para Dashboard

### Sintoma

- La web mostraba `Simulacion` en el panel de infrarrojos.
- El frontend estaba preparado para datos reales, pero el firmware no enviaba los 8 valores QTR ni la potencia real de motores.

### Diagnostico

- El robot enviaba `state`, `algorithm`, `speed_limit`, `line_position`, camara y umbral.
- Faltaban campos de sensores y motores.
- El `loop()` no actualizaba `lastSensorValues` cuando los motores estaban desactivados.

### Implementacion Aplicada

- Se anadio telemetria:
  - `sensor_values`
  - `sensor_active`
  - `sensor_active_count`
  - `qtr_threshold`
  - `qtr_calibrated`
  - `motor_left_pwm`
  - `motor_right_pwm`
  - `motor_left_percent`
  - `motor_right_percent`
- Se guarda el PWM real aplicado despues de `speedLimitPercent`.
- Se leen y guardan sensores aunque los motores esten desactivados, sin ejecutar logica de movimiento.

### Resultado

- `python -m platformio run -d robot` compila correctamente.
- La web puede cambiar el panel de infrarrojos de `Simulacion` a `Robot` cuando reciba telemetria nueva.

### Estado

- Cerrado para la telemetria QTR/motores validada en web.
- Pendiente flashear de nuevo si se quieren cargar los cambios posteriores de contador de cruces (`crossing_count`).

## 2026-06-11 - Base De Datos E Historial De Laberintos

### Sintoma

- La web tenia usuarios, apuestas y votaciones, pero faltaba guardar mas informacion util de cada carrera.
- El usuario pidio dejar almacenados usuarios, votaciones y tiempo del robot con parametros necesarios.

### Diagnostico

- La base de datos SQLite ya tenia tablas `users`, `runs`, `bets`, `polls`, `votes` y `events`.
- A `runs` le faltaban campos directos para cruces, restricciones y resumen de telemetria final.
- Las votaciones guardaban votos, pero la API no devolvia conteos agregados para mostrarlos de forma clara.

### Implementacion Aplicada

- Se ampliaron las migraciones de `backend/app/database.py`.
- Se anadieron a `runs`:
  - `crossing_count`
  - `restrictions`
  - `telemetry_summary`
- `POST /api/run/stop` y `POST /api/run/finish` guardan tiempo, obstaculos, cruces, resultado y resumen de telemetria.
- `GET /api/polls` devuelve `results` y `total_votes`.
- `GET /api/users/{id}` devuelve apuestas y votos del usuario.
- La pantalla de usuario muestra actividad reciente del usuario activo.
- El backend deja de guardar cada paquete `telemetry` en `events`; solo guarda eventos significativos para evitar que SQLite crezca sin aportar informacion util.

### Resultado

- `data/robobet.sqlite3` queda como base de datos local de la demo.
- La tabla de ultimos laberintos muestra tiempo, algoritmo, obstaculos, cruces, resultado y restricciones.
- Las votaciones muestran barras de resultado.

### Estado

- Cerrado a nivel backend/frontend.
- Pendiente prueba real completa para rellenar historial con una carrera fisica finalizada.

## 2026-06-11 - Backend No Respondia Tras Cambios

### Sintoma

- El navegador mostraba la pagina, pero las APIs de `http://127.0.0.1:8000` no respondian correctamente.
- El proceso `uvicorn` estaba arrancado pero el servidor no escuchaba bien.

### Diagnostico

- El import del backend fallaba con Python 3.9 por una anotacion moderna:

```text
list[str] | None
```

- Python 3.9 no soporta ese operador si las anotaciones no estan diferidas.

### Implementacion Aplicada

- Se anadio `from __future__ import annotations` en `backend/app/routers/run.py`.
- Se comprobo importacion con el Python del entorno virtual.

### Resultado

- `GET /api/run/state`, `GET /api/run/history` y `GET /api/polls` vuelven a responder.
- El robot aparece conectado y enviando telemetria QTR real.

### Estado

- Cerrado.

## 2026-06-11 - Revision De Logica Para Laberinto Completo

### Sintoma

- Habia que confirmar si la logica actual permite hacer el laberinto entero con las restricciones acordadas.

### Diagnostico

- El robot distingue:
  - Linea normal: sigue la posicion ponderada del QTR.
  - Cruce soportado: izquierda+recto o derecha+recto, nunca izquierda+derecha simultaneamente.
  - Mancha negra: 7/8 o mas sensores activos, confirmada durante `BLACK_PATCH_CONFIRM_MS`.
  - Verde: cruza la mancha.
  - Rojo, timeout o sin senal fiable: media vuelta y marca la ultima decision como bloqueada.
  - Negro: final de laberinto.
- La logica actual es suficiente para la prueba esperada si el laberinto cumple esas reglas fisicas.
- No es un solver topologico completo de laberintos arbitrarios: no reconoce el mismo nodo al volver desde otro lado, sino que aplica una exploracion practica por eventos y bloqueos.

### Implementacion Aplicada

- Se anadio `crossing_count` a telemetria para que backend e historial puedan registrar cruces.
- Se mantuvo la decision de seguridad: senal dudosa equivale a rojo/media vuelta.

### Resultado

- `python -m platformio run -d robot` compila correctamente.
- El backend ya puede guardar `crossing_count` cuando llegue la telemetria nueva.

### Estado

- Listo para prueba fisica controlada.
- Pendiente flashear WROOM con el cambio de `crossing_count` si se quiere registrar ese campo desde el robot real.

## Plantilla Para Nuevas Incidencias

```text
## FECHA - TITULO

### Sintoma
- Que se observo.

### Diagnostico
- Pruebas hechas.
- Causa probable.

### Implementacion Aplicada
- Archivos modificados.
- Cambios relevantes.

### Resultado
- Lecturas, comandos o comportamiento observado.

### Estado
- Cerrado, pendiente o siguiente prueba.
```
