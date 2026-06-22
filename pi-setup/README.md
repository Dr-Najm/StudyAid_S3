# StudyAid v12 — Raspberry Pi 5 Setup Guide

Replace the Windows laptop with a Raspberry Pi 5 that powers on, creates its
own WiFi hotspot, auto-starts Flask, and shows the live dashboard in a kiosk
browser. Zero manual steps once it's configured.

---

## Hardware requirements

- Raspberry Pi 5 (4GB)
- **Official 27W USB-C power adapter** — Pi 5 throttles on underpowered supplies
- HDMI monitor + cable
- MicroSD card with Raspberry Pi OS **Bookworm** (Desktop, 64-bit)
- Keyboard + mouse for initial setup (not needed after)

---

## Before you run these scripts

1. **Flash Raspberry Pi OS Bookworm Desktop** onto the SD card using Raspberry
   Pi Imager. During imaging, set your username and password. This guide
   assumes the username is `pi`; if you chose something else, the scripts
   auto-detect `$(whoami)` and patch themselves.

2. **Enable auto-login** (required for kiosk):
   ```
   sudo raspi-config
   ```
   → System Options → Boot / Auto Login → **Desktop Autologin**

3. **Clone the repo onto the Pi:**
   ```
   cd ~
   git clone https://github.com/Dr-Najm/StudyAid_S3.git
   cd StudyAid_S3
   git checkout v12-dev
   ```

4. **Make all scripts executable:**
   ```
   chmod +x pi-setup/*.sh
   ```

---

## Setup steps (run in order)

### Step 1 — WiFi Access Point
```bash
./pi-setup/01_setup_ap.sh
```
Creates the `studyaid-pi` AP using NetworkManager (`nmcli`).
**Do not use hostapd/dnsmasq tutorials** — Bookworm uses NetworkManager and
they conflict.

Verify:
```bash
nmcli connection show studyaid-pi
nmcli device status
```
The wlan0 device should show `connected` to `studyaid-pi`.

---

### Step 2 — Python environment + Flask
```bash
./pi-setup/02_setup_flask.sh
```
Creates a fresh venv inside `companion/venv/` and installs all dependencies
from `requirements.txt`. **Never copy a venv from the laptop** — they are not
portable across machines or paths.

Then add your Gemini API key to `companion/config.ini`:
```ini
[gemini]
api_key = YOUR_KEY_HERE
```

Test Flask manually before installing the service:
```bash
cd companion
venv/bin/python app.py
```
Open `http://192.168.4.1:5000` from a phone connected to `studyaid-pi` to
confirm it works. Ctrl+C to stop.

---

### Step 3 — Flask systemd service (auto-start on boot)
```bash
./pi-setup/03_install_service.sh
```
Installs and starts `studyaid.service`. Flask now starts automatically after
every reboot.

Useful commands:
```bash
sudo systemctl status studyaid       # check it's running
journalctl -u studyaid -f            # live logs
sudo systemctl restart studyaid      # restart after a code change
```

---

### Step 4 — Chromium kiosk
```bash
./pi-setup/04_setup_kiosk.sh
```
Creates autostart entries that:
- Disable screen blanking (monitor stays on during demo)
- Launch Chromium full-screen at `http://localhost:5000/student/live`
  (5-second delay gives Flask time to start first)

Then reboot:
```bash
sudo reboot
```

After reboot: desktop appears → 5 seconds → Chromium opens full-screen on
the live session monitor. No interaction needed.

To exit kiosk during testing: **Alt+F4**

---

## Network summary

| Device | SSID | IP |
|--------|------|----|
| Raspberry Pi (AP) | `studyaid-pi` | `192.168.4.1` |
| studyaid-01 (ESP32) | connects to Pi AP | `192.168.4.10`–`.50` (DHCP) |
| studyaid-02 (ESP32) | connects to Pi AP | `192.168.4.10`–`.50` (DHCP) |
| Flask (on Pi) | — | `http://192.168.4.1:5000` |

The ESP32 firmware (v12.0) is already compiled with:
- `COMP_SSID = "studyaid-pi"`
- `COMP_PASS = "studyaid123"`
- `COMP_SERVER_IP = "192.168.4.1"`

---

## Troubleshooting

**AP not appearing / device won't connect**
- Confirm wlan0 is not blocked: `rfkill list`
- Confirm band is 2.4GHz: `nmcli connection show studyaid-pi | grep band`
  (ESP32-S3 is 2.4GHz only — 5GHz will never connect)
- Check NM logs: `journalctl -u NetworkManager -f`

**Flask not starting**
- Check service: `sudo systemctl status studyaid`
- Check logs: `journalctl -u studyaid -n 50`
- Confirm venv exists: `ls companion/venv/bin/python`
- Confirm config.ini exists: `ls companion/config.ini`

**Kiosk doesn't open / shows old URL**
- Check autostart files exist: `ls ~/.config/autostart/`
- Run Chromium manually to see error:
  `chromium-browser --kiosk http://localhost:5000/student/live`
- Confirm Flask is running first: `curl http://localhost:5000/student/live`

**Live monitor stuck on "Tiada Sesi Aktif" during demo**
- This is the correct idle state — it clears as soon as a session starts
- If a previous session is stuck open: click "Tutup sesi lapuk" on the page,
  or call `POST /api/session/close_stale`

**Headless fallback (if HDMI fails at venue)**
- SSH into the Pi from a laptop on the same AP:
  `ssh pi@192.168.4.1`
- Flask is already running — open `http://192.168.4.1:5000` in any browser
  connected to `studyaid-pi`

---

*StudyAid v12 — SMK Gudang Rasau (SEGRA), Kuantan, Pahang*
