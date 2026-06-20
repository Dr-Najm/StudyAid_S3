# StudyAid v12 — Handover Document
## Delta from v11 — Raspberry Pi migration + focus formula fix
## For use at the start of a new chat

---

## INSTRUCTIONS FOR CLAUDE

This document describes the v12 direction. Read it alongside all previous
handover documents (v1 base, v2 = v9.2, v10, v11 design, and HANDOVER_V11_BUILT
which records what v11.0 actually shipped). Everything in those still applies
unless overridden here. Do not generate code or files until the user confirms
understanding.

**Workflow (unchanged):** accumulate change requests, confirm a written change
list before generating anything. Present a full outline before writing any
document. Bahasa Malaysia for UI/student-facing/demo content; English for code,
comments, READMEs, technical docs.

**IMPORTANT — v12 is mostly Linux sysadmin, not the usual firmware workflow.**
The bulk of v12 is Raspberry Pi system configuration (NetworkManager, systemd,
Chromium kiosk, venv setup) — config files and shell steps, not accumulated
firmware change lists. The next session will feel different: more "here is a
config file, here is where it goes, here is how to test it" than code
generation. Only a tiny firmware change and a one-line formula fix are actual
code edits.

---

## 1. REPOSITORY STATE

- **GitHub:** `github.com/Dr-Najm/StudyAid_S3` (public)
- **Branches:**
  - `master` — v8.4 stable
  - `v9-dev` — v9.3 stable
  - `v10-dev` — v10.8 final
  - `v11-dev` — **v11.0, pushed and complete**
  - `v12-dev` — **to be created from v11-dev** for all v12 work
- **Firmware version string:** currently `v11.0` (becomes `v12.0` when v12 firmware change lands)
- **Working directories:**
  - Laptop: `C:\Projects\StudyAid\StudyAid_S3`
  - Desktop: `D:\Projects\StudyAid\StudyAid_S3`

**First step for v12:** create and check out `v12-dev` from `v11-dev`.

---

## 2. WHERE v11 ENDED (context for v12)

v11.0 is complete, pushed, and verified on hardware:
- Three study profiles (Menulis 0.5×, Campuran 1.0×, Membaca 2.0×) scaling the
  warning-ladder timings.
- Membaca orientation rule: Statik Tegak (arm raised, holding material) never
  advances the warning ladder; only Statik Lintang escalates.
- Planning-time AI: saving a topic in the planner runs synchronous Gemini
  classification + quiz generation.
- Teacher profile override on the planner page (explainability anchor).
- `SCREEN_SESSION_TOPIC` device topic picker with profile hint + "Ulangkaji
  Bebas" free-study fallback.
- `/api/session/start` sends `topic`, returns `profile`.
- `/api/quiz/topics` returns `{topic, profile}` objects, student-scoped via
  `device_id`, always appends "Ulangkaji Bebas" sentinel.
- Topic-name truncation fixed in BOTH `renderSessionTopic()` and
  `renderQuizTopic()` (textSize(1) + printWrapped, 36-char rows).
- Demo data seeded: 2 students × 8 topics × 10 questions = 160 quiz questions
  with correct profiles (`seed_demo.py` rewritten for v11 subjects).
- Headline demo contrast confirmed on hardware: propped reading alarms under
  Campuran, stays calm under Membaca.

---

## 3. THE v12 GOAL

Replace the Windows laptop (running Flask + Windows Mobile Hotspot) with a
**Raspberry Pi 5** that does everything self-contained:

1. Powers on from a wall adapter.
2. Brings up its own WiFi access point (AP mode) — SSID `studyaid-pi`.
3. Auto-starts the Flask app on boot.
4. Launches Chromium in kiosk mode on the HDMI monitor, showing the **live
   session monitor** dashboard page.
5. The wearable connects to the Pi's AP and posts to the Pi's fixed IP.

Everything is ready in under a minute with zero interaction. For a competition
booth: wearable on the wrist, live dashboard on the monitor behind it, all on
one Pi.

**The Flask app and SQLite do NOT change** — they already run on Python. The Pi
just becomes the host. What changes is the surrounding system: network, boot,
power, and the device's server IP.

---

## 4. LOCKED DECISIONS (from the v12 discussion)

| Decision | Choice |
|----------|--------|
| Network mode | **AP mode** — Pi creates its own hotspot, no router needed |
| Pi hardware | **Raspberry Pi 5, 4GB** |
| Power | **Wall adapter** (use official 27W USB-C — Pi 5 throttles on underpowered supplies) |
| Boot | **Fully automatic** — AP + Flask + kiosk browser all on boot |
| Display | **HDMI monitor**, Chromium kiosk showing the dashboard locally |
| Hotspot SSID | **`studyaid-pi`** |
| Kiosk landing page | **Live session monitor** (`/student/live`) |
| Focus formula | **Remove the `-8` sleeping penalty** (see Section 7) |

---

## 5. PI 5 SPECIFIC NOTES (avoid the stale-tutorial trap)

