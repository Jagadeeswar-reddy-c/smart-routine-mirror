"""
Multi-user device management — APIRouter mounted in main.py.

Auth split:
  • Management endpoints  → JWT Bearer (web dashboard)
  • IoT login endpoint    → device_secret only (ESP32, no browser login)

Prefix: /api/devices
"""
import os
import secrets
from datetime import date, datetime
from typing import Optional

import httpx
from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.orm import Session

from database import get_db
from auth import get_current_user
import models
import schemas

router = APIRouter(prefix="/api/devices", tags=["devices"])

DEFAULT_LOCATION = "Saarbrücken"


# ── Shared data-fetching helpers (mirror of main.py logic) ────────────────────
# Duplicated here to keep device_routes self-contained (no circular imports).

async def _fetch_weather(location: str) -> dict:
    """Return weather dict for the given city.  Falls back to mock data if no API key."""
    api_key = os.getenv("WEATHER_API_KEY", "")
    if not api_key:
        return {
            "location": location,
            "temperature": 18,
            "condition": "Cloudy",
            "humidity": 65,
            "wind_speed": 12,
        }
    try:
        async with httpx.AsyncClient(timeout=10) as client:
            resp = await client.get(
                "https://api.openweathermap.org/data/2.5/weather",
                params={"q": location, "appid": api_key, "units": "metric"},
            )
        if resp.status_code == 200:
            w = resp.json()
            return {
                "location": w["name"],
                "temperature": round(w["main"]["temp"]),
                "condition": w["weather"][0]["description"].capitalize(),
                "humidity": w["main"]["humidity"],
                "wind_speed": round(w["wind"]["speed"]),
            }
    except Exception:
        pass
    return {"location": location, "temperature": 0, "condition": "Unavailable",
            "humidity": 0, "wind_speed": 0}


async def _fetch_news() -> dict:
    """Return news dict.  Falls back to mock data if no API key."""
    api_key = os.getenv("NEWS_API_KEY", "")
    if not api_key:
        return {
            "headlines": [
                "Scientists discover new renewable energy source",
                "Global tech summit opens with AI focus",
                "Local elections show record voter turnout",
                "New climate agreement signed by 50 nations",
                "Space mission returns with rare asteroid samples",
            ]
        }
    try:
        async with httpx.AsyncClient(timeout=10) as client:
            resp = await client.get(
                "https://newsapi.org/v2/top-headlines",
                params={"language": "en", "pageSize": 5, "apiKey": api_key},
            )
        if resp.status_code == 200:
            articles = resp.json().get("articles", [])
            return {"headlines": [a["title"] for a in articles[:5] if a.get("title")]}
    except Exception:
        pass
    return {"headlines": []}


def _get_today_schedule(user_id: int, db: Session) -> dict:
    """Return today's schedule items for a user."""
    today = date.today().isoformat()
    items = (
        db.query(models.ScheduleItem)
        .filter(
            models.ScheduleItem.user_id == user_id,
            models.ScheduleItem.date == today,
        )
        .order_by(models.ScheduleItem.time)
        .all()
    )
    return {
        "date": today,
        "items": [{"id": i.id, "time": i.time, "task": i.task} for i in items],
    }


async def _build_dashboard(user: models.User, db: Session) -> dict:
    """Assemble the full dashboard payload for a user (used by device login)."""
    location = (
        user.settings.weather_location
        if user.settings and user.settings.weather_location
        else DEFAULT_LOCATION
    )
    weather  = await _fetch_weather(location)
    schedule = _get_today_schedule(user.id, db)
    news     = await _fetch_news()
    quotes   = [q.text for q in sorted(user.quotes, key=lambda x: x.sort_order)]
    water    = (user.settings.water_interval_min if user.settings else 60)
    return {
        "user":      {"id": user.id, "username": user.username},
        "weather":   weather,
        "schedule":  schedule,
        "news":      news,
        "quotes":    quotes,
        "settings":  {"water_interval_min": water},
        "timestamp": datetime.now().isoformat(),
    }


# ── Access-control helpers ─────────────────────────────────────────────────────

def _get_device_or_404(device_id: int, db: Session) -> models.Device:
    device = db.query(models.Device).filter(models.Device.id == device_id).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    return device


