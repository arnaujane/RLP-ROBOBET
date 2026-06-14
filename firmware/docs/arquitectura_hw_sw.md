# Arquitectura HW/SW De RoboBet

## Alcance

Este documento resume la arquitectura hardware y software del proyecto `RLP-ROBOBET` tal como está reflejado en el repositorio.

Queda fuera de alcance la variante en la que un humano decide cruces u obstáculos desde el AP del ESP32. A nivel arquitectónico, aquí se asume el comportamiento objetivo del sistema:

- el robot detecta línea, cruces y nodos con QTR
- el robot consulta la cámara frontal para clasificar señales
- el robot decide por sí mismo cómo continuar

## Visión General

RoboBet es un sistema ciberfísico distribuido compuesto por:

- un robot móvil con `ESP32-WROOM` como controlador principal
- una `ESP32-CAM` separada para streaming y clasificación visual simple
- un backend `FastAPI` que actúa como API, hub de tiempo real y persistencia
- un frontend web con dashboard técnico y panel de usuario
- un bridge externo para leer el chat real de Twitch y reenviar comandos al backend

La separación de responsabilidades es deliberada:

- control físico en tiempo real dentro del robot
- percepción visual en un módulo dedicado
- observabilidad, persistencia e interacción social en el host

## Arquitectura Hardware

### Componentes principales

- `ESP32-WROOM`
  - controlador principal del robot
  - lee los sensores QTR
  - gobierna motores
  - ejecuta la lógica de navegación y el grafo del laberinto
  - se conecta al backend por WebSocket

- `QTR-8`
  - array de 8 sensores reflectivos IR
  - detecta línea negra sobre fondo claro
  - aporta la señal principal para seguimiento, detección de cruces y centrado en nodo

- `Driver de motores HW-166`
  - etapa de potencia para dos motores DC
  - recibe dirección y PWM desde el ESP32-WROOM

- `2 motores DC`
  - tracción diferencial
  - permiten recta, giro izquierdo, giro derecho y media vuelta

- `ESP32-CAM`
  - cámara frontal
  - expone stream MJPEG, snapshot, status y detect
  - clasifica señales visuales por análisis de color

- `Cámara cenital opcional`
  - no participa en control
  - sólo alimenta la visualización del dashboard
  - puede ser otra IP camera, webcam o móvil Android

- `Portátil / PC anfitrión`
  - ejecuta backend y frontend
  - almacena SQLite
  - recibe WebSocket del robot
  - sirve la UI y procesa Twitch

### Diagrama de interconexión física

```text
                      +---------------------------+
                      |      Portatil / Host      |
                      | FastAPI + Frontend + DB   |
                      +------------+--------------+
                                   |
                                   | WiFi local
                                   |
              +--------------------+--------------------+
              |                                         |
              |                                         |
  +-----------v-----------+                 +-----------v-----------+
  |      ESP32-WROOM      |                 |       ESP32-CAM       |
  | Control principal     |<-- HTTP detect -+ Stream + vision color |
  | QTR + FSM + DFS/BFS   |                 +-----------------------+
  +-----------+-----------+
              |
              | GPIO + PWM
              |
      +-------v--------+
      |   Driver HW166 |
      +---+--------+---+
          |        |
          |        |
   +------v--+  +--v------+
   | Motor L |  | Motor R |
   +---------+  +---------+

      +------------------+
      |      QTR-8       |
      | Sensores de linea|
      +--------+---------+
               |
               +--------> ESP32-WROOM
```

### Reparto de responsabilidades hardware

- `ESP32-WROOM`
  - lazo de control de movimiento
  - detección de eventos de navegación
  - construcción del grafo
  - selección de rutas DFS/BFS
  - integración del resultado de cámara

- `ESP32-CAM`
  - adquisición visual
  - streaming para observación humana
  - detección ligera basada en color

- `Host`
  - no gobierna el control de bajo nivel
  - coordina UI, base de datos, apuestas, votaciones y Twitch

## Arquitectura Software

### Capas lógicas

