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

#ifndef CAMERA_DIAGNOSTIC_LOGS
#define CAMERA_DIAGNOSTIC_LOGS 0
#endif

#ifndef CAMERA_DIAGNOSTIC_INTERVAL_MS
#define CAMERA_DIAGNOSTIC_INTERVAL_MS 3000
#endif

#define CAMERA_PROFILE_FLUID 1
#define CAMERA_PROFILE_BALANCED 2
#define CAMERA_PROFILE_STABLE 3

#ifndef CAMERA_PROFILE
#define CAMERA_PROFILE CAMERA_PROFILE_FLUID
#endif

#ifndef CAMERA_STREAM_MAX_MS
#define CAMERA_STREAM_MAX_MS 12000
#endif

#ifndef CAMERA_STREAM_PORT
#define CAMERA_STREAM_PORT 81
#endif

#ifndef CAMERA_SENSOR_BRIGHTNESS
#define CAMERA_SENSOR_BRIGHTNESS 0
#endif

#ifndef CAMERA_SENSOR_CONTRAST
#define CAMERA_SENSOR_CONTRAST 1
#endif

#ifndef CAMERA_SENSOR_SATURATION
#define CAMERA_SENSOR_SATURATION 0
#endif

#ifndef CAMERA_SENSOR_SHARPNESS
#define CAMERA_SENSOR_SHARPNESS -1
#endif

#ifndef CAMERA_SENSOR_AE_LEVEL
#define CAMERA_SENSOR_AE_LEVEL 0
#endif

#ifndef CAMERA_SENSOR_GAIN_CEILING
#define CAMERA_SENSOR_GAIN_CEILING GAINCEILING_8X
#endif

#ifndef CAMERA_SENSOR_HMIRROR
#define CAMERA_SENSOR_HMIRROR 0
#endif

#ifndef CAMERA_SENSOR_VFLIP
#define CAMERA_SENSOR_VFLIP 0
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
static constexpr uint32_t BACKGROUND_DETECT_INTERVAL_MS = 1500;
static constexpr uint8_t MAX_CONSECUTIVE_CAPTURE_FAILURES = 6;
static constexpr uint8_t ROI_LEFT_PERCENT = 20;
static constexpr uint8_t ROI_RIGHT_PERCENT = 80;
static constexpr uint8_t ROI_TOP_PERCENT = 15;
static constexpr uint8_t ROI_BOTTOM_PERCENT = 85;
static constexpr uint8_t ROI_SAMPLE_STEP = 3;
static constexpr uint8_t MIN_RED_PERCENT = 1;
static constexpr uint8_t MIN_GREEN_PERCENT = 1;
static constexpr uint8_t MIN_BLACK_PERCENT = 10;
static constexpr uint8_t DOMINANCE_MARGIN_PERCENT = 30;
static constexpr uint8_t COLOR_MIN_BRIGHTNESS = 55;
static constexpr uint8_t RED_MIN_CHANNEL = 55;
static constexpr uint8_t GREEN_MIN_CHANNEL = 52;
static constexpr uint8_t RED_MIN_DELTA = 12;
static constexpr uint8_t GREEN_MIN_DELTA = 22;
static constexpr uint8_t BLACK_MAX_CHANNEL = 65;
static constexpr uint16_t BLACK_MAX_BRIGHTNESS = 145;

WebServer server(80);
WiFiServer streamServer(CAMERA_STREAM_PORT);

struct CameraProfileConfig {
  const char *name;
  framesize_t frameSize;
  uint8_t jpegQuality;
  uint32_t xclkHz;
  uint8_t fbCountWithPsram;
  uint8_t fbCountWithoutPsram;
  camera_grab_mode_t grabModeWithPsram;
  camera_grab_mode_t grabModeWithoutPsram;
  uint16_t streamDelayMs;
  size_t minJpegBytes;
};

struct FrameDimensions {
  uint16_t width;
  uint16_t height;
};

CameraProfileConfig selectedCameraProfile() {
#if CAMERA_PROFILE == CAMERA_PROFILE_BALANCED
  return {"EQUILIBRADO", FRAMESIZE_VGA, 12, 20000000, 2, 1, CAMERA_GRAB_LATEST, CAMERA_GRAB_WHEN_EMPTY, 35, 7000};
#elif CAMERA_PROFILE == CAMERA_PROFILE_STABLE
  return {"ESTABLE", FRAMESIZE_QVGA, 18, 10000000, 1, 1, CAMERA_GRAB_WHEN_EMPTY, CAMERA_GRAB_WHEN_EMPTY, 80, 2500};
#else
  return {"FLUIDO", FRAMESIZE_QVGA, 12, 20000000, 2, 1, CAMERA_GRAB_LATEST, CAMERA_GRAB_WHEN_EMPTY, 20, 2500};
#endif
}

