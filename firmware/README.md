# RoboBet

RoboBet es una demo local de robotica interactiva: un robot sigue un laberinto de lineas negras, resuelve cruces con DFS/BFS practico, detecta obstaculos con manchas negras y senales de color, y una web permite apostar puntos ficticios y votar restricciones.

## Estructura

- `backend/`: API FastAPI, SQLite, WebSockets y logica de apuestas/votaciones.
- `frontend/`: interfaz web servida por el backend.
- `robot/`: firmware PlatformIO para ESP32-WROOM.
- `esp32_cam/`: firmware PlatformIO para ESP32-CAM.
- `data/robobet.sqlite3`: base de datos local SQLite con usuarios, apuestas, votaciones, votos, eventos y tiempos de carrera.
- `docs/`: conexionado, calibracion y protocolo.
- `docs/seguimiento_pruebas.md`: incidencias reales, diagnosticos e implementaciones aplicadas.
- `docs/frontend_integracion.md`: como conectar el dashboard con sensores, motores, camaras, Twitch e historial real.
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

4. Para probar la web sin robot fisico, abre otra terminal y ejecuta:

   ```powershell
   .\scripts\simulate_robot.ps1
   ```

   Luego pulsa `Iniciar` desde la web. El simulador se conecta a `/ws/robot`, emite cruces, obstaculos y final de carrera.

5. En `robot/platformio.ini` y `esp32_cam/platformio.ini`, cambia:

   - `WIFI_SSID`
   - `WIFI_PASSWORD`
   - `ROBOBET_SERVER_HOST` en el robot, usando la IP del portatil.

   El robot usa `AP+STA`: mantiene el AP `RLP-ROBOBET` para la ESP32-CAM y tambien se conecta al WiFi configurado para hablar con el backend.

6. Flashea:

   ```powershell
   pio run -d robot -t upload
   pio run -d esp32_cam -t upload
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
10. Preparar manchas negras de obstaculo/final y senales de camara verde, roja o negra.
11. Iniciar carrera.
12. Si la camara detecta verde, el robot cruza la mancha; si detecta rojo o no esta segura, hace media vuelta; si detecta negro, envia `FINISH_DETECTED` y el backend liquida apuestas.

## Notas Criticas

- No unir `+9V_BAT` con `+5V`.
- Todas las masas GND deben estar en comun.
- El driver real implementado es TB6612FNG/HW-166 para dos motores DC.
- `STBY` del TB6612FNG queda fijo a `+3V3`.
- `LEDON` del QTR queda fijo a `+3V3`.
- UART0 esta cableado entre ESP32-WROOM y ESP32-CAM. Se usa para senales `GREEN_SIGN`, `RED_SIGN` y `NO_SIGN`; evita usar logs por Serial despues de flashear.
