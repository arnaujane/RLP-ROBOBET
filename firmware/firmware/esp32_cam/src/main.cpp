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

#ifndef CAMERA_USE_STATIC_IP
#define CAMERA_USE_STATIC_IP 1
#endif

#ifndef CAMERA_STATIC_IP
#define CAMERA_STATIC_IP "192.168.4.2"
#endif

#ifndef CAMERA_GATEWAY_IP
#define CAMERA_GATEWAY_IP "192.168.4.1"
#endif

#ifndef CAMERA_SUBNET_MASK
#define CAMERA_SUBNET_MASK "255.255.255.0"
#endif

#ifndef CAMERA_SERIAL_REPORTS
#define CAMERA_SERIAL_REPORTS 0
#endif

#ifndef CAMERA_XCLK_FREQ_HZ
#define CAMERA_XCLK_FREQ_HZ 10000000
#endif

#ifndef CAMERA_FRAME_SIZE
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#endif

#ifndef CAMERA_JPEG_QUALITY
#define CAMERA_JPEG_QUALITY 82
#endif

#ifndef CAMERA_STREAM_DELAY_MS
#define CAMERA_STREAM_DELAY_MS 90
#endif

#ifndef CAMERA_STREAM_MAX_MS
#define CAMERA_STREAM_MAX_MS 12000
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
static constexpr uint8_t DETECT_SAMPLE_COUNT = 3;
static constexpr uint32_t DETECT_SAMPLE_DELAY_MS = 80;
static constexpr uint32_t DETECT_FIRST_SAMPLE_DELAY_MS = 35;
static constexpr uint32_t BACKGROUND_DETECT_INTERVAL_MS = 900;
static constexpr uint8_t MAX_CONSECUTIVE_CAPTURE_FAILURES = 6;
static constexpr uint8_t ROI_LEFT_PERCENT = 20;
static constexpr uint8_t ROI_RIGHT_PERCENT = 80;
static constexpr uint8_t ROI_TOP_PERCENT = 15;
static constexpr uint8_t ROI_BOTTOM_PERCENT = 85;
static constexpr uint8_t ROI_SAMPLE_STEP = 3;
static constexpr uint8_t MIN_COLOR_PERCENT = 2;
static constexpr uint8_t MIN_BLACK_PERCENT = 10;
static constexpr uint8_t DOMINANCE_MARGIN_PERCENT = 30;

WebServer server(80);

String lastSign = "NO_SIGN";
String networkMode = "booting";
String cameraHealth = "booting";
uint32_t lastSignReportMs = 0;
uint32_t lastBackgroundDetectMs = 0;
uint32_t lastCaptureOkMs = 0;
uint32_t lastRecoveryAttemptMs = 0;
uint32_t frameCounter = 0;
uint32_t lastFpsWindowMs = 0;
float fps = 0.0f;
bool cameraReady = false;
uint32_t cameraInitFailures = 0;
uint32_t captureFailures = 0;
uint32_t corruptFrames = 0;
uint8_t consecutiveCaptureFailures = 0;
uint32_t lastRedPixels = 0;
uint32_t lastGreenPixels = 0;
uint32_t lastBlackPixels = 0;
uint32_t lastSampledPixels = 0;
uint8_t lastConfidence = 0;
uint8_t lastSampleCount = 1;
uint8_t lastRedVotes = 0;
uint8_t lastGreenVotes = 0;
uint8_t lastBlackVotes = 0;
uint8_t lastNoSignVotes = 0;
String lastReason = "booting";

struct ColorStats {
  uint32_t red = 0;
  uint32_t green = 0;
  uint32_t black = 0;
  uint32_t sampled = 0;
};

struct DetectionResult {
  String sign = "NO_SIGN";
  uint8_t confidence = 0;
  String reason = "no_result";
  ColorStats stats;
};

void reportSign(bool force = false) {
#if CAMERA_SERIAL_REPORTS
  if (!force && millis() - lastSignReportMs < SIGN_REPORT_INTERVAL_MS) {
    return;
  }
  lastSignReportMs = millis();
  Serial.println(lastSign);
#else
  (void)force;
#endif
}

