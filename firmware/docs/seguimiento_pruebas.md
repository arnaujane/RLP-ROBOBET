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

## 2026-06-11 - Calidad Y Estabilidad De La ESP32-CAM Frontal

### Sintoma

- La camara frontal podia mostrar ruido, colores verdosos/morados, lineas horizontales o frames con aspecto corrupto.
- Cuando la web mantenia abierto `/stream`, las llamadas a `/detect` y `/status` podian tardar demasiado o fallar.

### Diagnostico

- La ESP32-CAM usa `WebServer`, que atiende peticiones de forma sincronica.
- Un MJPEG continuo en `/stream` mantiene la conexion abierta y puede bloquear otras rutas HTTP importantes.
- La camara trabajaba con captura RGB565 y conversion a JPEG en cada frame; con alimentacion justa, baja luz o WiFi debil, esto aumenta artefactos y latencia.

### Implementacion Aplicada

- Firmware ESP32-CAM:
  - `CAMERA_XCLK_FREQ_HZ` por defecto en `10000000` para priorizar estabilidad.
  - Resolucion conservadora `FRAMESIZE_QVGA`.
  - Un unico framebuffer para evitar frames pisados.
  - JPEG de snapshot en calidad `82`.
  - Deteccion de frames incompletos/corruptos antes de procesar o convertir.
  - Contadores de salud: `capture_failures`, `consecutive_failures`, `corrupt_frames`, `camera_ready`, `camera_health`.
  - Recuperacion automatica con `esp_camera_deinit()` + nueva inicializacion tras fallos consecutivos.
  - Deteccion de fondo menos agresiva para no saturar la camara.
  - `/stream` queda para diagnostico corto; la web usa `/snapshot`.
- Backend:
  - Default frontal cambiado a `http://192.168.1.56/snapshot`.
  - Errores de socket/camara se devuelven como `504`, no como `500`.
- Frontend:
  - La camara frontal usa snapshots con refresco, cache-busting y reconexion automatica.
  - Si falla la imagen se muestra placeholder profesional y estado `Reconectando`/`Sin imagen`.
  - Se muestran FPS y calidad de camara.

### Resultado

- `/api/streams/camera/status` devuelve `camera_ready=true` y `camera_health=ok`.
- `/snapshot` devuelve JPEG valido.
- `/detect` vuelve a responder sin bloquearse por el stream de la web.

### Estado

- Cerrado a nivel software.
- Si reaparecen artefactos, revisar alimentacion estable a 5 V, masa comun, cable de camara, iluminacion del laberinto, WiFi y vibraciones del soporte.

## 2026-06-12 - Optimizacion MJPEG Nativa ESP32-CAM

### Sintoma

- La imagen de la ESP32-CAM podia verse menos fluida que otros ejemplos de ESP32-CAM.
- La captura RGB565 obligaba a convertir a JPEG por software para mostrar imagen en web.
- Se queria comparar perfiles de FPS/calidad/estabilidad de forma limpia.

### Diagnostico

- Para streaming, `PIXFORMAT_JPEG` es mas adecuado que RGB565 porque la OV2640 entrega JPEG ya comprimido.
- El MJPEG continuo debe separarse de `/detect` para no bloquear decisiones del robot.
- La deteccion de senales puede decodificar JPEG solo cuando se pide una decision, no en cada frame visual.

### Implementacion Aplicada

- Firmware ESP32-CAM:
  - Captura principal en `PIXFORMAT_JPEG`.
  - Perfiles `FLUID`, `BALANCED` y `STABLE`.
  - Servidor MJPEG dedicado en `http://IP_CAM:81/stream`.
  - Puerto `80` reservado para `/status`, `/snapshot` y `/detect`.
  - Validacion de JPEG por tamano minimo, cabecera `FFD8` y marcador final `FFD9`.
  - `/detect` decodifica JPEG a RGB888 solo al muestrear senales.
  - `configureCameraSensor()` centraliza brillo, contraste, saturacion, nitidez, AWB, AEC, ganancia, gamma/correcciones, mirror y flip.
  - `WiFi.setSleep(false)`, autoreconexion y potencia WiFi alta.
  - Estado ampliado con perfil, PSRAM, RSSI, bytes/frame, ms de captura, heap y PSRAM libre.
- Backend:
  - Default frontal a `http://192.168.1.56:81/stream`.
  - Si recibe una URL de stream en puerto `81`, consulta `/status` y `/detect` en puerto `80`.
- Frontend:
  - Soporta MJPEG y snapshot.
  - En MJPEG solo abre stream en la pestana visible para no duplicar conexiones.
  - Reconecta con cache-busting y mantiene placeholder profesional.

### Resultado

- `python -m platformio run -d esp32_cam` compila correctamente.
- `node --check frontend/app.js` compila correctamente.
- `python -m compileall backend` compila correctamente.
- Flasheo ESP32-CAM-MB por `COM4` correcto, sin pulsar botones.
- `/status` tras flasheo:
  - `profile=FLUIDO`
  - `pixel_format=JPEG`
  - `frame_size=QVGA`
  - `jpeg_quality=12`
  - `xclk_hz=20000000`
  - `psram_found=true`
  - `fb_count=2`
  - `grab_mode=latest`
  - `rssi=-51`
  - `corrupt_frames=0`
  - `capture_failures=0`
- `/snapshot` devuelve JPEG valido.
- `http://192.168.1.56:81/stream` devuelve MJPEG valido.

### Estado

- Firmware nuevo cargado en la ESP32-CAM.
- Pendiente comparar calidad real con buena iluminacion. La primera captura tras flasheo llego completa, pero se ve oscura; si persiste, revisar luz frontal/difusa, foco de lente, alimentacion y vibraciones.

## 2026-06-12 - Ajuste De Deteccion Verde En Baja Luz

### Sintoma

