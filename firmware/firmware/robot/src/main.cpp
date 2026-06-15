#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <WebServer.h>
#include <WiFi.h>

#ifndef WIFI_SSID
#define WIFI_SSID "ROBOBET_WIFI"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "ROBOBET_PASS"
#endif

#ifndef ROBOBET_SERVER_HOST
#define ROBOBET_SERVER_HOST "192.168.1.100"
#endif

#ifndef ROBOBET_SERVER_PORT
#define ROBOBET_SERVER_PORT 8000
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 12000
#endif

#ifndef CAMERA_TRANSPORT_HTTP
#define CAMERA_TRANSPORT_HTTP 1
#endif

#ifndef CAMERA_TRANSPORT_UART
#define CAMERA_TRANSPORT_UART 0
#endif

#ifndef CAMERA_HTTP_HOST
#define CAMERA_HTTP_HOST "192.168.4.2"
#endif

#ifndef CAMERA_HTTP_PORT
#define CAMERA_HTTP_PORT 80
#endif

#ifndef CAMERA_HTTP_TIMEOUT_MS
#define CAMERA_HTTP_TIMEOUT_MS 2800
#endif

#ifndef CAMERA_UART_BAUD
#define CAMERA_UART_BAUD 115200
#endif

#ifndef CAMERA_UART_RX
#define CAMERA_UART_RX 16
#endif

#ifndef CAMERA_UART_TX
#define CAMERA_UART_TX 4
#endif

#ifndef CAMERA_UART_TIMEOUT_MS
#define CAMERA_UART_TIMEOUT_MS 1800
#endif

static constexpr char AP_SSID[] = "RLP-ROBOBET";
static constexpr char AP_PASSWORD[] = "robotlinea";

static constexpr uint8_t QTR_COUNT = 8;
static constexpr uint8_t QTR_PINS[QTR_COUNT] = {13, 12, 14, 27, 26, 25, 33, 32};

static constexpr uint8_t MOTOR_LEFT_PWM = 23;
static constexpr uint8_t MOTOR_LEFT_IN1 = 22;
static constexpr uint8_t MOTOR_LEFT_IN2 = 21;
static constexpr uint8_t MOTOR_RIGHT_PWM = 19;
static constexpr uint8_t MOTOR_RIGHT_IN1 = 18;
static constexpr uint8_t MOTOR_RIGHT_IN2 = 17;

static constexpr uint8_t PWM_LEFT_CHANNEL = 0;
static constexpr uint8_t PWM_RIGHT_CHANNEL = 1;
static constexpr uint32_t PWM_FREQ = 5000;
static constexpr uint8_t PWM_BITS = 8;
static constexpr int PWM_MAX = 255;
static constexpr uint16_t QTR_TIMEOUT_US = 3000;
static constexpr uint16_t INTERSECTION_ACTIVE_COUNT = 5;
static constexpr uint8_t BLACK_PATCH_MIN_ACTIVE_SENSORS = 7;
static constexpr uint8_t SIDE_PATH_MIN_ACTIVE_SENSORS = 2;
static constexpr uint8_t STRAIGHT_PATH_MIN_ACTIVE_SENSORS = 1;
static constexpr uint8_t EVENT_TRACKING_MIN_SENSOR = 2;
static constexpr uint8_t EVENT_TRACKING_MAX_SENSOR = 5;
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 500;
static constexpr uint32_t NODE_DEBOUNCE_MS = 700;
static constexpr uint32_t INTERSECTION_CONFIRM_MS = 80;
static constexpr uint8_t BACKTRACK_SIDE_EVENT_MIN_ACTIVE_SENSORS = 2;
static constexpr uint32_t BLACK_PATCH_CONFIRM_MS = 120;
static constexpr uint32_t BLACK_PATCH_CLEAR_TIMEOUT_MS = 900;
static constexpr uint32_t GAP_BRIDGE_MS = 420;
static constexpr uint32_t TURN_MIN_MS = 240;
static constexpr uint32_t TURN_TIMEOUT_MS = 1500;
static constexpr uint32_t TURN_CENTER_CONFIRM_MS = 45;
static constexpr uint8_t TURN_OUTER_SENSOR_COUNT = 2;
static constexpr float SENSOR_TO_WHEEL_AXIS_CM = 12.5f;
static constexpr float WHEEL_DIAMETER_CM = 3.0f;
static constexpr float WHEEL_BASE_CM = 12.3f;
static constexpr float WHEEL_CIRCUMFERENCE_CM = PI * WHEEL_DIAMETER_CM;
static constexpr float CENTER_ON_NODE_DISTANCE_CM = SENSOR_TO_WHEEL_AXIS_CM;
static constexpr float NODE_EXIT_ADVANCE_CM = 3.0f;
static constexpr uint8_t STRAIGHT_CONFIRM_MIN_CENTER_SENSORS = 1;
static constexpr float TURN_90_WHEEL_DISTANCE_CM = PI * WHEEL_BASE_CM / 4.0f;
static constexpr float TURN_180_WHEEL_DISTANCE_CM = PI * WHEEL_BASE_CM / 2.0f;
static constexpr float CENTER_ON_NODE_WHEEL_TURNS = CENTER_ON_NODE_DISTANCE_CM / WHEEL_CIRCUMFERENCE_CM;
static constexpr float TURN_90_WHEEL_TURNS = TURN_90_WHEEL_DISTANCE_CM / WHEEL_CIRCUMFERENCE_CM;
static constexpr float TURN_180_WHEEL_TURNS = TURN_180_WHEEL_DISTANCE_CM / WHEEL_CIRCUMFERENCE_CM;
static constexpr bool INVERT_LEFT_MOTOR = true;
static constexpr bool INVERT_RIGHT_MOTOR = true;

static constexpr int DEFAULT_BASE_PWM = 28;
static constexpr int DEFAULT_TURN_PWM = 35;
static constexpr int DEFAULT_SEARCH_TURN_PWM = 35;
static constexpr int DEFAULT_SEARCH_FORWARD_PWM = 0;
static constexpr int DEFAULT_SEARCH_TIMEOUT_MS = 800;
static constexpr int DEFAULT_LOOP_DELAY_MS = 80;
static constexpr int DEFAULT_MS_PER_CM = 70;
static constexpr int DEFAULT_MS_TURN_90 = 560;
static constexpr int DEFAULT_MS_TURN_180 = 1120;
static constexpr int DEFAULT_LINE_REACQUIRE_MS = 700;
static constexpr int DEFAULT_BACK_TURN_EXTRA_MS = 900;
static constexpr int DEFAULT_SPEED_LIMIT_PERCENT = 100;
static constexpr uint16_t DEFAULT_TARGET_LINE_THRESHOLD_RAW = 2500;
static constexpr uint16_t DEFAULT_MIN_ADAPTIVE_THRESHOLD_RAW = 1700;
static constexpr uint16_t DEFAULT_THRESHOLD_STEP_RAW = 25;
static constexpr int DEFAULT_POSITION_WINDOW = 2;
static constexpr int DEFAULT_POSITION_GAIN = 10;
static constexpr bool DEFAULT_LOCAL_MOTORS_ENABLED = false;
static constexpr bool DEFAULT_OBSTACLE_HANDLING_ENABLED = false;

// Estados principales del robot. Ayudan a que la web muestre en que parte
// del recorrido esta: seguimiento, cruce, giro, obstaculo o vuelta atras.
enum class RobotState {
  IDLE,
  CALIBRATING,
  WAITING_START,
  FOLLOWING_LINE,
  INTERSECTION_DETECTED,
  CENTER_ON_NODE,
  CLASSIFY_NODE,
  CHOOSE_DIRECTION,
  GO_STRAIGHT,
  TURN_LEFT,
  TURN_RIGHT,
  TURN_BACK,
  WAITING_TURN_CONFIRMATION,
  SEARCH_LINE,
  NODE_DETECTED,
  SELECTING_EDGE,
  OBSTACLE_CHECK,
  WAITING_OBSTACLE_DECISION,
  BACKTRACKING,
  RETURNING_FROM_BLOCKED_OBSTACLE,
  RETURNING_TO_UNFINISHED_NODE,
  RETURN_TO_LAST_NODE,
  FINISH_CHECK,
  FINISH_DETECTED,
  FINISHED,
  ERROR_STATE
};

enum class Algorithm {
  DFS,
  BFS
};

enum class Turn : int8_t {
  LEFT = -1,
  STRAIGHT = 0,
  RIGHT = 1,
  BACK = 2
};

enum class IntersectionType : uint8_t {
  NONE,
  LINE,
  LEFT_BRANCH,
  RIGHT_BRANCH,
  T_OR_CROSS,
  DEAD_END,
  BLACK_PATCH,
  LINE_LOST
};

enum class RobotOrientation : uint8_t {
  NORTH,
  EAST,
  SOUTH,
  WEST
};

enum class CameraSign {
  NO_SIGN,
  GREEN_SIGN,
  RED_SIGN,
  BLACK_SIGN,
  TIMEOUT
};

enum DirectionBit : uint8_t {
  DIR_LEFT = 0b001,
  DIR_STRAIGHT = 0b010,
  DIR_RIGHT = 0b100,
  DIR_BACK = 0b1000
};

// Foto completa de los QTR en un instante. Se guarda tanto el valor crudo
// como la lectura ya clasificada en izquierda, centro, derecha u obstaculo.
struct SensorFrame {
  uint16_t raw[QTR_COUNT]{};
  uint16_t normalized[QTR_COUNT]{};
  uint8_t activeCount = 0;
  uint8_t leftActiveCount = 0;
  uint8_t centerActiveCount = 0;
  uint8_t rightActiveCount = 0;
  bool left = false;
  bool center = false;
  bool right = false;
  bool lineSeen = false;
  bool intersection = false;
  bool blackPatch = false;
  bool lineTrackingProtected = false;
  IntersectionType intersectionType = IntersectionType::NONE;
  int position = 3500;
};

// Cada nodo guarda salidas absolutas. Asi el backtracking no depende de como
// este orientado el robot al volver a pasar por el mismo cruce.
struct NodeRecord {
  int8_t id = -1;
  int16_t x = 0;
  int16_t y = 0;
  RobotOrientation createdFacing = RobotOrientation::NORTH;
  uint8_t options = 0;
  uint8_t tried = 0;
  uint8_t blocked = 0;
  uint8_t passableGreen = 0;
  int8_t parent = -1;
  RobotOrientation parentOrientation = RobotOrientation::NORTH;
  int8_t neighbors[4] = {-1, -1, -1, -1};
  bool explored = false;
  bool finalNode = false;
};

// Las aristas son las conexiones reales del laberinto: normales, verdes o
// bloqueadas por una senal roja.
struct EdgeRecord {
  int8_t from = -1;
  int8_t to = -1;
  RobotOrientation fromOrientation = RobotOrientation::NORTH;
  Turn fromTurn = Turn::STRAIGHT;
  Turn toTurn = Turn::BACK;
  bool visited = false;
  bool blocked = false;
  bool passableGreen = false;
};

static constexpr uint8_t MAX_NODES = 64;
static constexpr uint8_t MAX_EDGES = 96;
NodeRecord nodes[MAX_NODES];
EdgeRecord edges[MAX_EDGES];
uint8_t nodeCount = 0;
uint8_t edgeCount = 0;
int currentNode = -1;
int finalNode = -1;
int previousNodeForEdge = -1;
Turn lastTurn = Turn::STRAIGHT;
Turn lastSelectedTurn = Turn::STRAIGHT;
Turn pendingTurn = Turn::STRAIGHT;
bool edgeInProgress = false;
RobotOrientation robotOrientation = RobotOrientation::NORTH;
RobotOrientation lastTraversalOrientation = RobotOrientation::NORTH;
RobotOrientation pendingExitOrientation = RobotOrientation::NORTH;
RobotOrientation blockedObstacleOrientation = RobotOrientation::NORTH;
IntersectionType lastIntersectionType = IntersectionType::NONE;
uint8_t lastAvailableExits = 0;
uint8_t shortestPathLength = 0;
int8_t shortestPathNodes[MAX_NODES] = {};

RobotState robotState = RobotState::WAITING_START;
Algorithm algorithm = Algorithm::DFS;
WebSocketsClient webSocket;
WebServer localServer(80);
HardwareSerial CameraSerial(2);

uint16_t qtrMin[QTR_COUNT];
uint16_t qtrMax[QTR_COUNT];
bool qtrCalibrated = false;
bool runActive = false;
bool wsConnected = false;
bool localMotorsEnabled = DEFAULT_LOCAL_MOTORS_ENABLED;
bool obstacleHandlingEnabled = DEFAULT_OBSTACLE_HANDLING_ENABLED;
bool pauseBeforeTurnEnabled = false;
bool waitingForTurnConfirmation = false;
bool waitForObstacleDecisionEnabled = true;
bool waitingForObstacleDecision = false;
bool returningFromBlockedObstacle = false;
bool navigatingToFrontier = false;
bool manualObstacleSignEnabled = false;
bool isSearching = false;
bool lastNodeQtrLearningEnabled = true;
int speedLimitPercent = DEFAULT_SPEED_LIMIT_PERCENT;
int runId = 0;
uint32_t runStartMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastNodeMs = 0;
uint32_t lastLoopMs = 0;
uint32_t searchStartMs = 0;
uint32_t blackPatchFirstSeenMs = 0;
uint32_t intersectionFirstSeenMs = 0;
int obstacleCount = 0;
int returnNodeAfterBlockedObstacle = -1;
int navigationTargetNode = -1;
int navigationNextNode = -1;
uint16_t crossingCount = 0;
int lastLinePosition = 3500;
int lastKnownDirection = 0;
float lastKnownPosition = 3.5f;
uint16_t lastSensorValues[QTR_COUNT] = {0};
bool lastLineDetected[QTR_COUNT] = {false};
bool lastLineTrackingProtected = false;
int lastLeftMotorPwm = 0;
int lastRightMotorPwm = 0;
CameraSign lastCameraSign = CameraSign::NO_SIGN;
CameraSign manualObstacleSign = CameraSign::NO_SIGN;
uint8_t lastCameraConfidence = 0;
String lastCameraReason = "not_checked";

int basePwm = DEFAULT_BASE_PWM;
int turnPwm = DEFAULT_TURN_PWM;
int searchTurnPwm = DEFAULT_SEARCH_TURN_PWM;
int searchForwardPwm = DEFAULT_SEARCH_FORWARD_PWM;
int searchTimeoutMs = DEFAULT_SEARCH_TIMEOUT_MS;
int loopDelayMs = DEFAULT_LOOP_DELAY_MS;
int msPerCm = DEFAULT_MS_PER_CM;
int msTurn90 = DEFAULT_MS_TURN_90;
int msTurn180 = DEFAULT_MS_TURN_180;
int lineReacquireMs = DEFAULT_LINE_REACQUIRE_MS;
int backTurnExtraMs = DEFAULT_BACK_TURN_EXTRA_MS;
int positionWindow = DEFAULT_POSITION_WINDOW;
int positionGain = DEFAULT_POSITION_GAIN;
uint16_t targetLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
uint16_t currentLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
uint16_t minAdaptiveThresholdRaw = DEFAULT_MIN_ADAPTIVE_THRESHOLD_RAW;
uint16_t thresholdStepRaw = DEFAULT_THRESHOLD_STEP_RAW;
String lastDebugMessage;