```text
+------------------+        HTTP / WebSocket        +------------------+
|   Frontend Web   | <----------------------------> | Backend FastAPI  |
| UI admin / user  |                                | API + Realtime   |
+------------------+                                +--------+---------+
                                                            |
                                                            | SQLite
                                                            |
                                                    +-------v-------+
                                                    |   SQLite DB   |
                                                    +---------------+

+------------------+      WebSocket /ws/robot       +------------------+
| Firmware Robot   | <----------------------------> | Backend FastAPI  |
| FSM + control    |                                | hub tiempo real  |
+--------+---------+                                +------------------+
         |
         | HTTP /detect
         |
+--------v---------+
| Firmware Camara  |
| stream + detect  |
+------------------+

+------------------+      POST /api/twitch/chat     +------------------+
| Twitch Bridge    | -----------------------------> | Backend FastAPI  |
| EventSub reader  |                                | parser de chat   |
+------------------+                                +------------------+
```

## Módulos Software

### 1. Firmware del robot

Ruta principal:

- [main.cpp](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/firmware/robot/src/main.cpp)

Responsabilidades:

- lectura del array QTR
- seguimiento de línea
- detección y clasificación de nodos
- modelado del laberinto como grafo
- navegación DFS/BFS
- backtracking
- control de motores PWM
- consulta a la ESP32-CAM
- envío de telemetría al backend
- recepción de comandos desde backend

### 2. Firmware de cámara

Ruta principal:

- [main.cpp](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/firmware/esp32_cam/src/main.cpp)

Responsabilidades:

- captura JPEG con `esp32-camera`
- stream MJPEG en puerto `81`
- endpoints HTTP `/status`, `/snapshot`, `/detect`
- cálculo de estadísticas de color
- clasificación de `GREEN_SIGN`, `RED_SIGN`, `BLACK_SIGN`, `NO_SIGN`

### 3. Backend FastAPI

Rutas principales:

- [main.py](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/main.py)
- [realtime.py](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/realtime.py)
- [database.py](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/database.py)

Responsabilidades:

- servir frontend estático
- API REST para usuarios, carreras, encuestas, apuestas y cámaras
- WebSocket UI `ws/ui`
- WebSocket robot `ws/robot`
- persistencia SQLite
- difusión de eventos a la UI
- integración de Twitch

### 4. Frontend web

Rutas principales:

- [index.html](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/frontend/index.html)
- [app.js](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/frontend/app.js)
- [styles.css](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/frontend/styles.css)

Responsabilidades:

- dashboard de administrador
- vista pública de usuario
- visualización de estado del robot
- stream frontal y cenital
- grafo del laberinto
- gestión de encuestas, apuestas y Twitch
- visualización de resultados en tiempo real

### 5. Bridge de Twitch

Rutas principales:

- [twitch_chat_bridge.py](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/scripts/twitch_chat_bridge.py)
- [twitch.py](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/routers/twitch.py)
- [twitch.py](/C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/backend/app/services/twitch.py)

Responsabilidades:

- conexión a EventSub WebSocket
- recepción de mensajes del chat real
- reenvío al backend
- parsing de `!estado`, `!voto`, `!apuesta`

## Interconexión Entre Dispositivos

### Flujo operativo principal

```text
1. El usuario abre el dashboard web.
2. El frontend pide HTML/JS/CSS al backend.
3. El frontend abre WebSocket /ws/ui.
4. El robot abre WebSocket /ws/robot.
5. El robot envía telemetría, eventos y estado de grafo al backend.
6. El backend reenvía el estado vivo al frontend.
7. El robot consulta la ESP32-CAM por HTTP /detect cuando necesita clasificación visual.
8. La ESP32-CAM responde con sign + confidence + reason.
9. El bridge de Twitch reenvía mensajes del chat real a POST /api/twitch/chat.
10. El backend actualiza votos, apuestas y eventos, y lo refleja en la UI.
```

### Conectividad y protocolos

- `Frontend -> Backend`
  - HTTP REST
  - WebSocket UI

- `ESP32-WROOM -> Backend`
  - WebSocket bidireccional