- La senal verde costaba de detectar.
- En escenas oscuras o con contraluz, la ROI acumulaba muchos pixeles negros y el verde quedaba por debajo del umbral anterior.

### Diagnostico

- El umbral antiguo exigia `g > 85` y una dominancia alta contra rojo/azul.
- La decision tambien penalizaba el color si el fondo oscuro generaba muchos pixeles negros.
- En una prueba previa la camara devolvia `BLACK_SIGN` aunque habia zona verdosa visible.

### Implementacion Aplicada

- Saturacion del sensor en `0` para no apagar tanto el color.
- Autoexposicion con `ae_level=1` para levantar escenas oscuras.
- Umbrales separados:
  - rojo minimo `2%`.
  - verde minimo `1%`.
  - canal verde minimo `45`.
  - delta verde minimo `14` frente a rojo y azul.
- La clasificacion prioriza rojo/verde cuando hay color dominante claro, y deja `BLACK_SIGN` para casos donde el negro domina sin color significativo.

### Resultado

- Tras flashear ESP32-CAM por `COM4`, `/status` queda en `camera_health=ok`, `pixel_format=JPEG`, `profile=FLUIDO`.
- Antes del ajuste: `BLACK_SIGN`, `green_pixels=0`.
- Despues del ajuste: `/detect` devuelve `GREEN_SIGN`, `votes.green=3/3`, `green_pixels=213`, `confidence=54`.
- No hay `corrupt_frames` ni `capture_failures`.

### Estado

- Cerrado para prueba inicial.
- Si hay falsos verdes con luz ambiente, subir `GREEN_MIN_CHANNEL` o `GREEN_MIN_DELTA`.
- Si todavia no detecta verde, mejorar iluminacion frontal/difusa y evitar contraluz directo en la camara.

## 2026-06-12 - Ajuste Rojo Contra Falsos Verdes

### Sintoma

- La senal roja se detectaba mal.
- Con mucha luz, el rojo podia lavarse y la camara podia interpretarlo como verde.

### Diagnostico

- La ESP32-CAM/OV2640 puede desplazar colores con AWB y autoexposicion si hay contraluz o luz directa.
- Un rojo sobreexpuesto puede parecer naranja/amarillo: rojo y verde suben a la vez, mientras azul queda bajo.
- El umbral verde anterior era demasiado permisivo frente al canal rojo.

### Implementacion Aplicada

- `CAMERA_SENSOR_AE_LEVEL` vuelve a `0` para reducir sobreexposicion.
- Rojo mas sensible:
  - `MIN_RED_PERCENT=1`.
  - `RED_MIN_CHANNEL=55`.
  - `RED_MIN_DELTA=12`.
- Verde mas estricto:
  - `GREEN_MIN_CHANNEL=52`.
  - `GREEN_MIN_DELTA=22`.
  - El canal verde debe superar claramente al rojo y azul.
- Se anadio deteccion de rojo calido/anaranjado:
  - Si rojo y verde estan altos pero azul queda claramente bajo, se cuenta como rojo, no como verde.

### Resultado

- Firmware compila y se flashea correctamente en ESP32-CAM por `COM4`, sin pulsar botones.
- Estado tras flasheo: `camera_health=ok`, `pixel_format=JPEG`, `profile=FLUIDO`, `corrupt_frames=0`.
- La captura de validacion disponible salio practicamente negra en toda la ROI, por lo que no confirma rojo real.

### Estado

- Firmware cargado y preparado para prueba fisica con senal roja visible en la ROI.
- Si el rojo sigue saliendo verde, subir aun mas `GREEN_MIN_DELTA` o fijar mejor la iluminacion para evitar reflejos directos.
- Si el rojo no aparece como rojo, bajar ligeramente `RED_MIN_CHANNEL` o acercar/centrar mas la senal en la ROI.

## 2026-06-12 - Maze Solver Sin Encoders

### Sintoma

- La lectura QTR ya esta resuelta y toca asegurar que el robot pueda completar un laberinto entero.
- El robot no tendra encoders, asi que no se puede medir distancia real de rueda.
- El controlador de motores confirmado es HW-166/TB6612FNG.

### Diagnostico

- El firmware ya seguia linea y tenia estados iniciales de nodo/obstaculo, pero no guardaba un grafo real del laberinto.
- Los nodos se creaban de forma secuencial sin coordenadas ni orientacion, por lo que una vuelta al mismo cruce podia confundirse con un nodo nuevo.
- Los giros dependian solo de detectar linea centrada; faltaban tiempos calibrables para avanzar hasta el eje de ruedas y para giros de 90/180 grados sin encoders.

### Implementacion Aplicada

- `robot/src/main.cpp` ahora guarda nodos con coordenadas aproximadas `(x, y)` y orientacion absoluta `NORTH/EAST/SOUTH/WEST`.
- Las salidas de cada nodo se almacenan en direcciones absolutas, no solo como izquierda/recto/derecha relativas.
- Se anadio registro de aristas, vecinos, salidas probadas, salidas bloqueadas y nodo final.
- DFS explora con prioridad `RIGHT -> STRAIGHT -> LEFT -> BACK`.
- BFS usa el mismo mapa descubierto y calcula el camino mas corto cuando se detecta el final.
- La marca negra de `7/8` sensores sigue tratandose como obstaculo/final y dispara la camara.
- Semantica de camara mantenida:
  - `GREEN_SIGN`: el obstaculo se puede pasar.
  - `RED_SIGN`, `NO_SIGN` o `TIMEOUT`: salida bloqueada y media vuelta.
  - `BLACK_SIGN`: final del laberinto.
- Como no hay encoders, se anadieron parametros calibrables:
  - `msPerCm`
  - `msTurn90`
  - `msTurn180`
  - `lineReacquireMs`