void sendTelemetry();
void followLine(const SensorFrame &frame);
bool tryBridgeWhiteGap();
bool searchForLineRecovery();
void handleNode(const SensorFrame &frame);

void debugLog(const String &message) {
  if (message == lastDebugMessage) {
    return;
  }
  lastDebugMessage = message;
  Serial.println(message);
}

const char *stateName(RobotState state) {
  switch (state) {
    case RobotState::IDLE: return "IDLE";
    case RobotState::CALIBRATING: return "CALIBRATING";
    case RobotState::WAITING_START: return "WAITING_START";
    case RobotState::FOLLOWING_LINE: return "FOLLOWING_LINE";
    case RobotState::INTERSECTION_DETECTED: return "INTERSECTION_DETECTED";
    case RobotState::CENTER_ON_NODE: return "CENTER_ON_NODE";
    case RobotState::CLASSIFY_NODE: return "CLASSIFY_NODE";
    case RobotState::CHOOSE_DIRECTION: return "CHOOSE_DIRECTION";
    case RobotState::GO_STRAIGHT: return "GO_STRAIGHT";
    case RobotState::TURN_LEFT: return "TURN_LEFT";
    case RobotState::TURN_RIGHT: return "TURN_RIGHT";
    case RobotState::TURN_BACK: return "TURN_BACK";
    case RobotState::WAITING_TURN_CONFIRMATION: return "WAITING_TURN_CONFIRMATION";
    case RobotState::SEARCH_LINE: return "SEARCH_LINE";
    case RobotState::NODE_DETECTED: return "NODE_DETECTED";
    case RobotState::SELECTING_EDGE: return "SELECTING_EDGE";
    case RobotState::OBSTACLE_CHECK: return "OBSTACLE_CHECK";
    case RobotState::WAITING_OBSTACLE_DECISION: return "WAITING_OBSTACLE_DECISION";
    case RobotState::BACKTRACKING: return "BACKTRACKING";
    case RobotState::RETURNING_FROM_BLOCKED_OBSTACLE: return "RETURNING_FROM_BLOCKED_OBSTACLE";
    case RobotState::RETURNING_TO_UNFINISHED_NODE: return "RETURNING_TO_UNFINISHED_NODE";
    case RobotState::RETURN_TO_LAST_NODE: return "RETURN_TO_LAST_NODE";
    case RobotState::FINISH_CHECK: return "FINISH_CHECK";
    case RobotState::FINISH_DETECTED: return "FINISH_DETECTED";
    case RobotState::FINISHED: return "FINISHED";
    case RobotState::ERROR_STATE: return "ERROR";
  }
  return "UNKNOWN";
}

const char *algorithmName() {
  return algorithm == Algorithm::DFS ? "DFS" : "BFS";
}

const char *turnName(Turn turn) {
  switch (turn) {
    case Turn::LEFT: return "LEFT";
    case Turn::STRAIGHT: return "STRAIGHT";
    case Turn::RIGHT: return "RIGHT";
    case Turn::BACK: return "BACK";
  }
  return "UNKNOWN";
}

const char *intersectionTypeName(IntersectionType type) {
  switch (type) {
    case IntersectionType::NONE: return "NONE";
    case IntersectionType::LINE: return "LINE";
    case IntersectionType::LEFT_BRANCH: return "LEFT_BRANCH";
    case IntersectionType::RIGHT_BRANCH: return "RIGHT_BRANCH";
    case IntersectionType::T_OR_CROSS: return "T_OR_CROSS";
    case IntersectionType::DEAD_END: return "DEAD_END";
    case IntersectionType::BLACK_PATCH: return "BLACK_PATCH";
    case IntersectionType::LINE_LOST: return "LINE_LOST";
  }
  return "UNKNOWN";
}

const char *orientationName(RobotOrientation orientation) {
  switch (orientation) {
    case RobotOrientation::NORTH: return "NORTH";
    case RobotOrientation::EAST: return "EAST";
    case RobotOrientation::SOUTH: return "SOUTH";
    case RobotOrientation::WEST: return "WEST";
  }
  return "UNKNOWN";
}

const char *cameraSignName(CameraSign sign) {
  switch (sign) {
    case CameraSign::NO_SIGN: return "NO_SIGN";
    case CameraSign::GREEN_SIGN: return "GREEN_SIGN";
    case CameraSign::RED_SIGN: return "RED_SIGN";
    case CameraSign::BLACK_SIGN: return "BLACK_SIGN";
    case CameraSign::TIMEOUT: return "TIMEOUT";
  }
  return "UNKNOWN";
}

const char *cameraTransportName() {
#if CAMERA_TRANSPORT_UART && CAMERA_TRANSPORT_HTTP
  return "UART_HTTP";
#elif CAMERA_TRANSPORT_UART
  return "UART";
#elif CAMERA_TRANSPORT_HTTP
  return "HTTP";
#else
  return "NONE";
#endif
}

// Conversiones entre giros relativos y orientacion absoluta del grafo.
uint8_t turnToBit(Turn turn) {
  switch (turn) {
    case Turn::LEFT: return DIR_LEFT;
    case Turn::STRAIGHT: return DIR_STRAIGHT;
    case Turn::RIGHT: return DIR_RIGHT;
    case Turn::BACK: return DIR_BACK;
  }
  return 0;
}

uint8_t turnIndex(Turn turn) {
  switch (turn) {
    case Turn::LEFT: return 0;
    case Turn::STRAIGHT: return 1;
    case Turn::RIGHT: return 2;
    case Turn::BACK: return 3;
  }
  return 1;
}

uint8_t orientationIndex(RobotOrientation orientation) {
  return static_cast<uint8_t>(orientation);
}

uint8_t orientationBit(RobotOrientation orientation) {
  return static_cast<uint8_t>(1 << orientationIndex(orientation));
}

RobotOrientation orientationFromIndex(uint8_t index) {
  return static_cast<RobotOrientation>(index % 4);
}

// Gira la orientacion actual sin mover todavia el robot fisico.
RobotOrientation rotateOrientation(RobotOrientation orientation, Turn turn) {
  int index = static_cast<int>(orientationIndex(orientation));
  switch (turn) {
    case Turn::LEFT: index -= 1; break;
    case Turn::RIGHT: index += 1; break;
    case Turn::BACK: index += 2; break;
    case Turn::STRAIGHT: break;
  }
  index = (index + 4) % 4;
  return orientationFromIndex(static_cast<uint8_t>(index));
}

RobotOrientation oppositeOrientation(RobotOrientation orientation) {
  return rotateOrientation(orientation, Turn::BACK);
}

Turn oppositeTurn(Turn turn) {
  switch (turn) {
    case Turn::LEFT: return Turn::RIGHT;
    case Turn::RIGHT: return Turn::LEFT;
    case Turn::STRAIGHT: return Turn::BACK;
    case Turn::BACK: return Turn::STRAIGHT;
  }
  return Turn::BACK;
}

Turn turnTowardOrientation(RobotOrientation facing, RobotOrientation target) {
  int diff = static_cast<int>(orientationIndex(target)) - static_cast<int>(orientationIndex(facing));
  diff = (diff + 4) % 4;

  if (diff == 0) {
    return Turn::STRAIGHT;
  }
  if (diff == 1) {
    return Turn::RIGHT;
  }
  if (diff == 3) {
    return Turn::LEFT;
  }
  return Turn::BACK;
}

// Pasa salidas detectadas por el QTR a coordenadas del mapa.
uint8_t relativeOptionsToAbsolute(uint8_t relativeOptions, RobotOrientation facing) {
  uint8_t absoluteOptions = 0;
  const Turn turns[] = {Turn::LEFT, Turn::STRAIGHT, Turn::RIGHT, Turn::BACK};

  for (Turn turn : turns) {
    uint8_t relativeBit = turnToBit(turn);
    if (relativeOptions & relativeBit) {
      absoluteOptions |= orientationBit(rotateOrientation(facing, turn));
    }
  }

  return absoluteOptions;
}

// Vuelve a expresar salidas del grafo como izquierda/recto/derecha.
uint8_t absoluteOptionsToRelative(uint8_t absoluteOptions, RobotOrientation facing) {
  uint8_t relativeOptions = 0;
  const Turn turns[] = {Turn::LEFT, Turn::STRAIGHT, Turn::RIGHT, Turn::BACK};

  for (Turn turn : turns) {
    RobotOrientation exitOrientation = rotateOrientation(facing, turn);
    if (absoluteOptions & orientationBit(exitOrientation)) {
      relativeOptions |= turnToBit(turn);
    }
  }

  return relativeOptions;
}

String relativeExitMaskText(uint8_t mask) {
  String text;
  if (mask & DIR_LEFT) text += "L";
  if (mask & DIR_STRAIGHT) text += text.length() ? ",S" : "S";
  if (mask & DIR_RIGHT) text += text.length() ? ",R" : "R";
  if (mask & DIR_BACK) text += text.length() ? ",B" : "B";
  return text.length() ? text : "-";
}

String absoluteExitMaskText(uint8_t mask) {
  String text;
  const RobotOrientation orientations[] = {
    RobotOrientation::NORTH,
    RobotOrientation::EAST,
    RobotOrientation::SOUTH,
    RobotOrientation::WEST
  };

  for (RobotOrientation orientation : orientations) {
    if (!(mask & orientationBit(orientation))) {
      continue;
    }
    if (text.length()) {
      text += ",";
    }
    text += orientationName(orientation);
  }

  return text.length() ? text : "-";
}

// El robot solo mueve motores si hay una carrera activa o modo local.
bool motorsAllowed() {
  return runActive || localMotorsEnabled;
}

// Mantiene vivos el servidor local y el websocket durante esperas largas.
void serviceNetwork() {
  localServer.handleClient();
  webSocket.loop();
}

// Normaliza respuestas de camara o botones manuales a un enum interno.
bool parseCameraSign(const String &text, CameraSign &sign) {
  String value = text;
  value.trim();
  value.toUpperCase();

  if (value == "NO_SIGN") {
    sign = CameraSign::NO_SIGN;
    return true;
  }
  if (value == "GREEN" || value == "GREEN_SIGN") {
    sign = CameraSign::GREEN_SIGN;
    return true;
  }
  if (value == "RED" || value == "RED_SIGN") {
    sign = CameraSign::RED_SIGN;
    return true;
  }
  if (value == "BLACK" || value == "BLACK_SIGN") {
    sign = CameraSign::BLACK_SIGN;
    return true;
  }
  if (value == "TIMEOUT") {
    sign = CameraSign::TIMEOUT;
    return true;
  }

  return false;
}

void setCameraDiagnostics(uint8_t confidence, const String &reason) {
  lastCameraConfidence = constrain(confidence, 0, 100);
  lastCameraReason = reason;
}

// Pide una lectura puntual al endpoint /detect de la ESP32-CAM.
CameraSign requestCameraSignHttp() {
#if CAMERA_TRANSPORT_HTTP
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + String(CAMERA_HTTP_HOST) + ":" + String(CAMERA_HTTP_PORT) + "/detect";

  http.setTimeout(CAMERA_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    setCameraDiagnostics(0, "http_begin_failed");
    return CameraSign::TIMEOUT;
  }

  int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    http.end();
    setCameraDiagnostics(0, String("http_status_") + String(statusCode));
    return CameraSign::TIMEOUT;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    setCameraDiagnostics(0, "json_error");
    return CameraSign::TIMEOUT;
  }

  CameraSign sign = CameraSign::TIMEOUT;
  const char *rawSign = doc["sign"] | "";
  if (!parseCameraSign(rawSign, sign)) {
    setCameraDiagnostics(0, "unknown_sign");
    return CameraSign::TIMEOUT;
  }
  const char *reason = doc["reason"] | "http_detect";
  uint8_t fallbackConfidence = sign == CameraSign::NO_SIGN ? 0 : 100;
  setCameraDiagnostics(doc["confidence"] | fallbackConfidence, reason);
  return sign;
#else
  setCameraDiagnostics(0, "http_disabled");
  return CameraSign::TIMEOUT;
#endif
}

// Variante por UART para cuando la camara quede cableada al robot.
CameraSign requestCameraSignUart() {
#if CAMERA_TRANSPORT_UART
  while (CameraSerial.available()) {
    CameraSerial.read();
  }

  CameraSerial.println("DETECT");

  String line;
  uint32_t start = millis();
  while (millis() - start < CAMERA_UART_TIMEOUT_MS) {
    serviceNetwork();

    while (CameraSerial.available()) {
      char c = static_cast<char>(CameraSerial.read());
      if (c == '\n' || c == '\r') {
        CameraSign sign = CameraSign::TIMEOUT;
        if (parseCameraSign(line, sign)) {
          uint8_t confidence = sign == CameraSign::NO_SIGN ? 0 : 100;
          setCameraDiagnostics(confidence, "uart_response");
          return sign;
        }
        line = "";
        continue;
      }

      line += c;
      if (line.length() > 48) {
        line = "";
      }
    }

    delay(5);
  }

  setCameraDiagnostics(0, "uart_timeout");
  return CameraSign::TIMEOUT;
#else
  setCameraDiagnostics(0, "uart_disabled");
  return CameraSign::TIMEOUT;
#endif
}

CameraSign requestCameraSign() {
#if CAMERA_TRANSPORT_UART
  CameraSign uartSign = requestCameraSignUart();
  if (uartSign != CameraSign::TIMEOUT) {
    return uartSign;
  }
#endif

#if CAMERA_TRANSPORT_HTTP
  return requestCameraSignHttp();
#else
  return CameraSign::TIMEOUT;
#endif
}

// Punto unico de decision para obstaculos, da igual si viene por HTTP o UART.
CameraSign requestObstacleSign() {
  if (manualObstacleSignEnabled) {
    setCameraDiagnostics(100, "manual_obstacle_override");
    return manualObstacleSign;
  }

  return requestCameraSign();
}

uint8_t pwmPercent(int pwm) {
  return static_cast<uint8_t>((abs(constrain(pwm, -PWM_MAX, PWM_MAX)) * 100) / PWM_MAX);
}

uint8_t currentDrivePowerPercent() {
  return pwmPercent(basePwm);
}

