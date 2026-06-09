/* Smart Morning Assistant — Auth Script (login.html / register.html) */

// Empty string = relative URLs, works when served from the FastAPI backend.
// Change to "http://localhost:8000" if opening HTML files directly.
const API_BASE = "";

// ── Login ─────────────────────────────────────────────────────────────────────

async function handleLogin(event) {
  event.preventDefault();
  const email    = document.getElementById("email").value.trim();
  const password = document.getElementById("password").value;

  const btn = document.getElementById("submit-btn");
  btn.disabled = true;
  btn.textContent = "Signing in…";
  hideError();

  try {
    const resp = await fetch(`${API_BASE}/api/auth/login`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password }),
    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.detail || "Login failed");

    // Store token and user info for the dashboard to use
    localStorage.setItem("auth_token", data.access_token);
    localStorage.setItem("user_id", String(data.user_id));

    window.location.href = "/dashboard.html";
  } catch (err) {
    showError(err.message);
    btn.disabled = false;
    btn.textContent = "Sign In";
  }
}

// ── Register ──────────────────────────────────────────────────────────────────

async function handleRegister(event) {
  event.preventDefault();
  const username = document.getElementById("username").value.trim();
  const email    = document.getElementById("email").value.trim();
  const password = document.getElementById("password").value;

  if (!username) { showError("Username is required"); return; }

  const btn = document.getElementById("submit-btn");
  btn.disabled = true;
  btn.textContent = "Creating account…";
  hideError();

  try {
    const resp = await fetch(`${API_BASE}/api/auth/register`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username, email, password }),
    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.detail || "Registration failed");

    // Registration succeeded — redirect to login
    window.location.href = "/login.html";
  } catch (err) {
    showError(err.message);
    btn.disabled = false;
    btn.textContent = "Create Account";
  }
}

// ── Utilities ─────────────────────────────────────────────────────────────────

function showError(msg) {
  const el = document.getElementById("error-msg");
  if (el) {
    el.textContent = msg;
    el.style.display = "block";
  }
}

function hideError() {
  const el = document.getElementById("error-msg");
  if (el) el.style.display = "none";
}
