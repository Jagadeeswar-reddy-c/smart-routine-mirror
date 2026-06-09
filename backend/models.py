"""
SQLAlchemy ORM models.

Tables (existing):
  users, user_settings, schedule_items, device_tokens

Tables (new — multi-user device extension):
  devices, device_users, device_fingerprints
"""
from datetime import datetime

from sqlalchemy import (
    Boolean, Column, DateTime, ForeignKey,
    Integer, String, UniqueConstraint,
)
from sqlalchemy.orm import relationship

from database import Base


# ── Existing tables ────────────────────────────────────────────────────────────

class User(Base):
    __tablename__ = "users"

    id            = Column(Integer, primary_key=True, index=True)
    username      = Column(String(50),  unique=True, index=True, nullable=False)
    email         = Column(String(100), unique=True, index=True, nullable=False)
    password_hash = Column(String(255), nullable=False)
    created_at    = Column(DateTime, default=datetime.utcnow)

    # ── Existing relationships ───────────────────────────────────────────────
    settings       = relationship("UserSettings",  back_populates="user",
                                  uselist=False, cascade="all, delete-orphan")
    schedule_items = relationship("ScheduleItem",  back_populates="user",
                                  cascade="all, delete-orphan")
    device_token   = relationship("DeviceToken",   back_populates="user",
                                  uselist=False, cascade="all, delete-orphan")

    # ── New relationships (multi-user device extension) ──────────────────────
    devices_owned      = relationship(
        "Device", foreign_keys="Device.owner_user_id", back_populates="owner"
    )
    device_memberships = relationship(
        "DeviceUser", back_populates="user", cascade="all, delete-orphan"
    )


class UserSettings(Base):
    __tablename__ = "user_settings"

    id               = Column(Integer, primary_key=True, index=True)
    user_id          = Column(Integer, ForeignKey("users.id"), unique=True, nullable=False)
    weather_location = Column(String(100), default="Saarbrücken")
    created_at       = Column(DateTime, default=datetime.utcnow)
    updated_at       = Column(DateTime, default=datetime.utcnow)

    user = relationship("User", back_populates="settings")


class ScheduleItem(Base):
    __tablename__ = "schedule_items"

    id         = Column(Integer, primary_key=True, index=True)
    user_id    = Column(Integer, ForeignKey("users.id"), nullable=False)
    time       = Column(String(5),   nullable=False)   # "HH:MM"
    task       = Column(String(255), nullable=False)
    date       = Column(String(10),  nullable=False)   # "YYYY-MM-DD"
    created_at = Column(DateTime, default=datetime.utcnow)

    user = relationship("User", back_populates="schedule_items")


class DeviceToken(Base):
    """Single-user permanent API key (X-Device-Token header). Unchanged."""
    __tablename__ = "device_tokens"

    id           = Column(Integer, primary_key=True, index=True)
    user_id      = Column(Integer, ForeignKey("users.id"), unique=True, nullable=False)
    token        = Column(String(64), unique=True, index=True, nullable=False)
    label        = Column(String(100), default="ESP32 Device")
    is_active    = Column(Boolean, default=True, nullable=False)
    created_at   = Column(DateTime, default=datetime.utcnow)
    last_used_at = Column(DateTime, nullable=True)

    user = relationship("User", back_populates="device_token")


# ── New tables — multi-user device extension ───────────────────────────────────

class Device(Base):
    """
    One row per physical ESP32-S3 device.

    device_code   — a short human-readable string the owner chooses
                    (e.g. "bathroom_001").  Stored in the ESP32 sketch.
    device_secret — a backend-generated hex string the ESP32 sends on
                    every /login call to prove it is a registered device.
    owner_user_id — the user who registered the device; always has role="owner"
                    in device_users.
    """
    __tablename__ = "devices"

    id            = Column(Integer, primary_key=True, index=True)
    device_code   = Column(String(64),  unique=True, index=True, nullable=False)
    device_name   = Column(String(100), nullable=False)
    device_secret = Column(String(64),  nullable=False)   # shown on dashboard, sent by ESP32
    owner_user_id = Column(Integer, ForeignKey("users.id"), nullable=False)
    created_at    = Column(DateTime, default=datetime.utcnow)

    owner        = relationship("User", foreign_keys=[owner_user_id], back_populates="devices_owned")
    device_users = relationship("DeviceUser",        back_populates="device",
                                cascade="all, delete-orphan")
    fingerprints = relationship("DeviceFingerprint", back_populates="device",
                                cascade="all, delete-orphan")


class DeviceUser(Base):
    """
    Many-to-many: one device can have many users; one user can belong to many devices.

    role: "owner"  — registered the device; can add/remove users and fingerprints
          "member" — can add/delete their own fingerprints only
    """
    __tablename__ = "device_users"

    id        = Column(Integer, primary_key=True, index=True)
    device_id = Column(Integer, ForeignKey("devices.id"), nullable=False)
    user_id   = Column(Integer, ForeignKey("users.id"),   nullable=False)
    role      = Column(String(20), default="member", nullable=False)
    created_at = Column(DateTime, default=datetime.utcnow)

    device = relationship("Device", back_populates="device_users")
    user   = relationship("User",   back_populates="device_memberships")

    __table_args__ = (
        UniqueConstraint("device_id", "user_id", name="uq_device_user"),
    )


class DeviceFingerprint(Base):
    """
    Maps a local fingerprint slot ID on a specific device to a cloud user_id.

    fingerprint_id — integer slot number on the ESP32 sensor (1–127); local
                     to each physical device, not globally unique.

    Example:
      device_id=1, fingerprint_id=3, user_id=2
      → On device 1, slot 3 belongs to user 2.

    The ESP32 sends fingerprint_id + device_secret.
    The backend looks up this table to identify the user.
    Actual biometric data NEVER leaves the device.
    """
    __tablename__ = "device_fingerprints"

    id             = Column(Integer, primary_key=True, index=True)
    device_id      = Column(Integer, ForeignKey("devices.id"), nullable=False)
    user_id        = Column(Integer, ForeignKey("users.id"),   nullable=False)
    fingerprint_id = Column(Integer, nullable=False)   # 1–127
    finger_label   = Column(String(50), default="Right thumb")
    created_at     = Column(DateTime, default=datetime.utcnow)

    device = relationship("Device", back_populates="fingerprints")
    user   = relationship("User")

    __table_args__ = (
        # One slot per device — a slot can only belong to one user
        UniqueConstraint("device_id", "fingerprint_id", name="uq_device_fp_slot"),
    )