// Empaqueta el grafo para que el dashboard pueda dibujar nodos y aristas.
void addGraphDiagnostics(JsonDocument &doc) {
  JsonArray graphNodes = doc["graph_nodes"].to<JsonArray>();
  for (uint8_t i = 0; i < nodeCount; i++) {
    JsonObject node = graphNodes.add<JsonObject>();
    node["id"] = nodes[i].id;
    node["x"] = nodes[i].x;
    node["y"] = nodes[i].y;
    node["options"] = nodes[i].options;
    node["tried"] = nodes[i].tried;
    node["blocked"] = nodes[i].blocked;
    node["green"] = nodes[i].passableGreen;
    node["parent"] = nodes[i].parent;
    node["explored"] = nodes[i].explored;
    node["final"] = nodes[i].finalNode;
  }

  JsonArray graphEdges = doc["graph_edges"].to<JsonArray>();
  for (uint8_t i = 0; i < edgeCount; i++) {
    JsonObject edge = graphEdges.add<JsonObject>();
    edge["from"] = edges[i].from;
    edge["to"] = edges[i].to;
    edge["orientation"] = orientationName(edges[i].fromOrientation);
    edge["turn"] = turnName(edges[i].fromTurn);
    edge["visited"] = edges[i].visited;
    edge["blocked"] = edges[i].blocked;
    edge["green"] = edges[i].passableGreen;
  }
}

// Telemetria extra de sensores, mapa y flags de navegacion.
void addLiveDiagnostics(JsonDocument &doc) {
  JsonArray sensorValues = doc["sensor_values"].to<JsonArray>();
  JsonArray sensorActive = doc["sensor_active"].to<JsonArray>();
  uint8_t activeCount = 0;

  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    sensorValues.add(lastSensorValues[i]);
    sensorActive.add(lastLineDetected[i]);
    if (lastLineDetected[i]) {
      activeCount++;
    }
  }

  doc["sensor_active_count"] = activeCount;
  doc["node_count"] = nodeCount;
  doc["edge_count"] = edgeCount;
  doc["crossing_count"] = crossingCount;
  doc["orientation"] = orientationName(robotOrientation);
  doc["last_intersection_type"] = intersectionTypeName(lastIntersectionType);
  doc["line_tracking_protected"] = lastLineTrackingProtected;
  doc["relative_exits"] = relativeExitMaskText(lastAvailableExits);
  doc["absolute_exits"] = currentNode >= 0 && currentNode < nodeCount ? absoluteExitMaskText(nodes[currentNode].options) : "-";
  doc["qtr_learning_enabled"] = lastNodeQtrLearningEnabled;
  doc["last_turn"] = turnName(lastTurn);
  doc["pending_turn"] = turnName(pendingTurn);
  doc["pending_exit_orientation"] = orientationName(pendingExitOrientation);
  doc["pause_before_turn_enabled"] = pauseBeforeTurnEnabled;
  doc["waiting_for_turn_confirmation"] = waitingForTurnConfirmation;
  doc["edge_in_progress"] = edgeInProgress;
  doc["obstacle_handling_enabled"] = obstacleHandlingEnabled;
  doc["wait_for_obstacle_decision_enabled"] = waitForObstacleDecisionEnabled;
  doc["waiting_for_obstacle_decision"] = waitingForObstacleDecision;
  doc["returning_from_blocked_obstacle"] = returningFromBlockedObstacle;
  doc["navigating_to_frontier"] = navigatingToFrontier;
  doc["manual_obstacle_sign_enabled"] = manualObstacleSignEnabled;
  doc["manual_obstacle_sign"] = cameraSignName(manualObstacleSign);
  doc["return_node_after_blocked_obstacle"] = returnNodeAfterBlockedObstacle;
  doc["navigation_target_node"] = navigationTargetNode;
  doc["navigation_next_node"] = navigationNextNode;
  doc["blocked_obstacle_orientation"] = orientationName(blockedObstacleOrientation);
  doc["previous_node"] = previousNodeForEdge;
  doc["final_node"] = finalNode;
  doc["shortest_path_length"] = shortestPathLength;
  doc["ms_per_cm"] = msPerCm;
  doc["ms_turn_90"] = msTurn90;
  doc["ms_turn_180"] = msTurn180;
  doc["back_turn_extra_ms"] = backTurnExtraMs;
  doc["qtr_threshold"] = currentLineThresholdRaw;
  doc["qtr_calibrated"] = qtrCalibrated;
  doc["motor_left_pwm"] = lastLeftMotorPwm;
  doc["motor_right_pwm"] = lastRightMotorPwm;
  doc["motor_left_percent"] = pwmPercent(lastLeftMotorPwm);
  doc["motor_right_percent"] = pwmPercent(lastRightMotorPwm);
  doc["drive_power_percent"] = currentDrivePowerPercent();
  addGraphDiagnostics(doc);
}

// Envia eventos con contexto suficiente para depurar desde la web.
void sendEvent(const char *event, JsonDocument *extra = nullptr) {
  JsonDocument doc;
  doc["event"] = event;
  doc["state"] = stateName(robotState);
  doc["algorithm"] = algorithmName();
  doc["speed_limit"] = speedLimitPercent;
  doc["current_node"] = currentNode;
  doc["obstacle_count"] = obstacleCount;
  doc["line_position"] = lastLinePosition;
  doc["camera_sign"] = cameraSignName(lastCameraSign);
  doc["camera_confidence"] = lastCameraConfidence;
  doc["camera_reason"] = lastCameraReason;
  doc["threshold"] = currentLineThresholdRaw;
  doc["elapsed_ms"] = motorsAllowed() ? millis() - runStartMs : 0;
  addLiveDiagnostics(doc);
  if (extra != nullptr) {
    for (JsonPair pair : extra->as<JsonObject>()) {
      doc[pair.key()] = pair.value();
    }
  }
  String payload;
  serializeJson(doc, payload);
  if (wsConnected) {
    webSocket.sendTXT(payload);
  }
}

// Escribe PWM y direccion respetando la inversion fisica de cada motor.
void setMotorRaw(uint8_t pwmChannel, uint8_t in1, uint8_t in2, int pwm, bool invert) {
  int value = constrain(pwm, -PWM_MAX, PWM_MAX);
  if (invert) {
    value = -value;
  }

  if (value > 0) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else if (value < 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }

  ledcWrite(pwmChannel, abs(value));
}

int applySpeedLimit(int pwm) {
  return constrain((pwm * speedLimitPercent) / 100, -PWM_MAX, PWM_MAX);
}

void setMotors(int left, int right) {
  lastLeftMotorPwm = applySpeedLimit(left);
  lastRightMotorPwm = applySpeedLimit(right);
  setMotorRaw(PWM_LEFT_CHANNEL, MOTOR_LEFT_IN1, MOTOR_LEFT_IN2, lastLeftMotorPwm, INVERT_LEFT_MOTOR);
  setMotorRaw(PWM_RIGHT_CHANNEL, MOTOR_RIGHT_IN1, MOTOR_RIGHT_IN2, lastRightMotorPwm, INVERT_RIGHT_MOTOR);
}

void stopMotors() {
  setMotors(0, 0);
}

void setupMotors() {
  pinMode(MOTOR_LEFT_IN1, OUTPUT);
  pinMode(MOTOR_LEFT_IN2, OUTPUT);
  pinMode(MOTOR_RIGHT_IN1, OUTPUT);
  pinMode(MOTOR_RIGHT_IN2, OUTPUT);
  ledcSetup(PWM_LEFT_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcSetup(PWM_RIGHT_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(MOTOR_LEFT_PWM, PWM_LEFT_CHANNEL);
  ledcAttachPin(MOTOR_RIGHT_PWM, PWM_RIGHT_CHANNEL);
  stopMotors();
}

// Lee el tiempo de descarga de cada sensor QTR.
void readQtrRaw(uint16_t values[QTR_COUNT]) {
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    pinMode(QTR_PINS[i], OUTPUT);
    digitalWrite(QTR_PINS[i], HIGH);
  }
  delayMicroseconds(10);

  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    pinMode(QTR_PINS[i], INPUT);
    values[i] = QTR_TIMEOUT_US;
  }

  uint32_t start = micros();
  bool pending = true;
  while (pending) {
    pending = false;
    uint16_t elapsed = micros() - start;
    for (uint8_t i = 0; i < QTR_COUNT; i++) {
      if (values[i] == QTR_TIMEOUT_US) {
        if (digitalRead(QTR_PINS[i]) == LOW) {
          values[i] = elapsed;
        } else if (elapsed < QTR_TIMEOUT_US) {
          pending = true;
        }
      }
    }

    if (elapsed >= QTR_TIMEOUT_US) {
      break;
    }
  }
}

bool anyLineDetected(const SensorFrame &frame) {
  return frame.lineSeen;
}

// Calcula si la linea queda a la izquierda, centro o derecha.
int getLineDirection(const SensorFrame &frame) {
  if (!frame.lineSeen) {
    return lastKnownDirection;
  }

  if (frame.position < 3000) {
    return -1;
  }

  if (frame.position > 4000) {
    return 1;
  }

  return 0;
}

// Detecta patrones laterales que pueden ser cruce, no simple correccion PID.
bool hasSideEventCandidate(const SensorFrame &frame) {
  return frame.center && (frame.left || frame.right || frame.blackPatch);
}

// Protege el seguimiento cuando una rama lateral aparece de golpe.
bool shouldProtectLineTracking(const SensorFrame &frame) {
  return frame.lineSeen && hasSideEventCandidate(frame);
}

bool knownBacktrackingActive() {
  return returningFromBlockedObstacle || navigatingToFrontier;
}

bool backtrackingSideNodeCandidate(const SensorFrame &frame) {
  if (!runActive || !knownBacktrackingActive() || !frame.lineSeen || frame.blackPatch || frame.intersection) {
    return false;
  }

  // En vuelta por camino conocido, una rama lateral no debe entrar al PID:
  // primero la tratamos como nodo y despues decide el grafo.
  bool leftBranchCandidate = frame.leftActiveCount >= BACKTRACK_SIDE_EVENT_MIN_ACTIVE_SENSORS &&
                             frame.centerActiveCount < STRAIGHT_PATH_MIN_ACTIVE_SENSORS;
  bool rightBranchCandidate = frame.rightActiveCount >= BACKTRACK_SIDE_EVENT_MIN_ACTIVE_SENSORS &&
                              frame.centerActiveCount < STRAIGHT_PATH_MIN_ACTIVE_SENSORS;
  return leftBranchCandidate || rightBranchCandidate;
}

// En backtracking, una rama lateral fuerte se trata como nodo aunque no sature todo.
void promoteBacktrackingNodeCandidate(SensorFrame &frame) {
  if (!backtrackingSideNodeCandidate(frame)) {
    return;
  }

  frame.intersection = true;
  if (frame.leftActiveCount >= BACKTRACK_SIDE_EVENT_MIN_ACTIVE_SENSORS &&
      frame.rightActiveCount >= BACKTRACK_SIDE_EVENT_MIN_ACTIVE_SENSORS) {
    frame.intersectionType = IntersectionType::T_OR_CROSS;
  } else if (frame.leftActiveCount >= BACKTRACK_SIDE_EVENT_MIN_ACTIVE_SENSORS) {
    frame.intersectionType = IntersectionType::LEFT_BRANCH;
  } else {
    frame.intersectionType = IntersectionType::RIGHT_BRANCH;
  }
}

// Mantiene vivo un candidato de cruce durante unos milisegundos.
void holdNodeCandidateDuringConfirmation(const SensorFrame &frame) {
  if (knownBacktrackingActive()) {
    // Si seguimos corrigiendo aqui, el robot puede pasar de largo el cruce.
    stopMotors();
    return;
  }

  followLine(frame);
}

// Media ponderada de sensores, limitada si estamos protegiendo el PID.
int computeLinePositionInRange(const SensorFrame &frame, uint8_t minIndex, uint8_t maxIndex) {
  int lastIndex = constrain(lastLinePosition / 1000, static_cast<int>(minIndex), static_cast<int>(maxIndex));
  int nearestIndex = -1;
  int nearestDistance = 999;

  for (uint8_t i = minIndex; i <= maxIndex; i++) {
    if (frame.raw[i] <= currentLineThresholdRaw) {
      continue;
    }

    int distance = abs(static_cast<int>(i) - lastIndex);
    if (distance < nearestDistance) {
      nearestDistance = distance;
      nearestIndex = i;
    }
  }

  if (nearestIndex < 0) {
    return lastLinePosition;
  }

  int startIndex = max(static_cast<int>(minIndex), nearestIndex - positionWindow);
  int endIndex = min(static_cast<int>(maxIndex), nearestIndex + positionWindow);
  uint32_t weighted = 0;
  uint32_t total = 0;

  for (int i = startIndex; i <= endIndex; i++) {
    if (frame.raw[i] <= currentLineThresholdRaw) {
      continue;
    }

    weighted += static_cast<uint32_t>(frame.raw[i]) * i * 1000;
    total += frame.raw[i];
  }

  return total > 0 ? static_cast<int>(weighted / total) : lastLinePosition;
}

// Clasifica una lectura QTR completa para seguimiento, cruces y obstaculos.
SensorFrame readSensors() {
  SensorFrame frame;
  readQtrRaw(frame.raw);

  frame.activeCount = 0;
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    uint16_t value = frame.raw[i];
    uint16_t normalized = value;
    if (qtrCalibrated && qtrMax[i] > qtrMin[i] + 20) {
      normalized = constrain(map(value, qtrMin[i], qtrMax[i], 0, 1000), 0, 1000);
    } else {
      normalized = constrain(map(value, 0, QTR_TIMEOUT_US, 0, 1000), 0, 1000);
    }
    frame.normalized[i] = normalized;
    if (value > currentLineThresholdRaw) {
      frame.activeCount++;
      if (i <= 2) {
        frame.leftActiveCount++;
      } else if (i <= 4) {
        frame.centerActiveCount++;
      } else {
        frame.rightActiveCount++;
      }
    }
  }

  frame.left = frame.leftActiveCount >= SIDE_PATH_MIN_ACTIVE_SENSORS;
  frame.center = frame.centerActiveCount >= STRAIGHT_PATH_MIN_ACTIVE_SENSORS;
  frame.right = frame.rightActiveCount >= SIDE_PATH_MIN_ACTIVE_SENSORS;
  frame.lineSeen = frame.activeCount > 0;
  frame.blackPatch = frame.activeCount >= BLACK_PATCH_MIN_ACTIVE_SENSORS;

  if (frame.blackPatch && obstacleHandlingEnabled && !returningFromBlockedObstacle) {
    frame.intersectionType = IntersectionType::BLACK_PATCH;
    frame.intersection = false;
  } else if (!frame.lineSeen) {
    frame.intersectionType = IntersectionType::LINE_LOST;
    frame.intersection = false;
  } else if (frame.blackPatch || (frame.left && frame.right)) {
    frame.intersectionType = IntersectionType::T_OR_CROSS;
    frame.intersection = frame.activeCount >= INTERSECTION_ACTIVE_COUNT;
  } else if (frame.left && frame.center) {
    frame.intersectionType = IntersectionType::LEFT_BRANCH;
    frame.intersection = true;
  } else if (frame.right && frame.center) {
    frame.intersectionType = IntersectionType::RIGHT_BRANCH;
    frame.intersection = true;
  } else {
    frame.intersectionType = IntersectionType::LINE;
    frame.intersection = false;
  }

  if (!frame.lineSeen) {
    frame.position = lastLinePosition;
    return frame;
  }

  frame.lineTrackingProtected = shouldProtectLineTracking(frame);
  if (frame.lineTrackingProtected) {
    frame.position = computeLinePositionInRange(frame, EVENT_TRACKING_MIN_SENSOR, EVENT_TRACKING_MAX_SENSOR);
  } else {
    frame.position = computeLinePositionInRange(frame, 0, QTR_COUNT - 1);
  }

  lastLinePosition = frame.position;
  lastKnownPosition = static_cast<float>(frame.position) / 1000.0f;

  return frame;
}

