#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "img_converters.h"

#ifndef WIFI_SSID
#define WIFI_SSID "ROBOBET_WIFI"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "ROBOBET_PASS"
#endif

// AI Thinker ESP32-CAM pinout.
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

static constexpr uint32_t SIGN_REPORT_INTERVAL_MS = 400;
static constexpr uint16_t MIN_SIGN_PIXELS = 75;

WebServer server(80);

String lastSign = "NO_SIGN";
String networkMode = "booting";
uint32_t lastSignReportMs = 0;
uint32_t frameCounter = 0;
uint32_t lastFpsWindowMs = 0;
float fps = 0.0f;
uint16_t lastRedPixels = 0;
uint16_t lastGreenPixels = 0;

struct ColorStats {
  uint16_t red = 0;
  uint16_t green = 0;
};

void reportSign(bool force = false) {
  if (!force && millis() - lastSignReportMs < SIGN_REPORT_INTERVAL_MS) {
    return;
  }
  lastSignReportMs = millis();
  Serial.println(lastSign);
}

ColorStats analyzeFrame(camera_fb_t *fb) {
  ColorStats stats;
  if (!fb || fb->format != PIXFORMAT_RGB565) {
    return stats;
  }

  const uint16_t *pixels = reinterpret_cast<const uint16_t *>(fb->buf);
  const size_t count = fb->len / 2;
  for (size_t i = 0; i < count; i += 12) {
    uint16_t p = pixels[i];
    uint8_t r = ((p >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((p >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (p & 0x1F) * 255 / 31;

    bool red = r > 115 && r > g + 35 && r > b + 35;
    bool green = g > 105 && g > r + 30 && g > b + 30;
    if (red) {
      stats.red++;
    }
    if (green) {
      stats.green++;
    }
  }
  return stats;
}

void updateSign(camera_fb_t *fb) {
  ColorStats stats = analyzeFrame(fb);
  lastRedPixels = stats.red;
  lastGreenPixels = stats.green;

  String detected = "NO_SIGN";
  if (stats.red >= MIN_SIGN_PIXELS && stats.red > stats.green) {
    detected = "RED_SIGN";
  } else if (stats.green >= MIN_SIGN_PIXELS) {
    detected = "GREEN_SIGN";
  }

  bool changed = detected != lastSign;
  lastSign = detected;
  reportSign(changed);
}

void updateFps() {
  frameCounter++;
  uint32_t now = millis();
  uint32_t elapsed = now - lastFpsWindowMs;
  if (elapsed >= 2000) {
    fps = frameCounter * 1000.0f / elapsed;
    frameCounter = 0;
    lastFpsWindowMs = now;
  }
}

bool setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 1);
    sensor->set_saturation(sensor, 1);
    sensor->set_whitebal(sensor, 1);
    sensor->set_awb_gain(sensor, 1);
    sensor->set_exposure_ctrl(sensor, 1);
  }
  return true;
}

void handleStatus() {
  JsonDocument doc;
  doc["sign"] = lastSign;
  doc["red_pixels"] = lastRedPixels;
  doc["green_pixels"] = lastGreenPixels;
  doc["fps"] = fps;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["ip"] = WiFi.localIP().toString();
  doc["mode"] = networkMode;
  doc["hostname"] = "esp32cam.local";
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void handleRoot() {
  server.send(
      200,
      "text/html",
      "<html><body><h1>RoboBet ESP32-CAM</h1><p><a href='/stream'>stream</a></p><p><a href='/status'>status</a></p></body></html>");
}

void handleStream() {
  WiFiClient client = server.client();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "multipart/x-mixed-replace; boundary=frame", "");

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      delay(20);
      continue;
    }

    updateSign(fb);
    updateFps();

    uint8_t *jpg = nullptr;
    size_t jpgLen = 0;
    bool ok = frame2jpg(fb, 78, &jpg, &jpgLen);
    esp_camera_fb_return(fb);

    if (!ok || jpg == nullptr) {
      if (jpg) {
        free(jpg);
      }
      delay(20);
      continue;
    }

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", static_cast<unsigned int>(jpgLen));
    client.write(jpg, jpgLen);
    client.print("\r\n");
    free(jpg);

    if (!client.connected()) {
      break;
    }
    delay(35);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print(".");
    delay(250);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    networkMode = "wifi";
    Serial.print("ESP32-CAM IP: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin("esp32cam")) {
      Serial.println("mDNS: http://esp32cam.local/");
    }
    return;
  }

  networkMode = "access_point";
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RoboBet-CAM", "robobetcam");
  Serial.println("WiFi connection failed. Started fallback AP.");
  Serial.println("SSID: RoboBet-CAM");
  Serial.println("Password: robobetcam");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
}

void setup() {
  // During standalone camera tests, Serial shows the IP. When wired to the robot,
  // ignore these boot messages and use only GREEN_SIGN/RED_SIGN/NO_SIGN after startup.
  Serial.begin(115200);
  connectWiFi();
  setupCamera();
  setupServer();
  lastFpsWindowMs = millis();
  reportSign(true);
}

void loop() {
  server.handleClient();
  reportSign(false);
  delay(5);
}
