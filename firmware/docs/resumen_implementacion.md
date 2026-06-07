# RoboBet: Resumen Completo De Implementacion

Este documento explica que se ha construido, donde esta cada parte del proyecto y como encajan todos los componentes. La idea es que cualquiera pueda abrir la carpeta y entender el sistema completo sin tener que leer todo el codigo primero.

## 1. Objetivo Del Proyecto

RoboBet es una experiencia interactiva de robotica basada en un robot maze solver.

El robot circula por un laberinto fisico construido con:

- Suelo blanco.
- Lineas negras.
- Obstaculos simulados con cinta blanca sobre la linea negra.
- Senal verde para indicar obstaculo colocado por usuarios.
- Senal roja para indicar final del laberinto.

La parte interactiva es una web local donde los usuarios pueden:

- Ver el stream frontal de la ESP32-CAM.
- Ver una camara cenital externa, por ejemplo un movil/IP camera.
- Crear un usuario con puntos ficticios.
- Apostar puntos.
- Votar si el robot usa DFS o BFS.
- Votar si se activa restriccion de motor al 80%.
- Votar en que segmento se coloca un obstaculo.

La idea de diseno es que el usuario tenga informacion parcial: suficiente para apostar con criterio, pero no toda la informacion interna del robot.

## 2. Estructura General De Carpetas

```text
robo/
  backend/
    app/
      main.py
      config.py
      database.py
      realtime.py
      schemas.py
      routers/
      services/
  frontend/
    index.html
    styles.css
    app.js
  firmware/
    robot/
      platformio.ini
      src/main.cpp
    esp32_cam/
      platformio.ini
      src/main.cpp
  docs/
    calibration.md
    guia_usuario.md
    hardware.md
    protocol.md
    resumen_implementacion.md
  scripts/
    run_backend.ps1
    check_backend.ps1
  requirements.txt
  README.md
```

## 3. Backend

El backend esta en:

```text
backend/app/
```

Se ha implementado con FastAPI. Es el centro del sistema: sirve la web, guarda datos, recibe telemetria del robot y envia comandos.

### Archivos Importantes

```text
backend/app/main.py
```

Es la entrada principal del backend. Hace estas cosas:

- Crea la app FastAPI.
- Activa CORS.
- Inicializa la base de datos al arrancar.
- Registra todas las rutas API.
- Crea WebSocket para navegador: `/ws/ui`.
- Crea WebSocket para robot: `/ws/robot`.
- Sirve el frontend desde la carpeta `frontend/`.

```text
backend/app/database.py
```

Gestiona SQLite. Crea automaticamente la base de datos en:

```text
data/robobet.sqlite3
```

Tablas creadas:

- `users`: usuarios y puntos.
- `runs`: carreras del robot.
- `bets`: apuestas.
- `polls`: votaciones.
- `votes`: votos de usuarios.
- `events`: eventos recibidos.

```text
backend/app/realtime.py
```

Gestiona WebSockets y estado en tiempo real.

Mantiene:

- Clientes web conectados.
- Robot conectado.
- Estado actual del robot.
- Cola de comandos si el robot aun no esta conectado.

```text
backend/app/schemas.py
```

Define modelos Pydantic para validar datos de entrada:

- Crear usuario.
- Crear apuesta.
- Crear votacion.
- Votar.
- Comandos.
- Iniciar/finalizar carrera.

## 4. Rutas API

Las rutas estan en:

```text
backend/app/routers/
```

### Usuarios

Archivo:

```text
backend/app/routers/users.py
```

Endpoints:

```text
GET  /api/users
POST /api/users
GET  /api/users/{user_id}
```

Sirven para crear usuarios temporales y consultar ranking.

### Apuestas

Archivo:

```text
backend/app/routers/bets.py
```

Endpoints:

```text
GET  /api/bets
POST /api/bets
```

Funcionamiento:

- El usuario apuesta puntos ficticios.
- Los puntos se descuentan al apostar.
- Si hay carrera activa, no se permiten apuestas nuevas.

### Votaciones

Archivo:

```text
backend/app/routers/polls.py
```

Endpoints:

```text
GET  /api/polls
POST /api/polls
POST /api/polls/{poll_id}/vote
POST /api/polls/{poll_id}/close
```

Cuando se cierra una votacion:

- Si es algoritmo, envia `SET_ALGORITHM` al robot.
- Si es velocidad, envia `SET_SPEED_LIMIT`.
- Si es obstaculo, envia `SET_OBSTACLE_EDGE`.

