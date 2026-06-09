/* Smart Morning Assistant — Device Management */

const API_BASE   = "";
const AUTH_TOKEN = localStorage.getItem("auth_token");

// Auth guard
if (!AUTH_TOKEN) window.location.replace("/login.html");

// ── State ─────────────────────────────────────────────────────────────────────

let currentDeviceId   = null;   // selected device id
let currentDeviceRole = null;   // "owner" | "member"
let currentMembers    = [];     // latest members list (for fingerprint user select)

// ── Auth-aware fetch ──────────────────────────────────────────────────────────

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

// ── User badge ────────────────────────────────────────────────────────────────

async function loadUserBadge() {
  try {
    const resp = await apiCall(`${API_BASE}/api/auth/me`);
    if (!resp || !resp.ok) return;
    const user = await resp.json();
    document.getElementById("user-badge").innerHTML =
      `Hi, <strong>${esc(user.username)}</strong>`;
  } catch (_) {}
}

// ── Device list ───────────────────────────────────────────────────────────────

async function loadDevices() {
  try {
    const resp = await apiCall(`${API_BASE}/api/devices`);
    if (!resp || !resp.ok) return;
    renderDevices(await resp.json());
  } catch (err) {
    setStatus("⚠ Failed to load devices.", true);
  }
}

function renderDevices(devices) {
  const el = document.getElementById("devices-list");
  if (!devices.length) {
    el.innerHTML = `<p class="empty-msg">No devices yet. Register one below.</p>`;
    return;
  }
  el.innerHTML = devices.map(d => `
    <div class="device-item ${d.id === currentDeviceId ? "active" : ""}"
         onclick="selectDevice(${d.id})">
      <div>
        <div class="device-item-name">${esc(d.device_name)}</div>
        <div class="device-item-code">${esc(d.device_code)}
          <span class="role-badge role-${d.my_role}">${d.my_role}</span>
        </div>
      </div>
    </div>`).join("");
}

// ── Select device → load detail ───────────────────────────────────────────────

async function selectDevice(deviceId) {
  currentDeviceId = deviceId;

  // Highlight selected in list
  document.querySelectorAll(".device-item").forEach(el => {
    el.classList.toggle("active", el.onclick?.toString().includes(deviceId));
  });

  // Show detail panel
  document.getElementById("detail-panel").style.display = "";

  setStatus("Loading device…");

  try {
    // Load device detail, members, and fingerprints in parallel
    const [detailResp, membersResp, fpResp] = await Promise.all([
      apiCall(`${API_BASE}/api/devices/${deviceId}`),
      apiCall(`${API_BASE}/api/devices/${deviceId}/users`),
      apiCall(`${API_BASE}/api/devices/${deviceId}/fingerprints`),
    ]);

    if (!detailResp || !membersResp || !fpResp) return;

    const detail  = await detailResp.json();
    const members = await membersResp.json();
    const fps     = await fpResp.json();

    currentDeviceRole = detail.my_role;
    currentMembers    = members;

    renderDetail(detail);
    renderMembers(members);
    renderFingerprints(fps);
    populateFpUserSelect(members);

    setStatus("");
  } catch (err) {
    setStatus("⚠ Failed to load device detail.", true);
  }
}

function renderDetail(d) {
  document.getElementById("detail-title").textContent  = d.device_name;
  document.getElementById("detail-id").textContent     = d.id;
  document.getElementById("detail-secret").textContent = d.device_secret;
}

// ── Members ───────────────────────────────────────────────────────────────────

function renderMembers(members) {
  const el  = document.getElementById("members-list");
  const isOwner = currentDeviceRole === "owner";

  // Show add-member form only to owners
  document.getElementById("add-member-form").style.display = isOwner ? "" : "none";

  if (!members.length) {
    el.innerHTML = `<p class="empty-msg">No members.</p>`;
    return;
  }
  el.innerHTML = members.map(m => `
    <div class="member-row">
      <span class="member-name">${esc(m.username)}</span>
      <span class="member-email">${esc(m.email)}</span>
      <span class="role-badge role-${m.role}">${m.role}</span>
      ${isOwner && m.role !== "owner"
        ? `<button class="delete-btn" onclick="removeMember(${m.user_id})"
             title="Remove">✕</button>`
        : ""}
    </div>`).join("");
}

async function addMember(event) {
  event.preventDefault();
  const email = document.getElementById("member-email").value.trim();
  if (!email || !currentDeviceId) return;

  try {
    const resp = await apiCall(
      `${API_BASE}/api/devices/${currentDeviceId}/users`,
      { method: "POST", body: JSON.stringify({ email, role: "member" }) }
    );
    if (!resp) return;
    if (!resp.ok) {
      const err = await resp.json();
      setStatus(`⚠ ${err.detail}`, true);
      return;
    }
    document.getElementById("member-email").value = "";
    setStatus("Member added!");
    await refreshDetail();
  } catch (err) {
    setStatus("⚠ Failed to add member.", true);
  }
}

async function removeMember(userId) {
  if (!confirm("Remove this user from the device? Their fingerprint mappings will also be deleted.")) return;
  try {
    const resp = await apiCall(
      `${API_BASE}/api/devices/${currentDeviceId}/users/${userId}`,
      { method: "DELETE" }
    );
    if (!resp) return;
    if (!resp.ok) {
      const err = await resp.json();
      setStatus(`⚠ ${err.detail}`, true);
      return;
    }
    setStatus("Member removed.");
    await refreshDetail();
  } catch (err) {
    setStatus("⚠ Failed to remove member.", true);
  }
}

