#include <Arduino.h>
#include <ArduinoJson.h>
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
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 500;
static constexpr uint32_t NODE_DEBOUNCE_MS = 700;
static constexpr uint32_t GAP_BRIDGE_MS = 420;
static constexpr uint32_t TURN_MIN_MS = 240;
static constexpr uint32_t TURN_TIMEOUT_MS = 1500;
static constexpr bool INVERT_LEFT_MOTOR = true;
static constexpr bool INVERT_RIGHT_MOTOR = true;

static constexpr int DEFAULT_BASE_PWM = 28;
static constexpr int DEFAULT_TURN_PWM = 35;
static constexpr int DEFAULT_SEARCH_TURN_PWM = 35;
static constexpr int DEFAULT_SEARCH_FORWARD_PWM = 0;
static constexpr int DEFAULT_SEARCH_TIMEOUT_MS = 800;
static constexpr int DEFAULT_LOOP_DELAY_MS = 80;
static constexpr int DEFAULT_SPEED_LIMIT_PERCENT = 100;
static constexpr uint16_t DEFAULT_TARGET_LINE_THRESHOLD_RAW = 2500;
static constexpr uint16_t DEFAULT_MIN_ADAPTIVE_THRESHOLD_RAW = 1700;
static constexpr uint16_t DEFAULT_THRESHOLD_STEP_RAW = 25;
static constexpr int DEFAULT_POSITION_WINDOW = 2;
static constexpr int DEFAULT_POSITION_GAIN = 10;
static constexpr bool DEFAULT_LOCAL_MOTORS_ENABLED = false;