- **Pi 5 runs Raspberry Pi OS Bookworm, which uses NetworkManager by default.**
  AP mode must be configured through **NetworkManager** (e.g. `nmcli`), NOT the
  older `hostapd` + `dnsmasq` hand-configuration that most online AP tutorials
  still show. Following an `hostapd` guide on a Pi 5 / Bookworm is the main trap.
- **Power:** Pi 5 wants the official 27W USB-C adapter. Underpowered supplies
  cause throttling and low-voltage warnings that look like random demo flakiness.
- **venv is not portable** — the Pi gets a fresh venv built from
  `requirements.txt`, never copied from the laptop. (Carried lesson from v9.)
- Pi 5 has dual-band WiFi; AP mode on 2.4GHz is required because the ESP32-S3 is
  **2.4GHz only** (carried hardware fact — 5GHz will never connect).

---

## 6. WHAT NEEDS BUILDING (starting list — confirm into a change list next session)

### 6.1 Pi system configuration (the bulk of v12 — NOT application code)

- **NetworkManager AP profile:** SSID `studyaid-pi`, a WPA2 password (decide
  value), Pi on a fixed IP (proposal: `192.168.4.1`), built-in DHCP range for
  the device. 2.4GHz band.
- **systemd service for Flask:** auto-start `python app.py` (inside the Pi venv)
  on boot, restart on failure, after network is up. Bind Flask to `0.0.0.0` so
  it is reachable over the AP (verify current bind address — may already be set).
- **Chromium kiosk autostart:** full-screen, no address bar, pointed at
  `http://localhost:5000/student/live`, launched on desktop login. Disable screen
  blanking / screensaver so the monitor stays live.
- **Python environment on the Pi:** fresh venv, `pip install -r requirements.txt`.
- **Config file for server IP:** the device must know the Pi's fixed AP IP.

### 6.2 Firmware (`firmware/StudyAid_v9/StudyAid_v9.ino`) — minimal, one reflash

- Update WiFi constants to match the Pi AP:
  - `COMP_SSID` → `"studyaid-pi"`
  - `COMP_PASS` → (the Pi AP password, decide value)
  - `COMP_SERVER_IP` → the Pi's fixed AP IP (proposal: `"192.168.4.1"`)
  - `COMP_SERVER_PORT` stays `5000`
- Bump version string to `v12.0`.
- No logic changes. One small edit, one reflash per device (studyaid-01 and -02).

### 6.3 Focus formula fix (the `-8` penalty removal) — see Section 7

---

## 7. THE `-8` SLEEPING PENALTY REMOVAL (locked for v12)

**Background:** The v10/v11 docs claimed the `sleepingCount * -8` penalty was
"retired," but it was never actually removed from the code (a discrepancy found
during v11 verification). v12 makes the code match the story: **only Stage 3
penalises, at -3.**

**What to change (verified line numbers as of v11.0 firmware — re-verify before
editing, they shift):**

1. **`recalcFocusScore()` formula — firmware ~line 869:**
   ```cpp
   focusScore=constrain(100-(warningCount*3)-(sleepingCount*8)
       +recoveryBonus+streakBonus+durationBonus,0,120);
   ```
   Remove the `-(sleepingCount*8)` term.

2. **Session summary display — firmware ~line 1612:**
   ```cpp
   M5.Display.printf("A:-%d T:-%d P:+%d B:+%d\n",
       warningCount*3,sleepingCount*8,recoveryBonus,durationBonus);
   ```
   This shows the penalty breakdown on the summary screen. Decide: either drop
   the `T:-` (Tidur) column entirely, or keep it showing `0`. Recommendation:
   drop the column so the display matches the formula and isn't misleading.

3. **HTML/JS session detail view — firmware ~lines 2778–2779** (embedded
   dashboard served by the device, if still used):
   ```javascript
   const total=120,base=Math.max(0,100-d.warningCount*3-d.sleepingCount*8);
   const wW=Math.min(d.warningCount*3,50),sW=Math.min(d.sleepingCount*8,50);
   ```
   Remove the `sleepingCount*8` contributions here too for consistency.

**Keep `sleepingCount` itself** — it is still incremented (line ~1151) and still
useful as a session-summary count ("times went inactive"). It just stops
affecting the score.

**Companion app:** check whether any server-side or template code reproduces the
`sleepingCount*8` penalty (e.g. in `routes/api.py` session/end adherence math or
a dashboard template). If so, remove it there too so device and server agree.
Grep the companion app for `sleeping` and `*8` before declaring done.

**After the change:** a long quiet session reaching the sleeping sub-state scores
only the -3 from Stage 3, matching the judge-facing explanation.

---

## 8. KIOSK DASHBOARD NOTE

The live session monitor (`/student/live`, built in v9.2) polls every 3 seconds
and shows real-time session data with a student switcher. On the Pi kiosk it will
idle on this page. Confirm during v12 testing that:
- It renders correctly full-screen at the monitor's resolution.
- The student switcher dropdown works (or pre-select a student for the demo).
- The "Tiada Sesi Aktif" empty state looks acceptable when no session is running
  (this is what shows before a demo session starts).
