"""
Authentication helpers:
  - Password hashing with bcrypt (direct — avoids passlib/bcrypt version conflicts)
  - JWT creation and validation (python-jose)
  - get_current_user  — JWT Bearer only  (web dashboard)
  - get_user_flexible — JWT Bearer OR X-Device-Token header (ESP32 + web)
"""
import os
from datetime import datetime, timedelta
from typing import Optional

import bcrypt
from fastapi import Depends, Header, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from jose import JWTError, jwt
from sqlalchemy.orm import Session

from database import get_db
import models

# Override SECRET_KEY via env var in production — the default is dev-only.
SECRET_KEY = os.getenv("SECRET_KEY", "dev-secret-change-me-in-production")
ALGORITHM  = "HS256"
TOKEN_EXPIRE_DAYS = 7   # long expiry is convenient for a prototype

bearer_scheme = HTTPBearer(auto_error=False)   # auto_error=False → nicer 401 message


# ── Password helpers ──────────────────────────────────────────────────────────

def hash_password(password: str) -> str:
    return bcrypt.hashpw(password.encode(), bcrypt.gensalt()).decode()


def verify_password(plain: str, hashed: str) -> bool:
    return bcrypt.checkpw(plain.encode(), hashed.encode())


# ── JWT helpers ───────────────────────────────────────────────────────────────

def create_access_token(user_id: int) -> str:
    payload = {
        "sub": str(user_id),
        "exp": datetime.utcnow() + timedelta(days=TOKEN_EXPIRE_DAYS),
    }
    return jwt.encode(payload, SECRET_KEY, algorithm=ALGORITHM)


# ── FastAPI auth dependencies ─────────────────────────────────────────────────

def get_current_user(
    credentials: Optional[HTTPAuthorizationCredentials] = Depends(bearer_scheme),
    db: Session = Depends(get_db),
) -> models.User:
    """
    JWT-only dependency used by all web-dashboard endpoints.
    Reads Authorization: Bearer <token>, validates it, returns the User row.
    """
    if credentials is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Not authenticated — please login first",
            headers={"WWW-Authenticate": "Bearer"},
        )
    try:
        payload = jwt.decode(credentials.credentials, SECRET_KEY, algorithms=[ALGORITHM])
        user_id = int(payload["sub"])
    except (JWTError, KeyError, ValueError):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or expired token",
            headers={"WWW-Authenticate": "Bearer"},
        )

    user = db.query(models.User).filter(models.User.id == user_id).first()
    if user is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User not found")
    return user


def get_user_flexible(
    credentials: Optional[HTTPAuthorizationCredentials] = Depends(bearer_scheme),
    x_device_token: Optional[str] = Header(None, alias="X-Device-Token"),
    db: Session = Depends(get_db),
) -> models.User:
    """
    Dual-auth dependency used only by GET /api/dashboard-data.

    Priority order:
      1. X-Device-Token header  → permanent IoT key (ESP32 path)
      2. Authorization: Bearer  → short-lived JWT (web-dashboard path)

    The ESP32 sends:
        GET /api/dashboard-data
        X-Device-Token: <64-char hex key>

    The web frontend sends:
        GET /api/dashboard-data
        Authorization: Bearer <jwt>
    """
    # ── 1. Device token (ESP32) ──────────────────────────────────────────────
    if x_device_token:
        dt = (
            db.query(models.DeviceToken)
            .filter(
                models.DeviceToken.token     == x_device_token,
                models.DeviceToken.is_active == True,
            )
            .first()
        )
        if dt is None:
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Invalid or revoked device token",
            )
        # Record the last-seen time for the dashboard display
        dt.last_used_at = datetime.utcnow()
        db.commit()
        return dt.user

    # ── 2. JWT Bearer (web) ──────────────────────────────────────────────────
    return get_current_user(credentials, db)
