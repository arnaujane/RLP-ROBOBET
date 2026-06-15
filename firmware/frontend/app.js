const DEFAULT_API_BASE = "http://127.0.0.1:8000";
const STREAM_DEFAULTS = {
  front: "http://10.22.81.56:81/stream",
  overhead: "http://127.0.0.1:8081/video",
};
const DEFAULT_TWITCH_CHANNEL = "roger33833";
const STALE_TWITCH_CHANNELS = new Set(["twitch_demo"]);
const STALE_FRONT_URLS = new Set([
  "http://192.168.4.2/stream",
  "http://esp32cam.local/stream",
  "http://192.168.1.56/stream",
  "http://192.168.1.56/snapshot",
  "http://192.168.1.56:81/stream",
]);
const SENSOR_COUNT = 8;
const DEFAULT_QTR_THRESHOLD = 2500;
const FRONT_SNAPSHOT_INTERVAL_MS = 420;
const FRONT_SNAPSHOT_TIMEOUT_MS = 1800;
const GRAPH_DIRECTION_BITS = [
  { bit: 1, name: "NORTH", dx: 0, dy: -1 },
  { bit: 2, name: "EAST", dx: 1, dy: 0 },
  { bit: 4, name: "SOUTH", dx: 0, dy: 1 },
  { bit: 8, name: "WEST", dx: -1, dy: 0 },
];
const GRAPH_REVERSE_ORIENTATION = {
  NORTH: "SOUTH",
  EAST: "WEST",
  SOUTH: "NORTH",
  WEST: "EAST",
};

const state = {
  backendOnline: false,
  wsOnline: false,
  user: JSON.parse(localStorage.getItem("robobet_user") || "null"),
  userDetail: null,
  twitch: JSON.parse(localStorage.getItem("robobet_twitch_user") || "null"),
  users: [],
  bets: [],
  polls: [],
  history: [],
  run: null,
  robot: {
    connected: false,
    state: "offline",
    algorithm: "DFS",
    speed_limit: 100,
    drive_power_percent: 11,
    obstacle_count: 0,
    camera_sign: "NO_SIGN",
  },
  camera: {
    online: false,
    sign: "NO_SIGN",
    confidence: 0,
    reason: "-",
    votes: {},
  },
  streamTimers: {},
  streamFailures: {},
  activeView: localStorage.getItem("robobet_view") || "admin",
  counters: {
    crossings: 0,
    lastNode: null,
  },
  simulation: {
    startedAt: Date.now(),
    tick: 0,
    linePosition: 3500,
    obstacleCount: 0,
    crossings: 0,
    cameraIndex: 0,
  },
};

const $ = (id) => document.getElementById(id);
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

function getApiBase() {
  const stored = localStorage.getItem("robobet_api_base");
  if (stored) return stored.replace(/\/$/, "");
  if (location.protocol.startsWith("http") && location.port === "8000") return "";
  if (location.protocol.startsWith("http") && location.hostname) return `http://${location.hostname}:8000`;
  return DEFAULT_API_BASE;
}

const API_BASE = getApiBase();

function apiUrl(path) {
  return `${API_BASE}${path}`;
}

function webSocketUrl(path) {
  const base = API_BASE ? new URL(API_BASE) : location;
  const proto = base.protocol === "https:" ? "wss" : "ws";
  return `${proto}://${base.host}${path}`;
}

async function api(path, options = {}) {
  let reachedBackend = false;
  try {
    const response = await fetch(apiUrl(path), {
      headers: { "Content-Type": "application/json", ...(options.headers || {}) },
      ...options,
    });
    reachedBackend = true;
    setBackendOnline(true);
    if (!response.ok) {
      const text = await response.text();
      throw new Error(text || response.statusText);
    }
    return response.json();
  } catch (error) {
    if (!reachedBackend) setBackendOnline(false);
    throw error;
  }
}

async function safeLoad(label, task) {
  try {
    await task();
  } catch (error) {
    log(`${label} no disponible`, { error: error.message });
  }
}

function setText(id, value) {
  const element = $(id);
  if (element) element.textContent = value;
}

function setBackendOnline(online) {
  state.backendOnline = online;
  $("backendDot")?.classList.toggle("online", online);
  setText("backendConnection", online ? "Backend conectado" : "Backend offline");
  renderSourceBadges();
}

function renderSourceBadges() {
  const realRobot = state.backendOnline && Boolean(state.robot.connected);
  const label = realRobot ? "Datos reales" : "Simulacion";
  setText("dataSourceBadge", label);
  setText("sensorSource", realRobot && hasRobotSensorValues() ? "Robot" : "Simulacion");
}

function log(message, payload) {
  const logBox = $("eventLog");
  if (!logBox) return;
  const suffix = payload ? ` ${JSON.stringify(payload)}` : "";
  const line = `[${new Date().toLocaleTimeString()}] ${message}${suffix}`;
  logBox.textContent = `${line}\n${logBox.textContent}`.slice(0, 9000);
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#039;",
  })[char]);
}

function escapeAttr(value) {
  return escapeHtml(value).replace(/:/g, "&#58;");
}