enum class RobotState {
  IDLE,
  CALIBRATING,
  WAITING_START,
  FOLLOWING_LINE,
  NODE_DETECTED,
  SELECTING_EDGE,
  OBSTACLE_CHECK,
  BACKTRACKING,
  FINISH_CHECK,
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

enum DirectionBit : uint8_t {
  DIR_LEFT = 0b001,
  DIR_STRAIGHT = 0b010,
  DIR_RIGHT = 0b100
};

struct SensorFrame {
  uint16_t raw[QTR_COUNT]{};
  uint16_t normalized[QTR_COUNT]{};
  uint8_t activeCount = 0;
  bool left = false;
  bool center = false;
  bool right = false;
  bool lineSeen = false;
  bool intersection = false;
  int position = 3500;
};

struct NodeRecord {
  uint8_t options = 0;
  uint8_t tried = 0;
  uint8_t blocked = 0;
};

static constexpr uint8_t MAX_NODES = 64;
NodeRecord nodes[MAX_NODES];
uint8_t nodeCount = 0;
int currentNode = -1;
Turn lastTurn = Turn::STRAIGHT;

RobotState robotState = RobotState::WAITING_START;
Algorithm algorithm = Algorithm::DFS;
WebSocketsClient webSocket;
WebServer localServer(80);

uint16_t qtrMin[QTR_COUNT];
uint16_t qtrMax[QTR_COUNT];
bool qtrCalibrated = false;
bool runActive = false;
bool wsConnected = false;
bool localMotorsEnabled = DEFAULT_LOCAL_MOTORS_ENABLED;
bool isSearching = false;
int speedLimitPercent = DEFAULT_SPEED_LIMIT_PERCENT;
int runId = 0;
uint32_t runStartMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastNodeMs = 0;
uint32_t lastLoopMs = 0;
uint32_t searchStartMs = 0;
int obstacleCount = 0;
int lastLinePosition = 3500;
int lastKnownDirection = 0;
float lastKnownPosition = 3.5f;
uint16_t lastSensorValues[QTR_COUNT] = {0};
bool lastLineDetected[QTR_COUNT] = {false};

int basePwm = DEFAULT_BASE_PWM;
int turnPwm = DEFAULT_TURN_PWM;
int searchTurnPwm = DEFAULT_SEARCH_TURN_PWM;
int searchForwardPwm = DEFAULT_SEARCH_FORWARD_PWM;
int searchTimeoutMs = DEFAULT_SEARCH_TIMEOUT_MS;
int loopDelayMs = DEFAULT_LOOP_DELAY_MS;
int positionWindow = DEFAULT_POSITION_WINDOW;
int positionGain = DEFAULT_POSITION_GAIN;
uint16_t targetLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
uint16_t currentLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
uint16_t minAdaptiveThresholdRaw = DEFAULT_MIN_ADAPTIVE_THRESHOLD_RAW;
uint16_t thresholdStepRaw = DEFAULT_THRESHOLD_STEP_RAW;
String lastDebugMessage;

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
    case RobotState::NODE_DETECTED: return "NODE_DETECTED";
    case RobotState::SELECTING_EDGE: return "SELECTING_EDGE";
    case RobotState::OBSTACLE_CHECK: return "OBSTACLE_CHECK";
    case RobotState::BACKTRACKING: return "BACKTRACKING";
    case RobotState::FINISH_CHECK: return "FINISH_CHECK";
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

uint8_t turnToBit(Turn turn) {
  switch (turn) {
    case Turn::LEFT: return DIR_LEFT;
    case Turn::STRAIGHT: return DIR_STRAIGHT;
    case Turn::RIGHT: return DIR_RIGHT;
    case Turn::BACK: return 0;
  }
  return 0;
}

bool motorsAllowed() {
  return runActive || localMotorsEnabled;
}

void serviceNetwork() {
  localServer.handleClient();
}

void sendEvent(const char *event, JsonDocument *extra = nullptr) {
  JsonDocument doc;
  doc["event"] = event;
  doc["state"] = stateName(robotState);
  doc["algorithm"] = algorithmName();
  doc["speed_limit"] = speedLimitPercent;
  doc["current_node"] = currentNode;
  doc["obstacle_count"] = obstacleCount;
  doc["line_position"] = lastLinePosition;
  doc["threshold"] = currentLineThresholdRaw;
  doc["elapsed_ms"] = motorsAllowed() ? millis() - runStartMs : 0;
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
  setMotorRaw(PWM_LEFT_CHANNEL, MOTOR_LEFT_IN1, MOTOR_LEFT_IN2, applySpeedLimit(left), INVERT_LEFT_MOTOR);
  setMotorRaw(PWM_RIGHT_CHANNEL, MOTOR_RIGHT_IN1, MOTOR_RIGHT_IN2, applySpeedLimit(right), INVERT_RIGHT_MOTOR);
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
    }
  }

  frame.left = frame.raw[0] > currentLineThresholdRaw || frame.raw[1] > currentLineThresholdRaw || frame.raw[2] > currentLineThresholdRaw;
  frame.center = frame.raw[3] > currentLineThresholdRaw || frame.raw[4] > currentLineThresholdRaw;
  frame.right = frame.raw[5] > currentLineThresholdRaw || frame.raw[6] > currentLineThresholdRaw || frame.raw[7] > currentLineThresholdRaw;
  frame.lineSeen = frame.activeCount > 0;
  frame.intersection = frame.activeCount >= INTERSECTION_ACTIVE_COUNT || (frame.left && frame.center && frame.right);

  if (!frame.lineSeen) {
    frame.position = lastLinePosition;
    return frame;
  }

  int lastIndex = constrain(lastLinePosition / 1000, 0, static_cast<int>(QTR_COUNT) - 1);
  int nearestIndex = -1;
  int nearestDistance = 999;

  for (uint8_t i = 0; i < QTR_COUNT; i++) {
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
    frame.position = lastLinePosition;
    return frame;
  }

  int startIndex = max(0, nearestIndex - positionWindow);
  int endIndex = min(static_cast<int>(QTR_COUNT) - 1, nearestIndex + positionWindow);
  uint32_t weighted = 0;
  uint32_t total = 0;

  for (int i = startIndex; i <= endIndex; i++) {
    if (frame.raw[i] <= currentLineThresholdRaw) {
      continue;
    }
    weighted += static_cast<uint32_t>(frame.raw[i]) * i * 1000;
    total += frame.raw[i];
  }

  if (total > 0) {
    frame.position = weighted / total;
    lastLinePosition = frame.position;
    lastKnownPosition = static_cast<float>(frame.position) / 1000.0f;
  } else {
    frame.position = lastLinePosition;
  }

  return frame;
}

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

