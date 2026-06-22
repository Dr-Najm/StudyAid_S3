#!/usr/bin/env bash
# StudyAid v12 — Step 2: Set up Python venv and Flask app on the Pi
#
# Run after 01_setup_ap.sh, from the repo root on the Pi:
#   chmod +x pi-setup/02_setup_flask.sh
#   ./pi-setup/02_setup_flask.sh
#
# IMPORTANT: Do NOT copy the venv from the laptop — venvs are not portable
# across machines or paths. Always build fresh from requirements.txt.

set -e

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPANION_DIR="$REPO_DIR/companion"
VENV_DIR="$COMPANION_DIR/venv"

echo "[1/5] Repo root: $REPO_DIR"
echo "[2/5] Installing system Python deps (if needed)..."
sudo apt-get install -y python3-venv python3-pip

echo "[3/5] Creating fresh venv at $VENV_DIR ..."
# Remove any old venv (e.g. copied from laptop) and start clean
rm -rf "$VENV_DIR"
python3 -m venv "$VENV_DIR"

echo "[4/5] Installing Python requirements..."
"$VENV_DIR/bin/pip" install --upgrade pip
"$VENV_DIR/bin/pip" install -r "$COMPANION_DIR/requirements.txt"

echo "[5/5] Setting up config.ini ..."
CONFIG="$COMPANION_DIR/config.ini"
if [ -f "$CONFIG" ]; then
    echo "  config.ini already exists — skipping (edit manually if needed)"
else
    cp "$COMPANION_DIR/config.example.ini" "$CONFIG"
    echo "  Copied config.example.ini → config.ini"
    echo ""
    echo "  ACTION REQUIRED: add your Gemini API key to $CONFIG"
    echo "  Edit the [gemini] section: api_key = YOUR_KEY_HERE"
fi

echo ""
echo "Done. Flask app is ready."
echo ""
echo "To install and start the systemd service, run 03_install_service.sh"
echo "To test manually first:  cd $COMPANION_DIR && venv/bin/python app.py"