// Guarda la ultima lectura para telemetria y panel de sensores.
void storeLastSensorFrame(const SensorFrame &frame) {
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    lastSensorValues[i] = frame.raw[i];
    lastLineDetected[i] = frame.raw[i] > currentLineThresholdRaw;
  }
  lastLineTrackingProtected = frame.lineTrackingProtected;
}

// Baja el umbral si se pierde la linea y lo recupera al volver a verla.
void updateAdaptiveThreshold(const SensorFrame &frame) {
  if (frame.lineSeen) {
    if (currentLineThresholdRaw < targetLineThresholdRaw) {
      currentLineThresholdRaw = min<uint16_t>(targetLineThresholdRaw, currentLineThresholdRaw + thresholdStepRaw);
    }
    return;
  }

  if (currentLineThresholdRaw > minAdaptiveThresholdRaw) {
    currentLineThresholdRaw = max<uint16_t>(minAdaptiveThresholdRaw, currentLineThresholdRaw - thresholdStepRaw);
  }
}

// Recuerda por donde estaba la linea para poder buscarla si se pierde.
void updateLineMemory(const SensorFrame &frame) {
  if (!frame.lineSeen || frame.blackPatch) {
    return;
  }

  lastKnownDirection = getLineDirection(frame);
  lastKnownPosition = static_cast<float>(frame.position) / 1000.0f;
}

// Calibracion rapida leyendo minimos y maximos de cada QTR.
void calibrateQtr() {
  robotState = RobotState::CALIBRATING;
  sendEvent("CALIBRATION_STARTED");
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    qtrMin[i] = QTR_TIMEOUT_US;
    qtrMax[i] = 0;
  }

  uint32_t start = millis();
  while (millis() - start < 5500) {
    uint16_t values[QTR_COUNT];
    readQtrRaw(values);
    for (uint8_t i = 0; i < QTR_COUNT; i++) {
      qtrMin[i] = min(qtrMin[i], values[i]);
      qtrMax[i] = max(qtrMax[i], values[i]);
    }
    serviceNetwork();
    delay(12);
  }

  qtrCalibrated = true;
  robotState = motorsAllowed() ? RobotState::FOLLOWING_LINE : RobotState::WAITING_START;
  sendEvent("CALIBRATION_DONE");
}

// Confirma si hay salida recta despues de centrar el eje de ruedas.
bool straightExitConfirmedFromFrame(const SensorFrame &frame) {
  bool blackPatchBlocksStraight = frame.blackPatch && obstacleHandlingEnabled && !returningFromBlockedObstacle;
  return !blackPatchBlocksStraight &&
         frame.lineSeen &&
         frame.center &&
         frame.centerActiveCount >= STRAIGHT_CONFIRM_MIN_CENTER_SENSORS;
}

// Convierte la foto del cruce en salidas disponibles.
uint8_t optionsFromFrame(const SensorFrame &frame, bool includeStraight) {
  if (frame.blackPatch && obstacleHandlingEnabled && !returningFromBlockedObstacle) {
    return 0;
  }

  uint8_t options = 0;
  if (frame.left) {
    options |= DIR_LEFT;
  }
  if (includeStraight && straightExitConfirmedFromFrame(frame)) {
    options |= DIR_STRAIGHT;
  }
  if (frame.right) {
    options |= DIR_RIGHT;
  }
  return options;
}

// DFS prioriza profundizar antes de volver a otras salidas.
Turn chooseDfsTurn(NodeRecord &node) {
  const Turn priority[] = {Turn::RIGHT, Turn::LEFT, Turn::STRAIGHT, Turn::BACK};
  for (Turn turn : priority) {
    RobotOrientation exitOrientation = rotateOrientation(robotOrientation, turn);
    uint8_t bit = orientationBit(exitOrientation);
    if ((node.options & bit) && !(node.tried & bit) && !(node.blocked & bit)) {
      node.tried |= bit;
      return turn;
    }
  }
  for (Turn turn : priority) {
    RobotOrientation exitOrientation = rotateOrientation(robotOrientation, turn);
    uint8_t bit = orientationBit(exitOrientation);
    if ((node.options & bit) && !(node.blocked & bit)) {
      node.tried |= bit;
      return turn;
    }
  }
  return Turn::BACK;
}

// BFS prioriza avance por niveles usando recto antes que ramas laterales.
Turn chooseBfsTurn(NodeRecord &node) {
  const Turn priority[] = {Turn::STRAIGHT, Turn::RIGHT, Turn::LEFT, Turn::BACK};
  for (Turn turn : priority) {
    RobotOrientation exitOrientation = rotateOrientation(robotOrientation, turn);
    uint8_t bit = orientationBit(exitOrientation);
    if ((node.options & bit) && !(node.tried & bit) && !(node.blocked & bit)) {
      node.tried |= bit;
      return turn;
    }
  }
  for (Turn turn : priority) {
    RobotOrientation exitOrientation = rotateOrientation(robotOrientation, turn);
    uint8_t bit = orientationBit(exitOrientation);
    if ((node.options & bit) && !(node.blocked & bit)) {
      node.tried |= bit;
      return turn;
    }
  }
  return Turn::BACK;
}

// Salidas aun no exploradas, ignorando vecinos que ya estan conectados.
uint8_t untriedOptionsMask(int nodeId) {
  if (nodeId < 0 || nodeId >= nodeCount) {
    return 0;
  }

  uint8_t knownNeighbors = 0;
  for (uint8_t direction = 0; direction < 4; direction++) {
    int neighbor = nodes[nodeId].neighbors[direction];
    if (neighbor >= 0 && neighbor < nodeCount) {
      knownNeighbors |= orientationBit(orientationFromIndex(direction));
    }
  }

  // Una arista ya conectada no es una frontera nueva. Se usa para volver
  // por camino conocido, no para abrir otra exploracion.
  return nodes[nodeId].options & ~nodes[nodeId].tried & ~nodes[nodeId].blocked & ~knownNeighbors;
}

bool hasUntriedOptions(int nodeId) {
  return untriedOptionsMask(nodeId) != 0;
}

bool chooseUntriedTurnAtNode(int nodeId, Turn &selected) {
  if (nodeId < 0 || nodeId >= nodeCount) {
    return false;
  }

  const Turn dfsPriority[] = {Turn::RIGHT, Turn::LEFT, Turn::STRAIGHT, Turn::BACK};
  const Turn bfsPriority[] = {Turn::STRAIGHT, Turn::RIGHT, Turn::LEFT, Turn::BACK};
  const Turn *priority = algorithm == Algorithm::DFS ? dfsPriority : bfsPriority;

  for (uint8_t i = 0; i < 4; i++) {
    Turn turn = priority[i];
    RobotOrientation exitOrientation = rotateOrientation(robotOrientation, turn);
    uint8_t bit = orientationBit(exitOrientation);
    int knownNeighbor = nodes[nodeId].neighbors[orientationIndex(exitOrientation)];
    bool hasKnownNeighbor = knownNeighbor >= 0 && knownNeighbor < nodeCount;
    if ((nodes[nodeId].options & bit) && !(nodes[nodeId].tried & bit) && !(nodes[nodeId].blocked & bit) && !hasKnownNeighbor) {
      selected = turn;
      return true;
    }
  }

  return false;
}

// Solo se puede navegar por aristas conocidas y no bloqueadas.
bool edgeCanBeUsed(int from, RobotOrientation orientation) {
  if (from < 0 || from >= nodeCount) {
    return false;
  }

  uint8_t bit = orientationBit(orientation);
  int neighbor = nodes[from].neighbors[orientationIndex(orientation)];
  return neighbor >= 0 && neighbor < nodeCount && !(nodes[from].blocked & bit);
}

// Busca ruta entre nodos conocidos para hacer backtracking estable.
bool computePathToNode(int start, int target, int8_t parents[MAX_NODES]) {
  for (uint8_t i = 0; i < MAX_NODES; i++) {
    parents[i] = -1;
  }

  if (start < 0 || start >= nodeCount || target < 0 || target >= nodeCount) {
    return false;
  }

  bool visited[MAX_NODES] = {};
  uint8_t queue[MAX_NODES];
  uint8_t head = 0;
  uint8_t tail = 0;
  queue[tail++] = start;
  visited[start] = true;

  while (head < tail) {
    int nodeId = queue[head++];
    if (nodeId == target) {
      return true;
    }

    for (uint8_t direction = 0; direction < 4; direction++) {
      RobotOrientation orientation = orientationFromIndex(direction);
      if (!edgeCanBeUsed(nodeId, orientation)) {
        continue;
      }

      int neighbor = nodes[nodeId].neighbors[direction];
      if (visited[neighbor]) {
        continue;
      }

      visited[neighbor] = true;
      parents[neighbor] = nodeId;
      queue[tail++] = neighbor;
    }
  }

  return false;
}

int firstStepOnPath(int start, int target, const int8_t parents[MAX_NODES]) {
  if (start == target) {
    return start;
  }

  int cursor = target;
  int previous = target;
  while (cursor >= 0 && cursor < nodeCount && cursor != start) {
    previous = cursor;
    cursor = parents[cursor];
  }

  return cursor == start ? previous : -1;
}

int findNearestFrontierNode(int start) {
  if (start < 0 || start >= nodeCount) {
    return -1;
  }

  bool visited[MAX_NODES] = {};
  uint8_t queue[MAX_NODES];
  uint8_t head = 0;
  uint8_t tail = 0;
  queue[tail++] = start;
  visited[start] = true;

  while (head < tail) {
    int nodeId = queue[head++];
    if (nodeId != start && hasUntriedOptions(nodeId)) {
      return nodeId;
    }

    for (uint8_t direction = 0; direction < 4; direction++) {
      RobotOrientation orientation = orientationFromIndex(direction);
      if (!edgeCanBeUsed(nodeId, orientation)) {
        continue;
      }

      int neighbor = nodes[nodeId].neighbors[direction];
      if (visited[neighbor]) {
        continue;
      }

      visited[neighbor] = true;
      queue[tail++] = neighbor;
    }
  }

  return -1;
}

// En DFS se vuelve al ultimo nodo con salidas pendientes.
int findDfsFrontierNode(int start) {
  int8_t parents[MAX_NODES];
  for (int nodeId = static_cast<int>(nodeCount) - 1; nodeId >= 0; nodeId--) {
    if (nodeId == start || !hasUntriedOptions(nodeId)) {
      continue;
    }

    if (computePathToNode(start, nodeId, parents)) {
      return nodeId;
    }
  }

  return -1;
}

RobotOrientation orientationToNeighbor(int from, int to) {
  for (uint8_t direction = 0; direction < 4; direction++) {
    if (nodes[from].neighbors[direction] == to) {
      return orientationFromIndex(direction);
    }
  }

  return robotOrientation;
}

bool chooseNavigationTurn(int nodeId, Turn &selected, int &targetNode, int &nextNode, bool &knownPath) {
  targetNode = nodeId;
  nextNode = -1;
  knownPath = false;

  if (chooseUntriedTurnAtNode(nodeId, selected)) {
    nodes[nodeId].explored = untriedOptionsMask(nodeId) == 0;
    return true;
  }

  nodes[nodeId].explored = true;
  targetNode = algorithm == Algorithm::DFS ? findDfsFrontierNode(nodeId) : findNearestFrontierNode(nodeId);
  if (targetNode < 0) {
    return false;
  }

  int8_t parents[MAX_NODES];
  if (!computePathToNode(nodeId, targetNode, parents)) {
    return false;
  }

  nextNode = firstStepOnPath(nodeId, targetNode, parents);
  if (nextNode < 0 || nextNode >= nodeCount || nextNode == nodeId) {
    return false;
  }

  RobotOrientation nextOrientation = orientationToNeighbor(nodeId, nextNode);
  selected = turnTowardOrientation(robotOrientation, nextOrientation);
  knownPath = true;
  return true;
}

// Cuando ya estamos volviendo, seguimos solo aristas conocidas.
bool chooseKnownPathTurnToTarget(int nodeId, int targetNode, Turn &selected, int &nextNode) {
  nextNode = -1;
  if (nodeId < 0 || nodeId >= nodeCount || targetNode < 0 || targetNode >= nodeCount || nodeId == targetNode) {
    return false;
  }

  int8_t parents[MAX_NODES];
  if (!computePathToNode(nodeId, targetNode, parents)) {
    return false;
  }

  nextNode = firstStepOnPath(nodeId, targetNode, parents);
  if (nextNode < 0 || nextNode >= nodeCount || nextNode == nodeId) {
    return false;
  }

  RobotOrientation nextOrientation = orientationToNeighbor(nodeId, nextNode);
  selected = turnTowardOrientation(robotOrientation, nextOrientation);
  return true;
}

uint8_t knownRelativeOptionsForNode(int nodeId) {
  if (nodeId < 0 || nodeId >= nodeCount) {
    return DIR_BACK;
  }

  uint8_t options = absoluteOptionsToRelative(nodes[nodeId].options, robotOrientation);
  return options != 0 ? options : DIR_BACK;
}

// Limpia mapa, obstaculos y estado de navegacion para una carrera nueva.
void resetMaze() {
  for (NodeRecord &node : nodes) {
    node = NodeRecord{};
  }
  for (EdgeRecord &edge : edges) {
    edge = EdgeRecord{};
  }
  for (int8_t &pathNode : shortestPathNodes) {
    pathNode = -1;
  }
  nodeCount = 0;
  edgeCount = 0;
  currentNode = -1;
  finalNode = -1;
  previousNodeForEdge = -1;
  obstacleCount = 0;
  crossingCount = 0;
  lastTurn = Turn::STRAIGHT;
  lastSelectedTurn = Turn::STRAIGHT;
  pendingTurn = Turn::STRAIGHT;
  edgeInProgress = false;
  robotOrientation = RobotOrientation::NORTH;
  lastTraversalOrientation = RobotOrientation::NORTH;
  pendingExitOrientation = RobotOrientation::NORTH;
  blockedObstacleOrientation = RobotOrientation::NORTH;
  waitingForObstacleDecision = false;
  returningFromBlockedObstacle = false;
  navigatingToFrontier = false;
  returnNodeAfterBlockedObstacle = -1;
  navigationTargetNode = -1;
  navigationNextNode = -1;
  waitingForTurnConfirmation = false;
  lastNodeQtrLearningEnabled = true;
  lastIntersectionType = IntersectionType::NONE;
  lastAvailableExits = 0;
  shortestPathLength = 0;
  lastKnownDirection = 0;
  lastKnownPosition = 3.5f;
  lastLinePosition = 3500;
  currentLineThresholdRaw = targetLineThresholdRaw;
  isSearching = false;
  blackPatchFirstSeenMs = 0;
  intersectionFirstSeenMs = 0;
  lastCameraSign = CameraSign::NO_SIGN;
  setCameraDiagnostics(0, "not_checked");
}