void updateLineMemory(const SensorFrame &frame) {
  if (!frame.lineSeen) {
    return;
  }

  lastKnownDirection = getLineDirection(frame);
  lastKnownPosition = static_cast<float>(frame.position) / 1000.0f;
}

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

uint8_t optionsFromFrame(const SensorFrame &frame) {
  uint8_t options = 0;
  if (frame.left) {
    options |= DIR_LEFT;
  }
  if (frame.center) {
    options |= DIR_STRAIGHT;
  }
  if (frame.right) {
    options |= DIR_RIGHT;
  }
  return options;
}

Turn chooseDfsTurn(NodeRecord &node) {
  const Turn priority[] = {Turn::LEFT, Turn::STRAIGHT, Turn::RIGHT};
  for (Turn turn : priority) {
    uint8_t bit = turnToBit(turn);
    if ((node.options & bit) && !(node.tried & bit) && !(node.blocked & bit)) {
      node.tried |= bit;
      return turn;
    }
  }
  return Turn::BACK;
}

Turn chooseBfsTurn(NodeRecord &node) {
  const Turn priority[] = {Turn::STRAIGHT, Turn::LEFT, Turn::RIGHT};
  for (Turn turn : priority) {
    uint8_t bit = turnToBit(turn);
    if ((node.options & bit) && !(node.tried & bit) && !(node.blocked & bit)) {
      node.tried |= bit;
      return turn;
    }
  }
  return Turn::BACK;
}

void resetMaze() {
  for (NodeRecord &node : nodes) {
    node = NodeRecord{};
  }
  nodeCount = 0;
  currentNode = -1;
  obstacleCount = 0;
  lastTurn = Turn::STRAIGHT;
  lastKnownDirection = 0;
  lastKnownPosition = 3.5f;
  lastLinePosition = 3500;
  currentLineThresholdRaw = targetLineThresholdRaw;
  isSearching = false;
}

int createNode(uint8_t options) {
  if (nodeCount >= MAX_NODES) {
    robotState = RobotState::ERROR_STATE;
    sendEvent("GRAPH_FULL");
    return currentNode;
  }
  nodes[nodeCount].options = options;
  nodes[nodeCount].tried = 0;
  nodes[nodeCount].blocked = 0;
  return nodeCount++;
}

bool lineCentered() {
  SensorFrame frame = readSensors();
  return frame.center && frame.lineSeen && !frame.intersection;
}

void driveFor(int left, int right, uint32_t durationMs) {
  uint32_t start = millis();
  setMotors(left, right);
  while (millis() - start < durationMs) {
    serviceNetwork();
    delay(5);
  }
  stopMotors();
}

void performTurn(Turn turn) {
  if (turn == Turn::STRAIGHT) {
    driveFor(basePwm, basePwm, 220);
    return;
  }

  uint32_t start = millis();
  if (turn == Turn::LEFT) {
    setMotors(-turnPwm, turnPwm);
  } else if (turn == Turn::RIGHT) {
    setMotors(turnPwm, -turnPwm);
  } else {
    setMotors(turnPwm, -turnPwm);
  }

  while (millis() - start < TURN_TIMEOUT_MS) {
    serviceNetwork();
    bool canStop = millis() - start > (turn == Turn::BACK ? TURN_MIN_MS + 280 : TURN_MIN_MS);
    if (canStop && lineCentered()) {
      break;
    }
    delay(8);
  }
  stopMotors();
  driveFor(basePwm, basePwm, 120);
}

void markLastTurnBlocked() {
  if (currentNode < 0 || currentNode >= nodeCount) {
    return;
  }
  uint8_t bit = turnToBit(lastTurn);
  if (bit) {
    nodes[currentNode].blocked |= bit;
  }
}