function formatTime(ms = 0) {
  const total = Math.max(0, Number(ms) || 0);
  const minutes = Math.floor(total / 60000);
  const seconds = Math.floor((total % 60000) / 1000);
  const tenths = Math.floor((total % 1000) / 100);
  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}.${tenths}`;
}

function normalizeRobot(raw = {}) {
  const cameraSign = raw.camera_sign || raw.cameraSign || state.robot.camera_sign || "NO_SIGN";
  return {
    ...state.robot,
    ...raw,
    connected: Boolean(raw.connected ?? state.robot.connected),
    state: raw.state || state.robot.state || "offline",
    algorithm: raw.algorithm || state.robot.algorithm || "DFS",
    speed_limit: Number(raw.speed_limit ?? raw.speedLimit ?? state.robot.speed_limit ?? 100),
    drive_power_percent: Number(raw.drive_power_percent ?? raw.drivePowerPercent ?? state.robot.drive_power_percent ?? 11),
    obstacle_count: Number(raw.obstacle_count ?? raw.obstacleCount ?? state.robot.obstacle_count ?? 0),
    current_node: raw.current_node ?? raw.currentNode ?? state.robot.current_node ?? null,
    line_position: raw.line_position ?? raw.linePosition ?? state.robot.line_position ?? null,
    camera_sign: cameraSign,
    camera_confidence: Number(raw.camera_confidence ?? raw.cameraConfidence ?? state.robot.camera_confidence ?? 0),
    camera_reason: raw.camera_reason || raw.cameraReason || state.robot.camera_reason || "-",
    threshold: Number(raw.threshold ?? raw.currentLineThresholdRaw ?? state.robot.threshold ?? DEFAULT_QTR_THRESHOLD),
    elapsed_ms: Number(raw.elapsed_ms ?? raw.elapsedMs ?? state.robot.elapsed_ms ?? 0),
    last_event: raw.last_event || raw.event || state.robot.last_event || "-",
    battery: raw.battery ?? state.robot.battery ?? null,
  };
}

function updateRobot(raw = {}) {
  state.robot = normalizeRobot(raw);

  if (state.robot.current_node !== null && state.robot.current_node !== state.counters.lastNode) {
    state.counters.lastNode = state.robot.current_node;
    state.counters.crossings += 1;
  }

  const connected = Boolean(state.robot.connected);
  $("robotDot")?.classList.toggle("online", connected);
  $("robotDot")?.classList.toggle("error", !connected);
  setText("robotConnection", connected ? "Robot conectado" : "Robot offline");
  renderDashboard();
}

function getRobotElapsedMs() {
  if (Number(state.robot.elapsed_ms) > 0) return Number(state.robot.elapsed_ms);
  if (state.run?.status === "running" && state.run.started_at) {
    const startedAt = Date.parse(`${state.run.started_at.replace(" ", "T")}Z`);
    if (!Number.isNaN(startedAt)) return Date.now() - startedAt;
  }
  if (!state.backendOnline || !state.robot.connected) return Date.now() - state.simulation.startedAt;
  return 0;
}

function getRobotStateLabel(robot = state.robot) {
  const stateName = String(robot.state || "offline");
  const labels = {
    offline: "Desconectado",
    connected: "Conectado",
    IDLE: "En espera",
    WAITING_START: "En espera",
    FOLLOWING_LINE: "Resolviendo laberinto",
    INTERSECTION_DETECTED: "Cruce detectado",
    CENTER_ON_NODE: "Centrando cruce",
    CLASSIFY_NODE: "Leyendo salidas",
    CHOOSE_DIRECTION: "Decidiendo giro",
    GO_STRAIGHT: "Siguiendo recto",
    TURN_LEFT: "Girando izquierda",
    TURN_RIGHT: "Girando derecha",
    TURN_BACK: "Media vuelta",
    WAITING_TURN_CONFIRMATION: "Esperando giro",
    WAITING_OPERATOR_CLASSIFICATION: "Esperando operador",
    SEARCH_LINE: "Buscando linea",
    NODE_DETECTED: "Cruce detectado",
    SELECTING_EDGE: "Seleccionando ruta",
    OBSTACLE_CHECK: "Obstaculo detectado",
    WAITING_OBSTACLE_DECISION: "Esperando obstaculo",
    BACKTRACKING: "Retrocediendo",
    RETURNING_FROM_BLOCKED_OBSTACLE: "Volviendo al cruce",
    RETURNING_TO_UNFINISHED_NODE: "Volviendo a nodo pendiente",
    FINISH_CHECK: "Final detectado",
    FINISHED: "Finalizado",
    ERROR: "Error",
    ERROR_STATE: "Error",
  };
  return labels[stateName] || stateName;
}

function getAlgorithmPhase(robot = state.robot) {
  const current = String(robot.state || "");
  const event = String(robot.last_event || "");
  if (current.includes("WAITING_OPERATOR_CLASSIFICATION")) return "Esperando operador";
  if (current.includes("WAITING_OBSTACLE_DECISION")) return "Esperando obstaculo";
  if (current.includes("RETURNING_FROM_BLOCKED_OBSTACLE")) return "Volviendo al cruce";
  if (current.includes("RETURNING_TO_UNFINISHED_NODE")) return "Volviendo a nodo pendiente";
  if (current.includes("BACKTRACK") || event.includes("BACKTRACK")) return "Retrocediendo";
  if (current.includes("WAITING_TURN_CONFIRMATION")) return "Esperando confirmacion";
  if (current.includes("TURN_LEFT")) return "Girando izquierda";
  if (current.includes("TURN_RIGHT")) return "Girando derecha";
  if (current.includes("TURN_BACK")) return "Media vuelta";
  if (current.includes("GO_STRAIGHT")) return "Recto";
  if (current.includes("SELECTING") || current.includes("CHOOSE") || event.includes("EDGE")) return "Recalculando ruta";
  if (current.includes("FINISH") || event.includes("FINISH")) return "Finalizado";
  if (current.includes("OBSTACLE") || event.includes("OBSTACLE")) return "Obstaculo";
  if (current.includes("FOLLOWING") || current.includes("NODE") || current.includes("INTERSECTION") || current.includes("CENTER")) return "Explorando";
  return "En espera";
}

function hasRobotSensorValues() {
  return Boolean(
    state.robot.sensor_values ||
      state.robot.sensorValues ||
      state.robot.qtr_raw ||
      state.robot.raw_us ||
      state.robot.sensors
  );
}

function parseSensorValues(robot = state.robot) {
  const candidates = [
    robot.sensor_values,
    robot.sensorValues,
    robot.qtr_raw,
    robot.raw_us,
    robot.sensors,
  ];

  for (const candidate of candidates) {
    if (Array.isArray(candidate) && candidate.length >= SENSOR_COUNT) {
      return candidate.slice(0, SENSOR_COUNT).map((value) => Number(value) || 0);
    }
    if (typeof candidate === "string" && candidate.trim()) {
      const values = candidate
        .split(/[,\s]+/)
        .map((value) => Number(value.trim()))
        .filter((value) => !Number.isNaN(value));
      if (values.length >= SENSOR_COUNT) return values.slice(0, SENSOR_COUNT);
    }
  }

  return synthesizeSensorValues(robot);
}

function synthesizeSensorValues(robot = state.robot) {
  const threshold = Number(robot.threshold || DEFAULT_QTR_THRESHOLD);
  const linePosition = Number(robot.line_position ?? state.simulation.linePosition);
  const position = clamp(Number.isFinite(linePosition) ? linePosition : 3500, 0, 7000) / 1000;
  const allBlack =
    String(robot.state || "").includes("OBSTACLE") ||
    String(robot.last_event || "").includes("BLACK_PATCH") ||
    String(robot.last_event || "").includes("OBSTACLE");

  return Array.from({ length: SENSOR_COUNT }, (_, index) => {
    if (allBlack) return threshold + 450 + Math.round(Math.random() * 120);
    const distance = Math.abs(index - position);
    const activeBoost = Math.max(0, 1.4 - distance) * 1350;
    const wave = Math.sin(state.simulation.tick / 3 + index) * 80;
    return Math.round(clamp(1120 + activeBoost + wave, 650, 3000));
  });
}

function getMotorPower(robot = state.robot) {
  const rawLeft = robot.motor_left_percent ?? robot.motorLeftPercent ?? robot.left_motor ?? robot.leftMotor;
  const rawRight = robot.motor_right_percent ?? robot.motorRightPercent ?? robot.right_motor ?? robot.rightMotor;
  if (rawLeft !== undefined && rawRight !== undefined) {
    return [clamp(Number(rawLeft) || 0, 0, 100), clamp(Number(rawRight) || 0, 0, 100)];
  }

  const runningStates = [
    "FOLLOWING_LINE",
    "INTERSECTION_DETECTED",
    "CENTER_ON_NODE",
    "CLASSIFY_NODE",
    "CHOOSE_DIRECTION",
    "GO_STRAIGHT",
    "TURN_LEFT",
    "TURN_RIGHT",
    "TURN_BACK",
    "SEARCH_LINE",
    "NODE_DETECTED",
    "SELECTING_EDGE",
    "OBSTACLE_CHECK",
    "WAITING_OBSTACLE_DECISION",
    "BACKTRACKING",
    "RETURNING_FROM_BLOCKED_OBSTACLE",
    "RETURNING_TO_UNFINISHED_NODE",
    "RETURN_TO_LAST_NODE",
  ];
  const running = runningStates.includes(String(robot.state));
  if (!running && state.backendOnline && robot.connected) return [0, 0];

  const speedLimit = Number(robot.speed_limit ?? 100);
  const linePosition = Number(robot.line_position ?? state.simulation.linePosition);
  const error = clamp(((Number.isFinite(linePosition) ? linePosition : 3500) - 3500) / 3500, -1, 1);
  const base = running || !state.backendOnline || !robot.connected ? speedLimit * 0.58 : 0;
  const correction = error * 18;
  return [
    Math.round(clamp(base + correction, 0, speedLimit)),
    Math.round(clamp(base - correction, 0, speedLimit)),
  ];
}

function renderSensorBoard() {
  const threshold = Number(state.robot.threshold || DEFAULT_QTR_THRESHOLD);
  const values = parseSensorValues();
  const active = values.map((value) => value >= threshold);
  const activeCount = active.filter(Boolean).length;
  const lineTrackingProtected = Boolean(state.robot.line_tracking_protected ?? state.robot.lineTrackingProtected);

  $("sensorBoard").innerHTML = values
    .map((value, index) => `
      <div class="sensor ${active[index] ? "active" : ""}">
        <span class="sensor-index">S${index + 1}</span>
        <span class="sensor-light"></span>
        <span class="sensor-value">${Math.round(value)}</span>
      </div>
    `)
    .join("");

  setText("sensorSummary", `${activeCount}/8 activos${lineTrackingProtected ? " - PID protegido" : ""}`);
  setText("sensorThreshold", threshold);
  setText("qtrValues", values.map((value) => Math.round(value)).join(", "));
}

function renderMotorPower() {
  const [left, right] = getMotorPower();
  const speedLimit = Number(state.robot.speed_limit ?? 100);
  const leftRing = document.querySelector(".gauge:first-child .gauge-ring");
  const rightRing = document.querySelector(".gauge:nth-child(2) .gauge-ring");

  if (leftRing) leftRing.style.setProperty("--value", left);
  if (rightRing) rightRing.style.setProperty("--value", right);
  setText("motorLeftValue", `${left}%`);
  setText("motorRightValue", `${right}%`);
  $("motorLeftBar").style.width = `${left}%`;
  $("motorRightBar").style.width = `${right}%`;

  const badge = $("speedLimitBadge");
  badge.textContent = speedLimit <= 80 ? "Restriccion 80%" : `${speedLimit}%`;
  badge.classList.toggle("limited", speedLimit <= 80);
}

function renderMazeGraph() {
  const svg = $("mazeGraph");
  if (!svg) return;

  const nodes = Array.isArray(state.robot.graph_nodes) ? state.robot.graph_nodes : [];
  const edges = Array.isArray(state.robot.graph_edges) ? state.robot.graph_edges : [];
  const currentNode = Number(state.robot.current_node ?? -1);
  const targetNode = Number(state.robot.navigation_target_node ?? -1);

  setText("graphSummary", `${nodes.length} nodos / ${edges.length} aristas`);

  if (!nodes.length) {
    svg.setAttribute("viewBox", "0 0 720 420");
    svg.innerHTML = `<text class="graph-empty" x="360" y="215">Sin grafo todavia</text>`;
    return;
  }

  const width = 720;
  const height = 420;
  const padding = 76;
  const nodeMap = new Map(nodes.map((node) => [Number(node.id), node]));
  const xs = nodes.map((node) => Number(node.x) || 0);
  const ys = nodes.map((node) => Number(node.y) || 0);
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const minY = Math.min(...ys);
  const maxY = Math.max(...ys);
  const spanX = Math.max(1, maxX - minX);
  const spanY = Math.max(1, maxY - minY);
  const scale = Math.min((width - padding * 2) / spanX, (height - padding * 2) / spanY);

  const pointFor = (node) => ({
    x: padding + ((Number(node.x) || 0) - minX) * scale,
    y: height - padding - ((Number(node.y) || 0) - minY) * scale,
  });

  const connectedOrientations = new Map();
  const markConnected = (nodeId, orientation) => {
    if (!connectedOrientations.has(nodeId)) connectedOrientations.set(nodeId, new Set());
    connectedOrientations.get(nodeId).add(orientation);
  };

  edges.forEach((edge) => {
    const from = nodeMap.get(Number(edge.from));
    const to = nodeMap.get(Number(edge.to));
    if (!from || !to) return;
    const fromPoint = pointFor(from);
    const toPoint = pointFor(to);
    const dx = toPoint.x - fromPoint.x;
    const dy = toPoint.y - fromPoint.y;
    const fallbackFromOrientation = Math.abs(dx) >= Math.abs(dy) ? (dx >= 0 ? "EAST" : "WEST") : (dy >= 0 ? "SOUTH" : "NORTH");
    const fromOrientation = GRAPH_REVERSE_ORIENTATION[edge.orientation] ? edge.orientation : fallbackFromOrientation;
    const toOrientation = GRAPH_REVERSE_ORIENTATION[fromOrientation] || (Math.abs(dx) >= Math.abs(dy) ? (dx >= 0 ? "WEST" : "EAST") : (dy >= 0 ? "NORTH" : "SOUTH"));
    markConnected(Number(edge.from), fromOrientation);
    markConnected(Number(edge.to), toOrientation);
  });

  const edgeMarkup = edges
    .map((edge) => {
      const from = nodeMap.get(Number(edge.from));
      const to = nodeMap.get(Number(edge.to));
      if (!from || !to) return "";
      const a = pointFor(from);
      const b = pointFor(to);
      const classes = ["graph-edge"];
      if (edge.blocked) classes.push("blocked");
      else if (edge.green) classes.push("green");
      const label = edge.blocked ? "RED" : edge.green ? "GREEN" : "";
      const labelMarkup = label
        ? `<text class="graph-edge-label ${edge.blocked ? "blocked" : "green"}" x="${(a.x + b.x) / 2}" y="${(a.y + b.y) / 2 - 8}">${label}</text>`
        : "";
      return `<line class="${classes.join(" ")}" x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}"><title>${edge.from} -> ${edge.to} ${edge.orientation || ""} ${label}</title></line>${labelMarkup}`;
    })
    .join("");

  const exitMarkup = nodes
    .map((node) => {
      const point = pointFor(node);
      const id = Number(node.id);
      const options = Number(node.options) || 0;
      const tried = Number(node.tried) || 0;
      const blocked = Number(node.blocked) || 0;
      const green = Number(node.green) || 0;
      const connected = connectedOrientations.get(id) || new Set();

      return GRAPH_DIRECTION_BITS.map((direction) => {
        if (!(options & direction.bit) || connected.has(direction.name)) return "";

        const isBlocked = Boolean(blocked & direction.bit);
        const isGreen = Boolean(green & direction.bit);
        const isUntried = !isBlocked && !isGreen && !(tried & direction.bit);
        const stubLength = isUntried ? 58 : 46;
        const end = {
          x: point.x + direction.dx * stubLength,
          y: point.y + direction.dy * stubLength,
        };
        const marker = `url(#${isBlocked ? "arrowBlocked" : isGreen ? "arrowGreen" : "arrowFrontier"})`;
        const classes = ["graph-exit"];
        if (isBlocked) classes.push("blocked");
        else if (isGreen) classes.push("green");
        else classes.push("frontier");

        const label = isBlocked ? "RED" : isGreen ? "GREEN" : "nuevo";
        const labelX = point.x + direction.dx * (stubLength + 14);
        const labelY = point.y + direction.dy * (stubLength + 14) + 4;

        return `
          <line class="${classes.join(" ")}" x1="${point.x}" y1="${point.y}" x2="${end.x}" y2="${end.y}" marker-end="${marker}">
            <title>Nodo ${id} salida ${direction.name}: ${label}</title>
          </line>
          <text class="graph-exit-label ${isBlocked ? "blocked" : isGreen ? "green" : "frontier"}" x="${labelX}" y="${labelY}">${label}</text>
        `;
      }).join("");
    })
    .join("");

  const nodeMarkup = nodes
    .map((node) => {
      const point = pointFor(node);
      const id = Number(node.id);
      const options = Number(node.options) || 0;
      const tried = Number(node.tried) || 0;
      const blocked = Number(node.blocked) || 0;
      const frontier = (options & ~tried & ~blocked) !== 0;
      const classes = ["graph-node"];
      const labelClasses = ["graph-label"];
      if (id === currentNode) {
        classes.push("current");
        labelClasses.push("current");
      } else if (node.final) {
        classes.push("final");
        labelClasses.push("final");
      } else if (frontier || id === targetNode) {
        classes.push("frontier");
      }

      return `
        <circle class="${classes.join(" ")}" cx="${point.x}" cy="${point.y}" r="14">
          <title>Nodo ${id} opciones=${options} tried=${tried} blocked=${blocked}</title>
        </circle>
        <text class="${labelClasses.join(" ")}" x="${point.x}" y="${point.y + 4}">${id}</text>
      `;
    })
    .join("");

  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("preserveAspectRatio", "xMidYMid meet");
  const defs = `
    <defs>
      <marker id="arrowFrontier" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">
        <path d="M0,0 L8,4 L0,8 Z" fill="#f5a623"></path>
      </marker>
      <marker id="arrowBlocked" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">
        <path d="M0,0 L8,4 L0,8 Z" fill="#ec3f3f"></path>
      </marker>
      <marker id="arrowGreen" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">
        <path d="M0,0 L8,4 L0,8 Z" fill="#22b95c"></path>
      </marker>
    </defs>
  `;
  svg.innerHTML = `${defs}${edgeMarkup}${exitMarkup}${nodeMarkup}`;
}

