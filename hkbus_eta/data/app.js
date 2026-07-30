"use strict";

const DEFAULT_ROUTES = [
  { route: "A43", stopId: "" },
  { route: "278A", stopId: "" },
  { route: "277X", stopId: "" }
];

const DEMO_STATUS = {
  configured: true,
  online: true,
  stopLabel: "上水",
  updatedAt: new Date().toISOString(),
  routes: [
    { route: "A43", eta: [3, 18, 32] },
    { route: "278A", eta: [7, 21, 39] },
    { route: "277X", eta: [1, 13, 28] }
  ],
  weather: {
    temperature: 29,
    humidity: 78,
    icon: 60,
    state: "normal",
    label: "多雲"
  }
};

const $ = (selector) => document.querySelector(selector);
const routeList = $("#route-list");
const settingsDialog = $("#settings-dialog");
const settingsForm = $("#settings-form");
const formStatus = $("#form-status");
let demoMode = location.protocol === "file:" ||
  new URLSearchParams(location.search).get("demo") === "1";
let lastSuccessfulUpdate = 0;

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function normaliseRoutes(routes) {
  return DEFAULT_ROUTES.map((fallback, index) => {
    const item = routes?.[index] || fallback;
    return {
      route: String(item.route || fallback.route).toUpperCase().slice(0, 8),
      eta: Array.isArray(item.eta) ? item.eta.slice(0, 3) : []
    };
  });
}

function renderRoutes(routes) {
  routeList.innerHTML = normaliseRoutes(routes).map((item) => {
    const eta = [0, 1, 2].map((index) => {
      const value = item.eta[index];
      const shown = Number.isFinite(value) ? Math.max(0, value) : "—";
      const unavailable = shown === "—" ? " unavailable" : "";
      return `<span class="eta-value${unavailable}">
        <b>${shown}</b>${shown === "—" ? "" : "<small>分</small>"}
      </span>`;
    }).join("");
    return `<article class="route-row">
      <strong class="route-number">${escapeHtml(item.route)}</strong>${eta}
    </article>`;
  }).join("");
}

function warningLabel(state, fallback) {
  const labels = {
    "rain-amber": "黃色暴雨警告",
    "rain-red": "紅色暴雨警告",
    "rain-black": "黑色暴雨警告",
    "tc-1": "一號風球",
    "tc-3": "三號風球",
    "tc-8": "八號風球",
    "tc-9": "九號風球",
    "tc-10": "十號風球"
  };
  return labels[state] || fallback || "天氣資料";
}

function weatherGlyph(icon) {
  if ([50, 51, 52, 53, 54].includes(icon)) return "☀";
  if ([60, 61, 62, 63, 64, 65].includes(icon)) return "⛅";
  if ([70, 71, 72, 73, 74, 75, 76, 77].includes(icon)) return "☂";
  if ([80, 81, 82, 83, 84, 85].includes(icon)) return "⚡";
  if ([90, 91, 92, 93].includes(icon)) return "❄";
  return "☁";
}

function renderStatus(status) {
  const weather = status.weather || {};
  renderRoutes(status.routes);
  $("#temperature").textContent = Number.isFinite(weather.temperature)
    ? `${weather.temperature}°` : "--°";
  $("#humidity").textContent = Number.isFinite(weather.humidity)
    ? `${weather.humidity}%` : "--%";

  const state = weather.state || "normal";
  $("#weather-icon").dataset.state = state;
  $(".icon-main").textContent = weatherGlyph(Number(weather.icon));
  $("#weather-label").textContent = warningLabel(state, weather.label);

  const updateState = $("#update-state");
  updateState.classList.toggle("error", !status.online);
  if (!status.configured) {
    updateState.textContent = "需要設定";
    settingsDialog.showModal();
  } else if (status.online) {
    updateState.textContent = demoMode ? "示範" : "已更新";
  } else {
    updateState.textContent = "離線資料";
  }
  lastSuccessfulUpdate = Date.now();
}

