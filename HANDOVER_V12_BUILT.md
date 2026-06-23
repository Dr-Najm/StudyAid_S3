# StudyAid v12 — BUILT Handover Document
## What v12.0 actually shipped — Raspberry Pi migration complete
## For use at the start of a new chat

---

## INSTRUCTIONS FOR CLAUDE

This document records what v12.0 **actually built and verified on hardware**,
as distinct from HANDOVER_V12.md (which was the v12 *plan*). Read it alongside
all previous handover documents (v1 base, v2 = v9.2, v10, v11 design,
HANDOVER_V11_BUILT, and HANDOVER_V12 plan). Everything in those still applies
unless overridden here. Do not generate code or files until the user confirms
understanding.

**Workflow (unchanged):** accumulate change requests, confirm a written change
list before generating anything. Present a full outline before writing any
document. Bahasa Malaysia for UI/student-facing/demo content; English for code,
comments, READMEs, technical docs.

---

## 1. REPOSITORY STATE

- **GitHub:** `github.com/Dr-Najm/StudyAid_S3` (public)
- **Branches:**
  - `master` — v8.4 stable
  - `v9-dev` — v9.3 stable
  - `v10-dev` — v10.8 final
  - `v11-dev` — v11.0 final
  - `v12-dev` — **v12.0, pushed and complete** (active branch)
- **Firmware:** `firmware/StudyAid_v9/StudyAid_v9.ino` — folder name unchanged
  (Arduino requires folder = filename). Version string in the header comment is
  `v12.0`. **Known cosmetic lag:** the boot splash and the `[BOOT]` Serial line
  still print `v10.8` (a hard-coded string in `setup()` that was never bumped).
  It does NOT affect behaviour. Fix in a future polish pass if desired.
- **Working directories:**
  - Laptop: `C:\Projects\StudyAid\StudyAid_S3`
  - Desktop: `D:\Projects\StudyAid\StudyAid_S3`

**v12-dev commit highlights (in order):**
1. v12.0 firmware + initial pi-setup scripts
2. chromium command-name fix (chromium, not chromium-browser)
3. kiosk Wayland-safe (drop X11 xset screensaver autostart)
4. kiosk uses labwc autostart (not freedesktop autostart folder)
5. AP script: sudo on nmcli commands
6. AP script: force WPA2 for ESP32-S3 compatibility

---

## 2. WHAT v12 DELIVERED (all verified on hardware)

The Windows laptop is fully replaced by a **Raspberry Pi 5** that boots into a
self-contained competition booth with zero interaction:

1. Powers on from the official 27W USB-C adapter.
2. Brings up its own 2.4GHz WiFi AP — SSID `studyaid-pi`, WPA2, IP `192.168.4.1`.
3. Auto-starts the Flask companion app via a systemd service.
4. Launches Chromium in kiosk mode on the HDMI monitor, showing
   `/student/live` (the live session monitor).
5. The wearable connects to the Pi AP and posts to `192.168.4.1:5000`.

**End-to-end loop confirmed working:** device → Pi AP → Flask → live dashboard,
with a real session flipping the kiosk from "Tiada Sesi Aktif" to live data.

**The Flask app and SQLite were NOT changed** — they already ran on Python.
Only the host, network, boot, and the device's server IP changed.

---

## 3. FIRMWARE CHANGES (v12.0) — all landed

`firmware/StudyAid_v9/StudyAid_v9.ino`:

- **WiFi constants → Pi AP:**
  - `COMP_SSID` = `"studyaid-pi"`
  - `COMP_PASS` = `"studyaid123"`
  - `COMP_SERVER_IP` = `"192.168.4.1"`
  - `COMP_SERVER_PORT` = `5000` (unchanged)
- **Focus formula fix — `-8` sleeping penalty removed:**
  - `recalcFocusScore()`: removed `-(sleepingCount*8)` term. Now only the
    Stage-3 warning penalty (`-3` via `warningCount`) applies.
  - Session summary display: dropped the `T:-` (Tidur) column. Now shows
    `A:-%d P:+%d B:+%d` only — matches the formula.
  - Embedded JS dashboard score bar: removed `sleepingCount*8` from the `base`
    and width calculations (and the now-unused `sW` variable).
  - `sleepingCount` itself is **kept** — still incremented, still useful as a
    session-summary count ("times went inactive"); it just no longer affects
    the score.