function renderCamera() {
  const sign = state.camera.sign || state.robot.camera_sign || "NO_SIGN";
  const confidence = Number(state.camera.confidence ?? state.robot.camera_confidence ?? 0);
  const reason = state.camera.reason || state.robot.camera_reason || "-";

  $("cameraDot")?.classList.toggle("online", Boolean(state.camera.online));
  setText("cameraConnection", state.camera.online ? "Camara conectada" : "Camara offline");
  setText("cameraSignBadge", sign);
  $("cameraSignBadge").dataset.sign = sign;
  $("cameraConfidenceBar").style.width = `${clamp(confidence, 0, 100)}%`;
  setText("cameraConfidenceText", `${confidence}%`);
  setText("cameraReason", reason);
  setText("cameraVotes", formatVotes(state.camera.votes));
  setText("cameraFps", formatCameraFps(state.camera.fps));
  setText("cameraQuality", cameraQualityLabel(state.camera));
  setText("cameraTransport", state.robot.camera_transport ? String(state.robot.camera_transport).toUpperCase() : "HTTP preparado");
  setText("robotCameraState", sign);
}

function renderRobotStatus() {
  const robot = state.robot;
  const label = getRobotStateLabel(robot);
  const phase = getAlgorithmPhase(robot);
  const elapsed = getRobotElapsedMs();
  const speedLimit = Number(robot.speed_limit ?? 100);
  const restrictions = speedLimit <= 80 ? "Motor 80%" : "Sin restricciones";
  const nodeCount = Number(robot.node_count ?? robot.nodeCount ?? 0);
  const edgeCount = Number(robot.edge_count ?? robot.edgeCount ?? 0);
  const crossingCount = Number(robot.crossing_count ?? robot.crossingCount ?? state.counters.crossings ?? 0);
  const orientation = robot.orientation || "-";
  const exits = robot.relative_exits || robot.relativeExits || "-";
  const navigationTarget = robot.navigation_target_node ?? robot.navigationTargetNode;
  const navigationNext = robot.navigation_next_node ?? robot.navigationNextNode;
  const navigatingToFrontier = Boolean(robot.navigating_to_frontier ?? robot.navigatingToFrontier);
  const waitingTurn = Boolean(robot.waiting_for_turn_confirmation ?? robot.waitingForTurnConfirmation);
  const pauseEnabled = Boolean(robot.pause_before_turn_enabled ?? robot.pauseBeforeTurnEnabled);
  const pendingTurn = robot.pending_turn || robot.pendingTurn || "-";
  const obstacleHandlingEnabled = Boolean(robot.obstacle_handling_enabled ?? robot.obstacleHandlingEnabled);
  const waitingObstacle = Boolean(robot.waiting_for_obstacle_decision ?? robot.waitingForObstacleDecision);
  const waitingObstacleMode = Boolean(robot.wait_for_obstacle_decision_enabled ?? robot.waitForObstacleDecisionEnabled);
  const waitingOperatorClassification = Boolean(robot.waiting_for_operator_classification ?? robot.waitingForOperatorClassification);
  const drivePowerPercent = Number(robot.drive_power_percent ?? robot.drivePowerPercent ?? 11);
  const statePill = $("adminStatePill");
  const stateText = String(robot.state || "offline");

  statePill.className = "state-pill";
  if (!robot.connected || stateText.includes("ERROR")) statePill.classList.add("error");
  else if (stateText.includes("WAITING") || stateText === "connected") statePill.classList.add("warning");
  else if (stateText.includes("OBSTACLE") || stateText.includes("FINISH")) statePill.classList.add("info");

  statePill.textContent = label;
  setText("adminStateText", stateText);
  const activeAlert = waitingOperatorClassification
    ? "Robot en pausa por incidencia"
    : waitingObstacle
    ? "Robot esperando decision"
    : waitingTurn
      ? `Giro pendiente: ${pendingTurn}`
      : robot.last_event || "Sin alertas activas";
  setText("adminAlertText", robot.connected ? activeAlert : "Robot desconectado");
  setText("currentTimer", formatTime(elapsed));
  setText("mazeTimer", formatTime(elapsed));
  setText("liveTime", formatTime(elapsed));
  setText("activeAlgorithm", robot.algorithm || "DFS");
  setText("mazeAlgorithm", robot.algorithm || "DFS");
  setText("liveAlgorithm", robot.algorithm || "DFS");
  setText("activeMode", phase);
  setText("drivePowerValue", `${drivePowerPercent}%`);
  setText("drivePowerHint", `PWM base actual del seguidor de linea: ${drivePowerPercent}%`);
  if ($("drivePowerSlider") && document.activeElement !== $("drivePowerSlider")) {
    $("drivePowerSlider").value = String(drivePowerPercent);
  }
  setText("algorithmName", robot.algorithm || "DFS");
  setText("algorithmPhase", phase);
  setText("activeNode", robot.current_node !== null && robot.current_node !== undefined ? `${robot.current_node}/${nodeCount}` : "-");
  setText("mazeState", label);
  setText("liveRobotState", label);
  const navigationText = navigatingToFrontier ? ` / objetivo ${navigationTarget} via ${navigationNext}` : "";
  setText("mazeRouteState", `Nodos ${nodeCount} / Aristas ${edgeCount} / ${orientation} / ${exits}${navigationText}`);
  setText("mazeObstacles", robot.obstacle_count ?? 0);
  setText("liveObstacles", robot.obstacle_count ?? 0);
  setText("obstacleCount", robot.obstacle_count ?? 0);
  setText("liveRestrictions", restrictions);
  setText("mazeCrossings", Math.max(crossingCount, nodeCount, state.simulation.crossings));

  const progress = phase === "Finalizado" ? 100 : phase === "Retrocediendo" ? 66 : phase === "Explorando" ? 42 : 18;
  $("algorithmProgress").style.width = `${progress}%`;

  const obstacleActive = waitingOperatorClassification || waitingObstacle || phase === "Obstaculo" || Number(robot.obstacle_count) > 0;
  setText("obstacleStatus", obstacleHandlingEnabled ? (obstacleActive ? "Gestion activa" : "Sin incidencias") : "Obstaculos desactivados");
  setText("obstacleType", waitingObstacleMode ? "Monitorizado" : "Automatico");

  const continueButton = $("continueTurn");
  const enableButton = $("enableTurnPause");
  const disableButton = $("disableTurnPause");
  const enableObstaclesButton = $("enableObstacles");
  const disableObstaclesButton = $("disableObstacles");
  if (continueButton) continueButton.disabled = !waitingTurn;
  if (enableButton) enableButton.disabled = pauseEnabled;
  if (disableButton) disableButton.disabled = !pauseEnabled;
  if (enableObstaclesButton) enableObstaclesButton.disabled = obstacleHandlingEnabled;
  if (disableObstaclesButton) disableObstaclesButton.disabled = !obstacleHandlingEnabled;
}

