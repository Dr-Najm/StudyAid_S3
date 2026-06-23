#!/bin/bash
# 05_setup_sudoers.sh — install the StudyAid sudoers drop-in on the Pi.
#
# Run once from the repo root (after cloning on the Pi):
#   bash pi-setup/05_setup_sudoers.sh
#
# What it does:
#   1. Copies pi-setup/studyaid_sudoers to /etc/sudoers.d/studyaid
#   2. Sets correct 440 permissions
#   3. Validates with visudo -c (aborts on syntax error)
#
# Safe to re-run — just overwrites the file with the same content.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/studyaid_sudoers"
DST="/etc/sudoers.d/studyaid"

echo "[1/3] Copying sudoers file..."
sudo cp "$SRC" "$DST"

echo "[2/3] Setting permissions (440)..."
sudo chmod 440 "$DST"

echo "[3/3] Validating syntax..."
sudo visudo -c -f "$DST"

echo ""
echo "OK — StudyAid sudoers installed at $DST"
echo "The Flask app can now run nmcli, pkill chromium, and shutdown -r"
echo "without a password prompt."