- **Version string** bumped to `v12.0` in the header comment + history block.
  (Boot-splash `v10.8` string NOT bumped — see Section 1 known cosmetic lag.)

**Companion app:** no penalty duplication existed server-side (grep for
`sleeping` / `*8` in `companion/` returned nothing; the server stores whatever
`focus_score` the device sends). So the `-8` removal was firmware-only.

**Flask bind:** already `0.0.0.0` (config fallback + `config.example.ini`), so it
was reachable over the AP with no change.

---

## 4. PI SETUP (`pi-setup/` — the bulk of v12)

All scripts target **Raspberry Pi OS Bookworm on a Pi 5**. Run in order. The
`pi-setup/README.md` has the full step-by-step with troubleshooting.

| File | Purpose |
|------|---------|
| `01_setup_ap.sh` | NetworkManager AP profile via `nmcli` — SSID `studyaid-pi`, **WPA2-forced** (see §5), IP `192.168.4.1`, 2.4GHz, DHCP `.10–.50` |
| `02_setup_flask.sh` | Fresh venv from `requirements.txt`, copies `config.ini` (add Gemini key) |
| `studyaid.service` | systemd unit — Flask auto-start on boot, restart on failure |
| `03_install_service.sh` | Installs + enables `studyaid.service` |
| `04_setup_kiosk.sh` | Chromium kiosk via **labwc autostart** (see §5) |
| `README.md` | Full setup guide + troubleshooting |

**Locked decisions (from the v12 plan, all honoured):** AP mode (no router),
Pi 5 4GB on the official 27W supply, fully automatic boot, HDMI kiosk on
`/student/live`, SSID `studyaid-pi`, password `studyaid123`, IP `192.168.4.1`,
`T:-` column dropped.

---

## 5. THE HARD-WON FIXES (the real value of this session)

These cost real debugging time and are **not obvious from any tutorial**. The
scripts in the repo now bake all of them in, but document them so they are never
rediscovered the hard way.

### 5.1 ESP32-S3 needs WPA2 — NetworkManager AP defaults to WPA1 (THE BIG ONE)

- **Symptom:** device shows the right SSID on screen, dots for ~10s, then
  "Gagal hubung WiFi. Kembali ke mod Solo." The Pi's NetworkManager log shows
  **zero** association attempts — the AP never even hears the device knock.
  Phones and laptops connect to the same AP fine.
- **Root cause:** the device WiFi scan revealed `studyaid-pi` advertising
  **`WPA_PSK` (WPA1)**, while home WiFi showed `WPA2_PSK`. The ESP32-S3 Arduino
  WiFi stack refuses to associate with a WPA1-only AP. Modern clients tolerate
  WPA1; the ESP32 does not, and fails silently before association.
- **Fix (now in `01_setup_ap.sh`):** force WPA2 on the AP profile —
  ```
  wifi-sec.proto rsn
  wifi-sec.pairwise ccmp
  wifi-sec.group ccmp
  wifi-sec.pmf 1
  ```
  After this the device scan shows `WPA2_PSK` and the ESP32 connects.
- **Live recovery command** (if ever needed on a running AP):
  ```
  sudo nmcli connection modify studyaid-pi 802-11-wireless-security.proto rsn
  sudo nmcli connection modify studyaid-pi 802-11-wireless-security.pairwise ccmp
  sudo nmcli connection modify studyaid-pi 802-11-wireless-security.group ccmp
  sudo nmcli connection up studyaid-pi
  ```