// Avanza una celda logica del mapa en la orientacion indicada.
void advanceNodeCoordinates(int16_t x, int16_t y, RobotOrientation orientation, int16_t &nextX, int16_t &nextY) {
  nextX = x;
  nextY = y;

  switch (orientation) {
    case RobotOrientation::NORTH: nextY += 1; break;
    case RobotOrientation::EAST: nextX += 1; break;
    case RobotOrientation::SOUTH: nextY -= 1; break;
    case RobotOrientation::WEST: nextX -= 1; break;
  }
}

// Busca si ya existe un nodo en esas coordenadas del grafo.
int findNodeAt(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (nodes[i].x == x && nodes[i].y == y) {
      return i;
    }
  }

  return -1;
}

// Crea un nodo nuevo con las salidas conocidas hasta ese momento.
int createNodeAt(uint8_t absoluteOptions, int16_t x, int16_t y) {
  if (nodeCount >= MAX_NODES) {
    robotState = RobotState::ERROR_STATE;
    sendEvent("GRAPH_FULL");
    return currentNode;
  }

  nodes[nodeCount].id = nodeCount;
  nodes[nodeCount].x = x;
  nodes[nodeCount].y = y;
  nodes[nodeCount].createdFacing = robotOrientation;
  nodes[nodeCount].options = absoluteOptions;
  nodes[nodeCount].tried = 0;
  nodes[nodeCount].blocked = 0;
  nodes[nodeCount].passableGreen = 0;
  nodes[nodeCount].parent = -1;
  nodes[nodeCount].parentOrientation = RobotOrientation::NORTH;
  nodes[nodeCount].explored = false;
  nodes[nodeCount].finalNode = false;
  return nodeCount++;
}

// Fusiona salidas nuevas sin borrar lo que ya se habia aprendido.
void updateNodeOptions(int nodeId, uint8_t absoluteOptions) {
  if (nodeId < 0 || nodeId >= nodeCount) {
    return;
  }

  nodes[nodeId].options |= absoluteOptions;
}

int findEdgeIndex(int from, RobotOrientation fromOrientation) {
  for (uint8_t i = 0; i < edgeCount; i++) {
    if (edges[i].from == from && edges[i].fromOrientation == fromOrientation) {
      return i;
    }
  }

  return -1;
}

int findEdgeIndexBetween(int a, int b) {
  for (uint8_t i = 0; i < edgeCount; i++) {
    if ((edges[i].from == a && edges[i].to == b) || (edges[i].from == b && edges[i].to == a)) {
      return i;
    }
  }

  return -1;
}

// Conecta dos nodos en ambos sentidos y evita duplicados.
void connectNodes(int from, int to, RobotOrientation fromOrientation) {
  if (from < 0 || to < 0 || from >= nodeCount || to >= nodeCount || from == to) {
    return;
  }

  RobotOrientation toOrientation = oppositeOrientation(fromOrientation);
  uint8_t fromBit = orientationBit(fromOrientation);
  uint8_t toBit = orientationBit(toOrientation);
  nodes[from].neighbors[orientationIndex(fromOrientation)] = to;
  nodes[to].neighbors[orientationIndex(toOrientation)] = from;
  nodes[from].options |= fromBit;
  nodes[to].options |= toBit;

  if (nodes[to].parent < 0 && from != to) {
    nodes[to].parent = from;
    nodes[to].parentOrientation = fromOrientation;
  }

  int edgeIndex = findEdgeIndex(from, fromOrientation);
  if (edgeIndex < 0) {
    edgeIndex = findEdgeIndexBetween(from, to);
  }
  bool newEdge = edgeIndex < 0;
  if (edgeIndex < 0) {
    if (edgeCount >= MAX_EDGES) {
      robotState = RobotState::ERROR_STATE;
      sendEvent("EDGE_GRAPH_FULL");
      return;
    }
    edgeIndex = edgeCount++;
  }

  edges[edgeIndex].from = from;
  edges[edgeIndex].to = to;
  edges[edgeIndex].fromOrientation = fromOrientation;
  edges[edgeIndex].fromTurn = lastSelectedTurn;
  edges[edgeIndex].toTurn = Turn::BACK;
  edges[edgeIndex].visited = true;
  if (newEdge) {
    edges[edgeIndex].blocked = false;
    edges[edgeIndex].passableGreen = false;
  }

  if ((nodes[from].blocked & fromBit) || (nodes[to].blocked & toBit)) {
    edges[edgeIndex].blocked = true;
    nodes[from].blocked |= fromBit;
    nodes[to].blocked |= toBit;
  }

  if ((nodes[from].passableGreen & fromBit) || (nodes[to].passableGreen & toBit) || edges[edgeIndex].passableGreen) {
    edges[edgeIndex].passableGreen = true;
    nodes[from].passableGreen |= fromBit;
    nodes[to].passableGreen |= toBit;
  }
}

// Marca una salida como bloqueada tras detectar senal roja.
void markEdgeBlocked(int from, RobotOrientation fromOrientation) {
  if (from < 0 || from >= nodeCount) {
    return;
  }

  uint8_t fromBit = orientationBit(fromOrientation);
  int neighbor = nodes[from].neighbors[orientationIndex(fromOrientation)];
  nodes[from].blocked |= fromBit;
  nodes[from].passableGreen &= ~fromBit;

  int edgeIndex = findEdgeIndex(from, fromOrientation);
  if (edgeIndex < 0 && neighbor >= 0 && neighbor < nodeCount) {
    edgeIndex = findEdgeIndexBetween(from, neighbor);
  }

  if (edgeIndex >= 0) {
    edges[edgeIndex].blocked = true;
    edges[edgeIndex].passableGreen = false;
  }

  if (neighbor >= 0 && neighbor < nodeCount) {
    RobotOrientation reverseOrientation = oppositeOrientation(fromOrientation);
    uint8_t reverseBit = orientationBit(reverseOrientation);
    nodes[neighbor].blocked |= reverseBit;
    nodes[neighbor].passableGreen &= ~reverseBit;
  }
}

// Guarda que una arista se puede cruzar porque la senal fue verde.
void markCurrentTraversalPassableGreen() {
  if (currentNode < 0 || currentNode >= nodeCount) {
    return;
  }

  uint8_t bit = orientationBit(lastTraversalOrientation);
  nodes[currentNode].passableGreen |= bit;

  int neighbor = nodes[currentNode].neighbors[orientationIndex(lastTraversalOrientation)];
  int edgeIndex = findEdgeIndex(currentNode, lastTraversalOrientation);
  if (edgeIndex < 0 && neighbor >= 0 && neighbor < nodeCount) {
    edgeIndex = findEdgeIndexBetween(currentNode, neighbor);
  }

  if (edgeIndex >= 0) {
    edges[edgeIndex].passableGreen = true;
  }

  if (neighbor >= 0 && neighbor < nodeCount) {
    RobotOrientation reverseOrientation = oppositeOrientation(lastTraversalOrientation);
    nodes[neighbor].passableGreen |= orientationBit(reverseOrientation);
  }
}

bool currentTraversalIsKnownGreen() {
  if (!edgeInProgress || currentNode < 0 || currentNode >= nodeCount) {
    return false;
  }

  uint8_t bit = orientationBit(lastTraversalOrientation);
  return (nodes[currentNode].passableGreen & bit) != 0;
}

// Resuelve si estamos creando nodo nuevo o llegando a uno ya conocido.
int resolveCurrentNode(uint8_t relativeOptions) {
  bool arrivedFromEdge = edgeInProgress && previousNodeForEdge >= 0 && previousNodeForEdge < nodeCount;
  if (arrivedFromEdge) {
    relativeOptions |= DIR_BACK;
  }

  uint8_t absoluteOptions = relativeOptionsToAbsolute(relativeOptions, robotOrientation);
  lastAvailableExits = relativeOptions;

  if (arrivedFromEdge) {
    int16_t nextX = nodes[previousNodeForEdge].x;
    int16_t nextY = nodes[previousNodeForEdge].y;
    advanceNodeCoordinates(nodes[previousNodeForEdge].x, nodes[previousNodeForEdge].y, lastTraversalOrientation, nextX, nextY);

    int nodeId = findNodeAt(nextX, nextY);
    if (nodeId < 0) {
      nodeId = createNodeAt(absoluteOptions, nextX, nextY);
      if (nodeId >= 0 && nodeId < nodeCount) {
        nodes[nodeId].parent = previousNodeForEdge;
        nodes[nodeId].parentOrientation = lastTraversalOrientation;
      }
    } else {
      updateNodeOptions(nodeId, absoluteOptions);
      if (nodes[nodeId].parent < 0) {
        nodes[nodeId].parent = previousNodeForEdge;
        nodes[nodeId].parentOrientation = lastTraversalOrientation;
      }
    }

    connectNodes(previousNodeForEdge, nodeId, lastTraversalOrientation);
    currentNode = nodeId;
    edgeInProgress = false;
    previousNodeForEdge = -1;
    return currentNode;
  }

  if (currentNode >= 0 && currentNode < nodeCount) {
    updateNodeOptions(currentNode, absoluteOptions);
    return currentNode;
  }

  int nodeId = findNodeAt(0, 0);
  if (nodeId < 0) {
    nodeId = createNodeAt(absoluteOptions, 0, 0);
  } else {
    updateNodeOptions(nodeId, absoluteOptions);
  }
  currentNode = nodeId;
  return currentNode;
}

// Empieza una arista: desde aqui el siguiente nodo cerrara la conexion.
void beginTraversal(Turn selectedTurn) {
  if (currentNode < 0 || currentNode >= nodeCount) {
    return;
  }

  RobotOrientation exitOrientation = rotateOrientation(robotOrientation, selectedTurn);
  uint8_t exitBit = orientationBit(exitOrientation);

  nodes[currentNode].options |= exitBit;
  nodes[currentNode].tried |= exitBit;
  previousNodeForEdge = currentNode;
  lastTurn = selectedTurn;
  lastSelectedTurn = selectedTurn;
  robotOrientation = exitOrientation;
  lastTraversalOrientation = exitOrientation;
  edgeInProgress = true;
}

// Calcula el camino final desde el inicio hasta la senal negra.
bool computeShortestPathToFinal() {
  shortestPathLength = 0;
  for (int8_t &pathNode : shortestPathNodes) {
    pathNode = -1;
  }

  if (finalNode < 0 || finalNode >= nodeCount || nodeCount == 0) {
    return false;
  }

  bool visited[MAX_NODES] = {};
  int8_t parent[MAX_NODES];
  uint8_t queue[MAX_NODES];
  for (uint8_t i = 0; i < MAX_NODES; i++) {
    parent[i] = -1;
  }

  uint8_t head = 0;
  uint8_t tail = 0;
  queue[tail++] = 0;
  visited[0] = true;

  while (head < tail) {
    uint8_t nodeId = queue[head++];
    if (nodeId == finalNode) {
      break;
    }

    for (uint8_t direction = 0; direction < 4; direction++) {
      if (nodes[nodeId].blocked & (1 << direction)) {
        continue;
      }

      int8_t neighbor = nodes[nodeId].neighbors[direction];
      if (neighbor < 0 || neighbor >= nodeCount || visited[neighbor]) {
        continue;
      }

      visited[neighbor] = true;
      parent[neighbor] = nodeId;
      queue[tail++] = neighbor;
    }
  }

  if (!visited[finalNode]) {
    return false;
  }

  int8_t reversed[MAX_NODES];
  uint8_t length = 0;
  int8_t cursor = finalNode;
  while (cursor >= 0 && length < MAX_NODES) {
    reversed[length++] = cursor;
    cursor = parent[cursor];
  }

  shortestPathLength = length;
  for (uint8_t i = 0; i < length; i++) {
    shortestPathNodes[i] = reversed[length - 1 - i];
  }

  return true;
}

// Comprueba que el robot ha vuelto a quedar sobre el centro de la linea.
bool lineCentered() {
  SensorFrame frame = readSensors();
  bool blackPatchBlocksCentering = frame.blackPatch && obstacleHandlingEnabled && !returningFromBlockedObstacle;
  return frame.center && frame.lineSeen && !frame.intersection && !blackPatchBlocksCentering;
}

bool sensorIsActive(const SensorFrame &frame, uint8_t index) {
  return index < QTR_COUNT && frame.raw[index] > currentLineThresholdRaw;
}

bool outerSensorSeesTurnLine(const SensorFrame &frame, Turn turn) {
  if (turn == Turn::LEFT) {
    for (uint8_t i = 0; i < TURN_OUTER_SENSOR_COUNT; i++) {
      if (sensorIsActive(frame, i)) {
        return true;
      }
    }
    return false;
  }

  if (turn == Turn::RIGHT) {
    for (uint8_t i = QTR_COUNT - TURN_OUTER_SENSOR_COUNT; i < QTR_COUNT; i++) {
      if (sensorIsActive(frame, i)) {
        return true;
      }
    }
  }

  return false;
}

bool centerSensorsSeeLine(const SensorFrame &frame) {
  bool blackPatchBlocksCentering = frame.blackPatch && obstacleHandlingEnabled && !returningFromBlockedObstacle;
  return frame.center && frame.lineSeen && !blackPatchBlocksCentering;
}

uint32_t msForDistance(float centimeters) {
  if (centimeters <= 0.0f) {
    return 0;
  }
  return static_cast<uint32_t>(centimeters * msPerCm);
}

// Movimiento temporizado, manteniendo telemetria y red activas.
void driveFor(int left, int right, uint32_t durationMs) {
  uint32_t start = millis();
  setMotors(left, right);
  while (millis() - start < durationMs) {
    serviceNetwork();
    sendTelemetry();
    if (!motorsAllowed()) {
      stopMotors();
      return;
    }
    delay(5);
  }
  stopMotors();
}