ColorStats analyzeFrame(camera_fb_t *fb) {
  ColorStats stats;
  if (!fb || fb->format != PIXFORMAT_RGB565) {
    return stats;
  }

  const uint16_t *pixels = reinterpret_cast<const uint16_t *>(fb->buf);
  const uint16_t width = fb->width;
  const uint16_t height = fb->height;
  const uint16_t xStart = (width * ROI_LEFT_PERCENT) / 100;
  const uint16_t xEnd = (width * ROI_RIGHT_PERCENT) / 100;
  const uint16_t yStart = (height * ROI_TOP_PERCENT) / 100;
  const uint16_t yEnd = (height * ROI_BOTTOM_PERCENT) / 100;

  for (uint16_t y = yStart; y < yEnd; y += ROI_SAMPLE_STEP) {
    for (uint16_t x = xStart; x < xEnd; x += ROI_SAMPLE_STEP) {
      const size_t i = static_cast<size_t>(y) * width + x;
      uint16_t p = pixels[i];
      uint8_t r = ((p >> 11) & 0x1F) * 255 / 31;
      uint8_t g = ((p >> 5) & 0x3F) * 255 / 63;
      uint8_t b = (p & 0x1F) * 255 / 31;

      uint8_t maxChannel = max(r, max(g, b));
      uint8_t minChannel = min(r, min(g, b));
      uint16_t brightness = static_cast<uint16_t>(r) + g + b;
      bool saturatedEnough = maxChannel > minChannel + 28;

      bool red = r > 90 && saturatedEnough && r * 100 > g * 145 && r * 100 > b * 145;
      bool green = g > 85 && saturatedEnough && g * 100 > r * 140 && g * 100 > b * 135;
      bool black = brightness < 145 && maxChannel < 65;
      if (red) {
        stats.red++;
      }
      if (green) {
        stats.green++;
      }
      if (black) {
        stats.black++;
      }
      stats.sampled++;
    }
  }
  return stats;
}

uint8_t percentOf(uint32_t value, uint32_t total) {
  if (total == 0) {
    return 0;
  }
  return static_cast<uint8_t>(min<uint32_t>(100, (value * 100) / total));
}

uint8_t confidenceFromPercent(uint8_t percent, uint8_t minimumPercent) {
  if (percent <= minimumPercent) {
    return 0;
  }
  return static_cast<uint8_t>(constrain(map(percent, minimumPercent, 25, 45, 100), 0, 100));
}

bool dominates(uint32_t value, uint32_t other) {
  return value * 100 > other * (100 + DOMINANCE_MARGIN_PERCENT);
}

DetectionResult classifySign(const ColorStats &stats) {
  DetectionResult result;
  result.stats = stats;

  if (stats.sampled == 0) {
    result.reason = "no_pixels";
    return result;
  }

  uint8_t redPercent = percentOf(stats.red, stats.sampled);
  uint8_t greenPercent = percentOf(stats.green, stats.sampled);
  uint8_t blackPercent = percentOf(stats.black, stats.sampled);

  bool blackStrong = blackPercent >= MIN_BLACK_PERCENT && stats.black > (stats.red + stats.green) * 2;
  bool redStrong = redPercent >= MIN_COLOR_PERCENT && dominates(stats.red, stats.green) && stats.red > stats.black;
  bool greenStrong = greenPercent >= MIN_COLOR_PERCENT && dominates(stats.green, stats.red) && stats.green > stats.black;

  if (blackStrong) {
    result.sign = "BLACK_SIGN";
    result.confidence = confidenceFromPercent(blackPercent, MIN_BLACK_PERCENT);
    result.reason = "black_roi_dominant";
    return result;
  }

  if (redStrong && !greenStrong) {
    result.sign = "RED_SIGN";
    result.confidence = confidenceFromPercent(redPercent, MIN_COLOR_PERCENT);
    result.reason = "red_roi_dominant";
    return result;
  }

  if (greenStrong && !redStrong) {
    result.sign = "GREEN_SIGN";
    result.confidence = confidenceFromPercent(greenPercent, MIN_COLOR_PERCENT);
    result.reason = "green_roi_dominant";
    return result;
  }

  if (redStrong && greenStrong) {
    result.sign = redPercent >= greenPercent ? "RED_SIGN" : "GREEN_SIGN";
    result.confidence = confidenceFromPercent(max(redPercent, greenPercent), MIN_COLOR_PERCENT) / 2;
    result.reason = "mixed_color_low_confidence";
    return result;
  }

  result.reason = "below_threshold";
  return result;
}

String updateSign(camera_fb_t *fb) {
  ColorStats stats = analyzeFrame(fb);
  lastRedPixels = stats.red;
  lastGreenPixels = stats.green;
  lastBlackPixels = stats.black;
  lastSampledPixels = stats.sampled;

  DetectionResult result = classifySign(stats);
  String detected = result.sign;
  bool changed = detected != lastSign;
  lastSign = detected;
  lastConfidence = result.confidence;
  lastReason = result.reason;
  lastSampleCount = 1;
  lastRedVotes = detected == "RED_SIGN" ? 1 : 0;
  lastGreenVotes = detected == "GREEN_SIGN" ? 1 : 0;
  lastBlackVotes = detected == "BLACK_SIGN" ? 1 : 0;
  lastNoSignVotes = detected == "NO_SIGN" ? 1 : 0;
  reportSign(changed);
  return detected;
}