void handleNode(const SensorFrame &frame) {
  if (!runActive) {
    return;
  }

  if (millis() - lastNodeMs < NODE_DEBOUNCE_MS) {
    return;
  }
  lastNodeMs = millis();
  stopMotors();
  robotState = RobotState::NODE_DETECTED;

  uint8_t options = optionsFromFrame(frame);
  if (options == 0) {
    options = DIR_STRAIGHT;
  }
  currentNode = createNode(options);
  if (currentNode < 0 || currentNode >= nodeCount) {
    return;
  }

  robotState = RobotState::SELECTING_EDGE;
  Turn selected = algorithm == Algorithm::DFS ? chooseDfsTurn(nodes[currentNode]) : chooseBfsTurn(nodes[currentNode]);
  lastTurn = selected;

  JsonDocument extra;
  extra["node"] = currentNode;
  extra["options"] = options;
  extra["turn"] = turnName(selected);
  sendEvent("EDGE_SELECTED", &extra);

  performTurn(selected);
  robotState = RobotState::FOLLOWING_LINE;
}

bool tryBridgeWhiteGap() {
  driveFor(basePwm, basePwm, GAP_BRIDGE_MS);
  SensorFrame frame = readSensors();
  return frame.lineSeen;
}

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
  markLastTurnBlocked();
  JsonDocument extra;
  extra["camera_sign"] = "NO_SIGN";
  extra["blocked_turn"] = turnName(lastTurn);
  sendEvent("OBSTACLE_DETECTED", &extra);

  robotState = RobotState::BACKTRACKING;
  sendEvent("BACKTRACK_STARTED");
  performTurn(Turn::BACK);
  robotState = RobotState::FOLLOWING_LINE;
  sendEvent("BACKTRACK_DONE");
}

void followLine(const SensorFrame &frame) {
  float error = (frame.position - 3500) / 1000.0f;
  int correction = static_cast<int>(error * positionGain);
  int left = constrain(basePwm + correction, 0, PWM_MAX);
  int right = constrain(basePwm - correction, 0, PWM_MAX);
  setMotors(left, right);
}

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
  doc["camera_sign"] = "NO_SIGN";
  doc["threshold"] = currentLineThresholdRaw;
  doc["elapsed_ms"] = motorsAllowed() ? millis() - runStartMs : 0;
  String payload;
  serializeJson(doc, payload);
  if (wsConnected) {
    webSocket.sendTXT(payload);
  }
}

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
    stopMotors();
    robotState = RobotState::IDLE;
    sendEvent("RUN_STOPPED");
  } else if (strcmp(type, "RESET_RUN") == 0) {
    runActive = false;
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
  } else if (strcmp(type, "SET_OBSTACLE_EDGE") == 0) {
    JsonDocument extra;
    extra["edge_id"] = doc["edge_id"] | "";
    sendEvent("OBSTACLE_EDGE_SELECTED", &extra);
  }
}

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

void resetDefaults() {
  basePwm = DEFAULT_BASE_PWM;
  turnPwm = DEFAULT_TURN_PWM;
  searchTurnPwm = DEFAULT_SEARCH_TURN_PWM;
  searchForwardPwm = DEFAULT_SEARCH_FORWARD_PWM;
  searchTimeoutMs = DEFAULT_SEARCH_TIMEOUT_MS;
  loopDelayMs = DEFAULT_LOOP_DELAY_MS;
  speedLimitPercent = DEFAULT_SPEED_LIMIT_PERCENT;
  targetLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
  currentLineThresholdRaw = DEFAULT_TARGET_LINE_THRESHOLD_RAW;
  minAdaptiveThresholdRaw = DEFAULT_MIN_ADAPTIVE_THRESHOLD_RAW;
  thresholdStepRaw = DEFAULT_THRESHOLD_STEP_RAW;
  positionWindow = DEFAULT_POSITION_WINDOW;
  positionGain = DEFAULT_POSITION_GAIN;
  localMotorsEnabled = DEFAULT_LOCAL_MOTORS_ENABLED;
  resetMaze();
  stopMotors();
}