function renderDashboard() {
  renderSourceBadges();
  renderRobotStatus();
  renderSensorBoard();
  renderMotorPower();
  renderMazeGraph();
  renderCamera();
}

function formatVotes(votes = {}) {
  return `R ${votes.red ?? 0} - V ${votes.green ?? 0} - N ${votes.black ?? 0} - sin ${votes.none ?? 0}`;
}

function formatCameraFps(value) {
  const fps = Number(value);
  return Number.isFinite(fps) && fps > 0 ? `${fps.toFixed(1)} fps` : "-";
}

function cameraQualityLabel(camera = state.camera) {
  if (!camera.online) return "Sin conexion";
  if (camera.camera_health && camera.camera_health !== "ok") return "Degradada";
  const failures = Number(camera.consecutive_failures ?? camera.consecutiveFailures ?? 0);
  const corrupt = Number(camera.corrupt_frames ?? camera.corruptFrames ?? 0);
  const fps = Number(camera.fps ?? 0);
  const rssi = Number(camera.rssi ?? 0);
  if (failures >= 2) return "Reconectando";
  if (rssi && rssi < -75) return "WiFi debil";
  if (corrupt > 0 || (fps > 0 && fps < 2)) return "Degradada";
  return "Estable";
}

function setCameraOffline(reason = "Sin respuesta") {
  state.camera = { online: false, sign: "NO_SIGN", confidence: 0, reason, votes: {} };
  renderCamera();
}

function updateCameraResult(payload, online = true) {
  state.camera = {
    ...state.camera,
    ...payload,
    online,
    sign: payload.sign || "NO_SIGN",
    confidence: Number(payload.confidence ?? 0),
    reason: payload.reason || "-",
    votes: payload.votes || {},
  };
  renderCamera();
}

function cameraApiPath(endpoint) {
  const streamUrl = $("frontUrl").value || STREAM_DEFAULTS.front;
  return `/api/streams/camera/${endpoint}?stream_url=${encodeURIComponent(streamUrl)}`;
}

async function refreshCameraStatus() {
  try {
    const payload = await api(cameraApiPath("status"));
    updateCameraResult(payload, true);
  } catch (error) {
    if (state.backendOnline) setCameraOffline(error.message);
    else simulateCamera();
  }
}

async function testCameraDetection() {
  setText("cameraReason", "detectando");
  try {
    const payload = await api(cameraApiPath("detect"), { method: "POST" });
    updateCameraResult(payload, true);
    log("CAMERA_DETECT", payload);
  } catch (error) {
    setCameraOffline(error.message);
    log("CAMERA_DETECT_FAILED", { error: error.message });
  }
}

function simulateCamera() {
  const signs = ["NO_SIGN", "NO_SIGN", "GREEN_SIGN", "NO_SIGN", "RED_SIGN", "BLACK_SIGN"];
  const sign = signs[state.simulation.cameraIndex % signs.length];
  const confidence = sign === "NO_SIGN" ? 18 : 78 + ((state.simulation.tick * 7) % 18);
  state.simulation.cameraIndex += 1;
  updateCameraResult(
    {
      sign,
      confidence,
      reason: sign === "NO_SIGN" ? "sin senal dominante" : "simulacion visual",
      votes: { red: sign === "RED_SIGN" ? 4 : 0, green: sign === "GREEN_SIGN" ? 4 : 0, black: sign === "BLACK_SIGN" ? 4 : 0, none: sign === "NO_SIGN" ? 5 : 1 },
    },
    false
  );
}

async function refreshState() {
  const payload = await api("/api/run/state");
  state.run = payload.run || null;
  updateRobot(payload.robot || {});
}

async function loadHistory() {
  const history = await api("/api/run/history");
  state.history = history;
  $("historySource").textContent = "API";
  renderHistory();
}

function renderHistory() {
  const rows = state.history.length ? state.history : sampleHistory();
  $("historyRows").innerHTML = rows
    .map((run) => {
      const result = parseRunResult(run);
      const status = run.status || result.status || "completed";
      const restrictions = formatRestrictions(run, result);
      return `
        <tr>
          <td>${escapeHtml(formatDate(run.started_at || run.finished_at || run.date))}</td>
          <td>${formatTime(run.elapsed_ms ?? result.elapsed_ms ?? 0)}</td>
          <td>${escapeHtml(run.algorithm || result.algorithm || "DFS")}</td>
          <td>${run.obstacle_count ?? result.obstacle_count ?? 0}</td>
          <td>${run.crossing_count ?? result.crossing_count ?? run.crossings ?? result.crossings ?? "-"}</td>
          <td><span class="result-tag ${escapeAttr(status)}">${escapeHtml(status)}</span></td>
          <td>${escapeHtml(restrictions)}</td>
        </tr>
      `;
    })
    .join("");
}

function parseRunResult(run = {}) {
  if (!run.result) return {};
  try {
    return JSON.parse(run.result);
  } catch {
    return {};
  }
}

function parseJsonObject(value) {
  if (!value) return {};
  if (typeof value === "object") return value;
  try {
    return JSON.parse(value);
  } catch {
    return {};
  }
}

function formatRestrictions(run = {}, result = {}) {
  const storedRestrictions = parseJsonObject(run.restrictions);
  const restrictions = Object.keys(storedRestrictions).length ? storedRestrictions : (result.restrictions || {});
  const labels = [];
  const speedLimit = Number(restrictions.speed_limit ?? run.speed_limit ?? result.speed_limit ?? 100);
  if (restrictions.motor_limited_80 || speedLimit <= 80) labels.push("Motor 80%");
  if (Array.isArray(restrictions.user_obstacles) && restrictions.user_obstacles.length) {
    labels.push(`${restrictions.user_obstacles.length} obstaculo usuario`);
  }
  return labels.length ? labels.join(" - ") : "Sin restricciones";
}

