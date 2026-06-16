"""
Smart Morning Assistant — FastAPI Backend

Auth:
  - Web dashboard  → JWT Bearer token (POST /api/auth/login)
  - ESP32 device   → permanent X-Device-Token header (GET /api/device/token from dashboard)

All /api endpoints except /api/auth/register and /api/auth/login require auth.
GET /api/dashboard-data accepts EITHER auth method (see auth.get_user_flexible).
"""
import os
import secrets
from datetime import date, datetime
from pathlib import Path

import httpx
from dotenv import load_dotenv
from fastapi import Depends, FastAPI, HTTPException, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles
from sqlalchemy.orm import Session

load_dotenv()

# Local modules (run uvicorn from the backend/ directory)
from database import engine, get_db
import models
import schemas
from auth import (
    create_access_token,
    get_current_user,
    get_user_flexible,
    hash_password,
    verify_password,
)
from device_routes import router as device_router

# Create all tables on startup if they don't exist yet
models.Base.metadata.create_all(bind=engine)

app = FastAPI(title="Smart Morning Assistant")

# Multi-user device extension (device_routes.py)
app.include_router(device_router)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

WEATHER_API_KEY  = os.getenv("WEATHER_API_KEY", "")
NEWS_API_KEY     = os.getenv("NEWS_API_KEY", "")
DEFAULT_LOCATION = "Saarbrücken"

DEFAULT_QUOTES = [
    "Make today amazing!",
    "Stay focused.",
    "You've got this!",
    "Small steps daily.",
    "Be kind today.",
    "Keep going!",
]


def mock_weather(location: str) -> dict:
    return {
        "location": location,
        "temperature": 18,
        "condition": "Cloudy",
        "humidity": 65,
        "wind_speed": 12,
    }


def mock_news() -> dict:
    return {
        "headlines": [
            "Scientists discover new renewable energy source",
            "Global tech summit opens with AI focus",
            "Local elections show record voter turnout",
            "New climate agreement signed by 50 nations",
            "Space mission returns with rare asteroid samples",
        ]
    }


# ── Internal helpers ───────────────────────────────────────────────────────────

def get_or_create_settings(user: models.User, db: Session) -> models.UserSettings:
    """Return the user's settings row, creating it with defaults if absent."""
    if user.settings is None:
        settings = models.UserSettings(
            user_id=user.id,
            weather_location=DEFAULT_LOCATION,
        )
        db.add(settings)
        db.commit()
        db.refresh(settings)
        return settings
    return user.settings


def get_or_create_device_token(user: models.User, db: Session) -> models.DeviceToken:
    """
    Return the user's device token row, auto-creating it on first call.
    This means a key is ready immediately after registration — no manual
    'generate' step needed.
    """
    if user.device_token is None:
        dt = models.DeviceToken(
            user_id=user.id,
            token=secrets.token_hex(32),   # 64-char hex string
            label="ESP32 Device",
        )
        db.add(dt)
        db.commit()
        db.refresh(dt)
        return dt
    return user.device_token


# ── Auth endpoints ─────────────────────────────────────────────────────────────

@app.post("/api/auth/register", status_code=status.HTTP_201_CREATED,
          response_model=schemas.UserOut)
async def register(req: schemas.RegisterRequest, db: Session = Depends(get_db)):
    """Create a new user account."""
    if not req.username.strip() or not req.email.strip() or not req.password:
        raise HTTPException(status_code=400, detail="All fields are required")

    if db.query(models.User).filter(models.User.email == req.email.lower().strip()).first():
        raise HTTPException(status_code=400, detail="Email already registered")
    if db.query(models.User).filter(models.User.username == req.username.strip()).first():
        raise HTTPException(status_code=400, detail="Username already taken")

    user = models.User(
        username=req.username.strip(),
        email=req.email.lower().strip(),
        password_hash=hash_password(req.password),
    )
    db.add(user)
    db.commit()
    db.refresh(user)

    # Create default settings, device token, and starter quotes
    db.add(models.UserSettings(user_id=user.id, weather_location=DEFAULT_LOCATION))
    db.add(models.DeviceToken(user_id=user.id, token=secrets.token_hex(32)))
    for i, text in enumerate(DEFAULT_QUOTES):
        db.add(models.UserQuote(user_id=user.id, text=text, sort_order=i))
    db.commit()

    return user