String buildControlPage() {
  String html;

  html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>RLP Line Tuning</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#f6f1e8;color:#1f1f1f;padding:20px;}";
  html += ".card{background:#fff;border-radius:14px;padding:16px;margin-bottom:16px;box-shadow:0 8px 24px rgba(0,0,0,.08);}";
  html += "label{display:block;font-weight:700;margin-bottom:6px;}pre{white-space:pre-wrap;}";
  html += ".row{display:grid;grid-template-columns:1fr;gap:8px;margin-bottom:14px;}";
  html += ".control{display:grid;grid-template-columns:52px 1fr 52px;gap:10px;align-items:center;}";
  html += ".valueInput{width:100%;padding:10px;border:1px solid #ccc;border-radius:10px;font-size:16px;box-sizing:border-box;}";
  html += "button{border:0;border-radius:10px;padding:10px 14px;background:#1b6ef3;color:#fff;font-weight:700;}";
  html += ".stepBtn{font-size:22px;line-height:1;padding:10px 0;}";
  html += "</style></head><body>";
  html += "<div class='card'><h2>RLP ROBOBET</h2><p>Tuning local del seguidor de linea.</p></div>";
  html += "<div class='card'>";
  html += "<div class='row'><label>Velocidad base</label><span id='basePwmValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='basePwm' data-step='-1'>-</button><input class='valueInput' id='basePwm' type='number' min='28' max='120' step='1' value='" + String(basePwm) + "'><button class='stepBtn' data-target='basePwm' data-step='1'>+</button></div>";
  html += "<div class='row'><label>Velocidad giro</label><span id='turnPwmValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='turnPwm' data-step='-1'>-</button><input class='valueInput' id='turnPwm' type='number' min='35' max='120' step='1' value='" + String(turnPwm) + "'><button class='stepBtn' data-target='turnPwm' data-step='1'>+</button></div>";
  html += "<div class='row'><label>Velocidad busqueda giro</label><span id='searchTurnPwmValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='searchTurnPwm' data-step='-1'>-</button><input class='valueInput' id='searchTurnPwm' type='number' min='35' max='120' step='1' value='" + String(searchTurnPwm) + "'><button class='stepBtn' data-target='searchTurnPwm' data-step='1'>+</button></div>";
  html += "<div class='row'><label>Velocidad busqueda avance</label><span id='searchForwardPwmValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='searchForwardPwm' data-step='-1'>-</button><input class='valueInput' id='searchForwardPwm' type='number' min='0' max='80' step='1' value='" + String(searchForwardPwm) + "'><button class='stepBtn' data-target='searchForwardPwm' data-step='1'>+</button></div>";
  html += "<div class='row'><label>Timeout busqueda (ms)</label><span id='searchTimeoutMsValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='searchTimeoutMs' data-step='-50'>-</button><input class='valueInput' id='searchTimeoutMs' type='number' min='100' max='3000' step='50' value='" + String(searchTimeoutMs) + "'><button class='stepBtn' data-target='searchTimeoutMs' data-step='50'>+</button></div>";
  html += "<div class='row'><label>Ventana posicion</label><span id='positionWindowValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='positionWindow' data-step='-1'>-</button><input class='valueInput' id='positionWindow' type='number' min='0' max='4' step='1' value='" + String(positionWindow) + "'><button class='stepBtn' data-target='positionWindow' data-step='1'>+</button></div>";
  html += "<div class='row'><label>Ganancia posicion</label><span id='positionGainValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='positionGain' data-step='-1'>-</button><input class='valueInput' id='positionGain' type='number' min='1' max='60' step='1' value='" + String(positionGain) + "'><button class='stepBtn' data-target='positionGain' data-step='1'>+</button></div>";
  html += "<div class='row'><label>Umbral negro objetivo</label><span id='targetLineThresholdRawValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='targetLineThresholdRaw' data-step='-10'>-</button><input class='valueInput' id='targetLineThresholdRaw' type='number' min='1200' max='2800' step='10' value='" + String(targetLineThresholdRaw) + "'><button class='stepBtn' data-target='targetLineThresholdRaw' data-step='10'>+</button></div>";
  html += "<div class='row'><label>Umbral minimo adaptativo</label><span id='minAdaptiveThresholdRawValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='minAdaptiveThresholdRaw' data-step='-10'>-</button><input class='valueInput' id='minAdaptiveThresholdRaw' type='number' min='1200' max='2800' step='10' value='" + String(minAdaptiveThresholdRaw) + "'><button class='stepBtn' data-target='minAdaptiveThresholdRaw' data-step='10'>+</button></div>";
  html += "<div class='row'><label>Paso adaptativo</label><span id='thresholdStepRawValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='thresholdStepRaw' data-step='-5'>-</button><input class='valueInput' id='thresholdStepRaw' type='number' min='5' max='100' step='5' value='" + String(thresholdStepRaw) + "'><button class='stepBtn' data-target='thresholdStepRaw' data-step='5'>+</button></div>";
  html += "<div class='row'><label>Delay loop (ms)</label><span id='loopDelayMsValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='loopDelayMs' data-step='-5'>-</button><input class='valueInput' id='loopDelayMs' type='number' min='20' max='300' step='5' value='" + String(loopDelayMs) + "'><button class='stepBtn' data-target='loopDelayMs' data-step='5'>+</button></div>";
  html += "<div class='row'><label>Limite velocidad (%)</label><span id='speedLimitPercentValue'></span></div>";
  html += "<div class='control'><button class='stepBtn' data-target='speedLimitPercent' data-step='-5'>-</button><input class='valueInput' id='speedLimitPercent' type='number' min='1' max='100' step='1' value='" + String(speedLimitPercent) + "'><button class='stepBtn' data-target='speedLimitPercent' data-step='5'>+</button></div>";
  html += "<div class='row'><label>Motores locales</label><span id='localMotorsEnabledValue'></span></div>";
  html += "<button id='toggleMotors' type='button'></button>";
  html += "<div style='margin-top:14px;'><button id='resetDefaults' type='button' style='background:#444;'>Reset to defaults</button></div>";
  html += "</div>";
  html += "<div class='card'><strong>Sensores</strong><pre id='sensorValues'></pre><strong>Mapa</strong><pre id='lineMap'></pre></div>";
  html += "<script>";
  html += "const ids=['basePwm','turnPwm','searchTurnPwm','searchForwardPwm','searchTimeoutMs','positionWindow','positionGain','targetLineThresholdRaw','minAdaptiveThresholdRaw','thresholdStepRaw','loopDelayMs','speedLimitPercent'];";
  html += "function clampValue(el){const min=Number(el.min);const max=Number(el.max);let value=Number(el.value);if(Number.isNaN(value))value=min;if(value<min)value=min;if(value>max)value=max;el.value=value;}";
  html += "function syncLabels(){ids.forEach(id=>document.getElementById(id+'Value').textContent=document.getElementById(id).value);}";
  html += "async function push(){const p=new URLSearchParams();ids.forEach(id=>{const el=document.getElementById(id);clampValue(el);p.set(id,el.value);});p.set('localMotorsEnabled',window.localMotorsEnabled?'1':'0');await fetch('/set?'+p.toString());syncLabels();refresh();}";
  html += "async function refresh(){const r=await fetch('/status');const s=await r.json();window.localMotorsEnabled=!!s.localMotorsEnabled;";
  html += "ids.forEach(id=>document.getElementById(id).value=s[id]);";
  html += "document.getElementById('sensorValues').textContent=s.sensorValues;";
  html += "document.getElementById('lineMap').textContent=s.lineMap + '\\nUmbral actual: ' + s.currentLineThresholdRaw + ' / objetivo: ' + s.targetLineThresholdRaw + '\\nMemoria direccion: ' + s.lastKnownDirection + '\\nPosicion: ' + s.lastKnownPosition + '\\nBuscando: ' + (s.isSearching ? 'SI' : 'NO') + '\\nRun activo: ' + (s.runActive ? 'SI' : 'NO');";
  html += "document.getElementById('localMotorsEnabledValue').textContent=window.localMotorsEnabled?'ON':'OFF';";
  html += "document.getElementById('toggleMotors').textContent=window.localMotorsEnabled?'Desactivar motores':'Activar motores';syncLabels();}";
  html += "ids.forEach(id=>{const el=document.getElementById(id);el.addEventListener('change',push);el.addEventListener('blur',push);});";
  html += "document.querySelectorAll('.stepBtn').forEach(btn=>btn.addEventListener('click',async()=>{const el=document.getElementById(btn.dataset.target);const step=Number(btn.dataset.step);const base=Number(el.value||el.min||0);el.value=base+step;clampValue(el);await push();}));";
  html += "document.getElementById('toggleMotors').addEventListener('click',async()=>{window.localMotorsEnabled=!window.localMotorsEnabled;await push();});";
  html += "document.getElementById('resetDefaults').addEventListener('click',async()=>{await fetch('/reset');await refresh();});";
  html += "window.localMotorsEnabled=false;refresh();setInterval(refresh,500);";
  html += "</script></body></html>";

  return html;
}