function formatDate(value) {
  if (!value) return "-";
  const date = new Date(String(value).replace(" ", "T"));
  if (Number.isNaN(date.getTime())) return String(value);
  return date.toLocaleString();
}

function sampleHistory() {
  $("historySource").textContent = "Simulacion";
  return [
    { date: new Date(Date.now() - 3600000).toISOString(), elapsed_ms: 74200, algorithm: "DFS", obstacle_count: 1, crossings: 8, status: "finished", speed_limit: 100 },
    { date: new Date(Date.now() - 7200000).toISOString(), elapsed_ms: 91800, algorithm: "BFS", obstacle_count: 2, crossings: 11, status: "finished", speed_limit: 80 },
    { date: new Date(Date.now() - 12800000).toISOString(), elapsed_ms: 43100, algorithm: "DFS", obstacle_count: 0, crossings: 5, status: "stopped", speed_limit: 100 },
  ];
}

async function loadUsers() {
  state.users = await api("/api/users");
  renderUsers();
  if (state.user) await safeLoad("Detalle usuario", loadUserDetail);
}

function renderUsers() {
  $("ranking").innerHTML = state.users
    .slice()
    .sort((a, b) => Number(b.balance) - Number(a.balance))
    .map((user) => `<li><strong>${escapeHtml(user.name)}</strong> - ${user.balance} puntos</li>`)
    .join("");

  if (state.user) {
    const fresh = state.users.find((user) => user.id === state.user.id);
    if (fresh) {
      state.user = fresh;
      localStorage.setItem("robobet_user", JSON.stringify(fresh));
    }
  }
  renderUserPanel();
}

function renderUserPanel() {
  if (!state.user) {
    setText("activeUser", "Sin usuario");
    state.userDetail = null;
    renderUserActivity();
    return;
  }
  setText("activeUser", `${state.user.name} - ${state.user.balance} puntos`);
  renderUserActivity();
}

async function loadUserDetail() {
  if (!state.user) {
    state.userDetail = null;
    renderUserActivity();
    return;
  }
  state.userDetail = await api(`/api/users/${state.user.id}`);
  renderUserActivity();
}

function renderUserActivity() {
  const element = $("userActivity");
  if (!element) return;
  if (!state.user) {
    element.textContent = "Sin actividad registrada";
    return;
  }

  const votes = state.userDetail?.votes || [];
  const bets = state.userDetail?.bets || state.bets || [];
  const latestVotes = votes.slice(0, 3).map((vote) => `Voto ${vote.title}: ${vote.choice}`);
  const latestBets = bets.slice(0, 3).map((bet) => `Apuesta ${bet.kind}: ${bet.choice} (${bet.status})`);
  const rows = [...latestVotes, ...latestBets].slice(0, 5);
  element.innerHTML = rows.length
    ? rows.map((row) => `<span>${escapeHtml(row)}</span>`).join("")
    : "Sin actividad registrada";
}

async function createUser() {
  const name = $("userName").value.trim();
  if (!name) return;
  state.user = await api("/api/users", { method: "POST", body: JSON.stringify({ name }) });
  localStorage.setItem("robobet_user", JSON.stringify(state.user));
  $("userName").value = "";
  await Promise.all([loadUsers(), loadUserDetail()]);
}

async function loadBets() {
  const suffix = state.user ? `?user_id=${state.user.id}` : "";
  state.bets = await api(`/api/bets${suffix}`);
  renderBets();
}

function renderBets() {
  $("betsList").innerHTML = state.bets
    .map((bet) => `<li>${escapeHtml(bet.kind)}: <strong>${escapeHtml(bet.choice)}</strong> - ${bet.amount} pts - ${escapeHtml(bet.status)}</li>`)
    .join("");
  renderUserActivity();
}

function updateBetChoices() {
  const kind = $("betKind").value;
  const choices = {
    finish: [["yes", "Si llega"], ["no", "No llega"]],
    time_range: [["fast", "< 60 s"], ["medium", "60-120 s"], ["slow", "> 120 s"]],
    obstacles: [["0", "0"], ["1", "1"], ["2+", "2 o mas"]],
    algorithm: [["DFS", "DFS"], ["BFS", "BFS"]],
  }[kind];
  $("betChoice").innerHTML = choices.map(([value, label]) => `<option value="${value}">${label}</option>`).join("");
}

async function placeBet() {
  if (!state.user) return setOperatorMessage("Crea un usuario antes de apostar");
  await api("/api/bets", {
    method: "POST",
    body: JSON.stringify({
      user_id: state.user.id,
      kind: $("betKind").value,
      choice: $("betChoice").value,
      amount: Number($("betAmount").value),
    }),
  });
  await Promise.all([loadUsers(), loadBets(), loadUserDetail()]);
}

async function loadPolls() {
  state.polls = await api("/api/polls");
  renderPolls();
}

function latestPollForKinds(kinds = []) {
  return state.polls
    .filter((poll) => kinds.includes(String(poll.kind || "").toLowerCase()))
    .sort((a, b) => Number(b.id) - Number(a.id))[0] || null;
}

function totalPollVotes(poll) {
  const results = poll?.results || [];
  return Number(poll?.total_votes ?? results.reduce((sum, item) => sum + Number(item.votes || 0), 0));
}

function pollLeader(poll) {
  const results = (poll?.results || []).slice().sort((a, b) => Number(b.votes || 0) - Number(a.votes || 0));
  return results[0]?.choice || null;
}

function voteCardMarkup({ label, stateText, tone, choice, meta }) {
  return `
    <div class="vote-card">
      <div class="vote-card-head">
        <span class="vote-label">${escapeHtml(label)}</span>
        <span class="vote-chip ${escapeAttr(tone)}">${escapeHtml(stateText)}</span>
      </div>
      <strong class="vote-choice">${escapeHtml(choice)}</strong>
      <span class="vote-meta">${escapeHtml(meta)}</span>
    </div>
  `;
}

function obstacleVoteCardMarkup(slot) {
  const poll = latestPollForKinds([`obstacle_${slot}`]);
  if (!poll) {
    return voteCardMarkup({ label: `Obstaculo ${slot}`, stateText: "Pendiente", tone: "pending", choice: "Pendiente", meta: "Sin resolver" });
  }

  const totalVotes = totalPollVotes(poll);
  const leader = pollLeader(poll);
  if (poll.status === "open") {
    return voteCardMarkup({
      label: `Obstaculo ${slot}`,
      stateText: "Abierta",
      tone: "open",
      choice: leader || "Pendiente",
      meta: `!voto obstaculo${slot} sigue|stop`,
    });
  }

  const winner = String(poll.winner || leader || "PENDIENTE").toUpperCase();
  const tone = winner === "STOP" ? "red" : winner === "SIGUE" ? "green" : "pending";
  return voteCardMarkup({
    label: `Obstaculo ${slot}`,
    stateText: winner === "STOP" ? "STOP" : winner === "SIGUE" ? "SIGUE" : "Pendiente",
    tone,
    choice: winner === "STOP" ? "STOP" : winner === "SIGUE" ? "SIGUE" : "Pendiente",
    meta: `${totalVotes} votos cerrados`,
  });
}

function algorithmVoteCardMarkup() {
  const poll = latestPollForKinds(["algorithm"]);
  if (!poll) {
    return voteCardMarkup({ label: "Algoritmo", stateText: "Pendiente", tone: "pending", choice: "Sin decidir", meta: "Aun no hay votacion cerrada" });
  }

  const totalVotes = totalPollVotes(poll);
  const leader = pollLeader(poll);
  if (poll.status === "open") {
    return voteCardMarkup({
      label: "Algoritmo",
      stateText: "Abierta",
      tone: "open",
      choice: leader || "Pendiente",
      meta: "!voto algoritmo dfs|bfs",
    });
  }

  const winner = String(poll.winner || leader || "SIN DECIDIR").toUpperCase();
  return voteCardMarkup({
    label: "Algoritmo",
    stateText: "Cerrada",
    tone: "green",
    choice: winner,
    meta: `${totalVotes} votos cerrados`,
  });
}

function renderVoteSummary(targetId) {
  const element = $(targetId);
  if (!element) return;
  element.innerHTML = `
    ${algorithmVoteCardMarkup()}
    <div class="vote-summary-grid">
      ${obstacleVoteCardMarkup(1)}
      ${obstacleVoteCardMarkup(2)}
      ${obstacleVoteCardMarkup(3)}
    </div>
  `;
}

function pollTargetHint(poll) {
  const kind = String(poll.kind || "").toLowerCase();
  if (kind === "algorithm") return "algoritmo";
  if (kind === "obstacle_1") return "obstaculo1";
  if (kind === "obstacle_2") return "obstaculo2";
  if (kind === "obstacle_3") return "obstaculo3";
  return String(poll.id);
}

