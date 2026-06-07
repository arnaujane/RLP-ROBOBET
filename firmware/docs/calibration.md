# Calibracion Y Pruebas

## QTR-8RC

1. Coloca el robot sobre la pista.
2. Pulsa `Calibrar QTR` desde la web.
3. Durante unos 5 segundos, mueve el robot para que todos los sensores vean blanco y negro.
4. Al terminar, la web recibira `CALIBRATION_DONE`.

Si la linea se detecta mal:

- Baja o sube `LINE_THRESHOLD` en `firmware/robot/src/main.cpp`.
- Revisa que `LEDON` este a `+3V3`.
- Revisa altura del QTR respecto al suelo.
- Evita luz directa fuerte sobre el sensor.

## Motores

Prueba primero con el robot levantado:

- `START_RUN` debe hacer avanzar ambos motores.
- Si un motor gira al reves, cambia `INVERT_LEFT_MOTOR` o `INVERT_RIGHT_MOTOR`.
- Si gira demasiado rapido, baja `BASE_PWM`.
- Si los giros no llegan a coger linea, ajusta `TURN_MIN_MS` y `TURN_TIMEOUT_MS`.

## Senales

La ESP32-CAM clasifica por color:

- Verde: `GREEN_SIGN`.
- Rojo: `RED_SIGN`.
- Nada claro: `NO_SIGN`.

Comprueba `http://IP_CAM/status` con la iluminacion real del aula. Si hay falsos positivos, ajusta `MIN_SIGN_PIXELS` y los umbrales RGB en `analyzeFrame`.

## Obstaculos

Para cinta blanca corta:

1. El QTR pierde la linea.
2. Si la camara ve verde, el robot avanza unos centimetros.
3. Si recupera linea, continua.
4. Si no recupera linea, marca obstaculo y hace backtracking.

Para final:

1. El QTR pierde la linea.
2. La camara ve rojo.
3. El robot envia `FINISH_DETECTED`.
4. El backend finaliza la carrera y liquida apuestas.

