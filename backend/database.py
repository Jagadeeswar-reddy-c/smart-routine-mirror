"""
SQLAlchemy database engine + session factory.
All models import Base from here and call Base.metadata.create_all(bind=engine)
once at startup (done in main.py).
"""
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker, declarative_base

# SQLite file stored next to this module
SQLALCHEMY_DATABASE_URL = "sqlite:///./smart_assistant.db"

engine = create_engine(
    SQLALCHEMY_DATABASE_URL,
    connect_args={"check_same_thread": False},  # required for SQLite
)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

Base = declarative_base()


def get_db():
    """FastAPI dependency — yields a DB session and closes it when the request ends."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