// Avanza una distancia corta corrigiendo con la linea si la ve.
void advanceFollowingLineCm(float centimeters) {
  uint32_t durationMs = msForDistance(centimeters);
  uint32_t start = millis();

  while (millis() - start < durationMs) {
    serviceNetwork();
    sendTelemetry();
    if (!motorsAllowed()) {
      stopMotors();
      return;
    }

    SensorFrame frame = readSensors();
    storeLastSensorFrame(frame);
    updateAdaptiveThreshold(frame);

    if (frame.lineSeen && !(frame.blackPatch && obstacleHandlingEnabled && !returningFromBlockedObstacle)) {
      followLine(frame);
    } else {
      setMotors(basePwm, basePwm);
    }

    delay(8);
  }

  stopMotors();
}

void advanceCm(float centimeters) {
  driveFor(basePwm, basePwm, msForDistance(centimeters));
}

void centerOnNode() {
  robotState = RobotState::CENTER_ON_NODE;
  advanceFollowingLineCm(CENTER_ON_NODE_DISTANCE_CM);
}

// La media vuelta recibe margen extra porque depende mucho de bateria y rozamiento.
uint32_t turnTimeoutMs(Turn turn, uint32_t targetMs) {
  uint32_t timeoutMs = targetMs + static_cast<uint32_t>(lineReacquireMs);
  if (turn != Turn::BACK) {
    return timeoutMs;
  }

  int effectiveSpeed = constrain(speedLimitPercent, 35, 100);
  uint32_t speedCompensatedTargetMs = (targetMs * 100UL) / static_cast<uint32_t>(effectiveSpeed);
  uint32_t backTimeoutMs = speedCompensatedTargetMs +
                           static_cast<uint32_t>(lineReacquireMs) +
                           static_cast<uint32_t>(backTurnExtraMs);
  return backTimeoutMs > timeoutMs ? backTimeoutMs : timeoutMs;
}

// Giro guiado por sensores: primero busca el lateral y luego centra.
void rotateForTurn(Turn turn) {
  if (turn == Turn::STRAIGHT) {
    return;
  }

  uint32_t targetMs = turn == Turn::BACK ? static_cast<uint32_t>(msTurn180) : static_cast<uint32_t>(msTurn90);
  uint32_t maxMs = turnTimeoutMs(turn, targetMs);
  uint32_t start = millis();
  bool useSideGuidedTurn = turn == Turn::LEFT || turn == Turn::RIGHT;
  bool outerLineSeen = false;
  bool centerReleasedAfterOuterLine = false;
  bool centerReleasedForBackTurn = false;
  uint32_t centeredSinceMs = 0;

  if (turn == Turn::LEFT) {
    setMotors(-turnPwm, turnPwm);
  } else {
    setMotors(turnPwm, -turnPwm);
  }

  while (millis() - start < maxMs) {
    serviceNetwork();
    sendTelemetry();
    if (!motorsAllowed()) {
      stopMotors();
      return;
    }

    SensorFrame frame = readSensors();
    storeLastSensorFrame(frame);

    uint32_t elapsed = millis() - start;

    if (useSideGuidedTurn) {
      if (!outerLineSeen && elapsed >= TURN_MIN_MS && outerSensorSeesTurnLine(frame, turn)) {
        outerLineSeen = true;
        centeredSinceMs = 0;
      }

      if (!outerLineSeen && elapsed >= targetMs && centerSensorsSeeLine(frame)) {
        break;
      } else if (outerLineSeen) {
        if (!centerSensorsSeeLine(frame)) {
          centerReleasedAfterOuterLine = true;
          centeredSinceMs = 0;
        } else if (centerReleasedAfterOuterLine) {
          if (centeredSinceMs == 0) {
            centeredSinceMs = millis();
          }
          if (millis() - centeredSinceMs >= TURN_CENTER_CONFIRM_MS) {
            break;
          }
        }
      }
    } else {
      bool centered = centerSensorsSeeLine(frame);
      uint32_t backMinReacquireMs = targetMs;

      if (!centered) {
        centerReleasedForBackTurn = true;
        centeredSinceMs = 0;
      } else if (centerReleasedForBackTurn && elapsed >= backMinReacquireMs) {
        if (centeredSinceMs == 0) {
          centeredSinceMs = millis();
        }
        if (millis() - centeredSinceMs >= TURN_CENTER_CONFIRM_MS) {
          break;
        }
      }
    }

    delay(8);
  }

  stopMotors();
}

// Aplica recto, izquierda, derecha o media vuelta en el mundo fisico.
void performTurn(Turn turn) {
  if (turn == Turn::STRAIGHT) {
    robotState = RobotState::GO_STRAIGHT;
    advanceFollowingLineCm(NODE_EXIT_ADVANCE_CM);
    return;
  }

  if (turn == Turn::LEFT) {
    robotState = RobotState::TURN_LEFT;
  } else if (turn == Turn::RIGHT) {
    robotState = RobotState::TURN_RIGHT;
  } else {
    robotState = RobotState::TURN_BACK;
  }

  rotateForTurn(turn);
  advanceFollowingLineCm(NODE_EXIT_ADVANCE_CM);
}

// Ejecuta el giro que quedo pausado desde la web.
void executePendingTurn() {
  if (!waitingForTurnConfirmation || currentNode < 0 || currentNode >= nodeCount) {
    return;
  }

  Turn turnToExecute = pendingTurn;
  waitingForTurnConfirmation = false;
  beginTraversal(turnToExecute);

  JsonDocument extra;
  extra["node"] = currentNode;
  extra["turn"] = turnName(turnToExecute);
  extra["exit_orientation"] = orientationName(pendingExitOrientation);
  sendEvent("TURN_CONFIRMED", &extra);

  performTurn(turnToExecute);
  robotState = RobotState::FOLLOWING_LINE;
}

// Cierra la arista actual como bloqueada para no volver a intentarla.
void markLastTurnBlocked() {
  if (currentNode < 0 || currentNode >= nodeCount) {
    return;
  }

  uint8_t bit = orientationBit(lastTraversalOrientation);
  nodes[currentNode].blocked |= bit;
  nodes[currentNode].tried |= bit;
  nodes[currentNode].passableGreen &= ~bit;
  markEdgeBlocked(currentNode, lastTraversalOrientation);
  edgeInProgress = false;
  previousNodeForEdge = -1;
}

// Evita que una lectura puntual saturada dispare un obstaculo falso.
bool blackPatchConfirmed(const SensorFrame &frame) {
  if (!frame.blackPatch) {
    blackPatchFirstSeenMs = 0;
    return false;
  }

  if (blackPatchFirstSeenMs == 0) {
    blackPatchFirstSeenMs = millis();
    return false;
  }

  return millis() - blackPatchFirstSeenMs >= BLACK_PATCH_CONFIRM_MS;
}

// Pasa por encima de una marca negra cuando la camara/manual dice verde.
bool drivePastBlackPatch() {
  uint32_t start = millis();
  setMotors(basePwm, basePwm);

  while (millis() - start < BLACK_PATCH_CLEAR_TIMEOUT_MS) {
    serviceNetwork();

    SensorFrame frame = readSensors();
    storeLastSensorFrame(frame);
    if (!frame.blackPatch && frame.lineSeen) {
      stopMotors();
      return true;
    }

    delay(10);
  }

  stopMotors();
  return false;
}

// Cierra la carrera y calcula la ruta cuando aparece la senal negra final.
void finishRunFromCameraSign() {
  robotState = RobotState::FINISH_DETECTED;
  int finishedNode = resolveCurrentNode(0);
  if (finishedNode >= 0 && finishedNode < nodeCount) {
    nodes[finishedNode].finalNode = true;
    finalNode = finishedNode;
    computeShortestPathToFinal();
  }

  runActive = false;
  localMotorsEnabled = false;
  stopMotors();
  robotState = RobotState::FINISHED;
  sendEvent("FINISH_DETECTED");
}

// Vuelve al nodo anterior despues de una senal roja.
void backtrackFromBlockedObstacle() {
  returnNodeAfterBlockedObstacle = currentNode;
  blockedObstacleOrientation = lastTraversalOrientation;
  returningFromBlockedObstacle = returnNodeAfterBlockedObstacle >= 0 && returnNodeAfterBlockedObstacle < nodeCount;

  markLastTurnBlocked();

  JsonDocument extra;
  extra["camera_sign"] = cameraSignName(lastCameraSign);
  extra["blocked_turn"] = turnName(lastTurn);
  extra["return_node"] = returnNodeAfterBlockedObstacle;
  extra["blocked_orientation"] = orientationName(blockedObstacleOrientation);
  sendEvent("OBSTACLE_BLOCKED", &extra);

  robotState = RobotState::BACKTRACKING;
  sendEvent("BACKTRACK_STARTED");
  performTurn(Turn::BACK);
  robotOrientation = oppositeOrientation(robotOrientation);
  robotState = returningFromBlockedObstacle ? RobotState::RETURNING_FROM_BLOCKED_OBSTACLE : RobotState::FOLLOWING_LINE;
  sendEvent("BACKTRACK_DONE");
}

// Aplica la decision de obstaculo, venga de camara o de botones manuales.
void resolveObstacleSign(CameraSign sign, const char *source) {
  waitingForObstacleDecision = false;
  lastCameraSign = sign;
  blackPatchFirstSeenMs = 0;

  JsonDocument extra;
  extra["camera_sign"] = cameraSignName(lastCameraSign);
  extra["transport"] = cameraTransportName();
  extra["decision_source"] = source;
  sendEvent("OBSTACLE_DECISION_APPLIED", &extra);

  if (lastCameraSign == CameraSign::BLACK_SIGN) {
    finishRunFromCameraSign();
    return;
  }

  obstacleCount++;

  if (lastCameraSign == CameraSign::GREEN_SIGN) {
    markCurrentTraversalPassableGreen();
    bool cleared = drivePastBlackPatch();
    JsonDocument passExtra;
    passExtra["camera_sign"] = cameraSignName(lastCameraSign);
    passExtra["cleared"] = cleared;
    passExtra["from_node"] = currentNode;
    passExtra["orientation"] = orientationName(lastTraversalOrientation);
    passExtra["green_edge"] = true;
    sendEvent("OBSTACLE_ALLOWED", &passExtra);
    robotState = RobotState::FOLLOWING_LINE;
    return;
  }

  // If the camera is unsure, the safest decision is the same as a red sign.
  backtrackFromBlockedObstacle();
}

// Pausa el robot para que el operador decida rojo, verde o negro.
void waitForObstacleDecision() {
  waitingForObstacleDecision = true;
  manualObstacleSignEnabled = false;
  lastCameraSign = CameraSign::NO_SIGN;
  setCameraDiagnostics(0, "waiting_manual_obstacle_decision");
  stopMotors();
  robotState = RobotState::WAITING_OBSTACLE_DECISION;

  JsonDocument extra;
  extra["transport"] = cameraTransportName();
  extra["manual_buttons"] = true;
  extra["return_node"] = currentNode;
  sendEvent("OBSTACLE_WAITING_DECISION", &extra);
}

// Entrada principal para una mancha negra tratada como obstaculo.
void handleObstaclePatch() {
  stopMotors();
  robotState = RobotState::OBSTACLE_CHECK;

  JsonDocument extra;
  extra["transport"] = cameraTransportName();
  extra["manual_override"] = manualObstacleSignEnabled;
  extra["wait_for_decision"] = waitForObstacleDecisionEnabled;
  sendEvent("OBSTACLE_PATCH_DETECTED", &extra);

  if (manualObstacleSignEnabled) {
    CameraSign sign = manualObstacleSign;
    manualObstacleSignEnabled = false;
    manualObstacleSign = CameraSign::NO_SIGN;
    setCameraDiagnostics(100, "manual_obstacle_button");
    resolveObstacleSign(sign, "manual_button");
    return;
  }

  if (waitForObstacleDecisionEnabled) {
    waitForObstacleDecision();
    return;
  }

  resolveObstacleSign(requestObstacleSign(), "camera_auto");
}