FrameDimensions dimensionsForFrameSize(framesize_t frameSize) {
  switch (frameSize) {
    case FRAMESIZE_QQVGA: return {160, 120};
    case FRAMESIZE_QVGA: return {320, 240};
    case FRAMESIZE_VGA: return {640, 480};
    case FRAMESIZE_SVGA: return {800, 600};
    case FRAMESIZE_XGA: return {1024, 768};
    default: return {320, 240};
  }
}

const char *frameSizeName(framesize_t frameSize) {
  switch (frameSize) {
    case FRAMESIZE_QQVGA: return "QQVGA";
    case FRAMESIZE_QVGA: return "QVGA";
    case FRAMESIZE_VGA: return "VGA";
    case FRAMESIZE_SVGA: return "SVGA";
    case FRAMESIZE_XGA: return "XGA";
    default: return "CUSTOM";
  }
}

const char *grabModeName(camera_grab_mode_t grabMode) {
  return grabMode == CAMERA_GRAB_LATEST ? "latest" : "when_empty";
}

CameraProfileConfig cameraProfile = selectedCameraProfile();
bool psramAvailable = false;
uint8_t activeFbCount = 1;
camera_grab_mode_t activeGrabMode = CAMERA_GRAB_WHEN_EMPTY;

String lastSign = "NO_SIGN";
String networkMode = "booting";
String cameraHealth = "booting";
uint32_t lastSignReportMs = 0;
uint32_t lastBackgroundDetectMs = 0;
uint32_t lastCaptureOkMs = 0;
uint32_t lastRecoveryAttemptMs = 0;
uint32_t lastDiagnosticLogMs = 0;
uint32_t frameCounter = 0;
uint32_t lastFpsWindowMs = 0;
float fps = 0.0f;
bool cameraReady = false;
uint32_t cameraInitFailures = 0;
uint32_t captureFailures = 0;
uint32_t corruptFrames = 0;
uint32_t slowCaptureFrames = 0;
uint32_t streamClientsAccepted = 0;
uint32_t streamWriteFailures = 0;
uint32_t lastFrameBytes = 0;
uint32_t lastCaptureDurationMs = 0;
uint16_t lastFrameWidth = 0;
uint16_t lastFrameHeight = 0;
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
WiFiClient activeStreamClient;
bool hasActiveStreamClient = false;
uint32_t nextStreamFrameMs = 0;

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

void accumulateColorStats(ColorStats &stats, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t maxChannel = max(r, max(g, b));
  uint8_t minChannel = min(r, min(g, b));
  uint16_t brightness = static_cast<uint16_t>(r) + g + b;
  bool usableColor = brightness >= COLOR_MIN_BRIGHTNESS && maxChannel > minChannel + 12;

  bool warmRed = r >= 80 &&
                 r + 10 >= g &&
                 g > b + 16 &&
                 r > b + 22;
  bool red = usableColor &&
             r >= RED_MIN_CHANNEL &&
             r > b + RED_MIN_DELTA &&
             ((r > g + RED_MIN_DELTA && r * 100 > g * 108) || warmRed) &&
             r * 100 > b * 112;
  bool green = usableColor &&
               g >= GREEN_MIN_CHANNEL &&
               g > r + GREEN_MIN_DELTA &&
               g > b + GREEN_MIN_DELTA &&
               g * 100 > r * 125 &&
               g * 100 > b * 112;
  bool black = brightness < BLACK_MAX_BRIGHTNESS && maxChannel < BLACK_MAX_CHANNEL;
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

ColorStats analyzeRgbPixelStream(const uint8_t *pixels, uint16_t width, uint16_t height, uint8_t bytesPerPixel) {
  ColorStats stats;
  if (!pixels || width == 0 || height == 0 || bytesPerPixel == 0) {
    return stats;
  }

  const uint16_t xStart = (width * ROI_LEFT_PERCENT) / 100;
  const uint16_t xEnd = (width * ROI_RIGHT_PERCENT) / 100;
  const uint16_t yStart = (height * ROI_TOP_PERCENT) / 100;
  const uint16_t yEnd = (height * ROI_BOTTOM_PERCENT) / 100;

  for (uint16_t y = yStart; y < yEnd; y += ROI_SAMPLE_STEP) {
    for (uint16_t x = xStart; x < xEnd; x += ROI_SAMPLE_STEP) {
      const size_t i = (static_cast<size_t>(y) * width + x) * bytesPerPixel;
      uint8_t r = pixels[i];
      uint8_t g = pixels[i + 1];
      uint8_t b = pixels[i + 2];
      accumulateColorStats(stats, r, g, b);
    }
  }
  return stats;
}

ColorStats analyzeRgb565Frame(camera_fb_t *fb) {
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
      uint8_t rgb[3] = {
          static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31),
          static_cast<uint8_t>(((p >> 5) & 0x3F) * 255 / 63),
          static_cast<uint8_t>((p & 0x1F) * 255 / 31),
      };
      accumulateColorStats(stats, rgb[0], rgb[1], rgb[2]);
    }
  }
  return stats;
}