### Carrera

Archivo:

```text
backend/app/routers/run.py
```

Endpoints:

```text
GET  /api/run/state
POST /api/run/start
POST /api/run/stop
POST /api/run/reset
POST /api/run/finish
GET  /api/run/history
```

Funcionamiento:

- `start` crea una carrera y manda comandos al robot.
- `stop` detiene el robot.
- `reset` reinicia el estado.
- `finish` cierra la carrera y liquida apuestas.

### Operador

Archivo:

```text
backend/app/routers/operator.py
```

Endpoint:

```text
POST /api/operator/command
```

Permite enviar comandos directos al robot, por ejemplo calibrar QTR.

### Streams

Archivo:

```text
backend/app/routers/streams.py
```

Endpoint:

```text
GET /api/streams/defaults
```

Devuelve URLs por defecto de camaras.

## 5. Logica De Apuestas

Archivo:

```text
backend/app/services/betting.py
```

Tipos implementados:

- `finish`: si el robot llega al final.
- `time_range`: tiempo por rangos.
- `obstacles`: numero de obstaculos.
- `algorithm`: algoritmo usado.

La funcion importante es:

```text
settle_bets()
```

Hace esto:

1. Busca apuestas abiertas.
2. Comprueba si cada apuesta ha ganado.
3. Calcula pago segun cuotas.
4. Actualiza saldo de usuarios.
5. Marca apuestas como `won` o `lost`.

## 6. Logica De Votaciones

Archivo:

```text
backend/app/services/polls.py
```

La funcion principal es:

```text
close_poll()
```

Hace:

1. Cuenta votos.
2. Escoge ganador.
3. Marca la votacion como cerrada.
4. Devuelve resultado.

Despues, el router traduce el resultado a comandos para el robot.

## 7. Frontend Web

El frontend esta en:

```text
frontend/
```

Archivos:

```text
frontend/index.html
frontend/styles.css
frontend/app.js
```

### index.html

Define la estructura visual:

- Cabecera con estado de conexion del robot.
- Panel de camara frontal.
- Panel de camara cenital.
- Panel de estado del robot.
- Panel de operador.
- Panel de usuario.
- Panel de apuestas.
- Panel de votaciones.
- Log de eventos.

### styles.css

Define el estilo:

- Layout responsive.
- Paneles.
- Botones.
- Estado visual del robot conectado/desconectado.
- Cuadros de video.
- Formularios.

### app.js

Contiene la logica del navegador:

- Llama a la API.
- Mantiene usuario activo en `localStorage`.
- Actualiza ranking.
- Crea apuestas.
- Crea y cierra votaciones.
- Conecta al WebSocket `/ws/ui`.
- Muestra telemetria en tiempo real.
- Permite pegar URLs de streams.

## 8. Firmware Del Robot ESP32-WROOM-32

Esta en:

```text
firmware/robot/
```

Archivos:

```text
firmware/robot/platformio.ini
firmware/robot/src/main.cpp
```

### platformio.ini

Configura PlatformIO:

```ini
board = esp32dev
```

Esto es correcto para ESP32-WROOM-32.

Tambien define:

```ini
WIFI_SSID
WIFI_PASSWORD
ROBOBET_SERVER_HOST
ROBOBET_SERVER_PORT
```

Estos valores deben cambiarse antes de flashear para que el robot se conecte a vuestro WiFi y al servidor del portatil.

### main.cpp Del Robot

Implementa:

- Pines del QTR-8RC.
- Pines del TB6612FNG.
- Lectura RC del QTR.
- Calibracion de sensores.
- PID de seguimiento de linea.
- Control de motores por PWM.
- Maquina de estados.
- Deteccion de cruces.
- Seleccion de movimiento DFS/BFS.
- Deteccion de perdida de linea.
- Tratamiento de obstaculos.
- Backtracking.
- Deteccion de final.
- WebSocket con backend.
- Recepcion UART desde ESP32-CAM.

### Pines QTR

```text
IO32 -> QTR_S1
IO33 -> QTR_S2
IO25 -> QTR_S3
IO26 -> QTR_S4
IO27 -> QTR_S5
IO14 -> QTR_S6
IO12 -> QTR_S7
IO13 -> QTR_S8
```

### Pines Motores