@app.post("/api/auth/login", response_model=schemas.TokenResponse)
async def login(req: schemas.LoginRequest, db: Session = Depends(get_db)):
    """Authenticate and return a short-lived JWT access token (web dashboard)."""
    user = (
        db.query(models.User)
        .filter(models.User.email == req.email.lower().strip())
        .first()
    )
    if not user or not verify_password(req.password, user.password_hash):
        raise HTTPException(status_code=401, detail="Invalid email or password")

    token = create_access_token(user.id)
    return {"access_token": token, "token_type": "bearer", "user_id": user.id}


@app.get("/api/auth/me", response_model=schemas.UserOut)
async def get_me(current_user: models.User = Depends(get_current_user)):
    """Return the profile of the currently logged-in user."""
    return current_user


# ── Device token endpoints (web dashboard → ESP32 key management) ──────────────

@app.get("/api/device/token", response_model=schemas.DeviceTokenOut)
async def get_device_token(
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Return the user's permanent device API key.
    If none exists yet it is auto-created.

    The web dashboard calls this to display the key the user should paste
    into their ESP32 sketch.
    """
    dt = get_or_create_device_token(current_user, db)
    return dt


@app.post("/api/device/token", response_model=schemas.DeviceTokenOut)
async def regenerate_device_token(
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Generate a brand-new device key, immediately invalidating the old one.

    Use this if the key is compromised or after re-flashing the ESP32.
    The ESP32 must be updated with the new key and re-flashed.
    """
    dt = get_or_create_device_token(current_user, db)
    dt.token        = secrets.token_hex(32)
    dt.is_active    = True
    dt.created_at   = datetime.utcnow()
    dt.last_used_at = None
    db.commit()
    db.refresh(dt)
    return dt


# ── Weather ────────────────────────────────────────────────────────────────────

@app.get("/api/weather")
async def get_weather(
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Return weather for this user's saved location."""
    settings = get_or_create_settings(current_user, db)
    location = settings.weather_location or DEFAULT_LOCATION

    if not WEATHER_API_KEY:
        return mock_weather(location)

    try:
        async with httpx.AsyncClient(timeout=10) as client:
            resp = await client.get(
                "https://api.openweathermap.org/data/2.5/weather",
                params={"q": location, "appid": WEATHER_API_KEY, "units": "metric"},
            )
    except httpx.HTTPError:
        return mock_weather(location)

    if resp.status_code != 200:
        return mock_weather(location)

    w = resp.json()
    return {
        "location": w["name"],
        "temperature": round(w["main"]["temp"]),
        "condition": w["weather"][0]["description"].capitalize(),
        "humidity": w["main"]["humidity"],
        "wind_speed": round(w["wind"]["speed"]),
    }


@app.post("/api/location")
async def set_location(
    req: schemas.LocationRequest,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Save a new weather location for the current user."""
    if not req.location.strip():
        raise HTTPException(status_code=400, detail="Location cannot be empty")
    settings = get_or_create_settings(current_user, db)
    settings.weather_location = req.location.strip()
    settings.updated_at = datetime.utcnow()
    db.commit()
    return {"message": f"Location updated to '{req.location.strip()}'"}


# ── Schedule ───────────────────────────────────────────────────────────────────

@app.get("/api/schedule")
async def get_schedule(
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Return today's schedule for the current user, sorted by time."""
    today = date.today().isoformat()
    items = (
        db.query(models.ScheduleItem)
        .filter(
            models.ScheduleItem.user_id == current_user.id,
            models.ScheduleItem.date == today,
        )
        .order_by(models.ScheduleItem.time)
        .all()
    )
    return {
        "date": today,
        "items": [{"id": i.id, "time": i.time, "task": i.task} for i in items],
    }


@app.post("/api/schedule")
async def add_schedule_item(
    item: schemas.ScheduleItemIn,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Add a schedule item for today for the current user."""
    if not item.task.strip():
        raise HTTPException(status_code=400, detail="Task cannot be empty")
    today = date.today().isoformat()
    new_item = models.ScheduleItem(
        user_id=current_user.id,
        time=item.time,
        task=item.task.strip(),
        date=today,
    )
    db.add(new_item)
    db.commit()
    db.refresh(new_item)
    return {
        "message": "Item added",
        "item": {
            "id": new_item.id,
            "time": new_item.time,
            "task": new_item.task,
            "date": new_item.date,
        },
    }


@app.delete("/api/schedule/{item_id}")
async def delete_schedule_item(
    item_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Delete a schedule item — users can only delete their own items."""
    item = (
        db.query(models.ScheduleItem)
        .filter(
            models.ScheduleItem.id == item_id,
            models.ScheduleItem.user_id == current_user.id,
        )
        .first()
    )
    if not item:
        raise HTTPException(status_code=404, detail="Item not found")
    db.delete(item)
    db.commit()
    return {"message": "Item deleted"}


# ── News ───────────────────────────────────────────────────────────────────────

@app.get("/api/news")
async def get_news(current_user: models.User = Depends(get_current_user)):
    """Return the latest headlines. News is shared across all users."""
    if not NEWS_API_KEY:
        return mock_news()

    try:
        async with httpx.AsyncClient(timeout=10) as client:
            resp = await client.get(
                "https://newsapi.org/v2/top-headlines",
                params={"language": "en", "pageSize": 5, "apiKey": NEWS_API_KEY},
            )
    except httpx.HTTPError:
        return mock_news()

    if resp.status_code != 200:
        return mock_news()

    articles = resp.json().get("articles", [])
    headlines = [a["title"] for a in articles[:5] if a.get("title")]
    return {"headlines": headlines}


# ── Dashboard — aggregated endpoint for web frontend and ESP32 ─────────────────

@app.get("/api/dashboard-data")
async def get_dashboard_data(
    current_user: models.User = Depends(get_user_flexible),
    db: Session = Depends(get_db),
):
    """
    Return weather + schedule + news for the authenticated user.

    Accepted auth (see auth.get_user_flexible):
      • Web dashboard → Authorization: Bearer <jwt>
      • ESP32         → X-Device-Token: <64-char hex key>
    """
    weather  = await get_weather(current_user=current_user, db=db)
    schedule = await get_schedule(current_user=current_user, db=db)
    news     = await get_news(current_user=current_user)
    return {
        "user":      {"id": current_user.id, "username": current_user.username},
        "weather":   weather,
        "schedule":  schedule,
        "news":      news,
        "timestamp": datetime.now().isoformat(),
    }


# ── Quotes CRUD ────────────────────────────────────────────────────────────────

@app.get("/api/quotes")
async def get_quotes(
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Return the authenticated user's motivational quotes, ordered by sort_order."""
    quotes = (
        db.query(models.UserQuote)
        .filter(models.UserQuote.user_id == current_user.id)
        .order_by(models.UserQuote.sort_order)
        .all()
    )
    return [{"id": q.id, "text": q.text, "sort_order": q.sort_order} for q in quotes]


@app.post("/api/quotes", status_code=201)
async def add_quote(
    req: schemas.QuoteIn,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Add a new quote for the current user."""
    text = req.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="Quote text cannot be empty")
    if len(text) > 120:
        raise HTTPException(status_code=400, detail="Quote must be 120 characters or fewer")

    max_order = (
        db.query(models.UserQuote)
        .filter(models.UserQuote.user_id == current_user.id)
        .count()
    )
    quote = models.UserQuote(user_id=current_user.id, text=text, sort_order=max_order)
    db.add(quote)
    db.commit()
    db.refresh(quote)
    return {"id": quote.id, "text": quote.text, "sort_order": quote.sort_order}


@app.put("/api/quotes/{quote_id}")
async def update_quote(
    quote_id: int,
    req: schemas.QuoteIn,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Edit the text of an existing quote."""
    quote = (
        db.query(models.UserQuote)
        .filter(models.UserQuote.id == quote_id,
                models.UserQuote.user_id == current_user.id)
        .first()
    )
    if not quote:
        raise HTTPException(status_code=404, detail="Quote not found")
    text = req.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="Quote text cannot be empty")
    quote.text = text
    db.commit()
    return {"id": quote.id, "text": quote.text, "sort_order": quote.sort_order}


@app.delete("/api/quotes/{quote_id}")
async def delete_quote(
    quote_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Delete one of the current user's quotes."""
    quote = (
        db.query(models.UserQuote)
        .filter(models.UserQuote.id == quote_id,
                models.UserQuote.user_id == current_user.id)
        .first()
    )
    if not quote:
        raise HTTPException(status_code=404, detail="Quote not found")
    db.delete(quote)
    db.commit()
    return {"message": "Quote deleted"}


# ── Multidisplay settings ───────────────────────────────────────────────────────

@app.post("/api/multidisplay/settings")
async def update_multidisplay_settings(
    req: schemas.MultidisplaySettingsIn,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Save display settings (water reminder interval)."""
    if not (5 <= req.water_interval_min <= 480):
        raise HTTPException(status_code=400,
                            detail="water_interval_min must be between 5 and 480")
    settings = get_or_create_settings(current_user, db)
    settings.water_interval_min = req.water_interval_min
    settings.updated_at = datetime.utcnow()
    db.commit()
    return {"message": "Settings saved", "water_interval_min": req.water_interval_min}


@app.get("/api/multidisplay/settings")
async def get_multidisplay_settings(
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    settings = get_or_create_settings(current_user, db)
    return {"water_interval_min": settings.water_interval_min}


# ── Multidisplay data — single endpoint for ESP32 display (JWT or X-Device-Token) ──

@app.get("/api/multidisplay/data")
async def get_multidisplay_data(
    current_user: models.User = Depends(get_user_flexible),
    db: Session = Depends(get_db),
):
    """
    Return all data the ESP32 multidisplay needs in a single call.

    Accepted auth:
      • Web test         → Authorization: Bearer <jwt>
      • ESP32 multidisplay → X-Device-Token: <64-char hex key from dashboard>
    """
    weather  = await get_weather(current_user=current_user, db=db)
    schedule = await get_schedule(current_user=current_user, db=db)
    news     = await get_news(current_user=current_user)
    settings = get_or_create_settings(current_user, db)

    quotes = (
        db.query(models.UserQuote)
        .filter(models.UserQuote.user_id == current_user.id)
        .order_by(models.UserQuote.sort_order)
        .all()
    )

    return {
        "user":      {"username": current_user.username},
        "quotes":    [q.text for q in quotes],
        "weather":   weather,
        "news":      news,
        "schedule":  schedule,
        "settings":  {"water_interval_min": settings.water_interval_min},
        "timestamp": datetime.now().isoformat(),
    }


# ── Root redirect ──────────────────────────────────────────────────────────────

@app.get("/", include_in_schema=False)
async def root():
    return RedirectResponse(url="/login.html")


# ── Serve frontend (must be last — after all /api routes) ─────────────────────

_frontend = Path(__file__).parent.parent / "frontend"
if _frontend.exists():
    app.mount("/", StaticFiles(directory=_frontend, html=True), name="frontend")