- `ESP32-WROOM -> ESP32-CAM`
  - HTTP para `/detect`
  - el stream MJPEG no se usa para control del robot

- `Twitch Bridge -> Backend`
  - HTTP `POST /api/twitch/chat`

## Interconexión Entre Módulos Software

```text
Frontend
  index.html
      |
      v
    app.js  <------------------------------+
      |                                    |
      | HTTP / WS                          |
      v                                    |
Backend main.py                            |
  |                                        |
  +--> realtime.py ------------------------+
  |
  +--> routers/run.py -------> settle_bets() en services/betting.py
  |
  +--> routers/polls.py -----> services/polls.py
  |
  +--> routers/bets.py ------> services/betting.py
  |
  +--> routers/streams.py ---> proxy lógico de cámara
  |
  +--> routers/twitch.py ----> services/twitch.py
  |
  +--> database.py ----------> SQLite

Firmware robot
  main.cpp
    |
    +--> lectura QTR
    +--> FSM de navegación
    +--> construcción de grafo
    +--> DFS/BFS
    +--> control motores
    +--> consulta cámara
    +--> telemetría WebSocket

Firmware cámara
  main.cpp
    |
    +--> captura JPEG
    +--> stream MJPEG
    +--> snapshot
    +--> detect
```

## Máquina De Estados Del Robot

El firmware del WROOM se estructura como una FSM. Es la pieza central del control.

Estados relevantes:

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

Aunque el código actual conserva estados de espera para decisiones manuales, la arquitectura objetivo del sistema supone:

- detección visual autónoma por cámara
- resolución autónoma de salidas bloqueadas o permitidas
- continuidad de navegación sin operador en el lazo crítico

## Algoritmos De Navegación

### 1. Seguimiento de línea

Base algorítmica:

- 8 sensores reflectivos
- cálculo de posición relativa de línea
- corrección proporcional izquierda/derecha
- protección ante eventos laterales para no confundir cruce con error de trayectoria

Modelo:

- no hay odometría con encoders
- el control longitudinal y los giros se calibran por tiempo
- la componente lateral se corrige con el array QTR

### 2. Detección de nodos

El robot identifica:

- línea normal
- rama izquierda
- rama derecha
- T o cruce
- callejón sin salida
- zona negra ancha
- línea perdida

Después de detectar un nodo:

- avanza hasta centrar el cuerpo en la intersección
- relee sensores
- infiere salidas relativas
- proyecta a orientaciones absolutas

### 3. Grafo del laberinto

El laberinto se representa con:

- `NodeRecord`
  - id
  - posición lógica `x,y`
  - opciones
  - salidas probadas
  - salidas bloqueadas
  - salidas verdes transitables

- `EdgeRecord`
  - origen
  - destino
  - orientación
  - estado visitado / bloqueado / verde

Esto permite:

- explorar incrementalmente
- volver a nodos útiles
- evitar repetir salidas bloqueadas
- recalcular el siguiente frente de exploración

### 4. DFS y BFS

#### DFS

Objetivo:

- profundizar en una rama hasta agotar opciones

Ventajas:

- implementación simple
- útil para exploración completa
- buena demostrabilidad del grafo creciendo en profundidad

#### BFS

Objetivo:

- regresar al nodo pendiente más cercano

Ventajas:

- menos coste de reposicionamiento cuando quedan ramas abiertas
- más eficiente para reexploración controlada

### 5. Backtracking

Si una salida resulta inválida o bloqueada:

- se marca la arista
- se ejecuta media vuelta
- se retorna a un nodo conocido
- se selecciona otra salida pendiente

## IA / VC: Visión Artificial

### Qué “IA” hay realmente

El sistema no usa un modelo entrenado de deep learning. La percepción visual es visión clásica basada en heurísticas de color.

Eso implica:

- baja complejidad
- baja latencia
- despliegue viable en ESP32-CAM
- comportamiento explicable y depurable

### Pipeline visual en ESP32-CAM

```text
Frame JPEG
   |
   v
Region of interest (ROI)
   |
   v
Muestreo de pixeles
   |
   v
Conteo de rojo / verde / negro
   |
   v
Reglas de umbral y dominancia
   |
   v
Clasificacion final:
  GREEN_SIGN / RED_SIGN / BLACK_SIGN / NO_SIGN
```