size_t expectedRgb565Length(const camera_fb_t *fb) {
  if (!fb) {
    return 0;
  }
  return static_cast<size_t>(fb->width) * static_cast<size_t>(fb->height) * 2;
}

bool frameLooksValid(const camera_fb_t *fb) {
  if (!fb || !fb->buf || fb->len == 0 || fb->width == 0 || fb->height == 0) {
    return false;
  }
  if (fb->format != PIXFORMAT_RGB565) {
    return false;
  }
  return fb->len >= expectedRgb565Length(fb);
}

void recordCaptureFailure(bool corrupt = false) {
  captureFailures++;
  consecutiveCaptureFailures++;
  cameraHealth = corrupt ? "corrupt_frame" : "capture_failed";
  if (corrupt) {
    corruptFrames++;
  }
}

void recordCaptureOk() {
  consecutiveCaptureFailures = 0;
  lastCaptureOkMs = millis();
  cameraHealth = "ok";
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

bool setupCamera();

void recoverCameraIfNeeded() {
  if (consecutiveCaptureFailures < MAX_CONSECUTIVE_CAPTURE_FAILURES) {
    return;
  }
  if (millis() - lastRecoveryAttemptMs < 3000) {
    return;
  }

  lastRecoveryAttemptMs = millis();
  cameraHealth = "recovering";
  cameraReady = false;
  esp_camera_deinit();
  delay(160);
  cameraReady = setupCamera();
}

bool captureAndUpdateSign(String *detected = nullptr) {
  if (!cameraReady) {
    recordCaptureFailure();
    recoverCameraIfNeeded();
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    recordCaptureFailure();
    recoverCameraIfNeeded();
    return false;
  }

  if (!frameLooksValid(fb)) {
    esp_camera_fb_return(fb);
    recordCaptureFailure(true);
    recoverCameraIfNeeded();
    return false;
  }

  String sign = updateSign(fb);
  updateFps();
  esp_camera_fb_return(fb);
  recordCaptureOk();

  if (detected != nullptr) {
    *detected = sign;
  }
  return true;
}

bool captureJpeg(uint8_t **jpg, size_t *jpgLen) {
  *jpg = nullptr;
  *jpgLen = 0;

  if (!cameraReady) {
    recordCaptureFailure();
    recoverCameraIfNeeded();
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    recordCaptureFailure();
    recoverCameraIfNeeded();
    return false;
  }

  if (!frameLooksValid(fb)) {
    esp_camera_fb_return(fb);
    recordCaptureFailure(true);
    recoverCameraIfNeeded();
    return false;
  }

  updateSign(fb);
  updateFps();

  bool ok = frame2jpg(fb, CAMERA_JPEG_QUALITY, jpg, jpgLen);
  esp_camera_fb_return(fb);
  if (ok && *jpg != nullptr && *jpgLen > 0) {
    recordCaptureOk();
    return true;
  }

  recordCaptureFailure();
  if (*jpg != nullptr) {
    free(*jpg);
    *jpg = nullptr;
  }
  *jpgLen = 0;
  recoverCameraIfNeeded();
  return false;
}

void discardStaleFrame() {
  if (!cameraReady) {
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }
}

String chooseSignFromVotes(uint8_t redVotes, uint8_t greenVotes, uint8_t blackVotes, uint8_t noSignVotes) {
  if (blackVotes >= 3 && blackVotes > redVotes && blackVotes > greenVotes && blackVotes > noSignVotes) {
    return "BLACK_SIGN";
  }
  if (redVotes >= 3 && redVotes > greenVotes && redVotes > blackVotes && redVotes > noSignVotes) {
    return "RED_SIGN";
  }
  if (greenVotes >= 3 && greenVotes > redVotes && greenVotes > blackVotes && greenVotes > noSignVotes) {
    return "GREEN_SIGN";
  }
  return "NO_SIGN";
}

String detectSignFromSamples() {
  uint8_t redVotes = 0;
  uint8_t greenVotes = 0;
  uint8_t blackVotes = 0;
  uint8_t noSignVotes = 0;
  uint16_t confidenceSum = 0;
  String reason = "votes";

  discardStaleFrame();
  delay(DETECT_FIRST_SAMPLE_DELAY_MS);

  for (uint8_t i = 0; i < DETECT_SAMPLE_COUNT; i++) {
    String sign = "NO_SIGN";
    uint8_t sampleConfidence = 0;
    if (!captureAndUpdateSign(&sign)) {
      noSignVotes++;
    } else if (sign == "RED_SIGN") {
      redVotes++;
      sampleConfidence = lastConfidence;
    } else if (sign == "GREEN_SIGN") {
      greenVotes++;
      sampleConfidence = lastConfidence;
    } else if (sign == "BLACK_SIGN") {
      blackVotes++;
      sampleConfidence = lastConfidence;
    } else {
      noSignVotes++;
    }

    confidenceSum += sampleConfidence;

    if (i + 1 < DETECT_SAMPLE_COUNT) {
      delay(DETECT_SAMPLE_DELAY_MS);
    }
  }

  lastSign = chooseSignFromVotes(redVotes, greenVotes, blackVotes, noSignVotes);
  lastSampleCount = DETECT_SAMPLE_COUNT;
  lastRedVotes = redVotes;
  lastGreenVotes = greenVotes;
  lastBlackVotes = blackVotes;
  lastNoSignVotes = noSignVotes;
  lastConfidence = DETECT_SAMPLE_COUNT == 0 ? 0 : confidenceSum / DETECT_SAMPLE_COUNT;
  if (lastSign == "NO_SIGN") {
    reason = "no_majority";
    lastConfidence = 0;
  }
  lastReason = reason;
  reportSign(true);
  return lastSign;
}

void addDetectionFields(JsonDocument &doc) {
  doc["sign"] = lastSign;
  doc["confidence"] = lastConfidence;
  doc["reason"] = lastReason;
  doc["red_pixels"] = lastRedPixels;
  doc["green_pixels"] = lastGreenPixels;
  doc["black_pixels"] = lastBlackPixels;
  doc["sampled_pixels"] = lastSampledPixels;
  doc["sample_count"] = lastSampleCount;

  JsonObject votes = doc["votes"].to<JsonObject>();
  votes["red"] = lastRedVotes;
  votes["green"] = lastGreenVotes;
  votes["black"] = lastBlackVotes;
  votes["none"] = lastNoSignVotes;

  JsonObject roi = doc["roi"].to<JsonObject>();
  roi["left_percent"] = ROI_LEFT_PERCENT;
  roi["right_percent"] = ROI_RIGHT_PERCENT;
  roi["top_percent"] = ROI_TOP_PERCENT;
  roi["bottom_percent"] = ROI_BOTTOM_PERCENT;
}

void sendJsonResponse(const String &body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

bool setupCamera() {
  cameraReady = false;

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
  config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = CAMERA_FRAME_SIZE;
  config.jpeg_quality = 14;
  config.fb_count = 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    cameraInitFailures++;
    cameraHealth = "init_failed";
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 1);
    sensor->set_saturation(sensor, -1);
    sensor->set_sharpness(sensor, -1);
    sensor->set_denoise(sensor, 1);
    sensor->set_special_effect(sensor, 0);
    sensor->set_whitebal(sensor, 1);
    sensor->set_awb_gain(sensor, 1);
    sensor->set_wb_mode(sensor, 0);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_aec2(sensor, 1);
    sensor->set_ae_level(sensor, 0);
    sensor->set_gain_ctrl(sensor, 1);
    sensor->set_gainceiling(sensor, GAINCEILING_8X);
    sensor->set_bpc(sensor, 1);
    sensor->set_wpc(sensor, 1);
    sensor->set_raw_gma(sensor, 1);
    sensor->set_lenc(sensor, 1);
    sensor->set_dcw(sensor, 1);
  }
  cameraReady = true;
  cameraHealth = "ok";
  lastCaptureOkMs = millis();
  return true;
}