- La UI local del robot expone estos parametros para ajustar movimientos sin recompilar.
- La telemetria incluye orientacion, tipo de cruce, salidas disponibles, nodos, aristas, arista activa, nodo final y longitud del camino calculado.

### Resultado

- `python -m platformio run -d robot` compila correctamente.
- No se ha flasheado en esta entrada; queda pendiente cargarlo en la ESP32-WROOM y probar en laberinto fisico.

### Estado

- Pendiente prueba fisica completa.
- Primera prueba recomendada:
  - Calibrar QTR.
  - Comprobar que en un cruce izquierda+recto el evento `EDGE_SELECTED` muestra salidas correctas.
  - Comprobar que en un cruce derecha+recto el evento muestra salidas correctas.
  - Ajustar `msTurn90` si gira corto/largo.
  - Ajustar `msTurn180` si la media vuelta no vuelve a centrar linea.
  - Ajustar `msPerCm` si al centrar en nodo se pasa o se queda corto.
  - Probar obstaculo con verde, rojo y negro.

## 2026-06-12 - Correccion Cruces Antes De Obstaculos

### Sintoma

- El robot no parecia guardar bien los cruces.
- Al girar no se veia claramente que respetara la restriccion de motores.
- Antes de seguir con obstaculos, se quiere comprobar solo que DFS/BFS elijan recto o giro correctamente.

### Diagnostico

- El firmware detectaba el cruce, avanzaba `8.5 cm` para centrar el eje de ruedas y despues volvia a leer los QTR para decidir salidas.
- Esa segunda lectura puede perder la rama lateral porque los sensores ya han pasado por encima del cruce.
- La web no mostraba suficientes campos del grafo para confirmar si se estaban guardando nodos/aristas.
- Durante giros largos no siempre llegaba telemetria intermedia, por lo que la web podia mostrar una potencia sintetica o antigua.

### Implementacion Aplicada

- Las salidas del nodo se calculan con la lectura que confirma el cruce.
- El avance de centrado se mantiene solo para colocar el robot antes de girar.
- La gestion de obstaculos queda desactivada por defecto para estas pruebas (`obstacleHandlingEnabled=false`).
- Con obstaculos desactivados, una zona negra ancha se interpreta como cruce, no como llamada a camara.
- `driveFor()` y `rotateForTurn()` envian telemetria mientras se mueven y paran si el robot deja de estar habilitado.
- La UI local muestra y permite activar/desactivar `Gestion de obstaculos`.
- La web principal muestra nodos, aristas, orientacion y salidas relativas.

### Resultado

- `python -m platformio run -d robot` compila correctamente.
- `node --check frontend/app.js` compila correctamente.

### Estado

- Pendiente flasheo y prueba fisica.
- Prueba esperada:
  - DFS en cruce derecha+recto debe elegir derecha primero.
  - BFS en cruce derecha+recto debe elegir recto primero.
  - La web debe mostrar `Nodos`, `Aristas`, orientacion y salidas en la tarjeta de algoritmo/eventos.

## 2026-06-12 - Modo Pausa Antes De Giro

### Sintoma

- Para validar DFS/BFS se necesita ver la decision del robot antes de que ejecute fisicamente el giro.
- El objetivo inmediato es comprobar cruces, no obstaculos.

### Implementacion Aplicada

- Nuevo comando web `SET_TURN_PAUSE`.
- Nuevo comando web `CONTINUE_TURN`.
- Nuevo estado robot `WAITING_TURN_CONFIRMATION`.
- Si la pausa esta activada:
  - El robot detecta el cruce.
  - Calcula el giro segun DFS/BFS.
  - Envia `EDGE_SELECTED` y `TURN_WAITING_CONFIRMATION`.
  - Se queda parado con motores a `0`.
  - Solo ejecuta el giro al recibir `CONTINUE_TURN`.
- La web de administrador incluye:
  - `Pausar en cruces`.
  - `Quitar pausa`.
  - `Continuar giro`.
- La telemetria incluye:
  - `pending_turn`.
  - `pending_exit_orientation`.
  - `pause_before_turn_enabled`.
  - `waiting_for_turn_confirmation`.

### Resultado

- `python -m platformio run -d robot` compila correctamente.
- `node --check frontend/app.js` compila correctamente.
- `python -m compileall backend` compila correctamente.

### Estado

- Pendiente flasheo y prueba fisica.
- Flujo de prueba:
  - Pulsar `Pausar en cruces`.
  - Iniciar carrera con DFS o BFS.
  - Al llegar al cruce, mirar evento `TURN_WAITING_CONFIRMATION`.
  - Confirmar si `pending_turn` es correcto.
  - Pulsar `Continuar giro`.

## 2026-06-12 - Calibracion Avance Y Giro En Cruce

### Sintoma

- En modo pausa antes de giro se comprobo que el robot se quedaba corto antes de girar.
- Faltaban aproximadamente `4 cm` para que el eje de ruedas llegara al centro correcto del cruce.
- El giro de 90 grados giraba aproximadamente `10 grados` de mas.

### Diagnostico

- El avance de centrado estaba en `8.5 cm`.
- La calibracion real de pista requiere `12.5 cm`.
- El giro de 90 grados usaba `620 ms`; si ese tiempo produce unos `100 grados`, el ajuste proporcional para 90 grados es `620 * 90 / 100`, aproximadamente `560 ms`.
- La media vuelta se ajusta con la misma proporcion: `1240 ms` pasa a `1120 ms`.

### Implementacion Aplicada

- `SENSOR_TO_WHEEL_AXIS_CM = 12.5`.
- `DEFAULT_MS_TURN_90 = 560`.
- `DEFAULT_MS_TURN_180 = 1120`.

### Resultado

- Pendiente compilar, flashear y repetir prueba de cruce.

### Estado

