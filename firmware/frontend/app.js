const state = {
  user: JSON.parse(localStorage.getItem("robobet_user") || "null"),
  polls: [],
  users: [],
  robot: {},
};

const $ = (id) => document.getElementById(id);

function log(message, payload) {
  const line = `[${new Date().toLocaleTimeString()}] ${message}${payload ? ` ${JSON.stringify(payload)}` : ""}`;
  $("eventLog").textContent = `${line}\n${$("eventLog").textContent}`.slice(0, 8000);
}

async function api(path, options = {}) {
  const res = await fetch(path, {
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
    ...options,
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text || res.statusText);
  }
  return res.json();
}

function setMessage(text) {
  $("operatorMessage").textContent = text;
  if (text) setTimeout(() => ($("operatorMessage").textContent = ""), 3500);
}

function updateUserPanel() {
  if (!state.user) {
    $("activeUser").textContent = "Sin usuario";
    return;
  }
  $("activeUser").textContent = `${state.user.name} · ${state.user.balance} puntos`;
}

function updateRobot(robot) {
  state.robot = { ...state.robot, ...robot };
  const connected = Boolean(state.robot.connected);
  $("robotDot").classList.toggle("online", connected);
  $("robotConnection").textContent = connected ? "Robot conectado" : "Robot offline";
  $("robotState").textContent = state.robot.state || "-";
  $("robotAlgorithm").textContent = state.robot.algorithm || "DFS";
  $("robotSpeed").textContent = `${state.robot.speed_limit || 100}%`;
  $("robotNode").textContent = state.robot.current_node ?? "-";
  $("robotObstacles").textContent = state.robot.obstacle_count ?? 0;
  $("robotEvent").textContent = state.robot.last_event || state.robot.event || "-";
}

async function loadUsers() {
  state.users = await api("/api/users");
  $("ranking").innerHTML = state.users
    .map((user) => `<li><strong>${escapeHtml(user.name)}</strong> · ${user.balance} puntos</li>`)
    .join("");
  if (state.user) {
    const fresh = state.users.find((user) => user.id === state.user.id);
    if (fresh) {
      state.user = fresh;
      localStorage.setItem("robobet_user", JSON.stringify(fresh));
      updateUserPanel();
    }
  }
}

async function loadBets() {
  const suffix = state.user ? `?user_id=${state.user.id}` : "";
  const bets = await api(`/api/bets${suffix}`);
  $("betsList").innerHTML = bets
    .map((bet) => `<li>${bet.kind}: <strong>${bet.choice}</strong> · ${bet.amount} pts · ${bet.status}</li>`)
    .join("");
}

function renderPolls() {
  $("polls").innerHTML = state.polls
    .map((poll) => {
      const options = poll.options
        .map((option) => `<button data-vote="${poll.id}:${escapeAttr(option)}">${escapeHtml(option)}</button>`)
        .join("");
      const closeButton =
        poll.status === "open" ? `<button class="small danger" data-close-poll="${poll.id}">Cerrar</button>` : "";
      return `
        <div class="poll">
          <div class="poll-title">${escapeHtml(poll.title)}</div>
          <div class="poll-options">${options}</div>
          <footer>
            <span>${poll.kind} · ${poll.status}${poll.winner ? ` · ganador ${escapeHtml(poll.winner)}` : ""}</span>
            ${closeButton}
          </footer>
        </div>
      `;
    })
    .join("");
}

async function loadPolls() {
  state.polls = await api("/api/polls");
  renderPolls();
}

function updateBetChoices() {
  const kind = $("betKind").value;
  const choices = {
    finish: [
      ["yes", "Sí llega"],
      ["no", "No llega"],
    ],
    time_range: [
      ["fast", "< 60 s"],
      ["medium", "60-120 s"],
      ["slow", "> 120 s"],
    ],
    obstacles: [
      ["0", "0"],
      ["1", "1"],
      ["2+", "2 o más"],
    ],
    algorithm: [
      ["DFS", "DFS"],
      ["BFS", "BFS"],
    ],
  }[kind];
  $("betChoice").innerHTML = choices.map(([value, label]) => `<option value="${value}">${label}</option>`).join("");
}

async function createPollFromTemplate(kind) {
  const templates = {
    algorithm: {
      kind: "algorithm",
      title: "¿Qué algoritmo debe usar el robot?",
      options: ["DFS", "BFS"],
    },
    speed: {
      kind: "speed",
      title: "¿Activar restricción de motor al 80%?",
      options: ["80", "100"],
    },
    obstacle: {
      kind: "obstacle",
      title: "¿Dónde colocamos el obstáculo?",
      options: ["edge_A", "edge_B", "edge_C"],
    },
  };
  await api("/api/polls", { method: "POST", body: JSON.stringify(templates[kind]) });
  await loadPolls();
}

