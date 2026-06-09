/* Smart Morning Assistant — Authenticated Dashboard Script */

const API_BASE = "";   // relative URLs work when served by the FastAPI backend

// ── Auth guard ────────────────────────────────────────────────────────────────

const AUTH_TOKEN = localStorage.getItem("auth_token");

// Redirect to login immediately if no token is stored
if (!AUTH_TOKEN) {
  window.location.replace("/login.html");
}

// ── Auth-aware fetch helpers ──────────────────────────────────────────────────

// Wraps fetch() and automatically adds the Authorization header
function authFetch(url, options = {}) {
  return fetch(url, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      "Authorization": `Bearer ${AUTH_TOKEN}`,
      ...(options.headers || {}),
    },
  });
}

// Calls authFetch and redirects to login on 401
async function apiCall(url, options = {}) {
  const resp = await authFetch(url, options);
  if (resp.status === 401) {
    localStorage.removeItem("auth_token");
    localStorage.removeItem("user_id");
    window.location.replace("/login.html");
    return null;
  }
  return resp;
}

// ── Clock ─────────────────────────────────────────────────────────────────────

function updateClock() {
  document.getElementById("clock").textContent = new Date().toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}
setInterval(updateClock, 1000);
updateClock();

// ── Refresh all data from the combined endpoint ───────────────────────────────

async function refreshAll() {
  const btn = document.getElementById("refresh-btn");
  btn.textContent = "↻ Loading…";
  btn.disabled = true;

  try {
    const resp = await apiCall(`${API_BASE}/api/dashboard-data`);
    if (!resp) return;   // redirected to login
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const data = await resp.json();

    // Show the logged-in username in the header
    if (data.user) {
      document.getElementById("user-badge").innerHTML =
        `Hi, <strong>${esc(data.user.username)}</strong>`;
    }

    renderWeather(data.weather);
    renderSchedule(data.schedule);
    renderNews(data.news);

    setStatus(`Last updated: ${new Date().toLocaleTimeString()}`);
  } catch (err) {
    setStatus("⚠ Failed to reach backend — is it running on port 8000?", true);
    console.error("Refresh error:", err);
  } finally {
    btn.textContent = "↻ Refresh";
    btn.disabled = false;
  }
}

// ── Render helpers ────────────────────────────────────────────────────────────

function renderWeather(w) {
  document.getElementById("weather-body").innerHTML = `
    <div>
      <div class="weather-temp">${w.temperature}°C</div>
      <div class="weather-condition">${esc(w.condition)}</div>
      <div class="weather-location">📍 ${esc(w.location)}</div>
      <div class="weather-meta">
        <span>💧 Humidity: ${w.humidity}%</span>
        <span>💨 Wind: ${w.wind_speed} km/h</span>
      </div>
    </div>`;
  document.getElementById("location-input").value = w.location;
}

function renderSchedule(s) {
  const el = document.getElementById("schedule-body");
  if (!s.items || s.items.length === 0) {
    el.innerHTML = `<p class="empty-msg">No events scheduled for today.</p>`;
    return;
  }
  el.innerHTML = `
    <ul class="schedule-list">
      ${s.items.map(item => `
        <li class="schedule-item">
          <span class="sched-time">${esc(item.time)}</span>
          <span class="sched-task">${esc(item.task)}</span>
          <button class="delete-btn" onclick="deleteItem(${item.id})" title="Remove">✕</button>
        </li>`).join("")}
    </ul>`;
}

function renderNews(n) {
  const el = document.getElementById("news-body");
  if (!n.headlines || n.headlines.length === 0) {
    el.innerHTML = `<p class="empty-msg">No headlines available.</p>`;
    return;
  }
  el.innerHTML = `
    <ul class="news-list">
      ${n.headlines.map((h, i) => `
        <li class="news-item">
          <span class="news-num">${i + 1}.</span>
          <span>${esc(h)}</span>
        </li>`).join("")}
    </ul>`;
}

// ── Actions ───────────────────────────────────────────────────────────────────