- Si aun gira de mas, bajar `msTurn90` en pasos de `20 ms`.
- Si gira de menos, subir `msTurn90` en pasos de `20 ms`.
- Si vuelve a quedarse corto/largo antes del giro, ajustar `SENSOR_TO_WHEEL_AXIS_CM` o temporalmente `msPerCm` desde la UI local.

## 2026-06-12 - Giro Guiado Por Sensor Lateral

### Sintoma

- Al avanzar hasta centrar el eje de ruedas antes del giro, si el robot entraba un poco inclinado, se desviaba bastante.
- Los QTR no son suficientemente precisos para confiar solo en giro temporizado.

### Diagnostico

- El avance de centrado era ya de distancia correcta, pero necesitaba mantenerse sobre la linea durante ese avance.
- En giros izquierda/derecha conviene usar los sensores laterales como referencia real:
  - giro derecha: buscar linea con los sensores derechos.
  - giro izquierda: buscar linea con los sensores izquierdos.
  - despues seguir girando hasta que los sensores centrales vean la linea estable.

### Implementacion Aplicada

- `centerOnNode()` ahora avanza siguiendo la linea, no con motores rectos a ciegas.
- `GO_STRAIGHT` despues de un cruce tambien avanza siguiendo la linea.
- `rotateForTurn(LEFT/RIGHT)` queda en dos fases:
  - fase 1: girar hasta que el sensor exterior vea linea.
  - fase 2: continuar hasta que el centro vea linea de forma estable.
- El tiempo `msTurn90` queda como fallback de seguridad si el sensor exterior no detecta.
- La media vuelta conserva el comportamiento temporizado con centrado por QTR.

### Resultado

- Pendiente compilar, flashear y repetir prueba con pausa antes de giro.

### Estado

- Si el giro se queda corto, revisar si los sensores exteriores ven la rama lateral demasiado pronto.
- Si se pasa, bajar `msTurn90` o reducir `TURN_CENTER_CONFIRM_MS`.
- Si oscila al centrar, bajar `turnPwm` desde la UI local.

## 2026-06-12 - Prioridad DFS Y Media Vuelta Sensada

### Sintoma

- En un cruce `izquierda + recto`, tanto DFS como BFS seguian recto.
- Esto parecia incorrecto durante la prueba de giro izquierda.
- Tambien se quiere que la media vuelta use realimentacion de QTR, no solo tiempo.

### Diagnostico

- La prioridad DFS era `RIGHT -> STRAIGHT -> LEFT -> BACK`.
- Por eso, si no habia derecha pero si recto e izquierda, DFS elegia recto igual que BFS.
- La media vuelta podia detenerse por ver linea central sin haber completado realmente 180 grados.

### Implementacion Aplicada

- Prioridad DFS cambiada a `RIGHT -> LEFT -> STRAIGHT -> BACK`.
- BFS se mantiene como `STRAIGHT -> RIGHT -> LEFT -> BACK`.
- Media vuelta actualizada:
  - gira hasta perder la linea central.
  - espera al menos `75%` del tiempo teorico de 180 grados.
  - termina solo cuando vuelve a ver linea centrada estable.

### Resultado

- Pendiente compilar, flashear y repetir prueba.

### Estado

- En `izquierda + recto`, DFS debe elegir `LEFT` y BFS debe elegir `STRAIGHT`.
- En `derecha + recto`, DFS debe elegir `RIGHT` y BFS debe elegir `STRAIGHT`.

## 2026-06-12 - Obstaculos Manuales Desde Web

### Sintoma

- Se quiere probar la logica de obstaculos aunque la camara todavia no detecte perfecto.
- Hace falta poder decir manualmente si el obstaculo es verde, rojo o negro desde la pagina web.

### Implementacion Aplicada

- Nuevos comandos robot:
  - `SET_OBSTACLE_HANDLING`.
  - `SET_OBSTACLE_SIGN_OVERRIDE`.
- Si el override manual esta activo, el robot no llama a la camara al detectar marca negra.
- Semantica manual:
  - `GREEN_SIGN`: pasa el obstaculo.
  - `RED_SIGN`: bloquea el camino y hace media vuelta.
  - `BLACK_SIGN`: finaliza la carrera.
  - `CAMERA_AUTO`: desactiva la espera manual y vuelve a usar `/detect`.
- La web de administrador permite:
  - activar/desactivar obstaculos.
  - resolver manualmente con botones `Verde pasa`, `Rojo media vuelta`, `Negro final`.
  - volver a `Camara auto` sin reflashear.
- Telemetria nueva:
  - `obstacle_handling_enabled`.
  - `manual_obstacle_sign_enabled`.
  - `manual_obstacle_sign`.

### Resultado

- Pendiente compilar, flashear y probar con marcas negras.

### Estado

- Para probar sin camara:
  - activar obstaculos.
  - iniciar carrera y dejar que el robot se pare en una marca negra.
  - pulsar `Verde pasa`, `Rojo media vuelta` o `Negro final`.

## 2026-06-12 - Decision Manual En Obstaculo Y Retorno Al Nodo

### Sintoma

- Al llegar a una marca negra se necesitaba mas tiempo para decidir si el obstaculo era verde, rojo o negro.
- El desplegable era lento para una prueba fisica.
- Despues de un obstaculo rojo, el robot volvia al cruce anterior pero podia tratar esa T como un cruce nuevo normal.

### Diagnostico

- Para probar sin camara conviene que el robot se pare en la marca negra y espere una decision explicita desde la web.
- El camino rojo debe marcarse como bloqueado en el nodo de origen.
- Al volver al mismo nodo, el robot debe conservar ese nodo y elegir otra salida segun DFS/BFS con la orientacion real que tiene despues de la media vuelta.

### Implementacion Aplicada