// ── Fingerprint mappings ──────────────────────────────────────────────────────

function renderFingerprints(fps) {
  const el = document.getElementById("fingerprints-list");
  if (!fps.length) {
    el.innerHTML = `<p class="empty-msg">No fingerprints mapped yet.</p>`;
    return;
  }
  el.innerHTML = fps.map(fp => `
    <div class="fp-row">
      <span class="fp-slot">#${fp.fingerprint_id}</span>
      <span class="fp-user">${esc(fp.username)}</span>
      <span class="fp-label">${esc(fp.finger_label)}</span>
      <button class="delete-btn" onclick="removeFingerprint(${fp.id})"
              title="Remove mapping">✕</button>
    </div>`).join("");
}

function populateFpUserSelect(members) {
  const myUserId = Number(localStorage.getItem("user_id"));
  const sel = document.getElementById("fp-user");
  sel.innerHTML = members.map(m =>
    `<option value="${m.user_id}" ${m.user_id === myUserId ? "selected" : ""}>
      ${esc(m.username)}
    </option>`
  ).join("");
}

async function addFingerprint(event) {
  event.preventDefault();
  const fpId    = Number(document.getElementById("fp-id").value);
  const userId  = Number(document.getElementById("fp-user").value);
  const label   = document.getElementById("fp-label").value.trim() || "Right thumb";

  if (!fpId || !userId || !currentDeviceId) return;

  try {
    const resp = await apiCall(
      `${API_BASE}/api/devices/${currentDeviceId}/fingerprints`,
      {
        method: "POST",
        body: JSON.stringify({ fingerprint_id: fpId, user_id: userId, finger_label: label }),
      }
    );
    if (!resp) return;
    if (!resp.ok) {
      const err = await resp.json();
      setStatus(`⚠ ${err.detail}`, true);
      return;
    }
    document.getElementById("fp-id").value    = "";
    document.getElementById("fp-label").value = "";
    setStatus("Fingerprint mapping added!");
    await refreshDetail();
  } catch (err) {
    setStatus("⚠ Failed to add fingerprint mapping.", true);
  }
}

async function removeFingerprint(fpRecordId) {
  if (!confirm("Remove this fingerprint mapping?")) return;
  try {
    const resp = await apiCall(
      `${API_BASE}/api/devices/${currentDeviceId}/fingerprints/${fpRecordId}`,
      { method: "DELETE" }
    );
    if (!resp) return;
    setStatus("Fingerprint mapping removed.");
    await refreshDetail();
  } catch (err) {
    setStatus("⚠ Failed to remove fingerprint.", true);
  }
}

// ── Register device ───────────────────────────────────────────────────────────

async function registerDevice(event) {
  event.preventDefault();
  const code = document.getElementById("reg-code").value.trim();
  const name = document.getElementById("reg-name").value.trim();
  if (!code || !name) return;

  try {
    const resp = await apiCall(`${API_BASE}/api/devices/register`, {
      method: "POST",
      body: JSON.stringify({ device_code: code, device_name: name }),
    });
    if (!resp) return;
    if (!resp.ok) {
      const err = await resp.json();
      setStatus(`⚠ ${err.detail}`, true);
      return;
    }
    const device = await resp.json();
    document.getElementById("reg-code").value = "";
    document.getElementById("reg-name").value = "";
    setStatus(`Device "${device.device_name}" registered! Copy your Device ID and Secret below.`);
    await loadDevices();
    selectDevice(device.id);
  } catch (err) {
    setStatus("⚠ Failed to register device.", true);
  }
}

// ── Helper: refresh only the detail panel ────────────────────────────────────

async function refreshDetail() {
  if (!currentDeviceId) return;
  await selectDevice(currentDeviceId);
}

// ── Copy to clipboard ─────────────────────────────────────────────────────────

async function copyText(elementId) {
  const text = document.getElementById(elementId)?.textContent?.trim();
  if (!text || text === "—") return;
  try {
    await navigator.clipboard.writeText(text);
    setStatus("Copied to clipboard!");
  } catch {
    // HTTP fallback
    const ta = document.createElement("textarea");
    ta.value = text;
    document.body.appendChild(ta);
    ta.select();
    document.execCommand("copy");
    ta.remove();
    setStatus("Copied!");
  }
}

// ── Logout ────────────────────────────────────────────────────────────────────

function logout() {
  localStorage.removeItem("auth_token");
  localStorage.removeItem("user_id");
  window.location.href = "/login.html";
}

// ── Utilities ─────────────────────────────────────────────────────────────────

function setStatus(msg, isError = false) {
  const el = document.getElementById("status");
  if (!el) return;
  el.textContent  = msg;
  el.style.color  = isError ? "var(--red)" : "";
  if (isError) setTimeout(() => { el.style.color = ""; }, 5000);
}

function esc(str) {
  return String(str ?? "")
    .replace(/&/g, "&amp;").replace(/</g, "&lt;")
    .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

// ── Boot ──────────────────────────────────────────────────────────────────────

loadUserBadge();
loadDevices();