async function updateLocation(event) {
  event.preventDefault();
  const location = document.getElementById("location-input").value.trim();
  if (!location) return;

  try {
    const resp = await apiCall(`${API_BASE}/api/location`, {
      method: "POST",
      body: JSON.stringify({ location }),
    });
    if (!resp) return;
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    await refreshAll();
  } catch (err) {
    setStatus("⚠ Failed to update location.", true);
    console.error(err);
  }
}

async function addScheduleItem(event) {
  event.preventDefault();
  const time = document.getElementById("sched-time").value;
  const task = document.getElementById("sched-task").value.trim();
  if (!time || !task) return;

  try {
    const resp = await apiCall(`${API_BASE}/api/schedule`, {
      method: "POST",
      body: JSON.stringify({ time, task }),
    });
    if (!resp) return;
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    document.getElementById("sched-task").value = "";
    await refreshAll();
  } catch (err) {
    setStatus("⚠ Failed to add schedule item.", true);
    console.error(err);
  }
}

async function deleteItem(id) {
  try {
    const resp = await apiCall(`${API_BASE}/api/schedule/${id}`, { method: "DELETE" });
    if (!resp) return;
    await refreshAll();
  } catch (err) {
    setStatus("⚠ Failed to delete item.", true);
    console.error(err);
  }
}

function logout() {
  localStorage.removeItem("auth_token");
  localStorage.removeItem("user_id");
  window.location.href = "/login.html";
}

// ── Utilities ─────────────────────────────────────────────────────────────────

function setStatus(msg, isError = false) {
  const el = document.getElementById("status");
  el.textContent = msg;
  el.style.color = isError ? "var(--red)" : "";
  if (isError) setTimeout(() => { el.style.color = ""; }, 5000);
}

function esc(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// ── Device API Key ────────────────────────────────────────────────────────────

async function loadDeviceToken() {
  try {
    const resp = await apiCall(`${API_BASE}/api/device/token`);
    if (!resp || !resp.ok) return;
    renderDeviceToken(await resp.json());
  } catch (err) {
    console.error("Device token load error:", err);
  }
}

function renderDeviceToken(dt) {
  const lastUsed = dt.last_used_at
    ? `Last used: ${new Date(dt.last_used_at).toLocaleString()}`
    : "Never used yet";

  document.getElementById("device-body").innerHTML = `
    <p class="device-hint">
      Paste this key into <code>DEVICE_API_KEY</code> in your ESP32 sketch.
      It never expires — regenerate only if the key is compromised.
    </p>
    <div class="device-token-row">
      <code class="device-token-value" id="device-key-display">${esc(dt.token)}</code>
      <button class="copy-btn" onclick="copyDeviceKey()" title="Copy key">📋</button>
    </div>
    <p class="device-meta">Created: ${new Date(dt.created_at).toLocaleDateString()} · ${esc(lastUsed)}</p>
    <button class="regen-btn" onclick="regenerateDeviceKey()">🔄 Regenerate Key</button>`;
}

async function copyDeviceKey() {
  const key = document.getElementById("device-key-display")?.textContent;
  if (!key) return;
  try {
    await navigator.clipboard.writeText(key);
    setStatus("Device key copied to clipboard!");
  } catch {
    // Fallback for non-HTTPS contexts (e.g. plain HTTP dev server)
    const ta = document.createElement("textarea");
    ta.value = key;
    document.body.appendChild(ta);
    ta.select();
    document.execCommand("copy");
    ta.remove();
    setStatus("Device key copied!");
  }
}

async function regenerateDeviceKey() {
  if (!confirm(
    "Regenerate your device key?\n\n" +
    "The current key will stop working immediately.\n" +
    "You will need to update your ESP32 sketch with the new key."
  )) return;

  try {
    const resp = await apiCall(`${API_BASE}/api/device/token`, { method: "POST" });
    if (!resp || !resp.ok) return;
    renderDeviceToken(await resp.json());
    setStatus("New device key generated. Update your ESP32 sketch and re-flash.");
  } catch (err) {
    setStatus("⚠ Failed to regenerate key.", true);
    console.error(err);
  }
}

// ── Boot ──────────────────────────────────────────────────────────────────────

refreshAll();
loadDeviceToken();
setInterval(refreshAll, 5 * 60 * 1000);   // auto-refresh every 5 minutes