- Nuevo estado `WAITING_OBSTACLE_DECISION`.
- Nuevo estado `RETURNING_FROM_BLOCKED_OBSTACLE`.
- Nuevo comando `RESOLVE_OBSTACLE` con `GREEN_SIGN`, `RED_SIGN`, `BLACK_SIGN` o `CAMERA_AUTO`.
- Nuevo comando `SET_OBSTACLE_DECISION_WAIT` para alternar entre parar/preguntar y camara automatica.
- La web cambia el desplegable por botones:
  - `Verde pasa`.
  - `Rojo media vuelta`.
  - `Negro final`.
  - `Camara auto`.
- Los sensores infrarrojos se han subido en el dashboard para verlos antes que los paneles secundarios.
- Si el obstaculo es rojo o la camara no esta segura:
  - se marca la orientacion elegida como bloqueada.
  - se hace media vuelta usando el mismo mecanismo sensado.
  - al llegar al cruce anterior se actualizan las salidas vistas por QTR.
  - DFS/BFS elige otra salida no bloqueada.

### Resultado

- Compilacion robot correcta con `python -m platformio run -d robot`.
- JS correcto con `node --check frontend/app.js`.
- Backend correcto con `python -m compileall backend`.

### Estado

- Pendiente flashear y probar en pista:
  - obstaculo verde: debe cruzar la marca negra y seguir.
  - obstaculo rojo: debe volver al cruce anterior y escoger otra salida.
  - obstaculo negro: debe finalizar carrera.
  - `Camara auto`: debe usar `/detect` cuando no se quiera decision manual.

## 2026-06-12 - Retorno Rojo Confundido Con Obstaculo

### Sintoma

- En tres pruebas iguales, el robot hacia bien el primer giro y llegaba al obstaculo rojo.
- Al recibir rojo, hacia media vuelta y cogia la linea correctamente.
- Al volver al cruce anterior, la T saturaba los ocho infrarrojos.
- Esa T se interpretaba como otra marca negra de obstaculo, aunque en ese contexto debia ser el nodo anterior.

### Diagnostico

- La deteccion de `blackPatch` tenia prioridad absoluta sobre cruces cuando `obstacleHandlingEnabled` estaba activo.
- Eso es correcto para obstaculos normales, pero no para el primer cruce encontrado despues de volver desde un obstaculo rojo.
- En ese retorno el robot ya sabe que la siguiente zona negra ancha tiene que ser el nodo de origen de la arista bloqueada.
- Ademas, una lectura saturada no sirve para aprender salidas nuevas, porque podria inventar salidas falsas.

### Implementacion Aplicada

- Mientras `returningFromBlockedObstacle` esta activo:
  - `blackPatch` deja de tratarse como obstaculo.
  - la zona negra ancha se clasifica como cruce/nodo.
  - `handleNode()` usa `returnNodeAfterBlockedObstacle` en vez de crear o resolver un nodo nuevo.
  - si la lectura esta saturada, no se actualizan salidas desde los QTR.
  - las salidas relativas se calculan desde el mapa absoluto ya conocido del nodo.
- Nueva conversion auxiliar `absoluteOptionsToRelative()`.
- El estado de telemetria se mantiene como `RETURNING_FROM_BLOCKED_OBSTACLE` hasta llegar al nodo.

### Resultado

- Compilacion robot correcta con `python -m platformio run -d robot`.

### Estado

- Pendiente flashear y repetir la prueba:
  - despues del rojo, al volver a la T no debe pedir obstaculo.
  - debe elegir la salida alternativa no bloqueada.
  - si venia de la arista izquierda y esa ya esta explorada/bloqueada, debe escoger la arista derecha disponible.

## 2026-06-12 - Proteccion PID Ante Cruces En Diagonal

### Sintoma

- En algunas ejecuciones el robot entra a un cruce u obstaculo en diagonal.
- Al activarse sensores de una esquina, el seguimiento de linea corrige hacia ese lado.
- Esa correccion puede hacer que una rama lateral, cruce u obstaculo se trate como si fuera una desviacion normal de la linea principal.

### Diagnostico

- La deteccion de eventos y el calculo de posicion para el PID usaban la misma lectura completa de QTR.
- Si los sensores centrales seguian viendo linea, una activacion brusca de sensores laterales debia considerarse primero candidato a cruce/obstaculo, no correccion de trayectoria.

### Implementacion Aplicada

- Se mantiene la deteccion de eventos con los 8 sensores.
- Si hay linea central y aparece una rama lateral o zona negra ancha:
  - `lineTrackingProtected` se activa.
  - el PID calcula la posicion solo con la banda central ampliada `S3-S6` (`indices 2..5`).
  - los sensores extremos no arrastran la correccion lateral.
- La confirmacion de cruce/obstaculo sigue funcionando con los sensores completos.
- Telemetria nueva:
  - `line_tracking_protected`.
  - UI local: `Proteccion PID: SI/NO`.
  - Dashboard principal: texto `PID protegido` junto a sensores.

### Resultado

- Compilacion robot correcta con `python -m platformio run -d robot`.
- JS correcto con `node --check frontend/app.js`.

### Estado

- Pendiente flashear y probar:
  - entrada diagonal a T.
  - entrada diagonal a marca negra.
  - comprobar que detecta el nodo sin girar hacia la esquina.

## 2026-06-12 - Backtracking Multi Nodo Y Memoria De Aristas

### Sintoma

- En un laberinto con varios cruces, el robot puede llegar a una rama bloqueada por rojo y tener que volver varios nodos atras.
- La logica anterior volvia bien al nodo inmediato, pero no navegaba de forma explicita hasta otro nodo antiguo con salidas pendientes.
- Un obstaculo verde ya validado podia volver a pedir decision al pasar de nuevo por la misma arista.

### Diagnostico

- Hacia falta separar dos decisiones:
  - explorar una salida nueva del nodo actual.
  - moverse por aristas conocidas hasta un nodo con salidas sin probar.