function updateClock() {
  const now = new Date();
  const hkt = new Date(now.toLocaleString("en-US", { timeZone: "Asia/Hong_Kong" }));
  $("#date").textContent =
    `${String(hkt.getDate()).padStart(2, "0")}/${String(hkt.getMonth() + 1).padStart(2, "0")}`;
  $("#weekday").textContent =
    ["星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"][hkt.getDay()];
  $("#clock").textContent =
    `${String(hkt.getHours()).padStart(2, "0")}:${String(hkt.getMinutes()).padStart(2, "0")}`;
}

async function updateBattery() {
  if (!navigator.getBattery) return;
  try {
    const battery = await navigator.getBattery();
    const render = () => {
      const value = Math.round(battery.level * 100);
      $("#battery-text").textContent = battery.charging ? `${value}%⚡` : `${value}%`;
      $("#battery-fill").style.width = `${value}%`;
    };
    render();
    battery.addEventListener("levelchange", render);
    battery.addEventListener("chargingchange", render);
  } catch {
    // Battery Status API is optional; "--%" remains a valid fallback.
  }
}

async function fetchJson(url, options = {}) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 8000);
  try {
    const response = await fetch(url, { ...options, signal: controller.signal });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return await response.json();
  } finally {
    clearTimeout(timer);
  }
}

async function refreshStatus() {
  if (demoMode) {
    renderStatus({ ...DEMO_STATUS, updatedAt: new Date().toISOString() });
    return;
  }
  try {
    renderStatus(await fetchJson("/api/status", { cache: "no-store" }));
  } catch {
    const state = $("#update-state");
    state.textContent = lastSuccessfulUpdate ? "連線中斷" : "無法連線";
    state.classList.add("error");
  }
}

function buildRouteSettings(routes = DEFAULT_ROUTES) {
  $("#route-settings").innerHTML = DEFAULT_ROUTES.map((fallback, index) => {
    const item = routes[index] || fallback;
    return `<div class="route-setting">
      <label>路線
        <input name="route${index}" value="${escapeHtml(item.route || fallback.route)}"
               maxlength="8" required>
      </label>
      <label>巴士站 ID
        <input name="stopId${index}" value="${escapeHtml(item.stopId || "")}"
               maxlength="20" placeholder="例如 B7A1E0A7..." required>
      </label>
    </div>`;
  }).join("");
}

async function loadConfig() {
  buildRouteSettings();
  try {
    const config = await fetchJson("/api/config", { cache: "no-store" });
    $("#ssid").value = config.ssid || "";
    $("#stopLabel").value = config.stopLabel || "上水";
    buildRouteSettings(config.routes || DEFAULT_ROUTES);
  } catch {
    formStatus.textContent = "未連接裝置；你仍可使用示範模式預覽。";
  }
}

$("#settings-button").addEventListener("click", async () => {
  await loadConfig();
  settingsDialog.showModal();
});

$("#close-settings").addEventListener("click", () => settingsDialog.close());

$("#demo-button").addEventListener("click", () => {
  demoMode = true;
  settingsDialog.close();
  refreshStatus();
});

settingsForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  formStatus.textContent = "正在儲存…";
  const data = new FormData(settingsForm);
  const payload = {
    ssid: data.get("ssid"),
    password: data.get("password"),
    stopLabel: data.get("stopLabel"),
    routes: DEFAULT_ROUTES.map((_, index) => ({
      route: data.get(`route${index}`),
      stopId: data.get(`stopId${index}`)
    }))
  };
  try {
    await fetchJson("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    formStatus.textContent = "已儲存，裝置正在重新啟動。";
    setTimeout(() => location.reload(), 5000);
  } catch {
    formStatus.textContent = "儲存失敗，請確認你仍然連接裝置。";
  }
});

renderRoutes(DEFAULT_ROUTES);
buildRouteSettings();
updateClock();
updateBattery();
refreshStatus();
setInterval(updateClock, 1000);
setInterval(refreshStatus, 30000);
