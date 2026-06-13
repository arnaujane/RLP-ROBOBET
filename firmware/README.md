# RoboBet

RoboBet es un robot maze solver interactivo. Sigue líneas negras sobre fondo blanco, detecta cruces y obstáculos, construye un grafo del laberinto y permite que una web local muestre la carrera, el estado del robot, las cámaras, las votaciones y las apuestas ficticias de los usuarios.

El objetivo no es solo que el robot llegue al final. El objetivo es que el sistema sea demostrable, depurable y entendible: mientras el robot avanza, la web enseña qué está viendo, qué nodo cree que tiene delante y por qué toma cada decisión.

## Tabla De Contenidos

- [Qué Hace](#qué-hace)
- [Arquitectura](#arquitectura)
- [Módulos Del Proyecto](#módulos-del-proyecto)
- [Algoritmos Implementados](#algoritmos-implementados)
- [Hardware](#hardware)
- [Requisitos](#requisitos)
- [Arranque Rápido](#arranque-rápido)
- [Simulación Sin Robot](#simulación-sin-robot)
- [Flasheo](#flasheo)
- [Flujo De Demo](#flujo-de-demo)
- [Base De Datos](#base-de-datos)
- [Documentación](#documentación)
- [Problemas Frecuentes](#problemas-frecuentes)
- [Estado Del Proyecto](#estado-del-proyecto)

## Qué Hace

RoboBet combina robótica física, algoritmia de grafos, visión con ESP32-CAM y una interfaz web.

Funciones principales:

- Sigue línea negra con un array QTR-8.
- Detecta cruces, giros, T, obstáculos y final.
- Decide rutas usando DFS o BFS.
- Hace backtracking cuando encuentra una salida bloqueada.
- Dibuja el grafo detectado en la web.
- Muestra cámaras frontal y cenital.
- Permite decisiones manuales de obstáculo: verde, rojo o negro.
- Guarda usuarios, apuestas, votaciones, eventos y tiempos de carrera en SQLite.
- Incluye simulador para probar backend y frontend sin robot físico.

## Arquitectura

```text
                +-------------------+
                |   Frontend Web    |
                | dashboard usuario |
                +---------+---------+
                          |
                          | HTTP + WebSocket
                          |
                +---------v---------+
                |  Backend FastAPI  |
                | SQLite + eventos  |
                +---------+---------+
                          |
                          | WebSocket robot
                          |
                +---------v---------+
                |   ESP32-WROOM     |
                | QTR + motores     |
                +----+---------+----+
                     |         |
                     |         | HTTP / UART preparado
                     |         |
             +-------v--+   +--v-------------+
             | HW-166   |   |   ESP32-CAM    |
             | motores  |   | stream + color |
             +----------+   +----------------+
```

La ESP32-WROOM toma las decisiones críticas del robot: sensores, motores, nodos y grafo. La ESP32-CAM queda separada para streaming y detección de señales, evitando cargar el controlador principal.

## Módulos Del Proyecto

```text
backend/
  app/
    main.py              Entrada FastAPI, WebSockets y frontend estático
    database.py          SQLite y creación de tablas
    realtime.py          Estado en tiempo real y comandos al robot
    routers/             API de usuarios, carreras, apuestas, votaciones y operador
    services/            Lógica de apuestas y votaciones

frontend/
  index.html             Estructura del dashboard
  styles.css             Diseño responsive
  app.js                 Estado, eventos, grafo, cámaras y acciones de usuario

robot/
  platformio.ini         Configuración ESP32-WROOM
  src/main.cpp           Firmware del robot

esp32_cam/
  platformio.ini         Configuración ESP32-CAM
  src/main.cpp           Streaming, snapshot, estado y detección de color

scripts/
  run_backend.ps1        Arranque local del backend
  simulate_robot.py      Simulador WebSocket del robot
  simulate_robot.ps1     Wrapper PowerShell del simulador
  twitch_chat_bridge.py  Puente Twitch EventSub -> backend
  run_twitch_bridge.ps1  Wrapper PowerShell del puente Twitch

docs/
  guia_usuario.md
  hardware.md
  calibration.md
  protocol.md
  resumen_implementacion.md
  seguimiento_pruebas.md
  camara_esp32cam.md
  frontend_integracion.md
  guion_presentacion.md
```

## Algoritmos Implementados

### Seguimiento De Línea

El robot usa los 8 sensores QTR para estimar la posición de la línea y corregir los motores con una ley proporcional simple. Cuando aparece una posible rama lateral, el seguimiento normal se protege para que el PID no confunda un cruce con una desviación.

### Detección De Nodos

Un nodo se detecta cuando los sensores ven una T, un cruce, una rama lateral o una zona negra amplia. Antes de decidir, el robot avanza hasta el centro del nodo para leer mejor las salidas disponibles.

### DFS Y BFS

El laberinto se modela como un grafo:

- Nodo: cruce o punto de decisión.
- Arista: camino entre nodos.
- Salida: orientación absoluta `NORTH`, `EAST`, `SOUTH` o `WEST`.

DFS prioriza exploración profunda. BFS prioriza llegar al nodo pendiente más cercano. En ambos casos el robot registra salidas probadas, bloqueadas, verdes y conectadas a otros nodos.

### Backtracking

Si una señal roja bloquea una salida, el robot:

1. Marca la arista como bloqueada.
2. Hace media vuelta.
3. Vuelve al nodo anterior.
4. Busca otra salida pendiente.
5. Si hace falta, navega por aristas conocidas hasta otro nodo con opciones.

### Máquina De Estados Del Robot

El firmware no funciona como una secuencia lineal, sino como una máquina de estados. Cada iteración del `loop` lee sensores, revisa eventos pendientes y ejecuta la lógica correspondiente al estado actual. Esto permite mezclar control fí­sico, decisión de ruta y tratamiento de obstáculos sin bloquear el robot.

Estados principales:

- `WAITING_START`: robot armado pero parado, esperando orden de inicio.
- `FOLLOWING_LINE`: seguimiento normal de la lí­nea con el array QTR.
- `INTERSECTION_DETECTED`: se detecta una rama, T, cruce o zona negra amplia.
- `CENTER_ON_NODE`: el robot avanza un poco para quedar centrado sobre el nodo.
- `CLASSIFY_NODE`: lee qué salidas existen realmente en el cruce.
- `CHOOSE_DIRECTION`: consulta DFS o BFS para decidir la siguiente acción.
- `GO_STRAIGHT`, `TURN_LEFT`, `TURN_RIGHT`, `TURN_BACK`: ejecuta el movimiento elegido.
- `SEARCH_LINE`: reacopla el robot con la lí­nea tras un giro.
- `OBSTACLE_CHECK`: confirma si delante hay un obstáculo y qué señal se ha visto.
- `WAITING_OBSTACLE_DECISION`: pausa opcional para esperar decisión manual o de cámara.
- `BACKTRACKING`, `RETURNING_FROM_BLOCKED_OBSTACLE`, `RETURNING_TO_UNFINISHED_NODE`: modos de regreso cuando una salida queda bloqueada o cuando toca volver a un nodo pendiente.
- `FINISH_CHECK`, `FINISH_DETECTED`, `FINISHED`: validación y cierre de una carrera completada.
- `ERROR_STATE`: estado de seguridad cuando algo inconsistente impide continuar.

Transiciones importantes:

- De `WAITING_START` pasa a `FOLLOWING_LINE` cuando se autoriza movimiento.
- De `FOLLOWING_LINE` pasa a `INTERSECTION_DETECTED` cuando los QTR ven un nodo o una condición especial.
- De `INTERSECTION_DETECTED` pasa a `CENTER_ON_NODE` y luego a `CLASSIFY_NODE` para decidir con el robot bien colocado.
- De `CLASSIFY_NODE` pasa a `CHOOSE_DIRECTION`, donde el grafo y el algoritmo activo determinan si toca explorar, volver o cerrar un camino.
- Tras `GO_STRAIGHT` o cualquier giro, entra en `SEARCH_LINE` o vuelve directamente a `FOLLOWING_LINE` cuando recupera la referencia sobre el suelo.
- Si aparece un obstáculo, entra en `OBSTACLE_CHECK` y, si hace falta, en `WAITING_OBSTACLE_DECISION` antes de seguir, retroceder o marcar la arista.
- Cuando una salida queda descartada, el robot entra en estados de retorno para volver al último nodo útil sin perder el mapa ya construido.
- Si detecta la señal negra de final, pasa por `FINISH_DETECTED` y termina en `FINISHED`.

La web refleja esta máquina de estados casi tal cual. Por eso en el dashboard aparecen nombres como `FOLLOWING_LINE`, `WAITING_OBSTACLE_DECISION` o `RETURNING_TO_UNFINISHED_NODE`: no son etiquetas decorativas, sino el estado real del firmware en ese instante.

### Movimiento Sin Encoders

El robot no usa encoders. La cinemática se calibra por tiempo:

- Milisegundos por centímetro.
- Giro de 90 grados.
- Giro de 180 grados.
- Tiempo de reacople con línea.
- Margen extra para media vuelta con poca potencia.

## Hardware

Componentes principales:

- ESP32-WROOM como controlador del robot.
- ESP32-CAM como cámara frontal.
- Sensor QTR-8 Pololu para línea negra.
- Driver de motores HW-166.
- Dos motores DC.
- Alimentación de 5 V para lógica.
- Alimentación separada para motores.
- Masa común entre módulos.

Notas críticas:

- No unir directamente `+9V_BAT` con `+5V`.
- Todas las masas deben estar en común.
- `LEDON` del QTR debe estar alimentado para que los emisores IR funcionen.
- Si los QTR se ven negros o no responden, revisar alimentación, GND y superficie de prueba.

## Requisitos

Software:

- Python 3.9 o superior.
- PlatformIO.
- Navegador moderno.
- PowerShell en Windows.

Dependencias Python:

```text
fastapi
uvicorn
pydantic
python-multipart
websockets
```

Las versiones exactas están en `requirements.txt`.

## Arranque Rápido

Ejecutar desde esta carpeta:

```powershell
.\scripts\setup_backend.ps1
```

Arrancar la web:

```powershell
.\scripts\run_backend.ps1
```

Nota: si el sistema tiene Python 3.14 como predeterminado, usar `.\scripts\setup_backend.ps1`; este proyecto queda estable con Python 3.13.

Abrir:

```text
http://127.0.0.1:8000
```

Si todo está correcto, la web mostrará el dashboard de RoboBet. El robot aparecerá como conectado cuando la ESP32-WROOM se conecte al WebSocket `/ws/robot`.

## Simulación Sin Robot

El simulador permite probar frontend, backend, WebSockets, eventos, apuestas y votaciones sin hardware.

Con el backend arrancado:

```powershell
.\scripts\simulate_robot.ps1
```

Escenario con obstáculo rojo y backtracking:

```powershell
.\scripts\simulate_robot.ps1 --scenario red-backtrack
```

Después, pulsar `Iniciar` desde la web.

## Twitch Chat Bridge

El backend ya sabe procesar `!estado`, `!voto` y `!apuesta`, pero necesita un proceso externo que lea el chat real de Twitch y reenvíe los mensajes a `POST /api/twitch/chat`.

Variables mínimas:

```powershell
$env:ROBOBET_TWITCH_ACCESS_TOKEN="TU_USER_ACCESS_TOKEN"
$env:ROBOBET_TWITCH_BROADCASTER_LOGIN="tu_canal"
```

Opcionales:

```powershell
$env:ROBOBET_TWITCH_CLIENT_ID="TU_CLIENT_ID"
$env:ROBOBET_TWITCH_BACKEND_URL="http://127.0.0.1:8000/api/twitch/chat"
$env:ROBOBET_TWITCH_CHAT_SECRET="TU_SECRETO"
```

Arranque:

```powershell
.\scripts\run_twitch_bridge.ps1
```

El token debe incluir `user:read:chat`. El puente abre EventSub WebSocket, se suscribe a `channel.chat.message` y reenvía cada mensaje al backend.

## Flasheo

Antes de flashear, revisar:

```text
robot/platformio.ini
esp32_cam/platformio.ini
```

Configurar:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `ROBOBET_SERVER_HOST`
- IP de la ESP32-CAM si se usa detección HTTP.

Compilar robot:

```powershell
.\.venv\Scripts\platformio.exe run -d robot
```

Flashear robot:

```powershell
.\.venv\Scripts\platformio.exe run -d robot -t upload
```

Compilar ESP32-CAM:

```powershell
.\.venv\Scripts\platformio.exe run -d esp32_cam
```

Flashear ESP32-CAM:

```powershell
.\.venv\Scripts\platformio.exe run -d esp32_cam -t upload
```

Si la ESP32 no entra en modo descarga:

1. Mantener pulsado `BOOT`.
2. Pulsar `RST` una vez.
3. Mantener `BOOT` hasta que empiece la escritura.
4. Soltar `BOOT`.

Si la subida se corta por USB, probar con otro cable o bajar temporalmente la velocidad de subida a `115200`.

## Flujo De Demo

1. Arrancar backend.
2. Encender ESP32-CAM.
3. Comprobar stream frontal.
4. Encender ESP32-WROOM.
5. Verificar robot conectado en la web.
6. Calibrar QTR si hace falta.
7. Crear usuarios.
8. Crear votaciones y apuestas.
9. Seleccionar algoritmo DFS o BFS.
10. Activar gestión de obstáculos.
11. Pulsar `Iniciar`.
12. Dar decisiones manuales de obstáculo si la cámara no está calibrada.
13. Revisar grafo, sensores y eventos en directo.

## Base De Datos

SQLite se crea automáticamente en:

```text
data/robobet.sqlite3
```

Tablas principales:

- `users`: usuarios y puntos.
- `runs`: carreras y tiempos.
- `bets`: apuestas.
- `polls`: votaciones.
- `votes`: votos.
- `events`: eventos del robot y del sistema.

## Documentación

Documentos útiles:

- `docs/guia_usuario.md`: uso paso a paso.
- `docs/resumen_implementacion.md`: explicación completa de arquitectura y lógica.
- `docs/hardware.md`: cableado y componentes.
- `docs/calibration.md`: calibración de sensores y movimiento.
- `docs/protocol.md`: protocolo robot/backend/cámara.
- `docs/camara_esp32cam.md`: streaming, calidad de imagen y detección de color.
- `docs/seguimiento_pruebas.md`: incidencias reales y soluciones aplicadas.
- `docs/frontend_integracion.md`: ideas e integración web/Twitch.
- `docs/guion_presentacion.md`: guion humano para la presentación.

## Problemas Frecuentes

### El robot da vueltas o no sigue la línea

- Revisar que `LEDON` del QTR esté alimentado.
- Revisar GND común.
- Comprobar que los sensores ven blanco y negro con valores distintos.
- Calibrar QTR.
- Bajar velocidad base si oscila demasiado.

### El cruce se detecta mal

- Revisar si el robot entra centrado al nodo.
- Mirar en la web `relative_options`, `qtr_learning_enabled` y el grafo.
- Confirmar que una arista conocida no aparece como salida pendiente.

### La media vuelta se queda corta

- Subir `msTurn180`.
- Subir `backTurnExtraMs`.
- Revisar batería de motores.
- Reducir rozamiento mecánico.

### La cámara se ve mal

- Alimentar ESP32-CAM con 5 V estable.
- Mejorar iluminación.
- Evitar cables flojos.
- Probar menor resolución o JPEG menos agresivo.
- Revisar `docs/camara_esp32cam.md`.

### La web no ve el robot

- Verificar IP en `ROBOBET_SERVER_HOST`.
- Confirmar que backend está en `http://127.0.0.1:8000`.
- Comprobar que el PC y la ESP32 están en la misma red.
- Revisar eventos del backend y telemetría WebSocket.

## Estado Del Proyecto

Implementado:

- Backend FastAPI.
- Frontend responsive.
- Base de datos local.
- Simulador de robot.
- Firmware robot con QTR, motores, DFS/BFS, backtracking y grafo.
- Firmware ESP32-CAM con streaming y detección de color.
- Decisiones manuales de obstáculos desde web.
- Visualización de sensores y grafo.
- Documentación técnica y seguimiento de pruebas.

Pendiente o mejorable:

- Calibrar definitivamente la cámara con iluminación real.
- Afinar tiempos de giro según batería y superficie.
- Integración Twitch completa con comandos de chat.
- Añadir capturas o vídeo final de demo al README cuando el montaje esté cerrado.

## Referencia De Estilo

La estructura del README se ha inspirado en proyectos de robótica bien documentados como `AtsushiSakai/PythonRobotics`: descripción clara, índice, requisitos, documentación, uso, módulos y algoritmos explicados por separado.
