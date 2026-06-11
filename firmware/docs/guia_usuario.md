# Guia De Usuario Y Ejecucion Del Proyecto RoboBet

## 1. Que Es El Proyecto

RoboBet es una demo de robotica interactiva. Un robot sigue un laberinto fisico hecho con lineas negras sobre fondo blanco, decide el recorrido usando DFS o BFS, detecta marcas negras de obstaculo/final y reconoce senales de color con una ESP32-CAM.

La parte web permite que los usuarios:

- Vean la camara frontal del robot.
- Vean una camara cenital desde movil/IP camera.
- Hagan apuestas con puntos ficticios.
- Voten el algoritmo del robot.
- Voten si se limita el motor al 80%.
- Voten en que segmento se coloca un obstaculo.

El objetivo no es dar toda la informacion al usuario, sino mostrar informacion parcial suficiente para decidir, manteniendo incertidumbre y riesgo.

## 2. Que Se Ha Instalado

En este ordenador se han instalado las dependencias necesarias:

- Python ya estaba instalado.
- Dependencias del backend desde `requirements.txt`.
- OpenCV y NumPy para soporte de vision/procesamiento si se usa camara cenital.
- PlatformIO para compilar firmware ESP32.
- Librerias PlatformIO de ESP32, ArduinoJson y WebSockets.

Comprobaciones realizadas:

```powershell
python -m compileall backend
python -m platformio run -d firmware/robot
python -m platformio run -d firmware/esp32_cam
```

Resultado: backend correcto, firmware del robot correcto y firmware de ESP32-CAM correcto.

## 3. Estructura Del Proyecto

```text
robo/
  backend/              Servidor FastAPI, SQLite, WebSockets y API
  frontend/             Interfaz web de usuario y operador
  firmware/robot/       Firmware ESP32-WROOM del robot
  firmware/esp32_cam/   Firmware ESP32-CAM
  docs/                 Documentacion tecnica y guias
  scripts/              Scripts de ayuda
  requirements.txt      Dependencias Python
```

Por que esta separado asi:

- El backend gestiona usuarios, apuestas, votaciones y comunicacion.
- El frontend solo muestra la interfaz y llama a la API.
- El firmware del robot controla sensores, motores y navegacion.
- El firmware de la camara gestiona streaming y deteccion rojo/verde/negro.
- La documentacion queda separada para que el montaje y la demo sean repetibles.

## 4. Ejecutar La Web Local

Desde PowerShell, dentro de:

```powershell
C:\Users\amine\Documents\robo
```

ejecuta:

```powershell
.\scripts\run_backend.ps1
```

Luego abre en el navegador:

```text
http://127.0.0.1:8000
```

Por que se hace asi:

- FastAPI levanta el servidor local.
- El mismo servidor entrega la web del frontend.
- SQLite se crea automaticamente en `data/robobet.sqlite3`.
- WebSockets quedan disponibles para robot y navegador.

Si el servidor ya esta arrancado, no hace falta repetirlo. En la ejecucion anterior quedo activo en:

```text
http://127.0.0.1:8000
```

## 5. Probar Sin Robot Fisico

Con el backend arrancado, abre otra terminal en la carpeta del proyecto y ejecuta:

```powershell
.\scripts\simulate_robot.ps1
```

Luego abre la web y pulsa `Iniciar`. El simulador se conectara a `/ws/robot`, recibira los comandos del backend y emitira una secuencia con cruces, marca negra verde y final negro.

Para probar una media vuelta por senal roja:

```powershell
.\scripts\simulate_robot.ps1 --scenario red-backtrack
```

Esta prueba valida frontend, backend, WebSockets, eventos y liquidacion de apuestas sin depender del hardware.

## 6. Configurar WiFi Antes De Flashear

Edita estos archivos:

```text
firmware/robot/platformio.ini
firmware/esp32_cam/platformio.ini
```

Cambia:

```ini
-D WIFI_SSID=\"ROBOBET_WIFI\"
-D WIFI_PASSWORD=\"ROBOBET_PASS\"
```

En `firmware/robot/platformio.ini`, cambia tambien:

```ini
-D ROBOBET_SERVER_HOST=\"192.168.1.100\"
```

Pon ahi la IP del portatil que ejecuta el backend.

Para ver la IP del portatil:

```powershell
ipconfig
```

Busca la direccion IPv4 de la WiFi.

Por que es necesario:

- El robot necesita saber a que servidor conectarse.
- El robot usa `AP+STA`: crea el AP `RLP-ROBOBET` para la ESP32-CAM y ademas se conecta al WiFi configurado para hablar con el backend.
- `127.0.0.1` no sirve para el robot, porque en el robot significaria "yo mismo", no el portatil.

