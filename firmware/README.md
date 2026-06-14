# RoboBet Firmware Workspace

La documentacion principal del proyecto esta en [README.md](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/README.md).

Este directorio concentra la parte ejecutable del sistema:

- backend `FastAPI`
- frontend web
- firmware del `ESP32-WROOM`
- firmware del `ESP32-CAM`
- scripts de arranque, simulacion y Twitch
- documentacion tecnica

## Estructura local

```text
firmware/
├─ backend/
│  └─ app/
├─ docs/
├─ firmware/
│  ├─ robot/
│  └─ esp32_cam/
├─ frontend/
├─ scripts/
├─ requirements.txt
└─ README.md
```

## Arranque rapido local

Preparar entorno:

```powershell
.\scripts\setup_backend.ps1
```

Lanzar backend y frontend:

```powershell
.\scripts\run_backend.ps1
```

Abrir:

```text
http://127.0.0.1:8000
```

## Simulacion

Para probar la UI y el backend sin robot:

```powershell
.\scripts\simulate_robot.ps1
```

## Twitch bridge

Variables minimas:

```powershell
$env:ROBOBET_TWITCH_CLIENT_ID="TU_CLIENT_ID"
$env:ROBOBET_TWITCH_ACCESS_TOKEN="TU_ACCESS_TOKEN"
$env:ROBOBET_TWITCH_BROADCASTER_LOGIN="tu_canal"
```

Arranque:

```powershell
.\scripts\run_twitch_bridge.ps1
```

## Firmwares

Robot:

```powershell
py -3.14 -m platformio run -d .\firmware\robot
py -3.14 -m platformio run -d .\firmware\robot -t upload --upload-port COMX
```

Camara:

```powershell
py -3.14 -m platformio run -d .\firmware\esp32_cam
py -3.14 -m platformio run -d .\firmware\esp32_cam -t upload --upload-port COMX
```

## Referencias utiles

- [Arquitectura HW/SW](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/docs/arquitectura_hw_sw.md)
- [Guia de usuario](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/docs/guia_usuario.md)
- [Hardware](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/docs/hardware.md)
- [Calibracion](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/docs/calibration.md)
- [Camara ESP32-CAM](C:/Users/bsodlover/Documents/GitHub/RLP-ROBOBET/firmware/docs/camara_esp32cam.md)
