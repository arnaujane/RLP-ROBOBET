const DEFAULT_API_BASE = "http://127.0.0.1:8000";
const STREAM_DEFAULTS = {
  front: "http://192.168.1.56/stream",
  overhead: "http://127.0.0.1:8081/video",
};
const STALE_FRONT_URLS = new Set(["http://192.168.4.2/stream", "http://esp32cam.local/stream"]);
const SENSOR_COUNT = 8;
const DEFAULT_QTR_THRESHOLD = 2500;

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
    NODE_DETECTED: "Cruce detectado",
    SELECTING_EDGE: "Seleccionando ruta",
    OBSTACLE_CHECK: "Obstaculo detectado",
    BACKTRACKING: "Retrocediendo",
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
  if (current.includes("BACKTRACK") || event.includes("BACKTRACK")) return "Retrocediendo";
  if (current.includes("SELECTING") || event.includes("EDGE")) return "Recalculando ruta";
  if (current.includes("FINISH") || event.includes("FINISH")) return "Finalizado";
  if (current.includes("OBSTACLE") || event.includes("OBSTACLE")) return "Obstaculo";
  if (current.includes("FOLLOWING") || current.includes("NODE")) return "Explorando";
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

  const running = ["FOLLOWING_LINE", "NODE_DETECTED", "SELECTING_EDGE", "OBSTACLE_CHECK", "BACKTRACKING"].includes(String(robot.state));
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

  $("sensorBoard").innerHTML = values
    .map((value, index) => `
      <div class="sensor ${active[index] ? "active" : ""}">
        <span class="sensor-index">S${index + 1}</span>
        <span class="sensor-light"></span>
        <span class="sensor-value">${Math.round(value)}</span>
      </div>
    `)
    .join("");

  setText("sensorSummary", `${activeCount}/8 activos`);
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
  const statePill = $("adminStatePill");
  const stateText = String(robot.state || "offline");

  statePill.className = "state-pill";
  if (!robot.connected || stateText.includes("ERROR")) statePill.classList.add("error");
  else if (stateText.includes("WAITING") || stateText === "connected") statePill.classList.add("warning");
  else if (stateText.includes("OBSTACLE") || stateText.includes("FINISH")) statePill.classList.add("info");

  statePill.textContent = label;
  setText("adminStateText", stateText);
  setText("adminAlertText", robot.connected ? robot.last_event || "Sin alertas activas" : "Robot desconectado");
  setText("currentTimer", formatTime(elapsed));
  setText("mazeTimer", formatTime(elapsed));
  setText("liveTime", formatTime(elapsed));
  setText("activeAlgorithm", robot.algorithm || "DFS");
  setText("mazeAlgorithm", robot.algorithm || "DFS");
  setText("liveAlgorithm", robot.algorithm || "DFS");
  setText("activeMode", phase);
  setText("algorithmName", robot.algorithm || "DFS");
  setText("algorithmPhase", phase);
  setText("activeNode", robot.current_node ?? "-");
  setText("mazeState", label);
  setText("liveRobotState", label);
  setText("mazeRouteState", phase);
  setText("mazeObstacles", robot.obstacle_count ?? 0);
  setText("liveObstacles", robot.obstacle_count ?? 0);
  setText("obstacleCount", robot.obstacle_count ?? 0);
  setText("liveRestrictions", restrictions);
  setText("mazeCrossings", Math.max(state.counters.crossings, state.simulation.crossings));

  const progress = phase === "Finalizado" ? 100 : phase === "Retrocediendo" ? 66 : phase === "Explorando" ? 42 : 18;
  $("algorithmProgress").style.width = `${progress}%`;

  const obstacleActive = phase === "Obstaculo" || Number(robot.obstacle_count) > 0;
  setText("obstacleStatus", obstacleActive ? "Obstaculo detectado" : "Sin obstaculos activos");
  setText("obstacleType", obstacleActive ? "Fisico o virtual segun evento" : "Preparado para deteccion");
}

function renderDashboard() {
  renderSourceBadges();
  renderRobotStatus();
  renderSensorBoard();
  renderMotorPower();
  renderCamera();
}

