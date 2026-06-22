#!/usr/bin/env bash
# StudyAid v12 — Step 4: Configure Chromium kiosk on HDMI monitor
#
# Run after 03_install_service.sh:
#   chmod +x pi-setup/04_setup_kiosk.sh
#   ./pi-setup/04_setup_kiosk.sh
#
# What this does:
#   - Disables screen blanking and screensaver (so the live dashboard
#     stays visible throughout the demo without any mouse movement)
#   - Creates an autostart entry that launches Chromium in kiosk mode
#     pointing at the live session monitor (/student/live) on boot
#
# Kiosk mode: full-screen, no address bar, no restore-session prompt,
# no notifications. The dashboard fills the entire monitor.
#
# Assumes the Pi desktop (LXDE/Wayfire on Bookworm) auto-logs in.
# Enable auto-login first via: sudo raspi-config → System → Auto Login

set -e

PI_USER="$(whoami)"
AUTOSTART_DIR="/home/$PI_USER/.config/autostart"
KIOSK_URL="http://localhost:5000/student/live"

echo "[1/3] Disabling screen blanking..."

# For X11 (most Bookworm desktop installs default to X11 on Pi 5)
XINITRC="/home/$PI_USER/.xinitrc"
XSESSION="/home/$PI_USER/.xsessionrc"

# Write a screen-blanking disable script sourced at session start
tee /home/$PI_USER/.config/studyaid-screensaver-off.sh > /dev/null <<'EOF'
#!/bin/bash
xset s off          # disable screensaver
xset s noblank      # disable screen blanking
xset -dpms          # disable DPMS (Energy Star) power saving
EOF
chmod +x /home/$PI_USER/.config/studyaid-screensaver-off.sh

echo "[2/3] Creating Chromium kiosk autostart entry..."
mkdir -p "$AUTOSTART_DIR"

tee "$AUTOSTART_DIR/studyaid-screensaver-off.desktop" > /dev/null <<EOF
[Desktop Entry]
Type=Application
Name=StudyAid Disable Screensaver
Exec=/home/$PI_USER/.config/studyaid-screensaver-off.sh
Hidden=false
X-LXDE-Autostart-Phase=Applications
EOF

tee "$AUTOSTART_DIR/studyaid-kiosk.desktop" > /dev/null <<EOF
[Desktop Entry]
Type=Application
Name=StudyAid Kiosk
# Wait 5 seconds for Flask to finish starting before opening the browser
Exec=bash -c "sleep 5 && chromium-browser \\
    --kiosk \\
    --noerrdialogs \\
    --disable-infobars \\
    --disable-session-crashed-bubble \\
    --disable-restore-session-state \\
    --no-first-run \\
    --check-for-update-interval=31536000 \\
    '$KIOSK_URL'"
Hidden=false
X-LXDE-Autostart-Phase=Applications
EOF

echo "[3/3] Done."
echo ""
echo "Kiosk configured for user: $PI_USER"
echo "URL: $KIOSK_URL"
echo ""
echo "NEXT STEPS:"
echo "  1. Make sure auto-login is enabled:"
echo "     sudo raspi-config → System Options → Boot / Auto Login → Desktop Autologin"
echo "  2. Reboot: sudo reboot"
echo "  3. Chromium should open automatically ~5s after desktop appears"
echo ""
echo "To exit kiosk during testing: press Alt+F4 (closes Chromium)"
echo "To disable kiosk: rm $AUTOSTART_DIR/studyaid-kiosk.desktop"