void handleStatus() {
  JsonDocument doc;
  addDetectionFields(doc);
  doc["camera_ready"] = cameraReady;
  doc["camera_health"] = cameraHealth;
  doc["capture_failures"] = captureFailures;
  doc["consecutive_failures"] = consecutiveCaptureFailures;
  doc["corrupt_frames"] = corruptFrames;
  doc["camera_init_failures"] = cameraInitFailures;
  doc["fps"] = fps;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["ip"] = WiFi.localIP().toString();
  doc["mode"] = networkMode;
  doc["hostname"] = "esp32cam.local";
  doc["xclk_hz"] = CAMERA_XCLK_FREQ_HZ;
  doc["jpeg_quality"] = CAMERA_JPEG_QUALITY;
  doc["stream_delay_ms"] = CAMERA_STREAM_DELAY_MS;
  doc["frame_width"] = 320;
  doc["frame_height"] = 240;
  doc["pixel_format"] = "RGB565";
  String body;
  serializeJson(doc, body);
  sendJsonResponse(body);
}

void handleRoot() {
  server.send(
      200,
      "text/html",
      "<html><body><h1>RoboBet ESP32-CAM</h1><p>Use /snapshot for the dashboard and /detect for robot decisions. /stream is available for short diagnostics.</p><p><a href='/snapshot'>snapshot</a></p><p><a href='/stream'>stream</a></p><p><a href='/status'>status</a></p><p><a href='/detect'>detect</a></p></body></html>");
}