ColorStats analyzeJpegFrame(camera_fb_t *fb) {
  ColorStats stats;
  if (!fb || fb->format != PIXFORMAT_JPEG || fb->width == 0 || fb->height == 0) {
    return stats;
  }

  const size_t rgbLen = static_cast<size_t>(fb->width) * fb->height * 3;
  uint8_t *rgb = static_cast<uint8_t *>(psramAvailable ? ps_malloc(rgbLen) : malloc(rgbLen));
  if (!rgb) {
    lastReason = "rgb_alloc_failed";
    return stats;
  }

  bool decoded = fmt2rgb888(fb->buf, fb->len, fb->format, rgb);
  if (decoded) {
    stats = analyzeRgbPixelStream(rgb, fb->width, fb->height, 3);
  } else {
    lastReason = "jpeg_decode_failed";
  }
  free(rgb);
  return stats;
}

ColorStats analyzeFrame(camera_fb_t *fb) {
  if (!fb) {
    return {};
  }
  if (fb->format == PIXFORMAT_JPEG) {
    return analyzeJpegFrame(fb);
  }
  if (fb->format == PIXFORMAT_RGB565) {
    return analyzeRgb565Frame(fb);
  }
  return {};
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

  bool redStrong = redPercent >= MIN_RED_PERCENT && dominates(stats.red, stats.green);
  bool greenStrong = greenPercent >= MIN_GREEN_PERCENT && dominates(stats.green, stats.red);
  bool blackStrong = blackPercent >= MIN_BLACK_PERCENT && stats.black > (stats.red + stats.green) * 3;

  if (redStrong && !greenStrong) {
    result.sign = "RED_SIGN";
    result.confidence = confidenceFromPercent(redPercent, MIN_RED_PERCENT);
    result.reason = "red_roi_dominant";
    return result;
  }

  if (greenStrong && !redStrong) {
    result.sign = "GREEN_SIGN";
    result.confidence = confidenceFromPercent(greenPercent, MIN_GREEN_PERCENT);
    result.reason = "green_roi_dominant";
    return result;
  }

  if (redStrong && greenStrong) {
    result.sign = redPercent >= greenPercent ? "RED_SIGN" : "GREEN_SIGN";
    result.confidence = confidenceFromPercent(max(redPercent, greenPercent), MIN_GREEN_PERCENT) / 2;
    result.reason = "mixed_color_low_confidence";
    return result;
  }

  if (blackStrong) {
    result.sign = "BLACK_SIGN";
    result.confidence = confidenceFromPercent(blackPercent, MIN_BLACK_PERCENT);
    result.reason = "black_roi_dominant";
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

bool jpegHasStartMarker(const camera_fb_t *fb) {
  return fb && fb->len >= 2 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
}

bool jpegHasEndMarker(const camera_fb_t *fb) {
  if (!fb || fb->len < 2) {
    return false;
  }
  const size_t scanStart = fb->len > 32 ? fb->len - 32 : 0;
  for (size_t i = scanStart; i + 1 < fb->len; i++) {
    if (fb->buf[i] == 0xFF && fb->buf[i + 1] == 0xD9) {
      return true;
    }
  }
  return false;
}

bool frameLooksValid(const camera_fb_t *fb) {
  if (!fb || !fb->buf || fb->len == 0 || fb->width == 0 || fb->height == 0) {
    return false;
  }
  if (fb->format == PIXFORMAT_JPEG) {
    return fb->len >= cameraProfile.minJpegBytes && jpegHasStartMarker(fb) && jpegHasEndMarker(fb);
  }
  if (fb->format == PIXFORMAT_RGB565) {
    return fb->len >= expectedRgb565Length(fb);
  }
  return false;
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

camera_fb_t *captureValidFrame() {
  if (!cameraReady) {
    recordCaptureFailure();
    recoverCameraIfNeeded();
    return nullptr;
  }

  uint32_t captureStartMs = millis();
  camera_fb_t *fb = esp_camera_fb_get();
  lastCaptureDurationMs = millis() - captureStartMs;
  if (!fb) {
    recordCaptureFailure();
    recoverCameraIfNeeded();
    return nullptr;
  }

  if (!frameLooksValid(fb)) {
    esp_camera_fb_return(fb);
    recordCaptureFailure(true);
    recoverCameraIfNeeded();
    return nullptr;
  }

  lastFrameBytes = fb->len;
  lastFrameWidth = fb->width;
  lastFrameHeight = fb->height;
  if (lastCaptureDurationMs > 450) {
    slowCaptureFrames++;
  }
  recordCaptureOk();
  return fb;
}

bool captureAndUpdateSign(String *detected = nullptr) {
  camera_fb_t *fb = captureValidFrame();
  if (!fb) {
    return false;
  }

  String sign = updateSign(fb);
  updateFps();
  esp_camera_fb_return(fb);

  if (detected != nullptr) {
    *detected = sign;
  }
  return true;
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
  const uint8_t requiredVotes = max<uint8_t>(2, (DETECT_SAMPLE_COUNT / 2) + 1);
  if (blackVotes >= requiredVotes && blackVotes > redVotes && blackVotes > greenVotes && blackVotes > noSignVotes) {
    return "BLACK_SIGN";
  }
  if (redVotes >= requiredVotes && redVotes > greenVotes && redVotes > blackVotes && redVotes > noSignVotes) {
    return "RED_SIGN";
  }
  if (greenVotes >= requiredVotes && greenVotes > redVotes && greenVotes > blackVotes && greenVotes > noSignVotes) {
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

void configureCameraSensor(sensor_t *sensor) {
  if (!sensor) {
    return;
  }

  // OV2640 tuning for a moving robot: automatic exposure/white balance stay on,
  // while saturation and sharpness are restrained to reduce chroma noise.
  sensor->set_framesize(sensor, cameraProfile.frameSize);
  sensor->set_quality(sensor, cameraProfile.jpegQuality);
  sensor->set_brightness(sensor, CAMERA_SENSOR_BRIGHTNESS);
  sensor->set_contrast(sensor, CAMERA_SENSOR_CONTRAST);
  sensor->set_saturation(sensor, CAMERA_SENSOR_SATURATION);
  sensor->set_sharpness(sensor, CAMERA_SENSOR_SHARPNESS);
  sensor->set_denoise(sensor, 1);
  sensor->set_special_effect(sensor, 0);
  sensor->set_whitebal(sensor, 1);
  sensor->set_awb_gain(sensor, 1);
  sensor->set_wb_mode(sensor, 0);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_aec2(sensor, 1);
  sensor->set_ae_level(sensor, CAMERA_SENSOR_AE_LEVEL);
  sensor->set_gain_ctrl(sensor, 1);
  sensor->set_gainceiling(sensor, static_cast<gainceiling_t>(CAMERA_SENSOR_GAIN_CEILING));
  sensor->set_bpc(sensor, 1);
  sensor->set_wpc(sensor, 1);
  sensor->set_raw_gma(sensor, 1);
  sensor->set_lenc(sensor, 1);
  sensor->set_dcw(sensor, 1);
  sensor->set_hmirror(sensor, CAMERA_SENSOR_HMIRROR);
  sensor->set_vflip(sensor, CAMERA_SENSOR_VFLIP);
}

bool setupCamera() {
  cameraReady = false;
  cameraProfile = selectedCameraProfile();
  psramAvailable = psramFound();
  activeFbCount = psramAvailable ? cameraProfile.fbCountWithPsram : cameraProfile.fbCountWithoutPsram;
  activeGrabMode = psramAvailable ? cameraProfile.grabModeWithPsram : cameraProfile.grabModeWithoutPsram;

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
  config.xclk_freq_hz = cameraProfile.xclkHz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = cameraProfile.frameSize;
  config.jpeg_quality = cameraProfile.jpegQuality;
  config.fb_count = activeFbCount;
  config.fb_location = psramAvailable ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = activeGrabMode;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    cameraInitFailures++;
    cameraHealth = "init_failed";
    return false;
  }

  configureCameraSensor(esp_camera_sensor_get());
  cameraReady = true;
  cameraHealth = "ok";
  lastCaptureOkMs = millis();
  return true;
}

void handleStatus() {
  JsonDocument doc;
  FrameDimensions dims = dimensionsForFrameSize(cameraProfile.frameSize);
  addDetectionFields(doc);
  doc["camera_ready"] = cameraReady;
  doc["camera_health"] = cameraHealth;
  doc["profile"] = cameraProfile.name;
  doc["capture_failures"] = captureFailures;
  doc["consecutive_failures"] = consecutiveCaptureFailures;
  doc["corrupt_frames"] = corruptFrames;
  doc["slow_capture_frames"] = slowCaptureFrames;
  doc["camera_init_failures"] = cameraInitFailures;
  doc["stream_clients"] = streamClientsAccepted;
  doc["stream_write_failures"] = streamWriteFailures;
  doc["fps"] = fps;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["ip"] = WiFi.localIP().toString();
  doc["mode"] = networkMode;
  doc["hostname"] = "esp32cam.local";
  doc["stream_url"] = "http://" + WiFi.localIP().toString() + ":" + String(CAMERA_STREAM_PORT) + "/stream";
  doc["xclk_hz"] = cameraProfile.xclkHz;
  doc["jpeg_quality"] = cameraProfile.jpegQuality;
  doc["stream_delay_ms"] = cameraProfile.streamDelayMs;
  doc["frame_size"] = frameSizeName(cameraProfile.frameSize);
  doc["frame_width"] = lastFrameWidth ? lastFrameWidth : dims.width;
  doc["frame_height"] = lastFrameHeight ? lastFrameHeight : dims.height;
  doc["last_frame_bytes"] = lastFrameBytes;
  doc["last_capture_ms"] = lastCaptureDurationMs;
  doc["pixel_format"] = "JPEG";
  doc["psram_found"] = psramAvailable;
  doc["fb_count"] = activeFbCount;
  doc["grab_mode"] = grabModeName(activeGrabMode);
  doc["free_heap"] = ESP.getFreeHeap();
  doc["free_psram"] = psramAvailable ? ESP.getFreePsram() : 0;
  String body;
  serializeJson(doc, body);
  sendJsonResponse(body);
}

void handleRoot() {
  String streamUrl = "http://" + WiFi.localIP().toString() + ":" + String(CAMERA_STREAM_PORT) + "/stream";
  String html = "<html><body><h1>RoboBet ESP32-CAM</h1><p>MJPEG stream: <a href='";
  html += streamUrl;
  html += "'>";
  html += streamUrl;
  html += "</a></p><p>Control API stays on port 80 for /status, /snapshot and /detect.</p><p><a href='/snapshot'>snapshot</a></p><p><a href='/status'>status</a></p><p><a href='/detect'>detect</a></p></body></html>";
  server.send(200, "text/html", html);
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

bool writeJpegPayload(WiFiClient &client, camera_fb_t *fb) {
  if (!client.connected() || !fb || fb->format != PIXFORMAT_JPEG) {
    return false;
  }
  size_t written = client.write(fb->buf, fb->len);
  return written == fb->len;
}

bool writeMultipartFrame(WiFiClient &client) {
  camera_fb_t *fb = captureValidFrame();
  if (!fb) {
    return false;
  }

  client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", static_cast<unsigned int>(fb->len));
  bool ok = writeJpegPayload(client, fb);
  client.print("\r\n");
  esp_camera_fb_return(fb);

  if (ok) {
    updateFps();
  } else {
    streamWriteFailures++;
  }
  return ok;
}

void handleSnapshot() {
  WiFiClient client = server.client();
  camera_fb_t *fb = captureValidFrame();
  if (!fb) {
    server.send(503, "text/plain", cameraHealth);
    return;
  }

  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: image/jpeg\r\n");
  client.printf("Content-Length: %u\r\n", static_cast<unsigned int>(fb->len));
  client.print("Cache-Control: no-store\r\n");
  client.print("Access-Control-Allow-Origin: *\r\n");
  client.print("Connection: close\r\n\r\n");
  writeJpegPayload(client, fb);
  esp_camera_fb_return(fb);
  updateFps();
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
    if (!writeMultipartFrame(client)) {
      delay(10);
      continue;
    }

    if (!client.connected()) {
      break;
    }
    delay(cameraProfile.streamDelayMs);
  }
}

void stopDedicatedStreamClient() {
  if (hasActiveStreamClient) {
    activeStreamClient.stop();
    hasActiveStreamClient = false;
  }
}

void drainStreamHttpRequest(WiFiClient &client) {
  client.setTimeout(180);
  uint32_t deadline = millis() + 260;
  while (client.connected() && millis() < deadline) {
    if (!client.available()) {
      delay(1);
      continue;
    }
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      break;
    }
  }
}

void acceptDedicatedStreamClient() {
  WiFiClient candidate = streamServer.available();
  if (!candidate) {
    return;
  }

  if (hasActiveStreamClient && activeStreamClient.connected()) {
    candidate.print("HTTP/1.1 503 Busy\r\nConnection: close\r\n\r\n");
    candidate.stop();
    return;
  }

  activeStreamClient = candidate;
  activeStreamClient.setNoDelay(true);
  drainStreamHttpRequest(activeStreamClient);
  activeStreamClient.print("HTTP/1.1 200 OK\r\n");
  activeStreamClient.print("Content-Type: multipart/x-mixed-replace; boundary=frame\r\n");
  activeStreamClient.print("Cache-Control: no-cache, no-store, must-revalidate\r\n");
  activeStreamClient.print("Pragma: no-cache\r\n");
  activeStreamClient.print("Access-Control-Allow-Origin: *\r\n");
  activeStreamClient.print("Connection: close\r\n\r\n");
  hasActiveStreamClient = true;
  streamClientsAccepted++;
  nextStreamFrameMs = 0;
}

void handleDedicatedStreamServer() {
  if (hasActiveStreamClient && !activeStreamClient.connected()) {
    stopDedicatedStreamClient();
  }

  acceptDedicatedStreamClient();

  if (!hasActiveStreamClient || millis() < nextStreamFrameMs) {
    return;
  }

  bool ok = writeMultipartFrame(activeStreamClient);
  if (!activeStreamClient.connected()) {
    stopDedicatedStreamClient();
    return;
  }

  nextStreamFrameMs = millis() + (ok ? cameraProfile.streamDelayMs : 35);
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
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
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

void maintainWiFi() {
  static uint32_t lastReconnectAttemptMs = 0;
  if (networkMode != "wifi" || WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (millis() - lastReconnectAttemptMs < 5000) {
    return;
  }
  lastReconnectAttemptMs = millis();
  stopDedicatedStreamClient();
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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

void logCameraDiagnostics(bool force = false) {
#if CAMERA_DIAGNOSTIC_LOGS
  if (!force && millis() - lastDiagnosticLogMs < CAMERA_DIAGNOSTIC_INTERVAL_MS) {
    return;
  }
  lastDiagnosticLogMs = millis();
  Serial.printf(
      "[CAM] profile=%s pixel=JPEG fps=%.1f frame=%ux%u %.1fKB capture=%ums q=%u xclk=%u fb=%u grab=%s rssi=%d heap=%u psram=%u health=%s corrupt=%u fail=%u\n",
      cameraProfile.name,
      fps,
      lastFrameWidth,
      lastFrameHeight,
      lastFrameBytes / 1024.0f,
      lastCaptureDurationMs,
      cameraProfile.jpegQuality,
      static_cast<unsigned int>(cameraProfile.xclkHz),
      activeFbCount,
      grabModeName(activeGrabMode),
      WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
      ESP.getFreeHeap(),
      psramAvailable ? ESP.getFreePsram() : 0,
      cameraHealth.c_str(),
      static_cast<unsigned int>(corruptFrames),
      static_cast<unsigned int>(captureFailures));
#else
  (void)force;
#endif
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/detect", HTTP_GET, handleDetect);
  server.on("/snapshot", HTTP_GET, handleSnapshot);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
  streamServer.begin();
  streamServer.setNoDelay(true);
}

void setup() {
  // During standalone camera tests, Serial shows the IP. When wired to the robot,
  // ignore boot messages and use only DETECT responses after startup.
  Serial.begin(115200);
  connectWiFi();
  cameraReady = setupCamera();
  setupServer();
  lastFpsWindowMs = millis();
  logCameraDiagnostics(true);
  reportSign(true);
}

void loop() {
  server.handleClient();
  handleDedicatedStreamServer();
  handleSerialCommands();
  maintainWiFi();
  updateBackgroundDetection();
  logCameraDiagnostics(false);
  reportSign(false);
  delay(1);
}