- Tambien hacia falta memoria por arista:
  - bloqueada por rojo.
  - transitable por verde.
  - visitada/conectada entre nodos.

### Implementacion Aplicada

- Nuevo estado `RETURNING_TO_UNFINISHED_NODE`.
- Cada nodo guarda:
  - `options`: salidas vistas por QTR.
  - `tried`: salidas ya intentadas.
  - `blocked`: salidas bloqueadas por rojo.
  - `passableGreen`: salidas con obstaculo verde ya validado.
  - `parent`: nodo desde el que se descubrio.
- Cada arista guarda:
  - `blocked`.
  - `passableGreen`.
- Nueva seleccion `chooseNavigationTurn()`:
  - si el nodo actual tiene una salida nueva, la explora segun DFS/BFS.
  - si no tiene salidas nuevas, busca un nodo reachable con salidas pendientes.
  - DFS prioriza el frontier mas reciente.
  - BFS prioriza el frontier mas cercano.
  - calcula el siguiente paso por aristas conocidas no bloqueadas.
- Si no queda ningun nodo pendiente alcanzable, envia `EXPLORATION_EXHAUSTED` y detiene la carrera.
- Las lecturas de salida del nodo combinan:
  - lectura al entrar al cruce.
  - lectura despues de centrar el eje de ruedas.
- Si una arista tiene `passableGreen`, una marca negra en esa arista se cruza recto sin volver a pedir decision.

### Resultado

- Pendiente compilacion y flasheo final.

### Estado

- Prueba objetivo:
  - explorar primero una rama a la derecha.
  - encontrar rojo en una rama profunda.
  - volver por varios nodos hasta el primer cruce pendiente.
  - cruzar obstaculos verdes ya conocidos sin preguntar.
  - elegir la siguiente salida sin bloquearse ni repetir indefinidamente.

## 2026-06-12 - Nodo Intermedio Inventaba Una Tercera Arista En Backtracking

### Sintoma

- Durante backtracking despues de una senal roja, el robot llegaba a un nodo intermedio con solo dos aristas reales.
- Ese nodo se detecto como si tuviera tres aristas.
- La navegacion queria girar a la derecha, aunque el camino correcto era seguir el camino conocido hacia la izquierda para pasar por el obstaculo verde ya validado.

### Diagnostico

- La navegacion `RETURNING_TO_UNFINISHED_NODE` calculaba un camino conocido hacia un nodo pendiente.
- Pero al pasar por nodos intermedios, `handleNode()` seguia aceptando nuevas salidas detectadas por QTR.
- Si una lectura diagonal o saturada inventaba una salida, `chooseNavigationTurn()` la trataba como salida nueva y la exploraba.
- En un nodo intermedio de backtracking eso es incorrecto: debe seguir la ruta conocida hasta el nodo objetivo y no aprender salidas nuevas.

### Implementacion Aplicada

- Si `navigatingToFrontier` esta activo y el nodo actual no es el objetivo:
  - se usa `navigationNextNode` como nodo esperado.
  - no se actualizan `options` desde QTR.
  - se ignoran las salidas QTR del nodo intermedio.
  - se calcula el siguiente giro con `chooseKnownPathTurnToTarget()`.
- Solo cuando se alcanza `navigationTargetNode` se vuelve a aceptar QTR y explorar salidas nuevas.
- Nuevo evento `KNOWN_PATH_NODE_REACHED` con:
  - `qtr_options_ignored`.
  - `target_node`.
  - `expected_node`.
- Nuevo evento `KNOWN_PATH_LOST` si el camino conocido deja de ser navegable.

### Visualizacion

- La web ahora dibuja el grafo detectado desde la telemetria:
  - nodos con id.
  - aristas normales.
  - aristas verdes ya transitables.
  - aristas rojas bloqueadas.
  - nodo actual.
  - nodos con salidas pendientes.

### Estado

- Pendiente probar de nuevo el caso de la imagen.

## 2026-06-12 - Grafo Con Salidas Pendientes Y Obstaculos

### Sintoma

- La vista de grafo solo mostraba aristas entre nodos ya conectados.
- No mostraba una salida detectada pero todavia no recorrida, aunque el robot ya la habia visto con QTR.
- Tampoco quedaba claro donde estaba un obstaculo verde o rojo si aun no habia nodo conectado al otro lado.

### Implementacion Aplicada

- El grafo web dibuja salidas detectadas sin arista como flechas amarillas `Salida pendiente`.
- Si una salida esta marcada como verde, se dibuja con flecha/etiqueta `GREEN`.
- Si una salida esta bloqueada por rojo, se dibuja con flecha/etiqueta `RED`.
- Las aristas reales siguen mostrando:
  - gris normal.
  - verde transitable.
  - rojo discontinuo bloqueado.
- Los nodos `frontier` se siguen resaltando cuando tienen salidas pendientes.

### Resultado

- No requiere reflashear solo para visualizar, porque usa `options`, `tried`, `blocked` y `green` ya enviados por telemetria.

### Estado

- En el primer cruce deberia verse la salida alternativa aunque aun no se haya recorrido.
- En el punto de la exclamacion no deberia aparecer salida alternativa a la derecha si el QTR no la detecto.

## 2026-06-12 - Salida Recta Falsa En Giros Simples

### Sintoma

- En un giro simple a la derecha, sin posibilidad fisica de seguir recto, el grafo dibujaba una salida recta pendiente.
- El nodo marcado como `1` en la prueba aparecia con una arista alternativa que no existe en el laberinto.

### Diagnostico

- `handleNode()` mezclaba las salidas vistas al entrar al cruce con las salidas vistas despues de centrar el eje de ruedas.
- La lectura de entrada es buena para no perder ramas laterales, pero no sirve para confirmar `STRAIGHT`.
- En un giro a 90 grados, los sensores centrales pueden seguir viendo la linea de entrada o la zona del cruce y eso se estaba guardando como salida recta.
- Ademas, `optionsFromFrame()` tenia un fallback que convertia cualquier linea vista en `DIR_STRAIGHT` si no habia otras opciones.