void handleDetect() {
  if (!cameraReady) {
    JsonDocument doc;
    addDetectionFields(doc);
    doc["sign"] = "NO_SIGN";
    doc["confidence"] = 0;
    doc["reason"] = cameraHealth;
    String body;
    serializeJson(doc, body);
    sendJsonResponse(body);
    recoverCameraIfNeeded();
    return;
  }

  String sign = detectSignFromSamples();
  (void)sign;

  JsonDocument doc;
  addDetectionFields(doc);
  String body;
  serializeJson(doc, body);
  sendJsonResponse(body);
}

void handleSnapshot() {
  WiFiClient client = server.client();
  uint8_t *jpg = nullptr;
  size_t jpgLen = 0;

  if (!captureJpeg(&jpg, &jpgLen)) {
    server.send(503, "text/plain", cameraHealth);
    return;
  }

  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: image/jpeg\r\n");
  client.printf("Content-Length: %u\r\n", static_cast<unsigned int>(jpgLen));
  client.print("Cache-Control: no-store\r\n");
  client.print("Access-Control-Allow-Origin: *\r\n");
  client.print("Connection: close\r\n\r\n");
  client.write(jpg, jpgLen);
  free(jpg);
}

void handleStream() {
  WiFiClient client = server.client();
  uint32_t streamStartMs = millis();
  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: multipart/x-mixed-replace; boundary=frame\r\n");
  client.print("Cache-Control: no-cache\r\n");
  client.print("Pragma: no-cache\r\n");
  client.print("Access-Control-Allow-Origin: *\r\n");
  client.print("Connection: close\r\n\r\n");

  while (client.connected() && millis() - streamStartMs < CAMERA_STREAM_MAX_MS) {
    uint8_t *jpg = nullptr;
    size_t jpgLen = 0;
    if (!captureJpeg(&jpg, &jpgLen)) {
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
    delay(CAMERA_STREAM_DELAY_MS);
  }
}

bool applyStaticIpConfig() {
#if CAMERA_USE_STATIC_IP
  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;
  if (!localIp.fromString(CAMERA_STATIC_IP) ||
      !gateway.fromString(CAMERA_GATEWAY_IP) ||
      !subnet.fromString(CAMERA_SUBNET_MASK)) {
    return false;
  }
  return WiFi.config(localIp, gateway, subnet);
#else
  return true;
#endif
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  if (!applyStaticIpConfig()) {
    Serial.println("Static IP configuration failed; using DHCP.");
  }
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

void handleSerialCommand(const String &rawCommand) {
  String command = rawCommand;
  command.trim();
  command.toUpperCase();

  if (command == "DETECT") {
    Serial.println(detectSignFromSamples());
  }
}

void handleSerialCommands() {
  static String command;

  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (command.length() > 0) {
        handleSerialCommand(command);
        command = "";
      }
      continue;
    }

    command += c;
    if (command.length() > 48) {
      command = "";
    }
  }
}

void updateBackgroundDetection() {
  if (!cameraReady) {
    recoverCameraIfNeeded();
    return;
  }
  if (millis() - lastBackgroundDetectMs < BACKGROUND_DETECT_INTERVAL_MS) {
    return;
  }

  lastBackgroundDetectMs = millis();
  captureAndUpdateSign();
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/detect", HTTP_GET, handleDetect);
  server.on("/snapshot", HTTP_GET, handleSnapshot);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
}

void setup() {
  // During standalone camera tests, Serial shows the IP. When wired to the robot,
  // ignore boot messages and use only DETECT responses after startup.
  Serial.begin(115200);
  connectWiFi();
  cameraReady = setupCamera();
  setupServer();
  lastFpsWindowMs = millis();
  reportSign(true);
}

void loop() {
  server.handleClient();
  handleSerialCommands();
  updateBackgroundDetection();
  reportSign(false);
  delay(5);
}