```text
IO23 -> MOTOR_IZQ_PWM
IO22 -> MOTOR_IZQ_IN1
IO21 -> MOTOR_IZQ_IN2
IO19 -> MOTOR_DER_PWM
IO18 -> MOTOR_DER_IN1
IO16 -> MOTOR_DER_IN2
```

### Estados Del Robot

```text
IDLE
CALIBRATING
WAITING_START
FOLLOWING_LINE
NODE_DETECTED
SELECTING_EDGE
OBSTACLE_CHECK
BACKTRACKING
FINISH_CHECK
FINISHED
ERROR_STATE
```

### Funcionamiento Del Robot

1. Espera comandos del servidor.
2. Se calibra el QTR si el operador lo pide.
3. Al iniciar carrera, sigue la linea.
4. Si detecta cruce, crea un nodo.
5. Decide salida segun DFS o BFS.
6. Si pierde la linea, consulta la senal recibida por camara.
7. Si hay rojo, termina.
8. Si hay verde y recupera linea, continua.
9. Si no recupera linea, marca obstaculo y vuelve atras.

## 9. Firmware ESP32-CAM

Esta en:

```text
firmware/esp32_cam/
```

Archivos:

```text
firmware/esp32_cam/platformio.ini
firmware/esp32_cam/src/main.cpp
```

### platformio.ini

Configura:

```ini
board = esp32cam
```

Y el WiFi:

```ini
WIFI_SSID
WIFI_PASSWORD
```

### main.cpp De La ESP32-CAM

Implementa:

- Configuracion de camara AI Thinker ESP32-CAM.
- Conexion WiFi.
- Servidor HTTP.
- Stream MJPEG en `/stream`.
- Estado JSON en `/status`.
- Deteccion de rojo y verde.
- Envio UART de `GREEN_SIGN`, `RED_SIGN` o `NO_SIGN`.
- mDNS con `http://esp32cam.local/`.
- Red fallback si no conecta al WiFi.

### Endpoints De La Camara

```text
http://IP_CAM/
http://IP_CAM/stream
http://IP_CAM/status
```

Si el WiFi falla, crea:

```text
SSID: RoboBet-CAM
Password: robobetcam
IP: http://192.168.4.1
```

### Deteccion De Senales

La camara analiza frames RGB565:

- Muchos pixeles rojos -> `RED_SIGN`.
- Muchos pixeles verdes -> `GREEN_SIGN`.
- Nada claro -> `NO_SIGN`.

Estos eventos se mandan por Serial/UART al robot.

## 10. Comunicacion Entre Componentes

### Web Con Backend

La web usa:

- REST para acciones normales.
- WebSocket `/ws/ui` para eventos en vivo.

### Backend Con Robot

El robot se conecta al backend por:

```text
ws://IP_SERVIDOR:8000/ws/robot
```

Comandos servidor -> robot:

```text
START_RUN
STOP_RUN
RESET_RUN
CALIBRATE_QTR
SET_ALGORITHM
SET_SPEED_LIMIT
SET_OBSTACLE_EDGE
```

Eventos robot -> servidor:

```text
ROBOT_READY
CALIBRATION_STARTED
CALIBRATION_DONE
RUN_STARTED
EDGE_SELECTED
LINE_LOST
OBSTACLE_DETECTED
BACKTRACK_STARTED
BACKTRACK_DONE
FINISH_DETECTED
RUN_STOPPED
RUN_RESET
```

### ESP32-CAM Con Robot

La ESP32-CAM manda por UART:

```text
GREEN_SIGN
RED_SIGN
NO_SIGN
```

El robot usa eso para distinguir obstaculo/final.

## 11. Documentacion Existente

```text
docs/guia_usuario.md
```

Guia principal para ejecutar, flashear y usar el proyecto.

```text
docs/hardware.md
```

Resumen de conexiones electricas y pinout.

```text
docs/protocol.md
```

Formato de comandos y eventos.

```text
docs/calibration.md
```

Como calibrar QTR, ajustar motores y probar senales.

```text
docs/resumen_implementacion.md
```

Este documento.

## 12. Scripts

```text
scripts/run_backend.ps1
```

Arranca el servidor:

```powershell
python -m uvicorn backend.app.main:app --host 0.0.0.0 --port 8000 --reload
```