function renderPollList(targetId, { closable = false, votable = false } = {}) {
  const container = $(targetId);
  if (!container) return;
  container.innerHTML = state.polls
    .map((poll) => {
      const results = poll.results || poll.options.map((option) => ({ choice: option, votes: 0 }));
      const totalVotes = Number(poll.total_votes ?? results.reduce((sum, item) => sum + Number(item.votes || 0), 0));
      const options = votable
        ? poll.options.map((option) => `<button data-vote="${poll.id}:${escapeAttr(option)}" type="button">${escapeHtml(option)}</button>`).join("")
        : "";
      const optionHint = poll.options.map((option) => String(option).toLowerCase()).join(" / ");
      const targetHint = pollTargetHint(poll);
      const twitchHint = poll.status === "open"
        ? `<div class="poll-subtitle">Twitch: <strong>!voto ${escapeHtml(targetHint)} ${escapeHtml(optionHint)}</strong></div>`
        : "";
      const resultBars = results
        .map((item) => {
          const votes = Number(item.votes || 0);
          const percent = totalVotes > 0 ? Math.round((votes / totalVotes) * 100) : 0;
          return `
            <div class="poll-result">
              <span>${escapeHtml(item.choice)}</span>
              <div><i style="width:${percent}%"></i></div>
              <strong>${votes}</strong>
            </div>
          `;
        })
        .join("");
      const closeButton = closable && poll.status === "open" ? `<button class="small danger" data-close-poll="${poll.id}" type="button">Cerrar encuesta</button>` : "";
      return `
        <div class="poll">
          <div class="poll-title">${escapeHtml(poll.title)}</div>
          ${twitchHint}
          ${options ? `<div class="poll-options">${options}</div>` : ""}
          <div class="poll-results">${resultBars}</div>
          <footer>
            <span>${escapeHtml(poll.kind)} - ${escapeHtml(poll.status)}${poll.winner ? ` - ganador ${escapeHtml(poll.winner)}` : ""}</span>
            ${closeButton}
          </footer>
        </div>
      `;
    })
    .join("");
}

function renderPolls() {
  renderVoteSummary("adminVoteSummary");
  renderVoteSummary("userVoteSummary");
  renderPollList("adminPolls", { closable: true, votable: false });
  renderPollList("userPolls", { closable: false, votable: false });
}

async function createPollFromTemplate(kind) {
  const templates = {
    algorithm: { kind: "algorithm", title: "Algoritmo de la proxima carrera", options: ["DFS", "BFS"] },
    obstacle_1: { kind: "obstacle_1", title: "Obstaculo 1", options: ["SIGUE", "STOP"] },
    obstacle_2: { kind: "obstacle_2", title: "Obstaculo 2", options: ["SIGUE", "STOP"] },
    obstacle_3: { kind: "obstacle_3", title: "Obstaculo 3", options: ["SIGUE", "STOP"] },
  };
  await api("/api/polls", { method: "POST", body: JSON.stringify(templates[kind]) });
  await loadPolls();
}

async function handlePollClick(event) {
  const vote = event.target.dataset.vote;
  const close = event.target.dataset.closePoll;
  if (vote) {
    if (!state.user) return setOperatorMessage("Crea un usuario antes de votar");
    const [pollId, choice] = vote.split(":");
    await api(`/api/polls/${pollId}/vote`, { method: "POST", body: JSON.stringify({ user_id: state.user.id, choice }) });
    await Promise.all([loadPolls(), loadUserDetail()]);
  }
  if (close) {
    await api(`/api/polls/${close}/close`, { method: "POST" });
    await loadPolls();
  }
}

async function startRun() {
  await api("/api/run/start", {
    method: "POST",
    body: JSON.stringify({ algorithm: $("startAlgorithm").value, speed_limit: Number($("startSpeed").value) }),
  });
  setOperatorMessage("Carrera iniciada");
  await refreshState();
}

async function stopRun() {
  await api("/api/run/stop", { method: "POST" });
  setOperatorMessage("Parada enviada");
  await refreshState();
}

async function resetRun() {
  await api("/api/run/reset", { method: "POST" });
  state.counters.crossings = 0;
  state.counters.lastNode = null;
  setOperatorMessage("Reset enviado");
  await refreshState();
}

async function calibrateQtr() {
  await api("/api/operator/command", { method: "POST", body: JSON.stringify({ type: "CALIBRATE_QTR" }) });
  setOperatorMessage("Calibracion solicitada");
}

async function setTurnPause(enabled) {
  await api("/api/operator/command", {
    method: "POST",
    body: JSON.stringify({ type: "SET_TURN_PAUSE", payload: { enabled } }),
  });
  setOperatorMessage(enabled ? "Pausa en cruces activada" : "Pausa en cruces desactivada");
  await refreshState();
}

async function continueTurn() {
  await api("/api/operator/command", { method: "POST", body: JSON.stringify({ type: "CONTINUE_TURN" }) });
  setOperatorMessage("Continuar giro enviado");
  await refreshState();
}

async function setObstacleHandling(enabled) {
  await api("/api/operator/command", {
    method: "POST",
    body: JSON.stringify({ type: "SET_OBSTACLE_HANDLING", payload: { enabled } }),
  });
  setOperatorMessage(enabled ? "Obstaculos activados" : "Obstaculos desactivados");
  await refreshState();
}

async function setDrivePower(percent) {
  const safePercent = Math.max(5, Math.min(100, Number(percent) || 11));
  await api("/api/operator/command", {
    method: "POST",
    body: JSON.stringify({ type: "SET_DRIVE_POWER", payload: { percent: safePercent } }),
  });
  setText("drivePowerValue", `${safePercent}%`);
  setText("drivePowerHint", `PWM base objetivo: ${safePercent}%`);
  await refreshState();
}

function setOperatorMessage(text) {
  setText("operatorMessage", text);
  if (text) setTimeout(() => setText("operatorMessage", ""), 3500);
}

async function loadStreamDefaults() {
  try {
    const defaults = await api("/api/streams/defaults");
    const frontDefault = defaults.front || STREAM_DEFAULTS.front;
    STREAM_DEFAULTS.front = STALE_FRONT_URLS.has(frontDefault) ? STREAM_DEFAULTS.front : frontDefault;
    STREAM_DEFAULTS.overhead = defaults.overhead || STREAM_DEFAULTS.overhead;
  } catch (error) {
    log("URLs de camara por defecto no disponibles", { error: error.message });
  }
  $("frontUrl").placeholder = STREAM_DEFAULTS.front;
  $("overheadUrl").placeholder = STREAM_DEFAULTS.overhead;
}

function getStoredStreamUrl(storageKey, defaultUrl, staleUrls = new Set()) {
  const stored = localStorage.getItem(storageKey);
  if (!stored || staleUrls.has(stored)) {
    localStorage.setItem(storageKey, defaultUrl);
    return defaultUrl;
  }
  return stored;
}

function normalizeFrontCameraUrl(rawUrl) {
  const value = (rawUrl || STREAM_DEFAULTS.front).trim();
  if (!value) return STREAM_DEFAULTS.front;
  return value;
}

function frontStreamMode(url) {
  try {
    return new URL(url).pathname.toLowerCase().endsWith("/stream") ? "mjpeg" : "snapshot";
  } catch {
    return "snapshot";
  }
}

function cameraControlUrl(url, path = "/snapshot") {
  try {
    const parsed = new URL(url);
    if (parsed.port === "81") parsed.port = "";
    parsed.pathname = path;
    parsed.search = "";
    parsed.hash = "";
    return parsed.toString();
  } catch {
    return STREAM_DEFAULTS.front.replace(/:81\/stream$/i, path);
  }
}

function withCacheBust(url) {
  const separator = url.includes("?") ? "&" : "?";
  return `${url}${separator}t=${Date.now()}`;
}

function setVideoState(imageId, statusId, text, level = "loading") {
  const image = $(imageId);
  const status = $(statusId);
  if (status) {
    status.textContent = text;
    status.classList.toggle("online", level === "online");
    status.classList.toggle("warning", level === "warning");
    status.classList.toggle("error", level === "error");
  }
  const box = image?.closest(".video-box");
  if (box) {
    box.classList.toggle("stream-offline", level === "error" || level === "warning");
    box.dataset.placeholder = text;
  }
}

function setImageSource(imageId, statusId, url) {
  const image = $(imageId);
  if (!image) return;
  setVideoState(imageId, statusId, "Cargando", "loading");
  image.removeAttribute("src");
  image.src = url;
}

function stopStreamTimer(key) {
  if (state.streamTimers[key]) {
    clearTimeout(state.streamTimers[key]);
    delete state.streamTimers[key];
  }
}

function clearFrontImages() {
  ["frontStream", "userFrontStream"].forEach((imageId) => {
    const image = $(imageId);
    if (!image) return;
    image.onload = null;
    image.onerror = null;
    image.removeAttribute("src");
  });
}

function activeFrontTarget() {
  return state.activeView === "user"
    ? { imageId: "userFrontStream", statusId: "userFrontStatus", idleImageId: "frontStream", idleStatusId: "frontStatus" }
    : { imageId: "frontStream", statusId: "frontStatus", idleImageId: "userFrontStream", idleStatusId: "userFrontStatus" };
}