// Gestiona un cruce completo: leer salidas, actualizar grafo y elegir giro.
void handleNode(const SensorFrame &frame) {
  if (!runActive) {
    return;
  }

  if (millis() - lastNodeMs < NODE_DEBOUNCE_MS) {
    return;
  }
  lastNodeMs = millis();
  stopMotors();
  robotState = RobotState::INTERSECTION_DETECTED;
  crossingCount++;
  lastIntersectionType = frame.intersectionType;
  uint8_t entrySideOptions = optionsFromFrame(frame, false);

  // Primero se centra el eje de ruedas; solo despues se confirma si hay recto.
  centerOnNode();

  SensorFrame centeredFrame = readSensors();
  storeLastSensorFrame(centeredFrame);
  robotState = RobotState::CLASSIFY_NODE;

  uint8_t centeredOptions = optionsFromFrame(centeredFrame, true);
  uint8_t qtrRelativeOptions = entrySideOptions | centeredOptions;
  if (qtrRelativeOptions == 0) {
    qtrRelativeOptions = DIR_BACK;
  }

  bool returningToKnownNode = returningFromBlockedObstacle &&
                              returnNodeAfterBlockedObstacle >= 0 &&
                              returnNodeAfterBlockedObstacle < nodeCount;
  bool followingKnownPath = navigatingToFrontier &&
                            navigationNextNode >= 0 &&
                            navigationNextNode < nodeCount;
  bool reachedKnownPathTarget = followingKnownPath && navigationNextNode == navigationTargetNode;
  bool qtrLearningEnabled = !returningToKnownNode && (!followingKnownPath || reachedKnownPathTarget);
  lastNodeQtrLearningEnabled = qtrLearningEnabled;

  // El QTR se sigue mandando a la web para depurar, pero en backtracking
  // no puede crear salidas nuevas hasta llegar al nodo objetivo.
  uint8_t relativeOptions = qtrLearningEnabled ? qtrRelativeOptions : DIR_BACK;
  if (!qtrLearningEnabled) {
    int knownNode = returningToKnownNode ? returnNodeAfterBlockedObstacle : navigationNextNode;
    relativeOptions = knownRelativeOptionsForNode(knownNode);
  }

  JsonDocument classifyExtra;
  classifyExtra["entry_options_no_straight"] = relativeExitMaskText(entrySideOptions);
  classifyExtra["centered_options"] = relativeExitMaskText(centeredOptions);
  classifyExtra["straight_confirmed"] = (centeredOptions & DIR_STRAIGHT) != 0;
  classifyExtra["qtr_options"] = relativeExitMaskText(qtrRelativeOptions);
  classifyExtra["qtr_learning_enabled"] = qtrLearningEnabled;
  classifyExtra["relative_options"] = relativeExitMaskText(relativeOptions);
  classifyExtra["center_active_count"] = centeredFrame.centerActiveCount;
  classifyExtra["center_line_seen"] = centeredFrame.lineSeen;
  sendEvent("NODE_EXITS_CLASSIFIED", &classifyExtra);

  if (returningToKnownNode) {
    // Tras una senal roja sabemos que este es el nodo anterior, aunque el QTR sature.
    currentNode = returnNodeAfterBlockedObstacle;
    bool saturatedReturnNode = frame.blackPatch || centeredFrame.blackPatch;
    lastAvailableExits = knownRelativeOptionsForNode(currentNode);

    edgeInProgress = false;
    previousNodeForEdge = -1;
    returningFromBlockedObstacle = false;
    returnNodeAfterBlockedObstacle = -1;

    robotState = RobotState::RETURNING_FROM_BLOCKED_OBSTACLE;
    JsonDocument returnedExtra;
    returnedExtra["node"] = currentNode;
    returnedExtra["blocked_orientation"] = orientationName(blockedObstacleOrientation);
    returnedExtra["saturated_return_node"] = saturatedReturnNode;
    returnedExtra["qtr_options_ignored"] = true;
    returnedExtra["qtr_options"] = relativeExitMaskText(qtrRelativeOptions);
    returnedExtra["relative_options"] = relativeExitMaskText(lastAvailableExits);
    returnedExtra["absolute_options"] = absoluteExitMaskText(nodes[currentNode].options);
    sendEvent("RETURNED_TO_NODE_AFTER_BLOCKED_OBSTACLE", &returnedExtra);
  } else if (followingKnownPath) {
    // Durante backtracking se confia en el grafo, no en salidas nuevas del QTR.
    int expectedNode = navigationNextNode;

    if (edgeInProgress && previousNodeForEdge >= 0 && previousNodeForEdge < nodeCount) {
      connectNodes(previousNodeForEdge, expectedNode, lastTraversalOrientation);
      edgeInProgress = false;
      previousNodeForEdge = -1;
    }

    currentNode = expectedNode;
    int previousNavigationTarget = navigationTargetNode;
    bool reachedNavigationTarget = currentNode == previousNavigationTarget;

    if (reachedNavigationTarget) {
      navigatingToFrontier = false;
      navigationTargetNode = -1;
      navigationNextNode = -1;
      lastAvailableExits = relativeOptions;
      updateNodeOptions(currentNode, relativeOptionsToAbsolute(relativeOptions, robotOrientation));
    } else {
      lastAvailableExits = knownRelativeOptionsForNode(currentNode);
    }

    robotState = RobotState::RETURNING_TO_UNFINISHED_NODE;
    JsonDocument pathExtra;
    pathExtra["node"] = currentNode;
    pathExtra["target_node"] = previousNavigationTarget;
    pathExtra["expected_node"] = expectedNode;
    pathExtra["reached_target"] = reachedNavigationTarget;
    pathExtra["qtr_options_ignored"] = !reachedNavigationTarget;
    pathExtra["qtr_options"] = relativeExitMaskText(qtrRelativeOptions);
    pathExtra["relative_options"] = relativeExitMaskText(lastAvailableExits);
    pathExtra["absolute_options"] = absoluteExitMaskText(nodes[currentNode].options);
    sendEvent("KNOWN_PATH_NODE_REACHED", &pathExtra);
  } else {
    currentNode = resolveCurrentNode(relativeOptions);
  }

  if (currentNode < 0 || currentNode >= nodeCount) {
    return;
  }

  robotState = RobotState::CHOOSE_DIRECTION;
  Turn selected = Turn::BACK;
  int selectedTargetNode = currentNode;
  int selectedNextNode = -1;
  bool selectedKnownPath = false;

  if (navigatingToFrontier && navigationTargetNode >= 0 && currentNode != navigationTargetNode) {
    // Si vamos hacia una frontera, seguimos el camino conocido paso a paso.
    selectedTargetNode = navigationTargetNode;
    if (!chooseKnownPathTurnToTarget(currentNode, navigationTargetNode, selected, selectedNextNode)) {
      stopMotors();
      runActive = false;
      navigatingToFrontier = false;
      robotState = RobotState::ERROR_STATE;

      JsonDocument pathError;
      pathError["node"] = currentNode;
      pathError["target_node"] = navigationTargetNode;
      pathError["reason"] = "known_path_lost";
      sendEvent("KNOWN_PATH_LOST", &pathError);
      return;
    }
    selectedKnownPath = true;
  } else if (!chooseNavigationTurn(currentNode, selected, selectedTargetNode, selectedNextNode, selectedKnownPath)) {
    stopMotors();
    runActive = false;
    navigatingToFrontier = false;
    robotState = RobotState::ERROR_STATE;

    JsonDocument exhaustedExtra;
    exhaustedExtra["node"] = currentNode;
    exhaustedExtra["node_count"] = nodeCount;
    exhaustedExtra["edge_count"] = edgeCount;
    exhaustedExtra["reason"] = "no_unexplored_reachable_edge";
    sendEvent("EXPLORATION_EXHAUSTED", &exhaustedExtra);
    return;
  }

  navigatingToFrontier = selectedKnownPath;
  navigationTargetNode = selectedKnownPath ? selectedTargetNode : -1;
  navigationNextNode = selectedKnownPath ? selectedNextNode : -1;

  RobotOrientation selectedOrientation = rotateOrientation(robotOrientation, selected);

  JsonDocument extra;
  extra["node"] = currentNode;
  extra["node_x"] = nodes[currentNode].x;
  extra["node_y"] = nodes[currentNode].y;
  extra["intersection_type"] = intersectionTypeName(lastIntersectionType);
  extra["relative_options"] = relativeExitMaskText(lastAvailableExits);
  extra["absolute_options"] = absoluteExitMaskText(nodes[currentNode].options);
  extra["turn"] = turnName(selected);
  extra["exit_orientation"] = orientationName(selectedOrientation);
  extra["navigation_mode"] = selectedKnownPath ? "KNOWN_PATH_TO_FRONTIER" : "EXPLORE_UNTRIED_EDGE";
  extra["target_node"] = selectedTargetNode;
  extra["next_node"] = selectedNextNode;
  sendEvent("EDGE_SELECTED", &extra);

  if (pauseBeforeTurnEnabled) {
    // Modo de pruebas: deja ver la decision antes de mover el robot.
    pendingTurn = selected;
    pendingExitOrientation = selectedOrientation;
    waitingForTurnConfirmation = true;
    robotState = RobotState::WAITING_TURN_CONFIRMATION;
    stopMotors();

    JsonDocument waitExtra;
    waitExtra["node"] = currentNode;
    waitExtra["turn"] = turnName(pendingTurn);
    waitExtra["exit_orientation"] = orientationName(pendingExitOrientation);
    waitExtra["relative_options"] = relativeExitMaskText(lastAvailableExits);
    waitExtra["absolute_options"] = absoluteExitMaskText(nodes[currentNode].options);
    waitExtra["navigation_mode"] = selectedKnownPath ? "KNOWN_PATH_TO_FRONTIER" : "EXPLORE_UNTRIED_EDGE";
    waitExtra["target_node"] = selectedTargetNode;
    waitExtra["next_node"] = selectedNextNode;
    sendEvent("TURN_WAITING_CONFIRMATION", &waitExtra);
    return;
  }

  beginTraversal(selected);
  performTurn(selected);
  robotState = RobotState::FOLLOWING_LINE;
}

bool tryBridgeWhiteGap() {
  driveFor(basePwm, basePwm, GAP_BRIDGE_MS);
  SensorFrame frame = readSensors();
  return frame.lineSeen;
}

// Busca la linea usando la ultima direccion fiable que recordamos.
bool searchForLineRecovery() {
  isSearching = true;
  searchStartMs = millis();

  while (millis() - searchStartMs < static_cast<uint32_t>(searchTimeoutMs)) {
    serviceNetwork();

    if (lastKnownDirection < 0) {
      setMotors(-searchForwardPwm, searchTurnPwm);
    } else if (lastKnownDirection > 0) {
      setMotors(searchTurnPwm, -searchForwardPwm);
    } else {
      setMotors(searchTurnPwm, searchTurnPwm);
    }

    SensorFrame frame = readSensors();
    updateAdaptiveThreshold(frame);
    if (frame.lineSeen) {
      updateLineMemory(frame);
      stopMotors();
      isSearching = false;
      return true;
    }

    delay(8);
  }

  stopMotors();
  return false;
}

// Decide si se intenta salvar un hueco o buscar la linea.
void handleLineLost() {
  stopMotors();
  robotState = RobotState::OBSTACLE_CHECK;
  sendEvent("LINE_LOST");

  if (searchForLineRecovery()) {
    JsonDocument extra;
    extra["recovered"] = true;
    sendEvent("LINE_RECOVERED", &extra);
    robotState = RobotState::FOLLOWING_LINE;
    return;
  }

  isSearching = false;

  if (!runActive) {
    localMotorsEnabled = false;
    robotState = RobotState::WAITING_START;
    sendEvent("LOCAL_STOP_AFTER_SEARCH");
    return;
  }

  obstacleCount++;
  lastCameraSign = CameraSign::NO_SIGN;
  setCameraDiagnostics(0, "line_lost_after_search");
  JsonDocument extra;
  extra["camera_sign"] = cameraSignName(lastCameraSign);
  extra["blocked_turn"] = turnName(lastTurn);
  sendEvent("OBSTACLE_DETECTED", &extra);
  backtrackFromBlockedObstacle();
}

// PID simple de seguimiento sobre la posicion calculada por los QTR.
void followLine(const SensorFrame &frame) {
  float error = (frame.position - 3500) / 1000.0f;
  int correction = static_cast<int>(error * positionGain);
  int left = constrain(basePwm + correction, 0, PWM_MAX);
  int right = constrain(basePwm - correction, 0, PWM_MAX);
  setMotors(left, right);
}

// Estado periodico para backend y dashboard.
void sendTelemetry() {
  if (millis() - lastTelemetryMs < TELEMETRY_INTERVAL_MS) {
    return;
  }
  lastTelemetryMs = millis();
  JsonDocument doc;
  doc["type"] = "telemetry";
  doc["state"] = stateName(robotState);
  doc["algorithm"] = algorithmName();
  doc["speed_limit"] = speedLimitPercent;
  doc["current_node"] = currentNode;
  doc["obstacle_count"] = obstacleCount;
  doc["line_position"] = lastLinePosition;
  doc["camera_sign"] = cameraSignName(lastCameraSign);
  doc["camera_confidence"] = lastCameraConfidence;
  doc["camera_reason"] = lastCameraReason;
  doc["threshold"] = currentLineThresholdRaw;
  doc["elapsed_ms"] = motorsAllowed() ? millis() - runStartMs : 0;
  addLiveDiagnostics(doc);
  String payload;
  serializeJson(doc, payload);
  if (wsConnected) {
    webSocket.sendTXT(payload);
  }
}

// Traduce comandos del backend a acciones reales del robot.
void applyCommand(const JsonDocument &doc) {
  const char *type = doc["type"] | "";
  if (strcmp(type, "START_RUN") == 0) {
    runId = doc["run_id"] | 0;
    resetMaze();
    runStartMs = millis();
    runActive = true;
    robotState = RobotState::FOLLOWING_LINE;
    sendEvent("RUN_STARTED");
  } else if (strcmp(type, "STOP_RUN") == 0) {
    runActive = false;
    waitingForTurnConfirmation = false;
    waitingForObstacleDecision = false;
    returningFromBlockedObstacle = false;
    navigatingToFrontier = false;
    navigationTargetNode = -1;
    navigationNextNode = -1;
    stopMotors();
    robotState = RobotState::IDLE;
    sendEvent("RUN_STOPPED");
  } else if (strcmp(type, "RESET_RUN") == 0) {
    runActive = false;
    waitingForTurnConfirmation = false;
    waitingForObstacleDecision = false;
    returningFromBlockedObstacle = false;
    navigatingToFrontier = false;
    navigationTargetNode = -1;
    navigationNextNode = -1;
    stopMotors();
    resetMaze();
    robotState = RobotState::WAITING_START;
    sendEvent("RUN_RESET");
  } else if (strcmp(type, "CALIBRATE_QTR") == 0) {
    calibrateQtr();
  } else if (strcmp(type, "SET_ALGORITHM") == 0) {
    const char *value = doc["algorithm"] | "DFS";
    algorithm = strcmp(value, "BFS") == 0 ? Algorithm::BFS : Algorithm::DFS;
    sendEvent("ALGORITHM_SET");
  } else if (strcmp(type, "SET_SPEED_LIMIT") == 0) {
    speedLimitPercent = constrain(doc["percent"] | 100, 1, 100);
    sendEvent("SPEED_LIMIT_SET");
  } else if (strcmp(type, "SET_DRIVE_POWER") == 0) {
    int requestedPercent = constrain(doc["percent"] | currentDrivePowerPercent(), 5, 100);
    int requestedBase = constrain((requestedPercent * PWM_MAX) / 100, 0, PWM_MAX);
    basePwm = requestedBase;
    turnPwm = constrain((requestedBase * DEFAULT_TURN_PWM) / max(1, DEFAULT_BASE_PWM), 0, PWM_MAX);
    searchTurnPwm = constrain((requestedBase * DEFAULT_SEARCH_TURN_PWM) / max(1, DEFAULT_BASE_PWM), 0, PWM_MAX);
    JsonDocument extra;
    extra["percent"] = currentDrivePowerPercent();
    extra["base_pwm"] = basePwm;
    extra["turn_pwm"] = turnPwm;
    extra["search_turn_pwm"] = searchTurnPwm;
    sendEvent("DRIVE_POWER_SET", &extra);
  } else if (strcmp(type, "SET_TURN_PAUSE") == 0) {
    pauseBeforeTurnEnabled = doc["enabled"] | true;
    JsonDocument extra;
    extra["enabled"] = pauseBeforeTurnEnabled;
    sendEvent("TURN_PAUSE_SET", &extra);
  } else if (strcmp(type, "CONTINUE_TURN") == 0) {
    executePendingTurn();
  } else if (strcmp(type, "SET_OBSTACLE_HANDLING") == 0) {
    obstacleHandlingEnabled = doc["enabled"] | true;
    if (!obstacleHandlingEnabled) {
      waitingForObstacleDecision = false;
    }
    JsonDocument extra;
    extra["enabled"] = obstacleHandlingEnabled;
    sendEvent("OBSTACLE_HANDLING_SET", &extra);
  } else if (strcmp(type, "SET_OBSTACLE_DECISION_WAIT") == 0) {
    waitForObstacleDecisionEnabled = doc["enabled"] | true;
    JsonDocument extra;
    extra["enabled"] = waitForObstacleDecisionEnabled;
    sendEvent("OBSTACLE_DECISION_WAIT_SET", &extra);

    if (!waitForObstacleDecisionEnabled && waitingForObstacleDecision) {
      resolveObstacleSign(requestCameraSign(), "camera_auto_after_wait");
    }
  } else if (strcmp(type, "RESOLVE_OBSTACLE") == 0) {
    const char *rawSign = doc["sign"] | "NO_SIGN";
    String requestedSign = rawSign;
    requestedSign.trim();
    requestedSign.toUpperCase();
    obstacleHandlingEnabled = true;

    if (requestedSign == "CAMERA" || requestedSign == "CAMERA_AUTO") {
      manualObstacleSignEnabled = false;
      manualObstacleSign = CameraSign::NO_SIGN;

      if (waitingForObstacleDecision) {
        resolveObstacleSign(requestCameraSign(), "camera_button");
      } else {
        waitForObstacleDecisionEnabled = false;
        JsonDocument extra;
        extra["enabled"] = false;
        sendEvent("OBSTACLE_CAMERA_AUTO_SET", &extra);
      }
      return;
    }

    CameraSign parsedSign = CameraSign::NO_SIGN;
    if (!parseCameraSign(requestedSign, parsedSign) || parsedSign == CameraSign::TIMEOUT) {
      JsonDocument extra;
      extra["sign"] = rawSign;
      sendEvent("OBSTACLE_DECISION_INVALID", &extra);
      return;
    }

    waitForObstacleDecisionEnabled = true;
    setCameraDiagnostics(100, "manual_obstacle_button");

    if (waitingForObstacleDecision) {
      resolveObstacleSign(parsedSign, "manual_button");
    } else {
      manualObstacleSignEnabled = true;
      manualObstacleSign = parsedSign;
      JsonDocument extra;
      extra["sign"] = cameraSignName(manualObstacleSign);
      sendEvent("OBSTACLE_DECISION_QUEUED", &extra);
    }
  } else if (strcmp(type, "SET_OBSTACLE_SIGN_OVERRIDE") == 0) {
    bool enabled = doc["enabled"] | false;
    const char *rawSign = doc["sign"] | "NO_SIGN";
    CameraSign parsedSign = CameraSign::NO_SIGN;

    if (!parseCameraSign(rawSign, parsedSign)) {
      JsonDocument extra;
      extra["sign"] = rawSign;
      sendEvent("OBSTACLE_OVERRIDE_INVALID", &extra);
      return;
    }

    manualObstacleSignEnabled = enabled;
    manualObstacleSign = parsedSign;

    JsonDocument extra;
    extra["enabled"] = manualObstacleSignEnabled;
    extra["sign"] = cameraSignName(manualObstacleSign);
    sendEvent("OBSTACLE_OVERRIDE_SET", &extra);

    if (manualObstacleSignEnabled && waitingForObstacleDecision) {
      CameraSign sign = manualObstacleSign;
      manualObstacleSignEnabled = false;
      manualObstacleSign = CameraSign::NO_SIGN;
      setCameraDiagnostics(100, "manual_obstacle_override");
      resolveObstacleSign(sign, "manual_override");
    }
  } else if (strcmp(type, "SET_OBSTACLE_EDGE") == 0) {
    JsonDocument extra;
    extra["edge_id"] = doc["edge_id"] | "";
    sendEvent("OBSTACLE_EDGE_SELECTED", &extra);
  }
}