async function refreshState() {
  const payload = await api("/api/run/state");
  updateRobot(payload.robot || {});
}

function connectWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${proto}://${location.host}/ws/ui`);
  ws.onopen = () => log("WebSocket UI conectado");
  ws.onclose = () => {
    log("WebSocket UI desconectado");
    setTimeout(connectWs, 1500);
  };
  ws.onmessage = async (event) => {
    const payload = JSON.parse(event.data);
    if (payload.robot) updateRobot(payload.robot);
    if (payload.type) log(payload.type, payload);
    if (["run_finished", "poll_closed"].includes(payload.type)) {
      await Promise.all([loadUsers(), loadBets(), loadPolls()]);
    }
  };
}

function applyStreams() {
  const front = localStorage.getItem("robobet_front_url") || $("frontUrl").placeholder;
  const overhead = localStorage.getItem("robobet_overhead_url") || $("overheadUrl").placeholder;
  $("frontUrl").value = front;
  $("overheadUrl").value = overhead;
  $("frontStream").src = front;
  $("overheadStream").src = overhead;
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#039;" }[char]));
}

function escapeAttr(value) {
  return escapeHtml(value).replace(/:/g, "&#58;");
}

document.addEventListener("DOMContentLoaded", async () => {
  updateBetChoices();
  updateUserPanel();
  applyStreams();
  connectWs();

  $("createUser").addEventListener("click", async () => {
    const name = $("userName").value.trim();
    if (!name) return;
    state.user = await api("/api/users", { method: "POST", body: JSON.stringify({ name }) });
    localStorage.setItem("robobet_user", JSON.stringify(state.user));
    updateUserPanel();
    await loadUsers();
  });

  $("betKind").addEventListener("change", updateBetChoices);
  $("placeBet").addEventListener("click", async () => {
    if (!state.user) return setMessage("Crea un usuario antes de apostar");
    await api("/api/bets", {
      method: "POST",
      body: JSON.stringify({
        user_id: state.user.id,
        kind: $("betKind").value,
        choice: $("betChoice").value,
        amount: Number($("betAmount").value),
      }),
    });
    await Promise.all([loadUsers(), loadBets()]);
  });

  $("startRun").addEventListener("click", async () => {
    await api("/api/run/start", {
      method: "POST",
      body: JSON.stringify({ algorithm: $("startAlgorithm").value, speed_limit: Number($("startSpeed").value) }),
    });
    setMessage("Carrera iniciada");
  });
  $("stopRun").addEventListener("click", async () => {
    await api("/api/run/stop", { method: "POST" });
    setMessage("Parada enviada");
  });
  $("resetRun").addEventListener("click", async () => {
    await api("/api/run/reset", { method: "POST" });
    setMessage("Reset enviado");
  });
  $("calibrateQtr").addEventListener("click", async () => {
    await api("/api/operator/command", { method: "POST", body: JSON.stringify({ type: "CALIBRATE_QTR" }) });
    setMessage("Calibración solicitada");
  });

  $("loadPolls").addEventListener("click", loadPolls);
  $("polls").addEventListener("click", async (event) => {
    const vote = event.target.dataset.vote;
    const close = event.target.dataset.closePoll;
    if (vote) {
      if (!state.user) return setMessage("Crea un usuario antes de votar");
      const [pollId, choice] = vote.split(":");
      await api(`/api/polls/${pollId}/vote`, { method: "POST", body: JSON.stringify({ user_id: state.user.id, choice }) });
    }
    if (close) {
      await api(`/api/polls/${close}/close`, { method: "POST" });
      await loadPolls();
    }
  });
  document.querySelectorAll("[data-poll-template]").forEach((button) => {
    button.addEventListener("click", () => createPollFromTemplate(button.dataset.pollTemplate));
  });

  $("applyFrontUrl").addEventListener("click", () => {
    localStorage.setItem("robobet_front_url", $("frontUrl").value);
    applyStreams();
  });
  $("applyOverheadUrl").addEventListener("click", () => {
    localStorage.setItem("robobet_overhead_url", $("overheadUrl").value);
    applyStreams();
  });
  $("refreshState").addEventListener("click", refreshState);
  $("clearLog").addEventListener("click", () => ($("eventLog").textContent = ""));

  await Promise.all([loadUsers(), loadBets(), loadPolls(), refreshState()]);
});