function startFrontSnapshotLoop(url) {
  stopStreamTimer("front");
  clearFrontImages();

  const loadFrame = () => {
    const requestUrl = withCacheBust(url);
    const preload = new Image();
    let settled = false;

    const schedule = (delay = FRONT_SNAPSHOT_INTERVAL_MS) => {
      state.streamTimers.front = setTimeout(loadFrame, delay);
    };

    const timeout = setTimeout(() => {
      if (settled) return;
      settled = true;
      state.streamFailures.front = (state.streamFailures.front || 0) + 1;
      setVideoState("frontStream", "frontStatus", "Reconectando", "warning");
      setVideoState("userFrontStream", "userFrontStatus", "Reconectando", "warning");
      schedule(Math.min(1600, FRONT_SNAPSHOT_INTERVAL_MS + state.streamFailures.front * 180));
    }, FRONT_SNAPSHOT_TIMEOUT_MS);

    preload.onload = () => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      state.streamFailures.front = 0;
      const frontImage = $("frontStream");
      const userFrontImage = $("userFrontStream");
      if (frontImage) frontImage.src = requestUrl;
      if (userFrontImage) userFrontImage.src = requestUrl;
      setVideoState("frontStream", "frontStatus", "Activo", "online");
      setVideoState("userFrontStream", "userFrontStatus", "Activo", "online");
      schedule();
    };

    preload.onerror = () => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      state.streamFailures.front = (state.streamFailures.front || 0) + 1;
      const text = state.streamFailures.front > 4 ? "Sin imagen" : "Reconectando";
      const level = state.streamFailures.front > 4 ? "error" : "warning";
      setVideoState("frontStream", "frontStatus", text, level);
      setVideoState("userFrontStream", "userFrontStatus", text, level);
      schedule(Math.min(2200, FRONT_SNAPSHOT_INTERVAL_MS + state.streamFailures.front * 250));
    };

    preload.src = requestUrl;
  };

  setVideoState("frontStream", "frontStatus", "Cargando", "loading");
  setVideoState("userFrontStream", "userFrontStatus", "Cargando", "loading");
  loadFrame();
}

function restartFrontMjpeg(url, delayMs) {
  stopStreamTimer("front");
  state.streamTimers.front = setTimeout(() => startFrontMjpegStream(url), delayMs);
}

function startFrontMjpegStream(url) {
  stopStreamTimer("front");
  clearFrontImages();

  const target = activeFrontTarget();
  const activeImage = $(target.imageId);
  if (!activeImage) return;

  state.streamFailures.front = state.streamFailures.front || 0;
  setVideoState(target.imageId, target.statusId, "Conectando", "loading");
  setVideoState(target.idleImageId, target.idleStatusId, "Vista en espera", "loading");

  activeImage.onload = () => {
    state.streamFailures.front = 0;
    setVideoState(target.imageId, target.statusId, "Activo", "online");
  };
  activeImage.onerror = () => {
    state.streamFailures.front = (state.streamFailures.front || 0) + 1;
    if (state.streamFailures.front > 4) {
      setVideoState(target.imageId, target.statusId, "Fallback snapshot", "warning");
      startFrontSnapshotLoop(cameraControlUrl(url, "/snapshot"));
      return;
    }
    setVideoState(target.imageId, target.statusId, "Reconectando", "warning");
    restartFrontMjpeg(url, Math.min(2600, 450 + state.streamFailures.front * 350));
  };
  activeImage.src = withCacheBust(url);
}

function startFrontStream(url) {
  if (frontStreamMode(url) === "mjpeg") {
    startFrontMjpegStream(url);
    return;
  }
  startFrontSnapshotLoop(url);
}

function applyStreams() {
  const front = normalizeFrontCameraUrl(getStoredStreamUrl("robobet_front_url", STREAM_DEFAULTS.front, STALE_FRONT_URLS));
  const overhead = getStoredStreamUrl("robobet_overhead_url", STREAM_DEFAULTS.overhead);
  localStorage.setItem("robobet_front_url", front);
  $("frontUrl").value = front;
  $("overheadUrl").value = overhead;
  startFrontStream(front);
  setImageSource("overheadStream", "overheadStatus", overhead);
  setImageSource("userOverheadStream", "userOverheadStatus", overhead);
}

function configureStreamFeedback() {
  [
    ["overheadStream", "overheadStatus"],
    ["userOverheadStream", "userOverheadStatus"],
  ].forEach(([imageId, statusId]) => {
    const image = $(imageId);
    const status = $(statusId);
    if (!image || !status) return;
    image.addEventListener("load", () => { status.textContent = "Activo"; });
    image.addEventListener("error", () => { status.textContent = "Sin imagen"; });
  });
}

function connectWs() {
  let ws;
  try {
    ws = new WebSocket(webSocketUrl("/ws/ui"));
  } catch (error) {
    log("WebSocket no disponible", { error: error.message });
    return;
  }

  ws.onopen = () => {
    state.wsOnline = true;
    log("WebSocket UI conectado");
  };
  ws.onclose = () => {
    state.wsOnline = false;
    log("WebSocket UI desconectado");
    setTimeout(connectWs, 1500);
  };
  ws.onerror = () => {
    state.wsOnline = false;
  };
  ws.onmessage = async (event) => {
    const payload = JSON.parse(event.data);
    if (payload.robot) updateRobot(payload.robot);
    if (payload.run) {
      state.run = payload.run;
      renderDashboard();
    }
    if (payload.type) log(payload.type, payload);
    if (["poll_created", "poll_vote", "poll_closed"].includes(payload.type)) {
      await safeLoad("Votaciones", loadPolls);
    }
    if (payload.type === "run_finished") {
      await Promise.all([safeLoad("Usuarios", loadUsers), safeLoad("Apuestas", loadBets), safeLoad("Detalle usuario", loadUserDetail), safeLoad("Votaciones", loadPolls), safeLoad("Historial", loadHistory)]);
    }
  };
}

function switchView(view) {
  document.querySelectorAll(".tab-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.view === view);
  });
  $("adminScreen").classList.toggle("active", view === "admin");
  $("userScreen").classList.toggle("active", view === "user");
  state.activeView = view;
  localStorage.setItem("robobet_view", view);
  const front = normalizeFrontCameraUrl(localStorage.getItem("robobet_front_url") || STREAM_DEFAULTS.front);
  if (frontStreamMode(front) === "mjpeg") {
    startFrontMjpegStream(front);
  }
}

function parseTwitchHash() {
  if (!location.hash.includes("access_token")) return;
  const params = new URLSearchParams(location.hash.slice(1));
  const token = params.get("access_token");
  if (token) localStorage.setItem("robobet_twitch_token", token);
  history.replaceState(null, "", location.pathname + location.search);
}

async function handleTwitchLogin() {
  const clientId = localStorage.getItem("robobet_twitch_client_id");
  const redirectUri = `${location.origin}${location.pathname}`;
  if (clientId) {
    const params = new URLSearchParams({
      client_id: clientId,
      redirect_uri: redirectUri,
      response_type: "token",
      scope: "user:read:email",
    });
    location.href = `https://id.twitch.tv/oauth2/authorize?${params.toString()}`;
    return;
  }

  state.twitch = {
    display_name: "twitch_demo",
    login: "twitch_demo",
    profile_image_url: "",
    mode: "demo",
  };
  localStorage.setItem("robobet_twitch_user", JSON.stringify(state.twitch));
  renderTwitch();
  await safeLoad("Usuario Twitch demo", syncTwitchUser);
}

async function loadTwitchProfile() {
  parseTwitchHash();
  const token = localStorage.getItem("robobet_twitch_token");
  if (!token) {
    renderTwitch();
    renderTwitchChat();
    return;
  }

  try {
    const payload = await api("/api/twitch/auth", {
      method: "POST",
      body: JSON.stringify({
        access_token: token,
      }),
    });
    state.twitch = payload.twitch_user || null;
    state.user = payload.user || state.user;
    localStorage.setItem("robobet_twitch_user", JSON.stringify(state.twitch));
    if (state.user) localStorage.setItem("robobet_user", JSON.stringify(state.user));
  } catch (error) {
    log("Twitch OAuth pendiente", { error: error.message });
  }
  renderTwitch();
  renderTwitchChat();
}

async function syncTwitchUser() {
  if (!state.twitch) return;
  const login = state.twitch.login || state.twitch.display_name || "twitch_demo";
  const displayName = state.twitch.display_name || login;
  state.user = await api("/api/users/twitch", {
    method: "POST",
    body: JSON.stringify({
      twitch_id: state.twitch.id || null,
      login,
      display_name: displayName,
      profile_image_url: state.twitch.profile_image_url || "",
    }),
  });
  localStorage.setItem("robobet_user", JSON.stringify(state.user));
  await Promise.all([loadUsers(), loadUserDetail()]);
}