- Polling continues when the page is the only tab (it already pauses when the tab
  is hidden — not an issue in kiosk mode where it is always visible).

---

## 9. OPEN DECISIONS FOR THE NEXT SESSION

- **Pi AP password:** what WPA2 password for `studyaid-pi`? (Old laptop hotspot
  used `studyaid123` — reuse or change.)
- **Pi fixed IP:** `192.168.4.1` proposed (NetworkManager AP default range). Confirm.
- **Summary screen `T:-` column:** drop it or show `0`? (Recommendation: drop.)
- **Does the device-served embedded dashboard (firmware ~line 2778) still get
  used,** or is the companion app the only dashboard now? If unused, that JS block
  could be left alone or cleaned up — decide scope.
- **Headless fallback:** if the HDMI monitor fails at the venue, is SSH-in +
  manual start an acceptable backup, or should there be a second display path?

---

## 10. CARRIED-FORWARD OPEN ITEMS (not v12 scope unless promoted)

- **Presentation materials** — slides (pptx) and demo script (docx) still reflect
  v9. They need updating to cover the v11 profile system, the Membaca orientation
  rule, the planning-time AI architecture, and now the Pi setup. High-stakes for
  competition. NOT v12 code scope, but flag to the user.
- **Battery optimisation** — deliberately deferred. The user will raise it again
  if it becomes a problem. Do not proactively work on it.
- **`session.topic` column** — Session table does not store which topic was
  studied; adding it would enable per-topic accuracy tracking. Post-demo idea.

---

## 11. KEY TECHNICAL FACTS (carried forward, still true)

- **Arduino IDE forward declarations — non-void returns are NEVER auto-generated.**
  Any function returning `int`, `String`, or any non-void type that is *called*
  before its *definition* in the file MUST have an explicit forward declaration at
  the top of the file. The IDE only auto-generates these reliably for `void`
  functions. Caught THREE times now: `String`-returning server helpers (v9),
  `MAX_TOPIC_LEN` global ordering (v11), and `int printWrapped(...)` called from
  `renderSessionTopic()`/`renderQuizTopic()` (v11). **Always add non-void forward
  declarations proactively when adding new helper functions.**
- **ESP32-S3 is 2.4GHz WiFi only** — 5GHz will never connect. Pi AP must be 2.4GHz.
- **Gemini model:** `gemini-2.5-flash-lite` (other models fail / retired on free tier).
- **Quiz questions are strictly subject-scoped** (`_pick_question` has
  `allow_cross_subject=False`; returns None rather than serving another subject).
- **ESP32 has no RTC** — server uses `datetime.utcnow()` for all timestamps.
- **Two-axis posture:** orientation from X-axis gravity, NOT pitch.
  `ORIENT_TEGAK_THRESHOLD=0.4` (flat ax~0, vertical ax~0.75+).
- **`MOTION_AKTIF_THRESHOLD=0.015f`** — actual hardware-tuned value (the docs that
  said 0.03f were wrong; 0.015f is correct).
- **`printWrapped()`** helper wraps text on the 240×135 display; used by quiz
  question screen and both topic pickers.
- **Unified palette** (v10.7): COL_ACCENT / COL_GOOD / COL_WARN / COL_DANGER /
  COL_INFO / COL_TEXT / COL_DIM.
- **ESPAsyncWebServer:** mathieucarbou fork required.
- **venv is not portable** — rebuild on the Pi from requirements.txt.
- **DB schema changes** require deleting `studyaid.db` and reseeding.
- **iCloud Drive corrupts Git refs** — keep the repo outside iCloud sync.
- **Git on Windows / PowerShell 5.x:** `&&` is not a valid separator; run git
  commands on separate lines.
- **Profiles:** Menulis 0.5×, Campuran 1.0× (unchanged v10 behaviour), Membaca
  2.0× + Statik Tegak trust rule. Demo/Real mode is orthogonal — it picks the base
  timing set, the profile then scales it.

---

## 12. USER PREFERENCES & WORKFLOW (unchanged)

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
  v12-dev = active.
- No placeholder branding — use SMK Gudang Rasau (SEGRA), Kuantan, Pahang.

---

## 13. HOW TO START THE NEXT CHAT

1. Paste the v1 handover, v2 handover, HANDOVER_V10, HANDOVER_V11 (design),
   HANDOVER_V11_BUILT, and this v12 document.
2. Add: *"These are the StudyAid handover documents. v11.0 is complete and pushed
   to v11-dev. This v12 document covers the Raspberry Pi migration. Please confirm
   understanding before we proceed."*
3. First actions in the new chat:
   - Create `v12-dev` from `v11-dev`.
   - Confirm the Pi 5 / Bookworm / NetworkManager approach for AP mode (not hostapd).
   - Produce a confirmed change list for the `-8` penalty removal (verify current
     line numbers first — they shift) before editing firmware.
   - Treat the Pi setup as guided sysadmin steps, not accumulated firmware edits.

---

*Generated at the close of the v11.0 development cycle, as the v12 (Raspberry Pi)
direction was locked. SMK Gudang Rasau (SEGRA), Kuantan, Pahang.*