void handleRoot() {
  localServer.send(200, "text/html", buildControlPage());
}

void handleSet() {
  if (localServer.hasArg("basePwm")) {
    basePwm = constrain(localServer.arg("basePwm").toInt(), DEFAULT_BASE_PWM, 255);
  }
  if (localServer.hasArg("turnPwm")) {
    turnPwm = constrain(localServer.arg("turnPwm").toInt(), DEFAULT_TURN_PWM, 255);
  }
  if (localServer.hasArg("searchTurnPwm")) {
    searchTurnPwm = constrain(localServer.arg("searchTurnPwm").toInt(), DEFAULT_SEARCH_TURN_PWM, 255);
  }
  if (localServer.hasArg("searchForwardPwm")) {
    searchForwardPwm = constrain(localServer.arg("searchForwardPwm").toInt(), 0, 255);
  }
  if (localServer.hasArg("searchTimeoutMs")) {
    searchTimeoutMs = constrain(localServer.arg("searchTimeoutMs").toInt(), 50, 10000);
  }
  if (localServer.hasArg("positionWindow")) {
    positionWindow = constrain(localServer.arg("positionWindow").toInt(), 0, QTR_COUNT / 2);
  }
  if (localServer.hasArg("positionGain")) {
    positionGain = constrain(localServer.arg("positionGain").toInt(), 1, 60);
  }
  if (localServer.hasArg("targetLineThresholdRaw")) {
    targetLineThresholdRaw = constrain(localServer.arg("targetLineThresholdRaw").toInt(), 0, QTR_TIMEOUT_US);
    currentLineThresholdRaw = min(currentLineThresholdRaw, targetLineThresholdRaw);
  }
  if (localServer.hasArg("minAdaptiveThresholdRaw")) {
    minAdaptiveThresholdRaw = constrain(localServer.arg("minAdaptiveThresholdRaw").toInt(), 0, QTR_TIMEOUT_US);
    currentLineThresholdRaw = constrain(currentLineThresholdRaw, minAdaptiveThresholdRaw, targetLineThresholdRaw);
  }
  if (localServer.hasArg("thresholdStepRaw")) {
    thresholdStepRaw = constrain(localServer.arg("thresholdStepRaw").toInt(), 1, 200);
  }
  if (localServer.hasArg("loopDelayMs")) {
    loopDelayMs = constrain(localServer.arg("loopDelayMs").toInt(), 20, 1000);
  }
  if (localServer.hasArg("speedLimitPercent")) {
    speedLimitPercent = constrain(localServer.arg("speedLimitPercent").toInt(), 1, 100);
  }
  if (localServer.hasArg("localMotorsEnabled")) {
    localMotorsEnabled = localServer.arg("localMotorsEnabled").toInt() != 0;
    if (!localMotorsEnabled && !runActive) {
      stopMotors();
    }
  }

  localServer.send(200, "text/plain", "ok");
}

