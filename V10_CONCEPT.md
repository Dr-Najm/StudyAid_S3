# StudyAid v10 — Concept & Build Document

> **Status:** Design locked, Part 1 ready to build.
> **Branch:** `v10-dev` (off `v9-dev`). `master` (v8.4 stable) and `v9-dev` untouched.
> **Audience:** Claude Code / any new chat picking up v10 implementation.
> **Read this fully before generating code.** Do not generate code until the
> relevant Part's change list is approved by Najmuddin.

---

## 0. How to use this document

This is the single source of truth for the v10 redesign. The conceptual design
(the *why*) was settled in discussion; this document captures it so the build
phase doesn't lose the reasoning. Work proceeds **Part by Part**, each Part
gated by an approved change list. The user's standing workflow applies:

- **Accumulate changes, confirm the change list in writing, THEN generate code.**
- Test each firmware version on real hardware before moving on.
- BM for UI/student-facing text; English for code comments and technical docs.
- Explanation-focused guidance preferred over pure step-by-step.

---

## 1. Why v10 exists

v9 worked, but as a *product* it fragmented — device, dashboards, and quizzes
answered three different questions for three different users, with AI bolted on
as a question generator. v10 is not a feature release; it is a **re-anchoring**
around one sentence:

> **StudyAid senses how a student is actually studying, and responds —
> without ever handing them a phone.**

Everything either serves that sentence or is demoted. The payoff: one signal
upgrade strengthens the sensor, the warnings, the quiz, AND the AI at once.
That interlock is the anti-fragmentation argument.

---

## 2. The honest problem v10 fixes

v9 infers focus from a single signal — accelerometer magnitude vs one threshold.
Still = focused, moving = distracted. This is backwards in the most common cases:

- Writing / problem-solving = constant motion → wrongly flagged as distraction.
- Daydreaming / phone-scrolling = stillness → wrongly flagged as focus.

The device measures *physical stillness* and labels it *focus*. A sharp judge
will find that gap. v10 closes it, and everything else follows from that.

---

## 3. The four-part spine

Each Part feeds the next. The dependency chain is what makes v10 coherent.

```
Posture signal (Part 1)
   |-> feeds the four-stage warning (Part 2)
   |-> powers the on-screen posture display (Part 1 showpiece)
   |-> makes the drift quiz adaptive (Part 3 + 4)
   |-> produces the telemetry digest -> Gemini coach + difficulty (Part 4)
```

### Part 1 — Honest posture & engagement sensing (foundation)

Two distinct layers, kept honest:

**Posture (directly measured).** Four literal wrist states from the IMU, no
interpretation. On-screen labels (BM):

| State    | Meaning                         | Sensor signature                |
|----------|---------------------------------|---------------------------------|
| `Tegak`  | forearm upright / tilted up     | pitch orientation high          |
| `Lintang`| forearm horizontal / flat       | pitch near level                |
| `Statik` | minimal movement                | low magnitude variance          |
| `Gerak`  | active movement                 | higher magnitude variance       |

Derived from `pitchWindow` (orientation) + `magWindow` (motion) — both already
captured in firmware but underused today.

**Engagement (inferred from posture over time).** Focus becomes a *consequence*
of posture patterns, not a raw threshold:
- `Lintang` + rhythmic motion -> writing -> ENGAGED
- `Tegak` + irregular motion -> likely phone -> DISENGAGED
- long `Statik` + `Lintang` -> possibly resting -> UNCERTAIN (check)

> Pitch to judges: *"We show you exactly what we sense — your wrist posture —
> and infer engagement from the pattern. We don't claim to read your mind;
> we read your study posture honestly."*

**On-screen posture display (demo showpiece).** Makes invisible sensing visible.
CAVEAT: an on-screen label is a real-time claim. Therefore:
- Show **posture** (direct, reliable) prominently.
- Show **engagement** (inferred, fallible) softly, secondary.
- Smoothing / hysteresis so the label does not flicker (flicker reads as broken).

### Part 2 — Graduated four-stage warning (humane intervention)

v9's warning punishes the deep-focus stillness it is meant to protect, and a
full-screen flashing buzz mid-thought is self-defeating. v10 replaces one abrupt
trigger with a four-stage ladder that climbs only as evidence builds and drops
the instant the student re-engages:

- **Stage 0 — Engaged:** posture + motion indicate work, INCLUDING productive
  stillness (reading/thinking). Nothing happens.
- **Stage 1 — Watching (silent):** stillness begins AND posture is ambiguous/
  disengaged. Internal timer starts; at most a subtle on-screen cue. No sound,
  no penalty. Most lapses self-correct here.
- **Stage 2 — Gentle nudge:** if Stage 1 persists, a single soft chime + small
  prompt. No full-screen takeover; little/no penalty.
- **Stage 3 — Full alert:** only on sustained disengagement CONFIRMED by posture
  (long `Statik` + `Lintang`/down) does the loud, full-screen, repeating alert
  fire — with meaningful penalty.

Supporting pieces:
- Recovery drops the stage instantly at any level.
- Scoring tied to stage, NOT motion. Deep-focus stillness costs nothing —
  removes the "fidget for the sensor" incentive.
