# Camara ESP32-CAM

## Objetivo

La camara frontal del robot debe priorizar baja latencia, estabilidad y una imagen suficientemente limpia para que el usuario vea el laberinto en primera persona. El firmware usa captura JPEG nativa para no convertir cada frame por software.

## Perfiles

Los perfiles se seleccionan con `CAMERA_PROFILE` en `esp32_cam/src/main.cpp` o por `build_flags`.

```text
CAMERA_PROFILE_FLUID
- QVGA 320x240
- JPEG quality 12
- XCLK 20 MHz
- fb_count 2 con PSRAM
- grab latest
- Recomendado para robot movil y vista web fluida

CAMERA_PROFILE_BALANCED
- VGA 640x480
- JPEG quality 12
- XCLK 20 MHz
- fb_count 2 con PSRAM
- grab latest
- Usar solo si WiFi, luz y alimentacion son estables

CAMERA_PROFILE_STABLE
- QVGA 320x240
- JPEG quality 18
- XCLK 10 MHz
- fb_count 1
- grab when empty
- Usar si aparecen lineas, frames corruptos o cortes
```

En la libreria `esp32-camera`, un valor menor de `jpeg_quality` suele significar mas calidad y mas peso por frame. Valores utiles para comparar: `10`, `12`, `15`, `18`, `20`.

## Endpoints

```text
http://IP_CAM/status
http://IP_CAM/snapshot
http://IP_CAM/detect
http://IP_CAM:81/stream
```

El control queda en el puerto `80`. El MJPEG continuo usa el puerto `81` para no bloquear `/detect` mientras la web esta viendo el stream.

## Pruebas Recomendadas

1. Flashear la ESP32-CAM.
2. Abrir `http://IP_CAM/status`.
3. Comprobar:
   - `profile`
   - `psram_found`
   - `fps`
   - `last_frame_bytes`
   - `last_capture_ms`
   - `rssi`
   - `corrupt_frames`
   - `capture_failures`
4. Abrir `http://IP_CAM:81/stream` durante 30 segundos.
5. Mientras el stream esta abierto, probar `http://IP_CAM/detect`.
6. Comparar perfiles:
   - FPS alto y pocos fallos: mantener `FLUID`.
   - Imagen mejor pero todavia estable: probar `BALANCED`.
   - Artefactos o cortes: usar `STABLE`.

## Comprobaciones Fisicas

- Alimentar la ESP32-CAM con 5 V estable.
- Evitar puertos USB debiles o cables largos de mala calidad.
- Separar alimentacion de motores y ESP32-CAM si hay ruido electrico.
- Mantener masa comun si hay fuentes separadas.
- Anadir un condensador cerca de la ESP32-CAM si hay caidas de tension.
- Revisar que el flex de la OV2640 este bien asentado.
- Reducir vibraciones con soporte rigido o goma.
- Mejorar la luz del laberinto con iluminacion blanca difusa.
- Ajustar el foco fisico de la lente si la imagen esta borrosa.

## Diagnostico Serial

`CAMERA_DIAGNOSTIC_LOGS=1` activa logs periodicos:

```text
profile, fps, KB/frame, capture ms, resolucion, JPEG quality, XCLK,
fb_count, grab_mode, RSSI, heap libre, PSRAM libre, health y fallos
```

Cuando se use UART entre WROOM y ESP32-CAM para decisiones del robot, estos logs deben estar desactivados para no mezclar diagnostico con mensajes de protocolo.

## Ajuste De Senal Verde

El verde se detecta con umbrales mas tolerantes que el rojo porque en baja luz suele perder saturacion antes. Los parametros relevantes estan en `esp32_cam/src/main.cpp`:

```text
MIN_GREEN_PERCENT
GREEN_MIN_CHANNEL
GREEN_MIN_DELTA
COLOR_MIN_BRIGHTNESS
CAMERA_SENSOR_SATURATION
CAMERA_SENSOR_AE_LEVEL
```

Si aparecen falsos positivos verdes, subir `GREEN_MIN_CHANNEL` o `GREEN_MIN_DELTA`. Si sigue costando detectar una senal verde real, revisar primero iluminacion y contraluz antes de bajar mas los umbrales.

## Ajuste De Senal Roja

El rojo puede lavarse con mucha luz y parecer naranja/amarillo. Por eso el firmware tambien cuenta como rojo los pixeles calidos donde rojo y verde estan altos, pero azul queda claramente por debajo.

Parametros relevantes:

```text
MIN_RED_PERCENT
RED_MIN_CHANNEL
RED_MIN_DELTA
GREEN_MIN_CHANNEL
GREEN_MIN_DELTA
CAMERA_SENSOR_AE_LEVEL
```

Si una senal roja sale como verde, normalmente hay demasiada luz directa o el verde esta demasiado permisivo: subir `GREEN_MIN_DELTA` o reducir reflejos. Si una senal roja real no se detecta, bajar `RED_MIN_CHANNEL` en pasos pequenos o centrar mejor la senal dentro de la ROI.