### Implementacion Aplicada

- La lectura inicial del cruce solo puede aportar `LEFT` y `RIGHT`.
- La salida `STRAIGHT` solo se acepta con una lectura QTR hecha despues de `centerOnNode()`.
- Se elimina el fallback que inventaba `STRAIGHT` desde una lectura generica de linea.
- Nuevo helper `straightExitConfirmedFromFrame()` para centralizar el criterio de salida recta.
- Nuevo evento `NODE_EXITS_CLASSIFIED` con:
  - `entry_options_no_straight`.
  - `centered_options`.
  - `straight_confirmed`.
  - `center_active_count`.
  - `relative_options`.

### Resultado

- En un giro simple derecha/izquierda, el grafo no debe dibujar salida recta salvo que el QTR la confirme al estar centrado en el nodo.
- En una T real con recto disponible, la lectura centrada debe seguir marcando `STRAIGHT`.

### Estado

- Pendiente compilar, flashear y repetir el nodo de la exclamacion.

## 2026-06-12 - Backtracking Aprendia Salidas Falsas Tras Obstaculo Rojo

### Sintoma

- Hasta llegar al bloqueo rojo, el robot gestionaba bien el laberinto.
- Al volver desde el bloqueo, el nodo de la exclamacion anadia salidas falsas `STRAIGHT` y `RIGHT`.
- En la T final, donde debia girar a la derecha, siguio recto y despues la decision mostrada paso a izquierda.
- La arista con obstaculo verde no quedaba suficientemente clara en el grafo durante la vuelta.

### Diagnostico

- El retorno desde obstaculo rojo usa `returningFromBlockedObstacle`, no `navigatingToFrontier`.
- La proteccion anterior solo ignoraba QTR en nodos intermedios de `RETURNING_TO_UNFINISHED_NODE`.
- Al llegar de vuelta al nodo origen del obstaculo rojo, si la lectura no estaba saturada, `handleNode()` volvia a actualizar `options` con QTR.
- En backtracking eso es incorrecto: el robot ya sabe que esta regresando a un nodo conocido por una arista conocida.
- Una lectura diagonal o una T parcial podia crear salidas nuevas falsas, alterando despues la decision DFS/BFS.

### Implementacion Aplicada

- `handleNode()` separa ahora:
  - `qtrRelativeOptions`: lo que ha visto el QTR, solo para diagnostico.
  - `relativeOptions`: las salidas reales que se permiten modificar el grafo.
- En `returningFromBlockedObstacle`, el QTR siempre se ignora para aprender salidas.
- En `navigatingToFrontier`, el QTR solo se acepta al llegar al nodo objetivo; en nodos intermedios se usa el grafo conocido.
- Nuevo helper `knownRelativeOptionsForNode()` para convertir opciones absolutas guardadas a izquierda/recto/derecha segun la orientacion actual.
- Telemetria nueva `qtr_learning_enabled`.
- Eventos `NODE_EXITS_CLASSIFIED`, `RETURNED_TO_NODE_AFTER_BLOCKED_OBSTACLE` y `KNOWN_PATH_NODE_REACHED` incluyen `qtr_options` y si se ignoraron.

### Visualizacion

- El grafo web marca aristas conectadas usando `edge.orientation` del firmware, no una inferencia por coordenadas.
- Las aristas reales verdes y rojas muestran etiqueta `GREEN` o `RED`, no solo color.
- Los eventos de obstaculo verde incluyen `from_node`, `orientation` y `green_edge`.

### Resultado

- Durante backtracking conocido, el robot debe limitarse a volver al nodo esperado y calcular el siguiente giro desde el grafo.
- No debe crear salidas nuevas en el nodo de la exclamacion hasta que llegue a un nodo objetivo con aristas realmente pendientes.

### Estado

- Compilado correctamente. Pendiente flashear y repetir la prueba completa.

## 2026-06-13 - Backtracking Perdia Cruce Lateral Y Media Vuelta Corta

### Sintoma

- Durante backtracking, al aparecer un cruce a la izquierda, el robot lo trataba como desviacion de linea.
- El PID corregia hacia esa rama y el cruce no llegaba a confirmarse como nodo.
- La media vuelta despues de bloqueo rojo no acababa de cerrar correctamente; parecia quedarse corta.

### Diagnostico

- La confirmacion de cruce dura `INTERSECTION_CONFIRM_MS`.
- Durante esa confirmacion se llamaba a `followLine(frame)`, por lo que el robot podia corregir y abandonar el cruce antes de confirmarlo.
- En backtracking por camino conocido, una rama lateral fuerte debe tener prioridad sobre la correccion de linea.
- En `rotateForTurn(BACK)`, el giro de 180 grados podia finalizar al 75% de `msTurn180` si los sensores centrales volvian a ver linea.

### Implementacion Aplicada

- Nuevo detector `backtrackingSideNodeCandidate()`:
  - solo se activa en `returningFromBlockedObstacle` o `navigatingToFrontier`.
  - si ve una rama lateral fuerte sin centro estable, la promociona a candidato de nodo.
- Nuevo `promoteBacktrackingNodeCandidate()` para convertir ese candidato en `LEFT_BRANCH`, `RIGHT_BRANCH` o `T_OR_CROSS`.
- Durante la confirmacion de cruce en backtracking, el robot se detiene con `holdNodeCandidateDuringConfirmation()` en vez de aplicar PID.
- La media vuelta ya no puede aceptar reacquisicion de linea antes de `msTurn180`; se elimina el corte temprano al 75%.

### Resultado Esperado

