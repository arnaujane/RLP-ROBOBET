# RoboBet

RoboBet es una demo local de robotica interactiva: un robot sigue un laberinto de lineas negras, resuelve cruces como grafo con DFS/BFS, detecta obstaculos con cinta blanca y señales de color, y una web permite apostar puntos ficticios y votar restricciones.

## Estructura

- `backend/`: API FastAPI, SQLite, WebSockets y logica de apuestas/votaciones.
- `frontend/`: interfaz web servida por el backend.
- `firmware/robot/`: firmware PlatformIO para ESP32-WROOM.
- `firmware/esp32_cam/`: firmware PlatformIO para ESP32-CAM.
- `docs/`: conexionado, calibracion y protocolo.
- `scripts/run_backend.ps1`: arranque del servidor local.

## Arranque Rapido

La guia completa de usuario y ejecucion esta en:

```text
docs/guia_usuario.md
```

El resumen completo de implementacion, arquitectura y ubicacion de archivos esta en:

```text
docs/resumen_implementacion.md
```

1. Instala dependencias Python:

   ```powershell
   python -m venv .venv
   .\.venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   ```

2. Arranca la web:

   ```powershell
   .\scripts\run_backend.ps1
   ```

3. Abre:

   ```text
   http://localhost:8000
   ```

4. En `firmware/robot/platformio.ini` y `firmware/esp32_cam/platformio.ini`, cambia:

   - `WIFI_SSID`
   - `WIFI_PASSWORD`
   - `ROBOBET_SERVER_HOST` en el robot, usando la IP del portatil.

5. Flashea:

   ```powershell
   pio run -d firmware/robot -t upload
   pio run -d firmware/esp32_cam -t upload
   ```

## Flujo De Demo

1. Encender powerbank de 5 V y bateria de motores.
2. Arrancar backend.
3. Encender ESP32-CAM y comprobar `http://IP_CAM/stream`.
4. Encender robot y esperar que aparezca como conectado.
5. Calibrar QTR desde la web.
6. Crear usuarios.
7. Crear votaciones de algoritmo, velocidad y obstaculo.
8. Crear apuestas.
9. Cerrar votaciones.
10. Colocar cinta blanca manualmente si gana una votacion de obstaculo.
11. Iniciar carrera.
12. Al detectar rojo, el robot envia `FINISH_DETECTED` y el backend liquida apuestas.

## Notas Criticas

- No unir `+9V_BAT` con `+5V`.
- Todas las masas GND deben estar en comun.
- El driver real implementado es TB6612FNG/HW-166 para dos motores DC.
- `STBY` del TB6612FNG queda fijo a `+3V3`.
- `LEDON` del QTR queda fijo a `+3V3`.
- UART0 esta cableado entre ESP32-WROOM y ESP32-CAM. Se usa para senales `GREEN_SIGN`, `RED_SIGN` y `NO_SIGN`; evita usar logs por Serial despues de flashear.