- Manual **"I'm thinking" snooze** (BtnA) suppresses escalation for a few
  minutes during heavy reading.
- Stage timings slot into the existing Settings pattern.

> Coupled to Part 1: the ladder only delivers its main benefit once posture can
> tell productive stillness from disengaged stillness. Hence **posture first.**

### Part 3 — Phone-free quiz (re-engagement without distraction)

The quiz's real value is not being a quiz — it is being a revision tool that
does NOT hand the student a phone. A distracted student opening a phone for a
"quick quiz" is three taps from TikTok. A self-contained device lets them
re-engage without reintroducing the exact distraction they are fighting.

This reframes the quiz from generic feature to **distraction-free re-engagement
tool**, fully consistent with the focus-first story. Decision: **Path B.**

- **Manual Quiz Mode** stays fully supported (student-initiated, phone-free
  revision), including the subject -> topic flow built in v9.3.
- **Drift quiz** stays the native mechanic (fires BECAUSE focus slipped) and
  becomes **adaptive** in v10 (see Part 4).
- Nothing deleted, nothing demoted.

> Demo-script line that defends the whole subsystem: *"Why not just an app?
> Because the phone is the distraction. The point is to re-engage without it."*

### Part 4 — AI telemetry -> coach + adaptive difficulty

Moves Gemini from *question generator* to *interpreter of focus data* —
something only StudyAid can do because only StudyAid has the posture stream.

The device builds a compact **session digest** (not raw data):

```
session_digest = {
  subject, time_of_day, duration_min,
  posture_pct: { lintang, tegak, statik, gerak },
  engagement_segments: [ {start_min, end_min, state} ],
  drift_events: [ {at_min, stage_reached, recovered_in_sec} ],
  focus_score
}
```

Pipeline: device accumulates timeline -> POSTs digest at session end ->
**server** (not device) calls Gemini -> returns short BM reflection ->
dashboard displays it. Heavy AI work stays server-side; the ESP32 stays light.

Two native uses:
- **Use A — debrief (hero):** e.g. *"Anda menulis dengan fokus 18 minit pertama,
  kemudian perhatian menurun setiap 5-6 minit, paling kerap selepas minit ke-25
  — pertimbangkan rehat pendek di situ."* Impossible without posture sensing.
- **Use B — adaptive input (showpiece):** same digest sets drift-quiz difficulty
  (easier to re-engage when focus low, harder to stretch when high) and feeds
  weak-topic detection.

Constraints: stay on `gemini-2.5-flash-lite`; keep the digest small to keep
prompts cheap.

---

## 4. Master Demo / Real switch (separate change list, AFTER Part 1)

One switch flips the entire system between Demo and Real. **Device-led**: the
device owns the mode (NVS-persisted, like `comp_mode`), and tells the server its
mode at session start (`/api/session/start` carries a `mode` field). Device
stays fully usable in Solo mode because it never depends on the server to know
its own mode.

Covers **all five timing configs together**:

| Configuration             | Demo                              | Real            |
|---------------------------|-----------------------------------|-----------------|
| Warning ladder timings    | Fast — all 4 stages in seconds    | Humane, minutes |
| AI debrief mode           | Session-end, immediate            | Weekly aggregate|
| Drift quiz cooldown       | Short (fires during a demo)       | 10 min          |
| Focus report interval     | Frequent                          | 15 s            |
| Quiz window (drift)       | Short                             | Normal          |

Safety: on-screen indicator whenever Demo is active, so it is never accidentally
left on for real student use.

> Why separate and after Part 1: the switch swaps timings for systems (warning,
> AI) that don't fully exist yet. Build it once those timing-driven systems are
> in place.

---

## 5. Recommended build sequence

1. **Posture & engagement signal** (Part 1) — foundation; nothing downstream is
   trustworthy without it.
2. **Honest relabeling + on-screen posture display** — cheap, high-impact,
   demos well.
3. **Four-stage warning** (Part 2) — built on the posture gate.
4. **Master Demo/Real switch** — separate change list, once warning + AI timings
   exist.
5. **Adaptive drift quiz** wiring (Part 3) — once posture/engagement is stable.
6. **Telemetry digest -> Gemini coach** (Part 4) — last; it consumes everything
   upstream.

Each step is independently valuable and demoable; stopping at any point still
leaves a coherent, stronger product.

---

## 6. Honest risks (carry into the pitch, do not hide)

- **Garbage in, garbage out.** Everything downstream is only as good as the
  posture signal. AI confidently describing a session that didn't happen is
  WORSE than no AI. This is why posture-first is correct and why the signal must
  be **validated on real students/hardware** before the AI layer is trusted.
- **Live-demo risk.** Richer classification has more ways to visibly misfire on
  stage. Mitigation: smoothing, conservative labels (posture loud, engagement
  soft), per-student calibration before the demo.
- **Per-session AI can become noise.** Durable value is the WEEKLY pattern.
  Lean to periodic insight over per-session verbosity (hence the Demo/Real
  debrief modes).
- **Deeper question:** does measuring focus improve focus, or just create
  score-anxiety? The humane warning design matters more for real student benefit
  than the AI does. Keep the device a nudge, not a surveillance score.
