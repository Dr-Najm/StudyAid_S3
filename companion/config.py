"""
StudyAid Companion App - Configuration Loader

Reads config.ini from the companion/ folder. If config.ini does not exist,
falls back to config.example.ini with a console warning so first-time users
can still boot the app and see what's wrong.
"""

import configparser
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_CONFIG_PATH = os.path.join(_HERE, "config.ini")
_EXAMPLE_PATH = os.path.join(_HERE, "config.example.ini")


def _load():
    parser = configparser.ConfigParser()
    if os.path.exists(_CONFIG_PATH):
        parser.read(_CONFIG_PATH)
        return parser, _CONFIG_PATH
    if os.path.exists(_EXAMPLE_PATH):
        print(
            "[CONFIG] WARNING: config.ini not found. "
            "Falling back to config.example.ini. "
            "Copy config.example.ini to config.ini and set your Gemini key.",
            file=sys.stderr,
        )
        parser.read(_EXAMPLE_PATH)
        return parser, _EXAMPLE_PATH
    raise FileNotFoundError(
        f"Neither config.ini nor config.example.ini found in {_HERE}"
    )


_parser, CONFIG_SOURCE = _load()

# ---- Server ----------------------------------------------------------------
SERVER_HOST = _parser.get("server", "host", fallback="0.0.0.0")
SERVER_PORT = _parser.getint("server", "port", fallback=5000)
SERVER_DEBUG = _parser.getboolean("server", "debug", fallback=True)

# ---- Gemini ----------------------------------------------------------------
GEMINI_API_KEY = _parser.get("gemini", "api_key", fallback="")
GEMINI_MODEL = _parser.get("gemini", "model", fallback="gemini-1.5-flash")

# ---- Demo ------------------------------------------------------------------
SEED_DEMO_DATA = _parser.getboolean("demo", "seed_demo_data", fallback=True)

# ---- Derived ---------------------------------------------------------------
SQLITE_PATH = os.path.join(_HERE, "studyaid.db")
SQLALCHEMY_DATABASE_URI = f"sqlite:///{SQLITE_PATH}"


def summary():
    """Return a short human-readable summary of the loaded config."""
    key_set = "set" if GEMINI_API_KEY and GEMINI_API_KEY != "YOUR_GEMINI_API_KEY_HERE" else "NOT SET"
    return (
        f"Config source: {os.path.basename(CONFIG_SOURCE)} | "
        f"Server: {SERVER_HOST}:{SERVER_PORT} (debug={SERVER_DEBUG}) | "
        f"Gemini: {GEMINI_MODEL} (key {key_set}) | "
        f"DB: {os.path.basename(SQLITE_PATH)} | "
        f"Seed demo: {SEED_DEMO_DATA}"
    )