## 7. Compilar Firmware

Robot ESP32-WROOM:

```powershell
python -m platformio run -d firmware/robot
```

ESP32-CAM:

```powershell
python -m platformio run -d firmware/esp32_cam
```

Por que se compila antes:

- Confirma que el codigo es valido.
- Descarga librerias necesarias.
- Genera el `.bin` que se subira a cada placa.

## 8. Flashear El Robot

Conecta la ESP32-WROOM por USB y ejecuta:

```powershell
python -m platformio run -d firmware/robot -t upload
```

Si hay varios puertos serie, puedes indicar el puerto:

```powershell
python -m platformio run -d firmware/robot -t upload --upload-port COM5
```

Por que se hace:

- Carga el firmware que controla QTR-8RC, motores, DFS/BFS y comunicacion con el servidor.

Importante:

- La comunicacion robot-camara puede probarse por HTTP antes de soldar UART.
- Cuando se use UART, puede ser necesario desconectar temporalmente la UART entre robot y camara durante flasheo si interfiere.
- Despues de flashear con UART activo, evita usar logs por Serial en ese canal porque se usa para comunicacion con la camara.

## 9. Flashear La ESP32-CAM

Conecta la ESP32-CAM con adaptador USB-serie.

Para flashear normalmente hay que:

- Conectar `IO0` a `GND`.
- Reiniciar o alimentar la placa.
- Ejecutar upload.
- Quitar `IO0` de `GND`.
- Reiniciar otra vez.

Comando:

```powershell
python -m platformio run -d firmware/esp32_cam -t upload
```

Con puerto concreto:

```powershell
python -m platformio run -d firmware/esp32_cam -t upload --upload-port COM6
```

Por que se hace:

- La ESP32-CAM servira el video frontal en `/stream`.
- Detectara senales verde/roja/negra.
- Respondera al robot `GREEN_SIGN`, `RED_SIGN`, `BLACK_SIGN` o `NO_SIGN` por HTTP o UART.

## 10. Conexiones Hardware Obligatorias

Resumen de alimentacion:

- Portapilas 6xAA:
  - `+9V_BAT -> TB6612FNG VM`
  - `GND -> GND comun`
- Powerbank 5 V:
  - `+5V -> ESP32-WROOM 5V`
  - `+5V -> ESP32-CAM 5V`
  - `GND -> GND comun`
- ESP32-WROOM:
  - `3V3 -> QTR Vcc`
  - `3V3 -> QTR LEDON`
  - `3V3 -> TB6612FNG VCC`
  - `3V3 -> TB6612FNG STBY`

No unir nunca:

```text
+9V_BAT con +5V
```

Si unir:

```text
Todas las masas GND
```

Por que:

- Los motores consumen mas corriente y ruido electrico, por eso usan bateria aparte.
- La electronica necesita 5 V estable desde la powerbank.
- Las senales solo funcionan bien si todos comparten referencia GND.

## 11. Uso De La Web

Abre:

```text
http://127.0.0.1:8000
```

### Usuario

1. Escribe un nombre.
2. Pulsa `Entrar`.
3. El usuario recibe puntos ficticios.

Por que:

- Las apuestas necesitan asociarse a un usuario.
- El ranking ordena usuarios por saldo.

### Streams

En la caja de camara frontal pon:

```text
http://IP_DE_LA_ESP32_CAM/stream
```

En camara cenital pon la URL del movil/IP camera.

Por que:

- La ESP32-CAM muestra lo que ve el robot.
- La camara cenital da vision global al usuario, pero no necesariamente toda la informacion interna del robot.

### Apuestas

Selecciona tipo, opcion y puntos.

Tipos incluidos:

- Si llega al final.
- Tiempo por rangos.
- Numero de obstaculos.
- Algoritmo.

Por que:

- Las apuestas crean incertidumbre y hacen que el usuario tome decisiones con informacion parcial.
- Se cierran al iniciar la carrera para evitar trampas.

### Votaciones

Se pueden crear votaciones para:

- DFS/BFS.
- Limite motor 80% o 100%.
- Segmento donde colocar obstaculo.

Al cerrar una votacion, el backend manda el comando al robot.

Por que:

- Permite interaccion real del publico.
- Mantiene controlado el sistema: los usuarios eligen entre opciones validas, no cualquier punto arbitrario.

### Operador

Botones:

- `Iniciar`: comienza carrera.
- `Parar`: detiene el robot.
- `Reset`: reinicia estado.
- `Calibrar QTR`: calibra sensores de linea.

Por que:

- El operador mantiene control de seguridad.
- La calibracion QTR es necesaria porque la luz ambiente y el suelo cambian las lecturas.

## 12. Orden Recomendado Para Una Demo