- En backtracking, una rama lateral conocida no debe comerse como correccion de linea.
- El robot debe llegar al nodo, pararse durante la confirmacion y luego usar el grafo conocido para decidir el giro.
- La media vuelta debe completar mas grados antes de intentar centrar.

### Estado

- Pendiente compilar, flashear y repetir prueba de bloqueo rojo + vuelta.

## 2026-06-13 - Revision De Codigo, Dashboard Y Twitch

### Objetivo

- El robot ya resuelve el laberinto y el grafo refleja bien los nodos/aristas.
- Se quiere revisar el codigo sin tocar la logica estable.
- Se quiere modernizar la interfaz de administrador, mejorar el grafo y preparar integracion Twitch.

### Revision

- Firmware:
  - no se cambia comportamiento de maze solver.
  - se anaden comentarios cortos en la parte de backtracking/QTR para explicar por que el PID se detiene y cuando el QTR puede aprender mapa.
- Backend:
  - se amplia `users` con datos Twitch: `twitch_id`, `twitch_login`, `twitch_display_name`, `profile_image_url`.
  - se crea indice unico parcial por `twitch_login`.
  - nuevo endpoint `POST /api/users/twitch` para crear o recuperar usuario desde OAuth de Twitch.
- Frontend:
  - el grafo pasa a un lienzo SVG mas grande y responsive.
  - nuevos botones `Centrar` y `Ampliar` para el grafo.
  - panel `Chat Twitch` en administrador.
  - chat Twitch tambien en vista publica.
  - canal Twitch configurable por `localStorage`.
  - login Twitch enlaza automaticamente con usuario RoboBet.

### Referencias Consultadas

- Tabler y Adminator como referencia de dashboards responsive con tokens CSS, paneles densos y UI clara.
- Documentacion oficial de Twitch Embed Chat para usar iframe con parametro `parent`.
- Ejemplo oficial `twitchdev/chatbot-javascript-sample` como referencia futura para comandos de chat.

### Validacion

- `node --check frontend/app.js`: OK.
- `python -m compileall backend/app`: OK.
- `platformio run -d robot`: OK.
- Backend reiniciado en `http://127.0.0.1:8000`.
- `POST /api/users/twitch`: OK.
- Usuario demo de prueba eliminado de SQLite.

### Pendiente

- Configurar `robobet_twitch_client_id` con el Client ID real.
- Poner el canal Twitch real desde la UI.
- Implementar bot/lector de chat para comandos reales `!apuesta`, `!voto`, `!estado`.

## 2026-06-13 - Arista Conocida Contaba Como Salida Pendiente

### Sintoma

- En el cruce en T del backtracking, el robot a veces volvia bien hasta el nodo, pero despues intentaba explorar una salida incorrecta.
- En la telemetria aparecia `EXPLORE_UNTRIED_EDGE` cuando realmente estaba recorriendo una arista ya conocida.
- Despues de perder linea en esa arista conocida, el grafo podia marcarla como bloqueada y terminar en `EXPLORATION_EXHAUSTED`.

### Diagnostico

- `connectNodes()` conectaba dos nodos y anadia la orientacion inversa como `option` del nodo destino.
- Esa `option` inversa representaba el camino conocido para volver, pero `untriedOptionsMask()` no la descartaba.
- Como resultado, el planificador podia tratar una arista con vecino conocido como si fuera una frontera nueva.

### Implementacion Aplicada

- `untriedOptionsMask()` ahora excluye las direcciones que ya tienen `neighbor`.
- `chooseUntriedTurnAtNode()` tambien ignora cualquier salida que apunte a un nodo ya conectado.
- La arista sigue existiendo para navegar y dibujarse en el grafo, pero ya no se considera salida pendiente.

### Resultado Esperado

- Al volver por un camino conocido, el robot debe entrar en `KNOWN_PATH_TO_FRONTIER`.
- Los cruces intermedios del backtracking no deben crear salidas nuevas por QTR.
- El cruce en T marcado debe usarse solo para volver al nodo correcto hasta llegar a una frontera real.

### Estado

- Compilado correctamente.
- Flasheado correctamente en COM3 tras entrar en bootloader manualmente.
- Siguiente paso: repetir la prueba del laberinto completo y revisar que el cruce en T del backtracking entra como `KNOWN_PATH_TO_FRONTIER`.

## 2026-06-13 - Margen Extra Para Media Vuelta

### Sintoma

- En algunas pruebas la media vuelta se quedaba corta, sobre todo cuando el robot tenia menos potencia.
- El corte de seguridad por tiempo podia parar el giro antes de que los QTR volvieran a encontrar la linea.

### Diagnostico

- El giro de 180 usaba `msTurn180` como tiempo minimo para aceptar reacople y `msTurn180 + lineReacquireMs` como timeout maximo.
- Si los motores giraban mas lento por bateria, rozamiento o limitador de velocidad, ese timeout maximo podia ser insuficiente.

### Implementacion Aplicada

- Nuevo parametro `backTurnExtraMs`, por defecto `900 ms`.
- La media vuelta mantiene `msTurn180` como minimo para no cortar demasiado pronto.
- El timeout maximo del giro de 180 ahora compensa automaticamente el `speedLimitPercent`.
- La pagina local de tuning del robot permite ajustar `Margen extra media vuelta (ms)` sin recompilar.

### Resultado Esperado

- A 100% el robot tiene mas margen antes de cortar por seguridad.
- Con velocidad limitada, el timeout maximo crece para dar tiempo real al giro.
- Los giros de 90 no cambian.

### Estado

- Compilado correctamente.
- Flasheado correctamente en COM3.
- El primer intento a velocidad rapida perdio conexion al 17%; se repitio a `115200` solo para la subida y despues se restauro la configuracion rapida.
- Siguiente paso: probar media vuelta con obstaculo rojo.

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