- **Diagnostic method that found it:** a throwaway `WiFiDiag.ino` sketch that
  scans (printing each network's auth mode) and attempts connection to the Pi AP
  then home WiFi, printing WiFi-event disconnect reason codes. The scan line
  `studyaid-pi ... WPA_PSK` was the smoking gun. Keep this technique in mind for
  any future ESP32 WiFi debugging — read the device side, not just the AP side.

### 5.2 Bookworm Pi 5 desktop is labwc (Wayland), not X11

This single fact caused three separate failures:

- **`~/.config/autostart/` (freedesktop) is silently ignored by labwc.** The
  kiosk `.desktop` entry there never fired. **Fix:** use labwc's own autostart
  at `~/.config/labwc/autostart`, with the launch backgrounded:
  ```
  (sleep 15 && chromium --kiosk ... http://localhost:5000/student/live) &
  ```
  (`04_setup_kiosk.sh` now writes to the labwc autostart.)
- **`xset` screen-blanking commands wedge the session.** The original kiosk
  script wrote an X11 `xset s off / -dpms` autostart; on Wayland it errors at
  session start and broke the whole desktop (missing panel, nothing autostarts).
  **Fix:** that file's creation was removed entirely. Disable screen blanking the
  supported way: `sudo raspi-config → Display Options → Screen Blanking → No`.
- **Keyboard shortcuts differ.** `Ctrl+Alt+T` and `Ctrl+Alt+F2` (VT switch) do
  NOT behave as on X11. To exit a kiosk: SSH in and `pkill chromium`, or
  power-cycle. To open a terminal on the desktop: use the menu
  (Raspberry menu → Accessories → Terminal).

### 5.3 Chromium binary name on Bookworm is `chromium`, not `chromium-browser`

The older `chromium-browser` alias does not exist on Pi 5 Bookworm; calling it
makes the kiosk silently not launch. All scripts/docs now use `chromium`.

### 5.4 `nmcli connection` commands need `sudo`

`nmcli connection delete/add/up` modify system NetworkManager state and require
root — without `sudo` they fail with "Insufficient privileges". Fixed in
`01_setup_ap.sh` (the other commands already had it).

### 5.5 SSH is the practical Pi workflow

Once the desktop session broke during kiosk debugging, SSH from the laptop
(`ssh pi@<pi-ip>`, or `ssh pi@192.168.4.1` once the AP is up) was the reliable
way back in. SSH was enabled during OS flashing. Recommended as the primary
way to work on the Pi — one machine, paste-able commands, and a lifeline if the
graphical session wedges.

---

## 6. NETWORK SUMMARY (as-built)

| Device | SSID | IP | Security |
|--------|------|----|----------|
| Raspberry Pi (AP) | `studyaid-pi` | `192.168.4.1` | WPA2 (rsn/ccmp) |
| studyaid-01 (ESP32) | joins Pi AP | `192.168.4.10–.50` (DHCP) | — |
| studyaid-02 (ESP32) | joins Pi AP | `192.168.4.10–.50` (DHCP) | — |
| Flask (on Pi) | — | `http://192.168.4.1:5000` | — |

- Pi AP password: `studyaid123`
- Any laptop/phone joined to `studyaid-pi` can open `http://192.168.4.1:5000`
  for the dashboard (multiple viewers supported simultaneously).
- While on `studyaid-pi`, a client has no internet (AP has no upstream) — switch
  back to home WiFi to `git push` / browse.

---

## 7. OPEN ITEMS FOR THE NEXT CHAT (polish + documentation)

**High priority (competition-facing):**
- **Presentation slides (pptx) and demo script (docx) still reflect v9.** They
  need updating to cover: the v11 three-profile system + Membaca orientation
  rule, the planning-time AI architecture, and the v12 Pi booth setup. This is
  the biggest remaining gap before competition.

**Medium:**
- **Boot-splash / `[BOOT]` version string still says `v10.8`** — cosmetic, bump
  to `v12.0` in `setup()` in a polish pass.
- **Reliable name-brand SD card** before competition day. The card used in v12
  setup flashed extremely slowly (hours, stalling ~50%), a classic sign of a
  counterfeit/failing card — a real demo-day risk. Get a SanDisk/Samsung 32GB A1
  and flash a known-good spare.
- **Repo housekeeping:** a stale `StudyAid_S3.ino` (v8.4, ~1963 lines) sits at
  the repo root, separate from the real firmware at
  `firmware/StudyAid_v9/StudyAid_v9.ino` (v12.0, ~3700 lines). It confuses any
  root-level grep/tooling. Decide whether to delete it.

**Low / deferred (not v12 scope unless promoted):**
- Battery optimisation — deliberately deferred; raise only if it becomes a problem.
- `session.topic` column on the Session table — would enable per-topic accuracy
  tracking. Post-demo idea.
- Headless fallback polish — SSH-in + manual start works if HDMI fails at the
  venue; could be made smoother.

---

## 8. KEY TECHNICAL FACTS (carried forward, still true)

- **ESP32-S3 needs WPA2** — will not associate with a WPA1-only AP (see §5.1).
- **ESP32-S3 is 2.4GHz WiFi only** — 5GHz will never connect. Pi AP is 2.4GHz (band bg).
- **Pi 5 Bookworm = NetworkManager + labwc (Wayland)** — AP via `nmcli` (not
  hostapd); kiosk via labwc autostart (not `~/.config/autostart/`); no `xset`.
- **Chromium binary is `chromium`** on Bookworm (not `chromium-browser`).
- **`nmcli connection` commands require `sudo`.**
- **Arduino IDE forward declarations — non-void returns are NEVER auto-generated.**
  Any non-void function called before its definition needs an explicit forward
  declaration at the top of the file. Caught multiple times (v9 String helpers,
  v11 `printWrapped`, `MAX_TOPIC_LEN` ordering). Add these proactively.
- **Gemini model:** `gemini-2.5-flash-lite` (others fail / retired on free tier).
- **Quiz questions are strictly subject-scoped** (`_pick_question` has
  `allow_cross_subject=False`).
- **ESP32 has no RTC** — server uses `datetime.utcnow()` for all timestamps.
- **Two-axis posture:** orientation from X-axis gravity, NOT pitch.
  `ORIENT_TEGAK_THRESHOLD=0.4` (flat ax~0, vertical ax~0.75+).
- **`MOTION_AKTIF_THRESHOLD=0.015f`** — actual hardware-tuned value.
- **Profiles:** Menulis 0.5×, Campuran 1.0×, Membaca 2.0× + Statik Tegak trust
  rule. Demo/Real mode is orthogonal — picks the base timing set; profile scales it.
- **Focus formula (v12):** `100 - warningCount*3 + recoveryBonus + streakBonus +
  durationBonus`, constrained 0–120. No sleeping penalty.
- **venv is not portable** — rebuild on the Pi from requirements.txt.
- **DB schema changes** require deleting `studyaid.db` and reseeding.
- **iCloud Drive corrupts Git refs** — keep the repo outside iCloud sync.
- **PowerShell 5.x:** `&&` is not a valid separator — run git commands on
  separate lines.

---

## 9. USER PREFERENCES & WORKFLOW (unchanged)

- Accumulate change requests; confirm a written change list before generating
  code. Never generate single-issue patches mid-session (except explicit small
  fixes the user asks for directly).
- Present a full outline before writing any document.
- Explanation-focused: prefers the reasoning behind decisions, not just steps.
- Hardware-driven iteration: test on real hardware, report observations,
  accumulate into the next named version.
- Scope discipline: features evaluated against judge scrutiny before building.
- Bahasa Malaysia for UI / student-facing / demo content; English for code,
  comments, README, technical docs.
- Git: master = stable, v9-dev = v9, v10-dev = v10 final, v11-dev = v11 final,
  v12-dev = v12 (current).
- No placeholder branding — use SMK Gudang Rasau (SEGRA), Kuantan, Pahang.
- Beginner-friendly on Linux/Pi sysadmin — step-by-step, one stage at a time,
  confirm each works before moving on.

---

## 10. HOW TO START THE NEXT CHAT

1. Paste the v1 handover, v2 handover, HANDOVER_V10, HANDOVER_V11 (design),
   HANDOVER_V11_BUILT, HANDOVER_V12 (plan), and this HANDOVER_V12_BUILT document.
2. Add: *"These are the StudyAid handover documents. v12.0 is complete, pushed to
   v12-dev, and verified on hardware — the Raspberry Pi booth works end to end.
   This BUILT document records what shipped and the hard-won fixes. Please confirm
   understanding before we proceed."*
3. Likely next focus: updating the presentation slides + demo script to v11/v12
   (the biggest competition-facing gap), plus the small polish items in Section 7.

---

*Generated at the close of the v12.0 development cycle — Raspberry Pi migration
complete and verified on hardware. SMK Gudang Rasau (SEGRA), Kuantan, Pahang.*