function renderTwitch() {
  const avatar = $("twitchAvatar");
  const login = $("twitchLogin");
  const logout = $("twitchLogout");
  if (!state.twitch) {
    avatar.textContent = "TW";
    avatar.innerHTML = "TW";
    setText("twitchName", "Sin sesion Twitch");
    setText("twitchMeta", "OAuth preparado");
    login.classList.remove("hidden");
    logout.classList.add("hidden");
    renderTwitchChat();
    return;
  }

  const name = state.twitch.display_name || state.twitch.login || "Usuario Twitch";
  if (state.twitch.profile_image_url) {
    avatar.innerHTML = `<img src="${escapeAttr(state.twitch.profile_image_url)}" alt="">`;
  } else {
    avatar.textContent = name.slice(0, 2).toUpperCase();
  }
  setText("twitchName", name);
  setText("twitchMeta", state.twitch.mode === "demo" ? "Sesion demo" : "Sesion Twitch");
  login.classList.add("hidden");
  logout.classList.remove("hidden");
  const storedChannel = (localStorage.getItem("robobet_twitch_channel") || "").trim().replace(/^@/, "");
  if (!storedChannel || STALE_TWITCH_CHANNELS.has(storedChannel.toLowerCase())) {
    localStorage.setItem("robobet_twitch_channel", DEFAULT_TWITCH_CHANNEL);
  }
  renderTwitchChat();
}

function logoutTwitch() {
  state.twitch = null;
  localStorage.removeItem("robobet_twitch_user");
  localStorage.removeItem("robobet_twitch_token");
  renderTwitch();
}

function getTwitchChannel() {
  const stored = (localStorage.getItem("robobet_twitch_channel") || "").trim().replace(/^@/, "");
  if (!stored || STALE_TWITCH_CHANNELS.has(stored.toLowerCase())) {
    localStorage.setItem("robobet_twitch_channel", DEFAULT_TWITCH_CHANNEL);
    return DEFAULT_TWITCH_CHANNEL;
  }
  return stored;
}

function twitchParents() {
  const parents = new Set();
  const host = location.hostname || "localhost";
  parents.add(host);
  if (host === "127.0.0.1") parents.add("localhost");
  if (host === "localhost") parents.add("127.0.0.1");
  return [...parents].filter(Boolean);
}

function twitchChatUrl(channel) {
  const params = new URLSearchParams();
  twitchParents().forEach((parent) => params.append("parent", parent));
  return `https://www.twitch.tv/embed/${encodeURIComponent(channel)}/chat?${params.toString()}&darkpopout`;
}

function renderTwitchChat() {
  const channel = getTwitchChannel();
  const status = channel ? `#${channel}` : "Sin canal";
  setText("twitchChatStatus", status);
  setText("userTwitchChatStatus", status);
  const input = $("twitchChannel");
  if (input && input.value !== channel) input.value = channel;

  ["adminTwitchChat", "userTwitchChat"].forEach((id) => {
    const frame = $(id);
    if (!frame) return;
    if (!channel) {
      frame.removeAttribute("src");
      frame.classList.add("empty");
      return;
    }
    const src = twitchChatUrl(channel);
    if (frame.getAttribute("src") !== src) frame.setAttribute("src", src);
    frame.classList.remove("empty");
  });
}

function applyTwitchChannel() {
  const channel = $("twitchChannel").value.trim().replace(/^@/, "");
  localStorage.setItem("robobet_twitch_channel", channel);
  renderTwitchChat();
}

function tickSimulation() {
  state.simulation.tick += 1;
  const tick = state.simulation.tick;
  state.simulation.linePosition = 3500 + Math.sin(tick / 5) * 2500;
  if (tick % 18 === 0) state.simulation.crossings += 1;
  if (tick % 40 === 0) state.simulation.obstacleCount = (state.simulation.obstacleCount + 1) % 4;

  if (!state.backendOnline || !state.robot.connected) {
    state.robot = normalizeRobot({
      connected: false,
      state: tick % 40 > 30 ? "OBSTACLE_CHECK" : "FOLLOWING_LINE",
      algorithm: tick % 30 > 15 ? "BFS" : "DFS",
      speed_limit: tick % 28 > 18 ? 80 : 100,
      obstacle_count: state.simulation.obstacleCount,
      line_position: state.simulation.linePosition,
      elapsed_ms: Date.now() - state.simulation.startedAt,
      last_event: "SIMULATION_TICK",
      camera_sign: state.camera.sign || "NO_SIGN",
      camera_confidence: state.camera.confidence || 0,
    });
  }

  renderDashboard();
}

function bindEvents() {
  document.querySelectorAll("[data-view]").forEach((button) => {
    button.addEventListener("click", () => switchView(button.dataset.view));
  });

  $("refreshState").addEventListener("click", () => safeLoad("Estado robot", refreshState));
  $("testCamera").addEventListener("click", testCameraDetection);
  $("clearLog").addEventListener("click", () => { $("eventLog").textContent = ""; });

  $("applyFrontUrl").addEventListener("click", () => {
    localStorage.setItem("robobet_front_url", normalizeFrontCameraUrl($("frontUrl").value));
    applyStreams();
    safeLoad("Estado camara", refreshCameraStatus);
  });
  $("applyOverheadUrl").addEventListener("click", () => {
    localStorage.setItem("robobet_overhead_url", $("overheadUrl").value.trim());
    applyStreams();
  });

  $("startRun").addEventListener("click", () => safeLoad("Iniciar carrera", startRun));
  $("stopRun").addEventListener("click", () => safeLoad("Parar carrera", stopRun));
  $("resetRun").addEventListener("click", () => safeLoad("Reset carrera", resetRun));
  $("calibrateQtr").addEventListener("click", () => safeLoad("Calibrar QTR", calibrateQtr));
  $("enableObstacles").addEventListener("click", () => safeLoad("Activar obstaculos", () => setObstacleHandling(true)));
  $("disableObstacles").addEventListener("click", () => safeLoad("Quitar obstaculos", () => setObstacleHandling(false)));
  $("drivePowerSlider").addEventListener("input", (event) => {
    const value = Number(event.target.value || 11);
    setText("drivePowerValue", `${value}%`);
    setText("drivePowerHint", `PWM base objetivo: ${value}%`);
  });
  $("drivePowerSlider").addEventListener("change", (event) => safeLoad("Potencia motores", () => setDrivePower(event.target.value)));
  $("enableTurnPause").addEventListener("click", () => safeLoad("Pausar cruces", () => setTurnPause(true)));
  $("disableTurnPause").addEventListener("click", () => safeLoad("Quitar pausa cruces", () => setTurnPause(false)));
  $("continueTurn").addEventListener("click", () => safeLoad("Continuar giro", continueTurn));

  $("createUser").addEventListener("click", () => safeLoad("Crear usuario", createUser));
  $("userName").addEventListener("keydown", (event) => {
    if (event.key === "Enter") safeLoad("Crear usuario", createUser);
  });
  $("betKind").addEventListener("change", updateBetChoices);
  $("placeBet").addEventListener("click", () => safeLoad("Apostar", placeBet));
  $("loadPolls").addEventListener("click", () => safeLoad("Votaciones", loadPolls));
  $("adminLoadPolls").addEventListener("click", () => safeLoad("Votaciones", loadPolls));
  $("adminPolls").addEventListener("click", (event) => safeLoad("Cerrar votacion", () => handlePollClick(event)));
  document.querySelectorAll("[data-poll-template]").forEach((button) => {
    button.addEventListener("click", () => safeLoad("Crear votacion", () => createPollFromTemplate(button.dataset.pollTemplate)));
  });

  $("twitchLogin").addEventListener("click", () => safeLoad("Twitch", handleTwitchLogin));
  $("twitchLogout").addEventListener("click", logoutTwitch);
  $("applyTwitchChannel").addEventListener("click", applyTwitchChannel);
  $("twitchChannel").addEventListener("keydown", (event) => {
    if (event.key === "Enter") applyTwitchChannel();
  });
  $("fitGraph").addEventListener("click", renderMazeGraph);
  $("fullscreenGraph").addEventListener("click", () => {
    const canvas = $("graphCanvas");
    if (canvas?.requestFullscreen) canvas.requestFullscreen();
  });
}

async function boot() {
  bindEvents();
  updateBetChoices();
  renderUserPanel();
  renderTwitch();
  renderTwitchChat();
  configureStreamFeedback();
  switchView(localStorage.getItem("robobet_view") || "admin");
  await loadTwitchProfile();
  await loadStreamDefaults();
  applyStreams();
  connectWs();

  await Promise.all([
    safeLoad("Usuarios", loadUsers),
    safeLoad("Apuestas", loadBets),
    safeLoad("Detalle usuario", loadUserDetail),
    safeLoad("Votaciones", loadPolls),
    safeLoad("Estado robot", refreshState),
    safeLoad("Historial", loadHistory),
    safeLoad("Estado camara", refreshCameraStatus),
  ]);

  renderHistory();
  renderDashboard();
  setInterval(tickSimulation, 1000);
  setInterval(() => safeLoad("Estado camara", refreshCameraStatus), 5000);
  setInterval(() => safeLoad("Historial", loadHistory), 15000);
}

document.addEventListener("DOMContentLoaded", boot);
