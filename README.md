# RoboBet

RoboBet es un robot seguidor de linea convertido en sistema interactivo de exploracion, visualizacion y juego social.

El robot recorre un laberinto sobre linea negra, detecta nodos, construye un grafo del entorno y decide su ruta con `DFS` o `BFS`. Mientras tanto, una aplicacion web muestra el estado interno del firmware, la topologia descubierta, los streams de camara, las votaciones activas de Twitch y las apuestas ficticias de los usuarios.

No es solo un coche que sigue una linea. Es una arquitectura distribuida con:

- control embebido en `ESP32-WROOM`
- vision ligera en `ESP32-CAM`
- backend `FastAPI` con `SQLite`
- frontend web en tiempo real
- bridge externo para integrar el chat de `Twitch`

## Tabla De Contenidos

- [Vision General](#vision-general)
- [Que Hace El Sistema](#que-hace-el-sistema)
- [Arquitectura](#arquitectura)
- [Dispositivos Y Modulos](#dispositivos-y-modulos)
- [Flujos Principales](#flujos-principales)
- [Algoritmos](#algoritmos)
- [Twitch, Votaciones Y Apuestas](#twitch-votaciones-y-apuestas)
- [Estructura Del Repo](#estructura-del-repo)
- [Requisitos](#requisitos)
- [Arranque Rapido](#arranque-rapido)
- [Simulacion Sin Robot](#simulacion-sin-robot)
- [Flasheo](#flasheo)
- [Demo Operativa](#demo-operativa)
- [Base De Datos](#base-de-datos)
- [Documentacion](#documentacion)
- [Problemas Frecuentes](#problemas-frecuentes)
- [Limitaciones Reales](#limitaciones-reales)

## Vision General

La idea central de RoboBet es separar bien las responsabilidades:

- el robot decide en local lo que afecta al movimiento
- la camara frontal clasifica señales visuales sin meter carga extra al controlador principal
- el backend persiste, coordina y difunde el estado
- la web enseña el sistema y permite operarlo
- Twitch añade capa social para votaciones y apuestas

## Que Hace El Sistema

Capacidades principales:

- seguir una linea negra con un array `QTR-8`
- detectar cruces, T, calles sin salida y perdida de linea
- construir un grafo incremental del laberinto
- explorar con `DFS` o `BFS`
- hacer backtracking cuando una rama queda bloqueada
- consultar una `ESP32-CAM` para clasificar señales por color
- emitir telemetria y eventos al backend por `WebSocket`
- mostrar grafo, estado, sensores y streams en el dashboard
- abrir votaciones desde la UI o desde Twitch
- registrar apuestas ficticias y liquidarlas al final de la carrera

## Arquitectura

### Interconexion fisica

```text
                         +-----------------------------+
                         |       Portatil / Host       |
                         | FastAPI + Frontend + SQLite |
                         +-------------+---------------+
                                       |
                                       | WiFi local
                                       |
                +----------------------+----------------------+
                |                                             |
                |                                             |
      +---------v----------+                        +----------v---------+
      |     ESP32-WROOM    | ---- HTTP /detect --->|      ESP32-CAM     |
      | control del robot  |<--- MJPEG opcional ---| stream + vision    |
      +----+-----------+---+                        +--------------------+
           |           |
           |           |
           |           +----------------------+
           |                                  |
           | GPIO / PWM                       |
           |                                  |
   +-------v--------+                   +-----v------+
   |   Driver HW166 |                   |   QTR-8    |
   | etapa potencia |                   | sensores   |
   +---+--------+---+                   +------------+
       |        |
       |        |
 +-----v--+  +--v-----+
 | Motor L|  | Motor R|
 +--------+  +--------+
```

### Interconexion software

```text
Frontend Web
  index.html
  styles.css
  app.js
      |
      | HTTP + WebSocket /ws/ui
      v
Backend FastAPI
  main.py
  realtime.py
  routers/*
  services/*
  database.py
      |
      +--> SQLite
      |
      +--> WebSocket /ws/robot <------ Firmware robot
      |
      +--> POST /api/twitch/chat <---- Twitch bridge

Firmware robot
  lectura QTR
  FSM
  grafo
  DFS/BFS
  motores
  consulta de camara

Firmware ESP32-CAM
  stream
  snapshot
  status
  detect
```

### Idea arquitectonica

La decision importante fue no mezclar video, control y UI en el mismo micro.

- `ESP32-WROOM`: control de tiempo real, FSM, sensores, motores, grafo y navegacion
- `ESP32-CAM`: streaming y clasificacion visual ligera
- `FastAPI`: persistencia, APIs, tiempo real y logica social
- `Frontend`: observabilidad, operacion y experiencia de usuario

## Dispositivos Y Modulos

### ESP32-WROOM

Ruta principal:

- [main.cpp](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/firmware/robot/src/main.cpp)

Responsabilidades:

- leer el `QTR-8`
- seguir linea
- detectar intersecciones
- centrar el robot en un nodo
- mantener orientacion logica
- construir el grafo del laberinto
- elegir salidas con `DFS` o `BFS`
- gestionar obstaculos
- consultar la camara frontal
- publicar telemetria por `WebSocket`

### ESP32-CAM

Ruta principal:

- [main.cpp](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/firmware/esp32_cam/src/main.cpp)

Responsabilidades:

- capturar imagen
- servir stream MJPEG
- servir snapshot
- exponer `status`
- ejecutar deteccion basada en color en `/detect`

### Backend

Rutas principales:

- [main.py](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/main.py)
- [realtime.py](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/realtime.py)
- [database.py](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/database.py)

Responsabilidades:

- servir la web
- exponer API REST
- mantener el estado vivo de la carrera
- hablar con el robot por `WebSocket`
- almacenar usuarios, runs, polls, votes, bets y events
- repartir cambios al frontend
- integrar el chat de Twitch

### Frontend

Rutas principales:

- [index.html](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/frontend/index.html)
- [app.js](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/frontend/app.js)
- [styles.css](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/frontend/styles.css)

Responsabilidades:

- dashboard tecnico
- panel de usuario
- visualizacion del grafo
- panel de streams
- panel de Twitch
- panel de votaciones
- panel de apuestas
- control operativo del robot

### Twitch bridge

Rutas principales:

- [twitch_chat_bridge.py](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/scripts/twitch_chat_bridge.py)
- [run_twitch_bridge.ps1](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/scripts/run_twitch_bridge.ps1)

Responsabilidades:

- abrir `EventSub WebSocket`
- escuchar el chat del canal
- reenviar mensajes al backend
- desacoplar Twitch del resto del sistema

## Flujos Principales

### 1. Flujo de exploracion del robot

```text
1. El robot entra en FOLLOWING_LINE.
2. El QTR detecta una posible interseccion o evento.
3. El firmware centra el robot en el nodo.
4. Clasifica las salidas disponibles.
5. Actualiza o crea nodos y aristas del grafo.
6. Elige la siguiente direccion con DFS o BFS.
7. Ejecuta giro o avance.
8. Si encuentra una senal, consulta la ESP32-CAM.
9. Si la rama esta bloqueada, marca la arista y hace backtracking.
10. Repite hasta explorar o alcanzar el final.
```

### 2. Flujo de tiempo real hacia la UI

```text
1. El frontend abre /ws/ui.
2. El robot abre /ws/robot.
3. El robot envia telemetria y eventos.
4. El backend actualiza el estado global.
5. El backend reemite cambios al frontend.
6. La UI refresca estado, grafo, streams, votaciones y apuestas.
```

### 3. Flujo de deteccion visual

```text
1. El robot necesita clasificar una senal.
2. Llama a /detect en la ESP32-CAM.
3. La camara toma un frame y calcula dominancia de color.
4. Responde GREEN_SIGN, RED_SIGN, BLACK_SIGN o NO_SIGN.
5. El robot decide si avanzar, retroceder o finalizar.
```

### 4. Flujo social de Twitch

```text
1. Un espectador escribe en el chat.
2. Twitch EventSub entrega el mensaje al bridge.
3. El bridge hace POST /api/twitch/chat al backend.
4. El backend interpreta comandos.
5. Se actualizan votos, estado o apuestas.
6. La UI refleja el cambio en tiempo real.
```

## Algoritmos

### Seguimiento de linea

El seguimiento usa los `8` sensores del `QTR` y una correccion proporcional simple.

Idea base:

- estimar posicion lateral de la linea
- calcular error respecto al centro
- corregir `PWM` de motor izquierdo y derecho

Ventajas:

- barato computacionalmente
- suficientemente reactivo
- facil de calibrar en campo

Limitacion:

- sin encoders, la estabilidad depende mucho de superficie, bateria y tuning

### Deteccion de nodos

El firmware distingue entre:

- linea normal
- rama izquierda
- rama derecha
- T o cruce
- callejon sin salida
- parche negro
- linea perdida

Cuando detecta nodo:

- para correccion agresiva
- centra el chasis
- relee sensores
- construye una mascara de salidas relativas
- proyecta esas salidas sobre la orientacion absoluta del robot

### Grafo del laberinto

El laberinto no se guarda como una matriz, sino como grafo:

- `NodeRecord`: nodo, coordenadas logicas, salidas, padre, vecinos, flags
- `EdgeRecord`: conexion entre nodos, orientacion, visitado, bloqueado, verde

Eso permite:

- explorar incrementalmente
- evitar repetir ramas bloqueadas
- volver a nodos con trabajo pendiente
- enseñar en la web una representacion legible del laberinto

### DFS

`DFS` prioriza profundidad.

Comportamiento esperado:

- entra por una rama
- la agota
- vuelve atras cuando no quedan salidas utiles

Cuadra bien cuando interesa ver crecer el grafo de forma marcada y cuando la demo quiere enseñar backtracking claramente.

### BFS

`BFS` prioriza el frente pendiente mas cercano.

Comportamiento esperado:

- al terminar una rama, busca el nodo abierto mas proximo
- reduce recolocaciones innecesarias respecto a estrategias mas profundas

Cuadra mejor cuando interesa eficiencia de exploracion sobre espectacularidad del recorrido.

### Backtracking

Si una arista queda invalidada:

- se marca como bloqueada
- el robot gira `180`
- vuelve a un nodo conocido
- selecciona otra salida disponible

Es la pieza que evita que el sistema se quede atascado cuando descubre tarde que una rama no sirve.

### Maquina de estados

El firmware del robot esta organizado como una `FSM`. No ejecuta una ruta lineal fija, sino que en cada iteracion del `loop` revisa sensores, contexto y estado actual.

Estados importantes:

- `WAITING_START`
- `FOLLOWING_LINE`
- `INTERSECTION_DETECTED`
- `CENTER_ON_NODE`
- `CLASSIFY_NODE`
- `CHOOSE_DIRECTION`
- `GO_STRAIGHT`
- `TURN_LEFT`
- `TURN_RIGHT`
- `TURN_BACK`
- `SEARCH_LINE`
- `OBSTACLE_CHECK`
- `WAITING_OBSTACLE_DECISION`
- `BACKTRACKING`
- `RETURNING_FROM_BLOCKED_OBSTACLE`
- `RETURNING_TO_UNFINISHED_NODE`
- `FINISH_DETECTED`
- `FINISHED`
- `ERROR_STATE`

La UI enseña muchos de estos estados tal cual llegan desde el firmware, precisamente para que el comportamiento interno del robot sea legible durante la demo.

### Vision artificial

No hay un modelo de deep learning entrenado. La camara usa vision clasica por color.

Pipeline conceptual:

```text
frame -> ROI -> muestreo -> conteo rojo/verde/negro -> umbrales -> clasificacion
```

Salida posible:

- `GREEN_SIGN`
- `RED_SIGN`
- `BLACK_SIGN`
- `NO_SIGN`

Ventajas:

- baja latencia
- explicable
- viable en `ESP32-CAM`
- facil de depurar

Limitaciones:

- dependiente de luz
- sensible a balance de blancos
- no entiende simbolos complejos

## Twitch, Votaciones Y Apuestas

### Comandos de Twitch

El backend ya esta preparado para interpretar comandos como:

- `!estado`
- `!voto algoritmo dfs`
- `!voto algoritmo bfs`
- `!voto obstaculo1 sigue`
- `!voto obstaculo1 stop`
- `!voto obstaculo2 sigue`
- `!voto obstaculo2 stop`
- `!voto obstaculo3 sigue`
- `!voto obstaculo3 stop`

### Tipos de votacion

Actualmente el sistema puede abrir votaciones para:

- elegir algoritmo `DFS` vs `BFS`
- decidir el estado de `obstaculo 1`
- decidir el estado de `obstaculo 2`
- decidir el estado de `obstaculo 3`

La UI de admin puede abrirlas y cerrarlas. La UI publica puede ver su estado y su resultado.

### Apuestas

Las apuestas son ficticias, pero la logica esta implementada de verdad:

- el usuario apuesta puntos
- el stake se descuenta al apostar
- si acierta, cobra `amount * odds`
- si falla, pierde el stake ya descontado

Esto permite una dinamica de participacion real sin mover dinero.

## Estructura Del Repo

```text
RLP-ROBOBET/
├─ README.md
├─ firmware/
│  ├─ backend/
│  │  └─ app/
│  ├─ docs/
│  ├─ firmware/
│  │  ├─ robot/
│  │  └─ esp32_cam/
│  ├─ frontend/
│  ├─ scripts/
│  ├─ requirements.txt
│  └─ README.md
├─ SCH_Schematic1_2026-05-20.pdf
└─ componentes_medidas.csv
```

## Requisitos

Software base:

- `Python 3.13` para backend y scripts
- `PlatformIO` para compilar y flashear firmwares
- `PowerShell` en Windows
- navegador moderno para la UI

Dependencias Python:

- se instalan desde [requirements.txt](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/requirements.txt)
- el script [setup_backend.ps1](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/scripts/setup_backend.ps1) ya prepara el entorno

## Arranque Rapido

### 1. Preparar backend

Desde [firmware](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware):

```powershell
.\scripts\setup_backend.ps1
```

Eso crea `.venv` con `Python 3.13` e instala dependencias.

### 2. Lanzar backend y frontend

```powershell
.\scripts\run_backend.ps1
```

Abrir:

```text
http://127.0.0.1:8000
```

### 3. Simular robot si no hay hardware

```powershell
.\scripts\simulate_robot.ps1
```

### 4. Lanzar el bridge de Twitch

Configurar variables:

```powershell
$env:ROBOBET_TWITCH_CLIENT_ID="TU_CLIENT_ID"
$env:ROBOBET_TWITCH_ACCESS_TOKEN="TU_ACCESS_TOKEN"
$env:ROBOBET_TWITCH_BROADCASTER_LOGIN="tu_canal"
```

Lanzar:

```powershell
.\scripts\run_twitch_bridge.ps1
```

## Simulacion Sin Robot

El repo trae simulador para probar backend, frontend, WebSockets, eventos, votaciones y apuestas sin hardware fisico.

Arranque basico:

```powershell
.\scripts\simulate_robot.ps1
```

Escenario de ejemplo:

```powershell
.\scripts\simulate_robot.ps1 --scenario red-backtrack
```

Archivos implicados:

- [simulate_robot.py](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/scripts/simulate_robot.py)
- [simulate_robot.ps1](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/scripts/simulate_robot.ps1)

## Flasheo

### Robot ESP32-WROOM

Compilar:

```powershell
py -3.14 -m platformio run -d .\firmware\firmware\robot
```

Flashear:

```powershell
py -3.14 -m platformio run -d .\firmware\firmware\robot -t upload --upload-port COMX
```

Monitor serie:

```powershell
py -3.14 -m platformio device monitor --port COMX --baud 115200
```

### ESP32-CAM

Compilar:

```powershell
py -3.14 -m platformio run -d .\firmware\firmware\esp32_cam
```

Flashear:

```powershell
py -3.14 -m platformio run -d .\firmware\firmware\esp32_cam -t upload --upload-port COMX
```

## Base De Datos

La persistencia se hace sobre `SQLite`.

Entidades principales:

- `users`
- `runs`
- `bets`
- `polls`
- `votes`
- `events`

Papel de la base:

- guardar usuarios y saldo de puntos
- registrar carreras y sus resultados
- persistir votaciones y votos
- persistir apuestas y liquidaciones
- almacenar trazas y eventos del sistema

## Resumen

RoboBet mezcla embebidos, vision, grafos, tiempo real y capa social en una arquitectura bastante limpia para el tamaño del proyecto.

La parte fuerte no es solo que el robot se mueva. La parte fuerte es que se puede entender lo que hace, verlo en vivo, influir en ello desde la web o Twitch y cerrar el ciclo completo con datos, votos y resultados.