// Recibe comandos y confirma conexion con el backend por WebSocket.
void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    wsConnected = true;
    sendEvent("ROBOT_READY");
  } else if (type == WStype_DISCONNECTED) {
    wsConnected = false;
  } else if (type == WStype_TEXT) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (!error) {
      applyCommand(doc);
    }
  }
}

// Texto compacto para la pagina local de diagnostico QTR.
String buildLineMap() {
  String mapText;
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    mapText += lastLineDetected[i] ? "[X] " : "[ ] ";
  }
  return mapText;
}

String buildSensorValues() {
  String valuesText;
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    if (i > 0) {
      valuesText += ", ";
    }
    valuesText += String(lastSensorValues[i]);
  }
  return valuesText;
}

String buildQtrPinListJson() {
  String json = "[";
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    if (i > 0) {
      json += ",";
    }
    json += String(QTR_PINS[i]);
  }
  json += "]";
  return json;
}

String buildQtrLevelListJson(const uint8_t levels[QTR_COUNT]) {
  String json = "[";
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    if (i > 0) {
      json += ",";
    }
    json += String(levels[i]);
  }
  json += "]";
  return json;
}

String buildQtrRawListJson(const uint16_t values[QTR_COUNT]) {
  String json = "[";
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    if (i > 0) {
      json += ",";
    }
    json += String(values[i]);
  }
  json += "]";
  return json;
}

// Lectura digital auxiliar para comprobar cableado de los QTR.
void readQtrDigitalLevels(uint8_t levels[QTR_COUNT]) {
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    pinMode(QTR_PINS[i], INPUT);
  }
  delayMicroseconds(80);

  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    levels[i] = digitalRead(QTR_PINS[i]);
  }
}

// Restaura parametros de prueba sin cambiar constantes de compilacion.
void resetDefaults() {
  basePwm = DEFAULT_BASE_PWM;
  turnPwm = DEFAULT_TURN_PWM;
  searchTurnPwm = DEFAULT_SEARCH_TURN_PWM;
  searchForwardPwm = DEFAULT_SEARCH_FORWARD_PWM;
  searchTimeoutMs = DEFAULT_SEARCH_TIMEOUT_MS;
  loopDelayMs = DEFAULT_LOOP_DELAY_MS;
  msPerCm = DEFAULT_MS_PER_CM;
  msTurn90 = DEFAULT_MS_TURN_90;
  msTurn180 = DEFAULT_MS_TURN_180;
  lineReacquireMs = DEFAULT_LINE_REACQUIRE_MS;
  backTurnExtraMs = DEFAULT_BACK_TURN_EXTRA_MS;
  speedLimitPercent = DEFAULT_SPEED_LIMIT_PERCENT;
  targetLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
  currentLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
  minAdaptiveThresholdRaw = DEFAULT_MIN_ADAPTIVE_THRESHOLD_RAW;
  thresholdStepRaw = DEFAULT_THRESHOLD_STEP_RAW;
  positionWindow = DEFAULT_POSITION_WINDOW;
  positionGain = DEFAULT_POSITION_GAIN;
  localMotorsEnabled = DEFAULT_LOCAL_MOTORS_ENABLED;
  obstacleHandlingEnabled = DEFAULT_OBSTACLE_HANDLING_ENABLED;
  waitForObstacleDecisionEnabled = true;
  waitingForObstacleDecision = false;
  returningFromBlockedObstacle = false;
  returnNodeAfterBlockedObstacle = -1;
  manualObstacleSignEnabled = false;
  manualObstacleSign = CameraSign::NO_SIGN;
  resetMaze();
  stopMotors();
}

// Pagina local minima; el panel principal vive en el frontend.
String buildControlPage() {
  return "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>RoboBet AP</title><style>html,body{margin:0;height:100%;background:#ffffff;}</style></head><body></body></html>";
}

void handleRoot() {
  localServer.sendHeader("Cache-Control", "no-store");
  localServer.send(200, "text/html", buildControlPage());
}

// Endpoint local para diagnosticar si cada sensor carga y descarga.
void handleQtrDebug() {
  if (runActive) {
    localServer.send(409, "application/json", "{\"error\":\"stop_robot_before_qtr_debug\"}");
    return;
  }

  stopMotors();

  uint8_t idleLevels[QTR_COUNT];
  uint8_t chargedLevels[QTR_COUNT];
  uint8_t releasedLevels[QTR_COUNT];
  uint16_t rawValues[QTR_COUNT];

  readQtrDigitalLevels(idleLevels);

  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    pinMode(QTR_PINS[i], OUTPUT);
    digitalWrite(QTR_PINS[i], HIGH);
  }
  delayMicroseconds(20);
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    chargedLevels[i] = digitalRead(QTR_PINS[i]);
  }

  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    pinMode(QTR_PINS[i], INPUT);
  }
  delayMicroseconds(50);
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    releasedLevels[i] = digitalRead(QTR_PINS[i]);
  }

  readQtrRaw(rawValues);

  String json = "{";
  json += "\"pins\":" + buildQtrPinListJson() + ",";
  json += "\"idle_levels\":" + buildQtrLevelListJson(idleLevels) + ",";
  json += "\"charged_high_levels\":" + buildQtrLevelListJson(chargedLevels) + ",";
  json += "\"released_after_50us_levels\":" + buildQtrLevelListJson(releasedLevels) + ",";
  json += "\"raw_us\":" + buildQtrRawListJson(rawValues) + ",";
  json += "\"timeout_us\":" + String(QTR_TIMEOUT_US) + ",";
  json += "\"note\":\"idle should change with wiring, charged should be all 1, raw below timeout on reflective white\"";
  json += "}";

  localServer.send(200, "application/json", json);
}

// Activa AP propio y, si puede, tambien se une al WiFi del proyecto.
void connectWiFi() {
  Serial.println("[BOOT] Configurando modo AP+STA");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("[BOOT] SSID AP: ");
  Serial.println(AP_SSID);
  Serial.print("[BOOT] IP AP: ");
  Serial.println(WiFi.softAPIP());

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[BOOT] Conectando STA a ");
  Serial.println(WIFI_SSID);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    localServer.handleClient();
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[BOOT] IP STA: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[BOOT] STA sin conexion; el AP local sigue activo");
  }
}

// Canal de control principal con el backend.
void setupWebSocket() {
  webSocket.begin(ROBOBET_SERVER_HOST, ROBOBET_SERVER_PORT, "/ws/robot");
  webSocket.onEvent(onWebSocketEvent);
  webSocket.setReconnectInterval(3000);
}

// Servidor local para pruebas rapidas desde 192.168.4.1.
void setupLocalUi() {
  localServer.on("/", handleRoot);
  localServer.on("/qtr-debug", handleQtrDebug);
  localServer.begin();
}

// Prepara el transporte de camara que este activado por macros.
void setupCameraTransport() {
#if CAMERA_TRANSPORT_UART
  CameraSerial.begin(CAMERA_UART_BAUD, SERIAL_8N1, CAMERA_UART_RX, CAMERA_UART_TX);
  Serial.print("[BOOT] Camara UART activa RX=");
  Serial.print(CAMERA_UART_RX);
  Serial.print(" TX=");
  Serial.println(CAMERA_UART_TX);
#endif

#if CAMERA_TRANSPORT_HTTP
  Serial.print("[BOOT] Camara HTTP configurada en http://");
  Serial.print(CAMERA_HTTP_HOST);
  Serial.print(":");
  Serial.print(CAMERA_HTTP_PORT);
  Serial.println("/detect");
#endif
}

// Inicializa motores, red, camara y estado base.
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[BOOT] Inicio firmware robot");
  setupMotors();
  Serial.println("[BOOT] Motores configurados");
  resetDefaults();
  Serial.println("[BOOT] Defaults cargados");
  resetMaze();
  Serial.println("[BOOT] Estado del laberinto reiniciado");
  connectWiFi();
  setupCameraTransport();
  setupLocalUi();
  setupWebSocket();
  Serial.println("[BOOT] WebUI local activa en http://192.168.4.1");
  Serial.print("[BOOT] WebSocket backend ws://");
  Serial.print(ROBOBET_SERVER_HOST);
  Serial.print(":");
  Serial.print(ROBOBET_SERVER_PORT);
  Serial.println("/ws/robot");
  robotState = RobotState::WAITING_START;
  Serial.println("[BOOT] Robot en WAITING_START");
}

// Bucle principal: red, sensores, obstaculos, cruces y seguimiento.
void loop() {
  serviceNetwork();
  sendTelemetry();

  if (millis() - lastLoopMs < static_cast<uint32_t>(loopDelayMs)) {
    return;
  }
  lastLoopMs = millis();

  SensorFrame frame = readSensors();
  storeLastSensorFrame(frame);

  if (!motorsAllowed()) {
    stopMotors();
    debugLog("[STATE] Motores desactivados, esperando activacion local");
    delay(5);
    return;
  }

  if (waitingForTurnConfirmation) {
    robotState = RobotState::WAITING_TURN_CONFIRMATION;
    stopMotors();
    debugLog("[STATE] Esperando confirmacion de giro");
    delay(5);
    return;
  }

  if (waitingForObstacleDecision) {
    robotState = RobotState::WAITING_OBSTACLE_DECISION;
    stopMotors();
    debugLog("[STATE] Esperando decision manual de obstaculo");
    delay(5);
    return;
  }

  if (runActive) {
    if (returningFromBlockedObstacle) {
      robotState = RobotState::RETURNING_FROM_BLOCKED_OBSTACLE;
    } else if (navigatingToFrontier) {
      robotState = RobotState::RETURNING_TO_UNFINISHED_NODE;
    } else {
      robotState = RobotState::FOLLOWING_LINE;
    }
  }

  updateAdaptiveThreshold(frame);
  updateLineMemory(frame);

  if (frame.blackPatch && runActive && !returningFromBlockedObstacle) {
    if (currentTraversalIsKnownGreen()) {
      debugLog("[STATE] Obstaculo verde conocido, pasando recto");
      bool cleared = drivePastBlackPatch();

      JsonDocument passExtra;
      passExtra["cleared"] = cleared;
      passExtra["known_green_edge"] = true;
      passExtra["from_node"] = currentNode;
      passExtra["orientation"] = orientationName(lastTraversalOrientation);
      passExtra["green_edge"] = true;
      sendEvent("KNOWN_GREEN_OBSTACLE_PASSED", &passExtra);
      return;
    }

    stopMotors();
    if (!blackPatchConfirmed(frame)) {
      debugLog("[STATE] Marca negra detectada, confirmando");
      return;
    }

    debugLog("[STATE] Marca negra confirmada");
    handleObstaclePatch();
    return;
  }
  blackPatchFirstSeenMs = 0;

  if (!frame.lineSeen) {
    debugLog("[STATE] Linea perdida");
    handleLineLost();
    return;
  }

  SensorFrame nodeFrame = frame;
  promoteBacktrackingNodeCandidate(nodeFrame);

  if (runActive && nodeFrame.intersection) {
    if (intersectionFirstSeenMs == 0) {
      intersectionFirstSeenMs = millis();
      lastIntersectionType = nodeFrame.intersectionType;
      debugLog("[STATE] Interseccion detectada, confirmando");
      holdNodeCandidateDuringConfirmation(nodeFrame);
      return;
    }

    if (millis() - intersectionFirstSeenMs < INTERSECTION_CONFIRM_MS) {
      holdNodeCandidateDuringConfirmation(nodeFrame);
      return;
    }

    debugLog("[STATE] Interseccion confirmada");
    handleNode(nodeFrame);
    intersectionFirstSeenMs = 0;
    return;
  }
  intersectionFirstSeenMs = 0;

  if (returningFromBlockedObstacle) {
    robotState = RobotState::RETURNING_FROM_BLOCKED_OBSTACLE;
    debugLog("[STATE] Volviendo al nodo bloqueado");
  } else if (navigatingToFrontier) {
    robotState = RobotState::RETURNING_TO_UNFINISHED_NODE;
    debugLog("[STATE] Navegando a nodo pendiente");
  } else {
    robotState = RobotState::FOLLOWING_LINE;
    debugLog("[STATE] Siguiendo linea");
  }
  followLine(frame);
}
