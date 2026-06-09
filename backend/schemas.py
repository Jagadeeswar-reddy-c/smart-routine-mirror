"""
Pydantic schemas — request bodies and response shapes.
"""
from datetime import datetime
from typing import Optional

from pydantic import BaseModel


# ── Auth ──────────────────────────────────────────────────────────────────────

class RegisterRequest(BaseModel):
    username: str
    email: str
    password: str


class LoginRequest(BaseModel):
    email: str
    password: str


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    user_id: int


class UserOut(BaseModel):
    id: int
    username: str
    email: str
    created_at: datetime

    model_config = {"from_attributes": True}


# ── Weather & Location ─────────────────────────────────────────────────────────

class LocationRequest(BaseModel):
    location: str


# ── Schedule ───────────────────────────────────────────────────────────────────

class ScheduleItemIn(BaseModel):
    time: str   # "HH:MM"
    task: str


class ScheduleItemOut(BaseModel):
    id: int
    time: str
    task: str
    date: str

    model_config = {"from_attributes": True}


# ── Single-user device token (unchanged) ──────────────────────────────────────

class DeviceTokenOut(BaseModel):
    token: str
    label: str
    is_active: bool
    created_at: datetime
    last_used_at: Optional[datetime]

    model_config = {"from_attributes": True}


# ── Multi-user device management (new) ────────────────────────────────────────

class DeviceRegisterRequest(BaseModel):
    """Body for POST /api/devices/register."""
    device_code: str   # short human-readable ID stored in ESP32 (e.g. "bathroom_001")
    device_name: str   # display name (e.g. "Bathroom Assistant")


class DeviceSummaryOut(BaseModel):
    """Returned in the devices list — no secret."""
    id: int
    device_code: str
    device_name: str
    owner_user_id: int
    created_at: datetime
    my_role: str          # "owner" | "member" — relative to the requesting user

    model_config = {"from_attributes": True}


class AddDeviceUserRequest(BaseModel):
    """Body for POST /api/devices/{id}/users — add a user by email."""
    email: str
    role: str = "member"  # "owner" | "member"


class DeviceUserOut(BaseModel):
    """One row from device_users enriched with user info."""
    id: int
    user_id: int
    username: str
    email: str
    role: str
    created_at: datetime


class AddFingerprintRequest(BaseModel):
    """
    Body for POST /api/devices/{id}/fingerprints.

    fingerprint_id — the slot number from the ESP32 sensor (1–127).
    user_id        — whose fingerprint this is; defaults to the caller if omitted.
    """
    fingerprint_id: int
    finger_label: str = "Right thumb"
    user_id: Optional[int] = None


class FingerprintOut(BaseModel):
    """One row from device_fingerprints enriched with username."""
    id: int
    fingerprint_id: int
    finger_label: str
    user_id: int
    username: str
    created_at: datetime


class DeviceLoginRequest(BaseModel):
    """
    Body for POST /api/devices/{id}/login — called by the ESP32.

    fingerprint_id — detected by the on-device sensor.
    device_secret  — hardcoded in the ESP32 sketch; proves this is a real device.
    """
    fingerprint_id: int
    device_secret: str
