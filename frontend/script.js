/* Smart Morning Assistant — Dashboard Script */

// Empty string = relative URLs (works when frontend is served by the backend).
// Change to "http://localhost:8000" if you open index.html directly in a browser.
const API_BASE = "";

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

function updateClock() {
  const now = new Date();
  document.getElementById("clock").textContent = now.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}
setInterval(updateClock, 1000);
updateClock();

// ---------------------------------------------------------------------------
// Refresh all data from the combined endpoint
// ---------------------------------------------------------------------------

async function refreshAll() {
  const btn = document.getElementById("refresh-btn");
  btn.textContent = "↻ Loading…";
  btn.disabled = true;

  try {
    const resp = await fetch(`${API_BASE}/api/dashboard-data`);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const data = await resp.json();

    renderWeather(data.weather);
    renderSchedule(data.schedule);
    renderNews(data.news);

    setStatus(`Last updated: ${new Date().toLocaleTimeString()}`);
  } catch (err) {
    setStatus(`⚠ Failed to reach backend — is it running on port 8000?`, true);
    console.error("Refresh error:", err);
  } finally {
    btn.textContent = "↻ Refresh";
    btn.disabled = false;
  }
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

async function updateLocation(event) {
  event.preventDefault();
  const location = document.getElementById("location-input").value.trim();
  if (!location) return;

  try {
    const resp = await fetch(`${API_BASE}/api/location`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ location }),
    });
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
    const resp = await fetch(`${API_BASE}/api/schedule`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ time, task }),
    });
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
    await fetch(`${API_BASE}/api/schedule/${id}`, { method: "DELETE" });
    await refreshAll();
  } catch (err) {
    setStatus("⚠ Failed to delete item.", true);
    console.error(err);
  }
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

refreshAll();
setInterval(refreshAll, 5 * 60 * 1000); // auto-refresh every 5 minutes