Si PowerShell bloquea scripts, usar:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_backend.ps1
```

O directamente:

```powershell
python -m uvicorn backend.app.main:app --host 127.0.0.1 --port 8000 --reload
```

```text
scripts/check_backend.ps1
```

Comprueba sintaxis del backend:

```powershell
python -m compileall backend
```

## 13. Comandos Principales

Desde:

```powershell
cd C:\Users\amine\Documents\robo
```

Arrancar web:

```powershell
python -m uvicorn backend.app.main:app --host 127.0.0.1 --port 8000 --reload
```

Compilar robot:

```powershell
python -m platformio run -d firmware/robot
```

Subir robot:

```powershell
python -m platformio run -d firmware/robot -t upload --upload-port COM9
```

Compilar ESP32-CAM:

```powershell
python -m platformio run -d firmware/esp32_cam
```

Subir ESP32-CAM:

```powershell
python -m platformio run -d firmware/esp32_cam -t upload --upload-port COM10
```

Monitor ESP32-CAM:

```powershell
python -m platformio device monitor -d firmware/esp32_cam --port COM10 --baud 115200
```

Listar puertos:

```powershell
python -m platformio device list
```

## 14. Que Se Ha Probado

Se ha probado:

- Instalacion de dependencias Python.
- Instalacion de PlatformIO.
- Compilacion del backend.
- Importacion de FastAPI.
- Compilacion del firmware del robot.
- Compilacion del firmware ESP32-CAM.
- Flasheo correcto del robot ESP32-WROOM-32 por `COM9`.

Resultado del flasheo del robot:

```text
Chip is ESP32-D0WD-V3
Hash of data verified.
Hard resetting via RTS pin.
SUCCESS
```

La ESP32-CAM se detecto como:

```text
COM10
USB-SERIAL CH340
```

Queda pendiente flashearla cuando el puerto no este ocupado.

## 15. Por Que Se Ha Implementado Asi

### Web Propia

Se eligio web propia porque permite controlar:

- Apuestas.
- Puntos ficticios.
- Votaciones.
- Telemetria.
- Streams.
- Panel de operador.

Con Twitch seria mas dificil controlar todo y dependeriamos de servicios externos.

### Backend Local

El servidor local reduce latencia y evita depender de internet durante la demo.

### SQLite

SQLite es suficiente porque:

- Es una demo local.
- No necesita instalar servidor de base de datos.
- Guarda usuarios, apuestas y carreras de forma simple.

### ESP32-WROOM Para Robot

La ESP32-WROOM controla sensores y motores porque necesita respuesta rapida y estable.

### ESP32-CAM Separada

La ESP32-CAM hace video y color. Separarla del robot evita cargar al controlador principal con procesamiento de imagen.

### UART Entre Camara Y Robot

La senal critica de color llega directamente al robot aunque la web vaya lenta.

### DFS/BFS

DFS y BFS son algoritmos claros para explicar resolucion de grafos:

- DFS explora profundo.
- BFS explora por niveles.

Esto encaja con la idea del laberinto como grafo.

### Obstaculo Con Cinta Blanca

La cinta blanca corta la linea negra. El QTR interpreta eso como perdida de linea. Con la senal verde, el robot sabe que no es final sino obstaculo de usuario.

### Senal Roja

La senal roja evita confundir final de laberinto con obstaculo.

## 16. Limitaciones Actuales

- El grafo del robot es una implementacion practica para demo, no un mapa geometrico perfecto.
- La deteccion rojo/verde usa umbrales simples RGB, puede requerir ajuste con la luz real.
- La UART0 se comparte con programacion/monitor serie, asi que durante montaje puede hacer falta desconectar camara para flashear.
- La camara cenital se muestra como stream externo, no se ha implementado aun deteccion avanzada de mapa desde OpenCV.
- Los segmentos de obstaculo en la web son opciones simbolicas `edge_A`, `edge_B`, `edge_C`; se pueden adaptar a los segmentos reales del laberinto.

## 17. Siguiente Trabajo Recomendado

1. Configurar WiFi real en ambos `platformio.ini`.
2. Flashear ESP32-CAM.
3. Confirmar IP de la camara con monitor serie o `esp32cam.local`.
4. Probar `/stream` y `/status`.
5. Conectar robot al backend.
6. Calibrar QTR.
7. Ajustar motores si giran al reves.
8. Ajustar `LINE_THRESHOLD`, `BASE_PWM` y `TURN_PWM` con el laberinto real.
9. Sustituir `edge_A`, `edge_B`, `edge_C` por nombres reales del laberinto.
10. Hacer demo completa con apuestas, votaciones, obstaculo y final rojo.