1. Montar circuito y comprobar GND comun.
2. Encender powerbank de 5 V.
3. Encender alimentacion de motores de 9 V.
4. Arrancar backend:

   ```powershell
   .\scripts\run_backend.ps1
   ```

5. Abrir web:

   ```text
   http://127.0.0.1:8000
   ```

6. Comprobar stream ESP32-CAM:

   ```text
   http://IP_DE_LA_ESP32_CAM/stream
   ```

7. Pegar URL del stream frontal en la web.
8. Pegar URL de la camara cenital.
9. Crear usuario de prueba.
10. Calibrar QTR.
11. Crear votacion de algoritmo.
12. Crear votacion de velocidad.
13. Crear votacion de obstaculo.
14. Crear apuestas.
15. Cerrar votaciones.
16. Colocar marca negra de obstaculo/final y la senal de camara correspondiente si aplica.
17. Pulsar `Iniciar`.
18. Supervisar telemetria y video.
19. Al detectar `BLACK_SIGN`, se finaliza la carrera y se liquidan puntos.

## 13. Como Funciona El Robot

El robot hace tres cosas principales:

1. Sigue la linea con QTR-8RC.
2. Detecta cruces y los convierte en nodos del grafo.
3. Decide el siguiente tramo usando DFS o BFS.

Cuando encuentra una marca negra completa con los QTR:

- Si la camara ve verde, atraviesa la marca y continua.
- Si la camara ve rojo, marca el camino como bloqueado y hace media vuelta.
- Si la camara ve negro, considera que ha llegado al final.
- Si no hay senal o hay timeout, actua como rojo por seguridad.

Por que:

- La marca negra actua como punto de decision donde el robot consulta la camara.
- El robot no debe quedarse parado; debe replanificar.
- El grafo permite recordar tramos bloqueados y explorados.

## 14. Ajustes Si Algo No Va Bien

### El robot no conecta

Revisar:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `ROBOBET_SERVER_HOST`
- Que el robot este conectado al WiFi del backend.
- Que la ESP32-CAM este conectada al AP `RLP-ROBOBET` del robot si se prueba por HTTP.
- Que el backend este arrancado.

### La linea se detecta mal

Revisar:

- Altura del QTR.
- Luz ambiente.
- Calibracion.
- Valor `LINE_THRESHOLD` en `firmware/robot/src/main.cpp`.

### Motores giran al reves

Soluciones:

- Cambiar terminales del motor.
- Cambiar en codigo:

```cpp
static constexpr bool INVERT_LEFT_MOTOR = true;
static constexpr bool INVERT_RIGHT_MOTOR = true;
```

### Robot gira demasiado o poco

Ajustar:

```cpp
BASE_PWM
TURN_PWM
TURN_MIN_MS
TURN_TIMEOUT_MS
```

### Camara detecta mal rojo/verde/negro

Ajustar en `firmware/esp32_cam/src/main.cpp`:

```cpp
MIN_SIGN_PIXELS
```

y los umbrales RGB dentro de:

```cpp
analyzeFrame()
```

## 15. Comandos Utiles

Arrancar backend:

```powershell
.\scripts\run_backend.ps1
```

Comprobar backend:

```powershell
.\scripts\check_backend.ps1
```

Simular robot:

```powershell
.\scripts\simulate_robot.ps1
```

Compilar robot:

```powershell
python -m platformio run -d firmware/robot
```

Compilar ESP32-CAM:

```powershell
python -m platformio run -d firmware/esp32_cam
```

Subir robot:

```powershell
python -m platformio run -d firmware/robot -t upload
```

Subir ESP32-CAM:

```powershell
python -m platformio run -d firmware/esp32_cam -t upload
```

Ver procesos Python:

```powershell
Get-Process python
```

Parar servidor si conoces el PID:

```powershell
Stop-Process -Id PID
```

## 16. Por Que Esta Arquitectura Es Adecuada

- **Web propia**: da control total sobre apuestas, votaciones, telemetria y streams.
- **Puntos ficticios**: evita problemas legales y permite demo academica segura.
- **Servidor local**: reduce latencia y dependencia de internet.
- **ESP32-WROOM separada de ESP32-CAM**: el robot no se bloquea por procesar video.
- **UART camara-robot**: las senales criticas llegan aunque la web vaya lenta.
- **QTR-8RC**: es adecuado para linea negra sobre blanco.
- **TB6612FNG**: encaja con dos motores DC y control PWM.
- **DFS/BFS**: permite comparar estrategias de resolucion de laberintos.
- **Grafo**: representa cruces y caminos de forma clara para replanificar.
- **Camara cenital**: da contexto global al publico sin sustituir la autonomia del robot.