- **BMI270 threshold tuning** is the real work in Part 1 — the classifier logic
  is simple; making it accurate needs bench time on hardware.

---

## 7. What honestly helps a real student vs. competition shine

- **Real, durable benefit:** the warning rework (removes active harm) and a
  weekly pattern they wouldn't notice themselves. The phone-free re-engagement
  angle is genuinely sound.
- **Conditional benefit:** the AI coach — valuable ONLY IF the signal is
  accurate and it's framed as periodic insight, not per-session noise.
- **Highest-leverage work:** make the core signal trustworthy and the warnings
  humane. Everything else stacks on that.

---

## 8. Part 1 change list (APPROVED scope — ready to implement)

**File:** `firmware/StudyAid_v9/StudyAid_v9.ino` -> version bumps to **v10.0**
**Branch:** `v10-dev`

**A. Posture classification**
1. New posture enum: `POSTURE_TEGAK`, `POSTURE_LINTANG`, `POSTURE_STATIK`,
   `POSTURE_GERAK`, with BM labels `"Tegak"/"Lintang"/"Statik"/"Gerak"`.
2. New `classifyPosture()` combining orientation (`pitchWindow`) + motion
   variance (`magWindow`). Motion level decides Statik vs Gerak; when moving,
   orientation distinguishes Tegak vs Lintang. Mark boundaries
   `// TODO: tune on BMI270 hardware`.
3. New `getMagVariance()` helper over `magWindow` (distinguishes rhythmic
   productive motion from irregular motion). Existing magnitude-average stays.

**B. Engagement inference (separate from posture)**
4. New engagement enum: `ENGAGED`, `UNCERTAIN`, `DISENGAGED`, inferred from
   posture patterns over time. **Advisory only in Part 1** — informs the display,
   does NOT drive the warning yet (that's Part 2).

**C. On-screen display**
5. Session screen (`renderHome()` active branch): add a live posture line shown
   prominently (e.g. `Postur: Lintang`); engagement shown softly/secondary.
6. Smoothing/hysteresis: posture label changes only after N consistent samples.

**D. Calibration**
7. Extend calibration flow with a short "write for 20 seconds" capture that
   learns the student's productive-motion variance baseline; store in NVS
   alongside existing `cal_high`/`cal_low`. Existing magnitude calibration stays.

**E. Honest relabeling**
8. `"Tertidur!"` -> `"Tidak Aktif"` in `stateNames[]` and the overlay. Internal
   `STATE_SLEEPING` enum name unchanged; only user-facing string changes.

**F. Housekeeping**
9. Version bump: header comment, boot splash, Serial string -> **v10.0**, plus a
   v10.0 changelog entry.

**Explicitly NOT in Part 1 (deferred):** four-stage warning (Part 2); master
Demo/Real switch (separate list); adaptive quiz + AI telemetry (Parts 3-4); no
companion/server changes (posture is device-only for now).

---

## 9. Decisions locked

- Posture labels: **Tegak / Lintang / Statik / Gerak**.
- Warning: **four stages**, switchable as a preset SET (demo vs real), via the
  master switch.
- AI debrief: switchable **demo (session-end) vs weekly**, via the master switch.
- Master switch: **device-led**, covers **all five timings**, on-screen Demo
  indicator.
- Branch: **`v10-dev` off `v9-dev`**.
- Quiz: **kept (Path B)** — phone-free re-engagement; drift quiz made adaptive;
  nothing deleted.

## 10. Open decisions (still to settle in later Parts)

- Exact per-stage warning timings for the demo and real presets.
- Whether the weekly debrief is the default for real use (lean: yes).
- Companion/server schema additions needed for the telemetry digest (Part 4).

---

## 11. Context carried from v9 (still applies)

- Hardware: M5StickS3 + M5 Unit NFC (U216, ST25R3916, I2C Grove Port A); Watch
  Accessory Kit strap. Two devices: `studyaid-01` (Muhammad Khalish),
  `studyaid-02` (Rania Batrisyia).
- Subjects (device index 0-7 = DB ID 1-8): Bahasa Melayu, Matematik, Sejarah,
  Geografi, Pendidikan Islam, Fizik, Kimia, Biologi.
- NFC uses native `UnitST25R3916` methods (`nfcaRequest`,
  `nfcaSelectWithAnticollision`, `nfcaHlt`) — not PN532 APIs.
- `M5.update()` pumped inside `speakerBeep()` — no blocking `delay()`.
- ESPAsyncWebServer: mathieucarbou fork required.
- ESP32-S3 is 2.4GHz only.
- Arduino: forward declarations required for non-void return functions.
- Companion: Flask + SQLite; Gemini `gemini-2.5-flash-lite`; server-side
  `utcnow()`; `| fromjson` not `| tojson | fromjson`; `quiz_answers.session_id`
  nullable.
- Hotspot: SSID `StudyAid-Laptop`, pass `studyaid123`, server
  `192.168.137.1:5000`.

---

*StudyAid v10 concept — SMK Gudang Rasau (SEGRA), Kuantan, Pahang.*