### Entradas y salidas

Entradas:

- frame JPEG capturado por OV2640
- ROI central configurable implícitamente por porcentajes

Salidas:

- `GREEN_SIGN`
- `RED_SIGN`
- `BLACK_SIGN`
- `NO_SIGN`

### Variables clave

El firmware de cámara usa umbrales como:

- porcentaje mínimo de píxeles verdes/rojos/negros
- brillo mínimo
- delta mínimo entre canales
- dominancia relativa

No clasifica formas complejas. Clasifica predominio cromático dentro de una región útil.

### Ventajas de esta aproximación

- explicable
- rápida
- reproducible
- barata computacionalmente

### Limitaciones

- sensible a iluminación
- sensible a balance de blancos y exposición
- no generaliza a señales complejas o textuales
- puede confundir colores lavados o reflejos

## Streaming y Telemetría

### Cámara frontal

Endpoints:

- `GET /status`
- `GET /snapshot`
- `GET /detect`
- `GET :81/stream`

El stream continuo está separado del control para no bloquear:

- observación humana
- detección puntual por HTTP

### Telemetría robot

El robot publica por WebSocket:

- estado FSM
- algoritmo activo
- nodo actual
- conteo de nodos, aristas, cruces y obstáculos
- QTR raw/active
- posición de línea
- potencia de motores
- estado de cámara
- grafo detectado

## Backend y Persistencia

La base SQLite mantiene:

- `users`
- `runs`
- `bets`
- `polls`
- `votes`
- `events`

### Papel del backend

El backend no sustituye al controlador del robot. Su papel es:

- coordinar interfaces
- almacenar eventos
- exponer APIs
- difundir telemetría
- cerrar carreras y liquidar apuestas

## Twitch, Votaciones Y Apuestas

### Twitch

El chat real no entra directo al frontend. El flujo correcto es:

```text
Twitch Chat
   |
   v
Twitch EventSub Bridge
   |
   v
POST /api/twitch/chat
   |
   v
Backend FastAPI
   |
   +--> actualiza SQLite
   |
   +--> emite evento a la UI
```

### Votaciones

Las votaciones sirven para:

- elegir `DFS` o `BFS`
- decidir estados de obstáculos desde la capa social

Arquitectónicamente:

- el backend las persiste
- el frontend las visualiza
- Twitch puede emitir votos

### Apuestas

Las apuestas son ficticias y se liquidan al finalizar la carrera:

- se descuenta stake al abrir la apuesta
- si acierta, se paga `amount * odds`
- si falla, el stake ya quedó perdido

## Decisiones De Diseño Clave

### Separar WROOM y CAM

Razones:

- el control de motores no compite con la captura de vídeo
- la percepción visual no bloquea el seguimiento de línea
- mejora depuración y robustez

### WebSocket para robot

Razones:

- telemetría continua
- latencia baja
- comunicación bidireccional con comandos

### FastAPI + SQLite

Razones:

- stack simple
- suficiente para demo local
- fácil de desplegar y depurar

## Riesgos Técnicos Y Limitaciones

- control sin encoders: depende de calibración por tiempo
- visión por color: depende mucho de luz y enfoque
- WiFi local: introduce dependencia de red para UI, cámara y Twitch bridge
- si backend cae, el robot puede seguir físicamente, pero se pierde observabilidad y capa social

## Resumen Ejecutivo

RoboBet está diseñado como una arquitectura distribuida ligera:

- `ESP32-WROOM` toma decisiones físicas y de navegación
- `ESP32-CAM` aporta visión frontal y streaming
- `FastAPI` coordina interfaces, tiempo real y persistencia
- `Frontend` visualiza y opera el sistema
- `Twitch` añade participación social

La idea central del proyecto es correcta desde el punto de vista arquitectónico:

- desacoplar control, visión, UI y social layer
- mantener el lazo crítico dentro del robot
- usar la web como observabilidad, configuración y experiencia de usuario