function formatVotes(votes = {}) {
  return `R ${votes.red ?? 0} - V ${votes.green ?? 0} - N ${votes.black ?? 0} - sin ${votes.none ?? 0}`;
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

function renderPolls() {
  $("polls").innerHTML = state.polls
    .map((poll) => {
      const results = poll.results || poll.options.map((option) => ({ choice: option, votes: 0 }));
      const totalVotes = Number(poll.total_votes ?? results.reduce((sum, item) => sum + Number(item.votes || 0), 0));
      const options = poll.options
        .map((option) => `<button data-vote="${poll.id}:${escapeAttr(option)}" type="button">${escapeHtml(option)}</button>`)
        .join("");
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
      const closeButton = poll.status === "open" ? `<button class="small danger" data-close-poll="${poll.id}" type="button">Cerrar</button>` : "";
      return `
        <div class="poll">
          <div class="poll-title">${escapeHtml(poll.title)}</div>
          <div class="poll-options">${options}</div>
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

async function createPollFromTemplate(kind) {
  const templates = {
    algorithm: { kind: "algorithm", title: "Algoritmo de la proxima carrera", options: ["DFS", "BFS"] },
    speed: { kind: "speed", title: "Limitar motores al 80%", options: ["80", "100"] },
    obstacle: { kind: "obstacle", title: "Activar obstaculo de usuario", options: ["edge_A", "edge_B", "edge_C"] },
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

function setImageSource(imageId, statusId, url) {
  const image = $(imageId);
  const status = $(statusId);
  if (!image) return;
  if (status) status.textContent = "Cargando";
  image.removeAttribute("src");
  image.src = url;
}

function applyStreams() {
  const front = getStoredStreamUrl("robobet_front_url", STREAM_DEFAULTS.front, STALE_FRONT_URLS);
  const overhead = getStoredStreamUrl("robobet_overhead_url", STREAM_DEFAULTS.overhead);
  $("frontUrl").value = front;
  $("overheadUrl").value = overhead;
  setImageSource("frontStream", "frontStatus", front);
  setImageSource("userFrontStream", "userFrontStatus", front);
  setImageSource("overheadStream", "overheadStatus", overhead);
  setImageSource("userOverheadStream", "userOverheadStatus", overhead);
}

function configureStreamFeedback() {
  [
    ["frontStream", "frontStatus"],
    ["userFrontStream", "userFrontStatus"],
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
    if (["run_finished", "poll_closed"].includes(payload.type)) {
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
  localStorage.setItem("robobet_view", view);
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
    profile_image_url: "",
    mode: "demo",
  };
  localStorage.setItem("robobet_twitch_user", JSON.stringify(state.twitch));
  renderTwitch();
}

async function loadTwitchProfile() {
  parseTwitchHash();
  const token = localStorage.getItem("robobet_twitch_token");
  const clientId = localStorage.getItem("robobet_twitch_client_id");
  if (!token || !clientId) {
    renderTwitch();
    return;
  }

  try {
    const response = await fetch("https://api.twitch.tv/helix/users", {
      headers: {
        Authorization: `Bearer ${token}`,
        "Client-Id": clientId,
      },
    });
    if (!response.ok) throw new Error(response.statusText);
    const payload = await response.json();
    state.twitch = payload.data?.[0] || null;
    localStorage.setItem("robobet_twitch_user", JSON.stringify(state.twitch));
  } catch (error) {
    log("Twitch OAuth pendiente", { error: error.message });
  }
  renderTwitch();
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
}

function logoutTwitch() {
  state.twitch = null;
  localStorage.removeItem("robobet_twitch_user");
  localStorage.removeItem("robobet_twitch_token");
  renderTwitch();
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
    localStorage.setItem("robobet_front_url", $("frontUrl").value.trim());
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

  $("createUser").addEventListener("click", () => safeLoad("Crear usuario", createUser));
  $("userName").addEventListener("keydown", (event) => {
    if (event.key === "Enter") safeLoad("Crear usuario", createUser);
  });
  $("betKind").addEventListener("change", updateBetChoices);
  $("placeBet").addEventListener("click", () => safeLoad("Apostar", placeBet));
  $("loadPolls").addEventListener("click", () => safeLoad("Votaciones", loadPolls));
  $("polls").addEventListener("click", (event) => safeLoad("Votacion", () => handlePollClick(event)));
  document.querySelectorAll("[data-poll-template]").forEach((button) => {
    button.addEventListener("click", () => safeLoad("Crear votacion", () => createPollFromTemplate(button.dataset.pollTemplate)));
  });

  $("twitchLogin").addEventListener("click", () => safeLoad("Twitch", handleTwitchLogin));
  $("twitchLogout").addEventListener("click", logoutTwitch);
}

async function boot() {
  bindEvents();
  updateBetChoices();
  renderUserPanel();
  renderTwitch();
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
