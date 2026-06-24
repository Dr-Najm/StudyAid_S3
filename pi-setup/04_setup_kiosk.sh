#!/usr/bin/env bash
# StudyAid v12 — Step 4: Configure Chromium kiosk on HDMI monitor
#
# Run after 03_install_service.sh:
#   chmod +x pi-setup/04_setup_kiosk.sh
#   ./pi-setup/04_setup_kiosk.sh
#
# What this does:
#   - Adds a Chromium kiosk launch to the labwc autostart so it opens
#     full-screen on the live session monitor (/student/live) at boot.
#
# IMPORTANT — Pi 5 / Bookworm desktop notes:
#   * The default desktop compositor is WAYLAND. On current Bookworm Pi 5
#     images the compositor is **labwc**, which reads its autostart from
#     ~/.config/labwc/autostart  — NOT the freedesktop ~/.config/autostart/
#     folder (that folder is silently ignored, so .desktop kiosk entries
#     there never fire). This script targets labwc autostart.
#     (If your image uses Wayfire instead, the file is ~/.config/wayfire.ini
#     under an [autostart] section — adjust accordingly.)
#   * Screen blanking must NOT be disabled with `xset` (X11-only; it errors
#     on Wayland and can wedge the session). Use raspi-config instead:
#         sudo raspi-config -> Display Options -> Screen Blanking -> No
#   * The Chromium command on Bookworm is `chromium`, NOT `chromium-browser`.
#
# Assumes the Pi desktop auto-logs in.
# Enable auto-login first via: sudo raspi-config -> System Options -> Auto Login

set -e

PI_USER="$(whoami)"
LABWC_DIR="/home/$PI_USER/.config/labwc"
AUTOSTART_FILE="$LABWC_DIR/autostart"
KIOSK_URL="http://localhost:5000/booth"

# The kiosk launch line. ( ... ) & runs it in the background so the 15s sleep
# does not block the rest of the labwc autostart. 15s lets the session + Flask
# finish coming up first. 'chromium' is the Bookworm command name.
KIOSK_LINE="(sleep 15 && chromium --kiosk --noerrdialogs --disable-infobars --disable-session-crashed-bubble --disable-restore-session-state --no-first-run --check-for-update-interval=31536000 $KIOSK_URL) &"

echo "[1/2] Ensuring labwc autostart exists..."
mkdir -p "$LABWC_DIR"
touch "$AUTOSTART_FILE"

echo "[2/2] Adding kiosk launch to $AUTOSTART_FILE ..."
# Avoid duplicate entries if the script is run more than once
if grep -q "studyaid kiosk" "$AUTOSTART_FILE" 2>/dev/null || grep -q "booth" "$AUTOSTART_FILE" 2>/dev/null; then
    echo "  Kiosk launch already present — skipping (edit $AUTOSTART_FILE to change)."
else
    {
        echo "# studyaid kiosk — launch Chromium full-screen on the live dashboard"
        echo "$KIOSK_LINE"
    } >> "$AUTOSTART_FILE"
    echo "  Added."
fi

echo ""
echo "Done. Kiosk configured for user: $PI_USER"
echo "URL: $KIOSK_URL"
echo ""
echo "NEXT STEPS:"
echo "  1. Enable auto-login (if not already):"
echo "     sudo raspi-config -> System Options -> Boot / Auto Login -> Desktop Autologin"
echo "  2. Disable screen blanking (Wayland-safe, NOT xset):"
echo "     sudo raspi-config -> Display Options -> Screen Blanking -> No"
echo "  3. Reboot: sudo reboot"
echo "  4. Chromium should open full-screen ~15s after the desktop appears."
echo ""
echo "To exit the kiosk: SSH in from another machine and run 'pkill chromium',"
echo "or power-cycle (no reliable Wayland keyboard shortcut)."
echo "To disable kiosk: remove the studyaid kiosk lines from $AUTOSTART_FILE"
