#!/usr/bin/env bash
# StudyAid v12 — Step 3: Install Flask systemd service
#
# Run after 02_setup_flask.sh:
#   chmod +x pi-setup/03_install_service.sh
#   ./pi-setup/03_install_service.sh
#
# Installs studyaid.service so Flask auto-starts on boot.
# Logs viewable with: journalctl -u studyaid -f

set -e

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SERVICE_SRC="$REPO_DIR/pi-setup/studyaid.service"
SERVICE_DEST="/etc/systemd/system/studyaid.service"

# Patch the User= and paths in the service file to match this Pi's username
PI_USER="$(whoami)"
PI_HOME="$(eval echo ~$PI_USER)"

echo "[1/4] Installing service for user: $PI_USER (home: $PI_HOME)"

# Replace placeholder paths with actual paths for this Pi
sed \
    -e "s|User=pi|User=$PI_USER|g" \
    -e "s|/home/pi|$PI_HOME|g" \
    "$SERVICE_SRC" | sudo tee "$SERVICE_DEST" > /dev/null

echo "[2/4] Reloading systemd..."
sudo systemctl daemon-reload

echo "[3/4] Enabling service to start on boot..."
sudo systemctl enable studyaid

echo "[4/4] Starting service now..."
sudo systemctl start studyaid

sleep 2
echo ""
echo "Service status:"
sudo systemctl status studyaid --no-pager

echo ""
echo "Flask should now be running at http://localhost:5000"
echo "Logs: journalctl -u studyaid -f"