def _get_membership_or_403(
    device: models.Device, user: models.User, db: Session
) -> models.DeviceUser:
    """Return the DeviceUser row or raise 403 if the user is not a member."""
    membership = (
        db.query(models.DeviceUser)
        .filter(
            models.DeviceUser.device_id == device.id,
            models.DeviceUser.user_id   == user.id,
        )
        .first()
    )
    if not membership:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="You are not a member of this device",
        )
    return membership


def _require_owner(membership: models.DeviceUser) -> None:
    if membership.role != "owner":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Only the device owner can perform this action",
        )


# ── Device registration & listing ──────────────────────────────────────────────

@router.post("/register", status_code=status.HTTP_201_CREATED)
async def register_device(
    req: schemas.DeviceRegisterRequest,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Register a new physical device.

    The backend auto-generates a device_secret.  The caller (owner) must copy
    it to the ESP32 sketch as DEVICE_SECRET, and the returned id as DEVICE_ID.
    """
    if not req.device_code.strip() or not req.device_name.strip():
        raise HTTPException(status_code=400, detail="device_code and device_name are required")

    if db.query(models.Device).filter(
        models.Device.device_code == req.device_code.strip()
    ).first():
        raise HTTPException(status_code=400, detail="device_code already in use")

    device = models.Device(
        device_code   = req.device_code.strip(),
        device_name   = req.device_name.strip(),
        device_secret = secrets.token_hex(24),   # 48-char hex
        owner_user_id = current_user.id,
    )
    db.add(device)
    db.flush()   # get device.id before committing

    # Owner is automatically added as a device member with role="owner"
    db.add(models.DeviceUser(
        device_id = device.id,
        user_id   = current_user.id,
        role      = "owner",
    ))
    db.commit()
    db.refresh(device)

    return {
        "id":            device.id,
        "device_code":   device.device_code,
        "device_name":   device.device_name,
        "device_secret": device.device_secret,
        "owner_user_id": device.owner_user_id,
        "created_at":    device.created_at,
        "my_role":       "owner",
    }


@router.get("")
async def list_devices(
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Return all devices the current user belongs to (as owner or member)."""
    memberships = (
        db.query(models.DeviceUser)
        .filter(models.DeviceUser.user_id == current_user.id)
        .all()
    )
    result = []
    for m in memberships:
        d = m.device
        result.append({
            "id":            d.id,
            "device_code":   d.device_code,
            "device_name":   d.device_name,
            "owner_user_id": d.owner_user_id,
            "created_at":    d.created_at,
            "my_role":       m.role,
        })
    return result


@router.get("/{device_id}")
async def get_device(
    device_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Return full device details including device_secret.

    The device_secret is shown so the owner can copy it to the ESP32 sketch
    at any time.  Only device members can access this.
    """
    device = _get_device_or_404(device_id, db)
    membership = _get_membership_or_403(device, current_user, db)
    return {
        "id":            device.id,
        "device_code":   device.device_code,
        "device_name":   device.device_name,
        "device_secret": device.device_secret,
        "owner_user_id": device.owner_user_id,
        "created_at":    device.created_at,
        "my_role":       membership.role,
    }


# ── Device users ───────────────────────────────────────────────────────────────

@router.get("/{device_id}/users")
async def list_device_users(
    device_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """List all users linked to this device."""
    device = _get_device_or_404(device_id, db)
    _get_membership_or_403(device, current_user, db)

    members = (
        db.query(models.DeviceUser)
        .filter(models.DeviceUser.device_id == device_id)
        .all()
    )
    return [
        {
            "id":         m.id,
            "user_id":    m.user_id,
            "username":   m.user.username,
            "email":      m.user.email,
            "role":       m.role,
            "created_at": m.created_at,
        }
        for m in members
    ]


@router.post("/{device_id}/users", status_code=status.HTTP_201_CREATED)
async def add_device_user(
    device_id: int,
    req: schemas.AddDeviceUserRequest,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Add a user to this device by email.  Owner only."""
    device = _get_device_or_404(device_id, db)
    membership = _get_membership_or_403(device, current_user, db)
    _require_owner(membership)

    # Find the user to add
    target = db.query(models.User).filter(
        models.User.email == req.email.lower().strip()
    ).first()
    if not target:
        raise HTTPException(status_code=404, detail="No user found with that email")

    # Already a member?
    existing = db.query(models.DeviceUser).filter(
        models.DeviceUser.device_id == device_id,
        models.DeviceUser.user_id   == target.id,
    ).first()
    if existing:
        raise HTTPException(status_code=400, detail="User is already a member of this device")

    # Validate role
    role = req.role if req.role in ("owner", "member") else "member"

    du = models.DeviceUser(device_id=device_id, user_id=target.id, role=role)
    db.add(du)
    db.commit()
    db.refresh(du)

    return {
        "id":         du.id,
        "user_id":    target.id,
        "username":   target.username,
        "email":      target.email,
        "role":       du.role,
        "created_at": du.created_at,
    }


@router.delete("/{device_id}/users/{user_id}", status_code=status.HTTP_200_OK)
async def remove_device_user(
    device_id: int,
    user_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Remove a user from this device.  Owner only.  Cannot remove the owner."""
    device = _get_device_or_404(device_id, db)
    membership = _get_membership_or_403(device, current_user, db)
    _require_owner(membership)

    if user_id == device.owner_user_id:
        raise HTTPException(status_code=400, detail="Cannot remove the device owner")

    target = db.query(models.DeviceUser).filter(
        models.DeviceUser.device_id == device_id,
        models.DeviceUser.user_id   == user_id,
    ).first()
    if not target:
        raise HTTPException(status_code=404, detail="User is not a member of this device")

    # Also remove their fingerprints on this device
    db.query(models.DeviceFingerprint).filter(
        models.DeviceFingerprint.device_id == device_id,
        models.DeviceFingerprint.user_id   == user_id,
    ).delete()

    db.delete(target)
    db.commit()
    return {"message": "User removed from device"}


# ── Fingerprint mappings ───────────────────────────────────────────────────────

@router.get("/{device_id}/fingerprints")
async def list_fingerprints(
    device_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """List all fingerprint slot → user mappings for this device."""
    device = _get_device_or_404(device_id, db)
    _get_membership_or_403(device, current_user, db)

    fps = (
        db.query(models.DeviceFingerprint)
        .filter(models.DeviceFingerprint.device_id == device_id)
        .order_by(models.DeviceFingerprint.fingerprint_id)
        .all()
    )
    return [
        {
            "id":             fp.id,
            "fingerprint_id": fp.fingerprint_id,
            "finger_label":   fp.finger_label,
            "user_id":        fp.user_id,
            "username":       fp.user.username,
            "created_at":     fp.created_at,
        }
        for fp in fps
    ]


@router.post("/{device_id}/fingerprints", status_code=status.HTTP_201_CREATED)
async def add_fingerprint(
    device_id: int,
    req: schemas.AddFingerprintRequest,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Map a fingerprint slot to a user account.

    Any device member can map their own fingerprint.
    Only the owner can map fingerprints for other users.
    """
    device = _get_device_or_404(device_id, db)
    membership = _get_membership_or_403(device, current_user, db)

    # Determine which user this fingerprint belongs to
    target_user_id = req.user_id if req.user_id else current_user.id

    # Non-owners can only add fingerprints for themselves
    if target_user_id != current_user.id and membership.role != "owner":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Only the device owner can add fingerprints for other users",
        )

    # Verify the target user is actually a device member
    target_membership = db.query(models.DeviceUser).filter(
        models.DeviceUser.device_id == device_id,
        models.DeviceUser.user_id   == target_user_id,
    ).first()
    if not target_membership:
        raise HTTPException(status_code=400, detail="Target user is not a member of this device")

    # Validate slot range
    if not (1 <= req.fingerprint_id <= 127):
        raise HTTPException(status_code=400, detail="fingerprint_id must be between 1 and 127")

    # Slot already taken?
    existing = db.query(models.DeviceFingerprint).filter(
        models.DeviceFingerprint.device_id      == device_id,
        models.DeviceFingerprint.fingerprint_id == req.fingerprint_id,
    ).first()
    if existing:
        raise HTTPException(
            status_code=400,
            detail=f"Slot {req.fingerprint_id} is already mapped to user '{existing.user.username}'",
        )

    fp = models.DeviceFingerprint(
        device_id      = device_id,
        user_id        = target_user_id,
        fingerprint_id = req.fingerprint_id,
        finger_label   = req.finger_label or "Right thumb",
    )
    db.add(fp)
    db.commit()
    db.refresh(fp)

    target_user = db.query(models.User).filter(models.User.id == target_user_id).first()
    return {
        "id":             fp.id,
        "fingerprint_id": fp.fingerprint_id,
        "finger_label":   fp.finger_label,
        "user_id":        fp.user_id,
        "username":       target_user.username,
        "created_at":     fp.created_at,
    }


@router.delete("/{device_id}/fingerprints/{fp_record_id}")
async def remove_fingerprint(
    device_id: int,
    fp_record_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Remove a fingerprint mapping.

    Any member can remove their own.  Owner can remove anyone's.
    """
    device = _get_device_or_404(device_id, db)
    membership = _get_membership_or_403(device, current_user, db)

    fp = db.query(models.DeviceFingerprint).filter(
        models.DeviceFingerprint.id        == fp_record_id,
        models.DeviceFingerprint.device_id == device_id,
    ).first()
    if not fp:
        raise HTTPException(status_code=404, detail="Fingerprint mapping not found")

    # Non-owners can only delete their own
    if fp.user_id != current_user.id and membership.role != "owner":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="You can only remove your own fingerprint mappings",
        )

    db.delete(fp)
    db.commit()
    return {"message": f"Slot {fp.fingerprint_id} mapping removed"}


# ── IoT device login — no JWT, called directly by ESP32 ───────────────────────

@router.post("/{device_id}/login")
async def device_login(
    device_id: int,
    req: schemas.DeviceLoginRequest,
    db: Session = Depends(get_db),
):
    """
    Fingerprint-based login for IoT devices.

    The ESP32 sends:
      POST /api/devices/{DEVICE_ID}/login
      { "fingerprint_id": <slot>, "device_secret": "<secret>" }

    The backend:
      1. Verifies the device exists and the secret matches.
      2. Finds which user owns that fingerprint slot.
      3. Returns that user's personalised dashboard data.

    No JWT is required — the device_secret authenticates the physical device.
    Actual biometric data stays on the ESP32 and is never sent here.
    """
    # ── 1. Verify device ──────────────────────────────────────────────────────
    device = db.query(models.Device).filter(models.Device.id == device_id).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")

    if device.device_secret != req.device_secret:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid device secret",
        )

    # ── 2. Look up fingerprint → user mapping ─────────────────────────────────
    fp = db.query(models.DeviceFingerprint).filter(
        models.DeviceFingerprint.device_id      == device_id,
        models.DeviceFingerprint.fingerprint_id == req.fingerprint_id,
    ).first()

    if not fp:
        # Not a hard error — the ESP32 should show "please register your fingerprint"
        return {
            "success": False,
            "message": "Fingerprint not linked to any user on this device",
        }

    # ── 3. Build and return the user's dashboard ──────────────────────────────
    user = fp.user
    dashboard = await _build_dashboard(user, db)

    return {"success": True, **dashboard}


# ── Optional: per-user dashboard via device context ───────────────────────────

@router.get("/{device_id}/dashboard/{user_id}")
async def get_device_user_dashboard(
    device_id: int,
    user_id: int,
    current_user: models.User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Return a specific user's dashboard data within a device context.
    Useful for owners who want to preview another member's data from the web.

    Requires JWT.  Caller must be a device member.
    """
    device = _get_device_or_404(device_id, db)
    _get_membership_or_403(device, current_user, db)

    # Verify target user is also a member of this device
    target_membership = db.query(models.DeviceUser).filter(
        models.DeviceUser.device_id == device_id,
        models.DeviceUser.user_id   == user_id,
    ).first()
    if not target_membership:
        raise HTTPException(status_code=404, detail="User is not a member of this device")

    target_user = db.query(models.User).filter(models.User.id == user_id).first()
    dashboard = await _build_dashboard(target_user, db)
    return {"success": True, **dashboard}
