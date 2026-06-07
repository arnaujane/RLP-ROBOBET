# Hardware

## Alimentacion

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

No conectes `+9V_BAT` con `+5V`. Solo las masas van en comun.

## Pinout ESP32-WROOM

| Funcion | GPIO |
| --- | --- |
| QTR_S1 | IO32 |
| QTR_S2 | IO33 |
| QTR_S3 | IO25 |
| QTR_S4 | IO26 |
| QTR_S5 | IO27 |
| QTR_S6 | IO14 |
| QTR_S7 | IO12 |
| QTR_S8 | IO13 |
| MOTOR_IZQ_PWM | IO23 |
| MOTOR_IZQ_IN1 | IO22 |
| MOTOR_IZQ_IN2 | IO21 |
| MOTOR_DER_PWM | IO19 |
| MOTOR_DER_IN1 | IO18 |
| MOTOR_DER_IN2 | IO16 |
| TXD0 -> ESP32-CAM U0R | TXD0 |
| RXD0 <- ESP32-CAM U0T | RXD0 |

## TB6612FNG / HW-166

| TB6612FNG | Conexion |
| --- | --- |
| VM | +9V_BAT |
| VCC | +3V3 |
| GND | GND |
| STBY | +3V3 |
| PWMA | IO23 |
| AIN1 | IO22 |
| AIN2 | IO21 |
| A01/A02 | Motor izquierdo |
| PWMB | IO19 |
| BIN1 | IO18 |
| BIN2 | IO16 |
| B01/B02 | Motor derecho |

Si algun motor gira al reves, cambia los terminales del motor o modifica `INVERT_LEFT_MOTOR` / `INVERT_RIGHT_MOTOR` en `firmware/robot/src/main.cpp`.

## ESP32-CAM

- Alimentacion: `5V` y `GND`.
- Stream: `http://IP_CAM/stream`.
- Estado: `http://IP_CAM/status`.
- UART:
  - Envia `GREEN_SIGN`, `RED_SIGN` o `NO_SIGN` al ESP32-WROOM.

## Camara Cenital

La web acepta una URL configurable. Para un movil, usa una app de IP camera y pega la URL del stream en el panel "Camara cenital".