void handleReset() {
  resetDefaults();
  localServer.send(200, "text/plain", "ok");
}

void handleStatus() {
  String json = "{";
  json += "\"basePwm\":" + String(basePwm) + ",";
  json += "\"turnPwm\":" + String(turnPwm) + ",";
  json += "\"searchTurnPwm\":" + String(searchTurnPwm) + ",";
  json += "\"searchForwardPwm\":" + String(searchForwardPwm) + ",";
  json += "\"searchTimeoutMs\":" + String(searchTimeoutMs) + ",";
  json += "\"positionWindow\":" + String(positionWindow) + ",";
  json += "\"positionGain\":" + String(positionGain) + ",";
  json += "\"targetLineThresholdRaw\":" + String(targetLineThresholdRaw) + ",";
  json += "\"currentLineThresholdRaw\":" + String(currentLineThresholdRaw) + ",";
  json += "\"minAdaptiveThresholdRaw\":" + String(minAdaptiveThresholdRaw) + ",";
  json += "\"thresholdStepRaw\":" + String(thresholdStepRaw) + ",";
  json += "\"loopDelayMs\":" + String(loopDelayMs) + ",";
  json += "\"speedLimitPercent\":" + String(speedLimitPercent) + ",";
  json += "\"localMotorsEnabled\":" + String(localMotorsEnabled ? 1 : 0) + ",";
  json += "\"runActive\":" + String(runActive ? 1 : 0) + ",";
  json += "\"lastKnownDirection\":" + String(lastKnownDirection) + ",";
  json += "\"lastKnownPosition\":" + String(lastKnownPosition, 2) + ",";
  json += "\"isSearching\":" + String(isSearching ? 1 : 0) + ",";
  json += "\"sensorValues\":\"" + buildSensorValues() + "\",";
  json += "\"lineMap\":\"" + buildLineMap() + "\"";
  json += "}";

  localServer.send(200, "application/json", json);
}

void connectWiFi() {
  Serial.println("[BOOT] Configurando modo AP");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("[BOOT] SSID AP: ");
  Serial.println(AP_SSID);
  Serial.print("[BOOT] IP AP: ");
  Serial.println(WiFi.softAPIP());
}

void setupWebSocket() {
  webSocket.begin(ROBOBET_SERVER_HOST, ROBOBET_SERVER_PORT, "/ws/robot");
  webSocket.onEvent(onWebSocketEvent);
  webSocket.setReconnectInterval(3000);
}

void setupLocalUi() {
  localServer.on("/", handleRoot);
  localServer.on("/set", handleSet);
  localServer.on("/reset", handleReset);
  localServer.on("/status", handleStatus);
  localServer.begin();
}

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
  setupLocalUi();
  Serial.println("[BOOT] WebUI local activa en http://192.168.4.1");
  robotState = RobotState::WAITING_START;
  Serial.println("[BOOT] Robot en WAITING_START");
}

void loop() {
  serviceNetwork();
  sendTelemetry();

  if (millis() - lastLoopMs < static_cast<uint32_t>(loopDelayMs)) {
    return;
  }
  lastLoopMs = millis();

  if (!motorsAllowed()) {
    stopMotors();
    debugLog("[STATE] Motores desactivados, esperando activacion local");
    delay(5);
    return;
  }

  if (runActive) {
    robotState = RobotState::FOLLOWING_LINE;
  }

  SensorFrame frame = readSensors();
  for (uint8_t i = 0; i < QTR_COUNT; i++) {
    lastSensorValues[i] = frame.raw[i];
    lastLineDetected[i] = frame.raw[i] > currentLineThresholdRaw;
  }

  updateAdaptiveThreshold(frame);
  updateLineMemory(frame);

  if (!frame.lineSeen) {
    debugLog("[STATE] Linea perdida");
    handleLineLost();
    return;
  }

  if (runActive && frame.intersection) {
    debugLog("[STATE] Interseccion detectada");
    handleNode(frame);
    return;
  }

  robotState = RobotState::FOLLOWING_LINE;
  debugLog("[STATE] Siguiendo linea");
  followLine(frame);
}
