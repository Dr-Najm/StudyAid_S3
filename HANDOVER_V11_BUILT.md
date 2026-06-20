# StudyAid v11 — Handover Document (Built)
## Delta from HANDOVER_V11.md (concept) — what was actually implemented
## For use at the start of a new chat

---

## INSTRUCTIONS FOR CLAUDE

This document describes what was built in the v11.0 development session. Read it
alongside all previous handover documents (v1 base, v2 delta, v10 delta, and
HANDOVER_V11.md which contains the design rationale). Everything in those still
applies unless overridden here. Do not generate code or files until the user
confirms understanding.

The user works in accumulated change lists: confirm a written change list before
generating anything. Bahasa Malaysia for UI/student-facing content; English for
code, comments, READMEs.

---

## 1. REPOSITORY STATE

- **GitHub:** `github.com/Dr-Najm/StudyAid_S3` (public)
- **Branches:**
  - `master` — v8.4 stable, untouched
  - `v9-dev` — v9.3 stable
  - `v10-dev` — v10.8 final
  - `v11-dev` — **active**, v11.0 complete
- **Firmware version string:** `v11.0` (in `firmware/StudyAid_v9/StudyAid_v9.ino`)
- **Working directories:**
  - Laptop: `C:\Projects\StudyAid\StudyAid_S3`
  - Desktop: `D:\Projects\StudyAid\StudyAid_S3`

---

## 2. WHAT WAS VERIFIED AGAINST REAL CODE

At session start the v10-dev branch was cloned and verified. Two meaningful
discrepancies were found between the documentation and the actual v10.8 code:

**Discrepancy 1 — MOTION_AKTIF_THRESHOLD is 0.015f, not 0.03f.**
HANDOVER_V10 Section 7 and HANDOVER_V11 Section 2 both list this as a "locked"
value of 0.03f. The actual code says 0.015f. This is a gentler threshold — the
device falls into STATIK less readily than the docs imply. The hardware-tuned
value is 0.015f; treat that as correct.

**Discrepancy 2 — -8 sleeping penalty is NOT retired.**
HANDOVER_V10 Section 4.2 and the firmware's own v10.1 header comment both say
the `sleepingCount * -8` penalty was "retired." It is not: `sleepingCount++`
still fires at line 1151 (v10.8 numbering) when Stage 3 escalates into
`STATE_SLEEPING`, and the focus formula still subtracts `sleepingCount*8`. So a
long quiet session can score -3 (S3) then -8 (sleeping) = -11 total. This was
left as-is in v11.0 — not in scope to fix.

**Git history squash:** v10.2 through v10.8 are a single commit `dec5e4e` in
Git, not eight commits. HANDOVER_V10's recorded v10.1 hash `2e6b642` does not
match the actual `2a9b7af`. Harmless for development but worth knowing.

---

## 3. V11.0 — WHAT WAS BUILT

### 3.1 The three profiles (locked, implemented)

| Profile | Multiplier | Demo (per stage → full alert) | Real (per stage → full alert) |
|---------|-----------|-------------------------------|-------------------------------|
| Menulis | 0.5× | 2.5 s → 7.5 s | 60 s → 3 min |
| Campuran | 1.0× | 5 s → 15 s (unchanged) | 120 s → 6 min (unchanged) |
| Membaca | 2.0× | 10 s → 30 s | 240 s → 12 min |

Membaca also adds an orientation trust rule: **Statik Tegak never advances the
warning ladder** — only Statik Lintang (flat, resting arm) escalates. This is
implemented as a guard that resets `stageEnteredMs` on every Statik+Tegak sample
so the S0 timer never fires while the arm is raised. Campuran and Menulis are
unchanged in orientation behaviour.

The `SLEEPING_TRIGGER_MS` (sleeping escalation inside Stage 3) is also scaled by
the same profile multiplier, via `activeSleepingMs`.

Snooze duration is NOT scaled — it is a recovery action, not an escalation timer.

### 3.2 Architecture principle (confirmed implemented)

All AI runs at planning time, never during a session. When a student saves a
topic in the planner, the server fires one synchronous Gemini pass that both
classifies the profile AND generates the quiz bank. The device fetches the
pre-computed profile at session start from the server — no live AI during a
session.

---

## 4. COMPANION APP CHANGES (v11.0)

### 4.1 Schema — `models/schema.py`
- `TopicDeadline` gained a `profile` column:
  `db.Column(db.String(20), nullable=False, default="Campuran")`
- **DB reseed required:** delete `studyaid.db`, run `python app.py`

### 4.2 New function — `routes/admin.py`
- `_classify_topic_profile(subject_name_bm, topic)` — calls `gemini-2.5-flash-lite`
  with a strict one-word prompt, returns `"Menulis"`, `"Membaca"`, or `"Campuran"`.
  On any API failure or unrecognised response, returns `"Campuran"` (never raises).

### 4.3 Planner POST — `routes/student.py`
- `deadline_add()` rewritten:
  1. Classify profile BEFORE writing the DB row (read-only Gemini call; on failure,
     profile defaults to Campuran and topic is still saved).
  2. `TopicDeadline` row committed with correct profile in one commit.
  3. Quiz bank generation in a separate try/except so topic save is never rolled
     back if Gemini quiz-gen fails.
  4. Flash message reports profile + question count.
- `plan()` now includes `"profile": d.profile` in each deadline dict.
- New route `POST /student/plan/topic/profile` → `deadline_profile_override()`:
  accepts `deadline_id` + `profile`, validates against `{"Menulis","Membaca","Campuran"}`,
  updates the row. This is the teacher override / explainability anchor.
- `import json` added to student.py.

### 4.4 Planner template — `templates/student/plan.html`
- Topic deadline table gains a "Profil" column with:
  - Colour-coded badge: amber (Menulis), teal (Membaca), grey (Campuran).
  - Inline override dropdown (onchange submits form to `POST /student/plan/topic/profile`).
  - Selecting "✎ tukar" and picking a value saves immediately; no extra save button.

### 4.5 API changes — `routes/api.py`
- `TopicDeadline` added to imports.
- **`POST /api/session/start`** (extended):
  - Accepts optional `"topic"` string in request body.
  - Looks up `TopicDeadline(student_id, subject_id, topic)` → reads `.profile`.
  - If no topic, or topic not in DB: profile = `"Campuran"`.
  - Response now: `{"session_id": N, "profile": "Membaca"}` (was just `session_id`).
  - Log line updated: includes topic and profile.
- **`GET /api/quiz/topics`** (extended):
  - Accepts optional `device_id` query param.
  - If `device_id` provided: returns this student's `TopicDeadline` topics (with
    their classified profiles) rather than all `QuizBank` topics.
  - If no `device_id` or student not found: falls back to QuizBank topics
    (backward compatibility for Quiz Mode calls without device_id).
  - Response format changed from `{"topics": ["string",...]}` to
    `{"topics": [{"topic": "...", "profile": "..."},...]}` — always objects.
  - Always appends `{"topic": "Ulangkaji Bebas", "profile": "Campuran"}` sentinel.

---

## 5. FIRMWARE CHANGES (v11.0)

**File:** `firmware/StudyAid_v9/StudyAid_v9.ino`, version string `v11.0`, branch `v11-dev`.

### 5.1 New enum and globals (added near existing companionMode globals)

```cpp
enum StudyProfile { PROFILE_CAMPURAN, PROFILE_MENULIS, PROFILE_MEMBACA };
StudyProfile activeProfile = PROFILE_CAMPURAN;
const char*  profileNames[] = { "Campuran", "Menulis", "Membaca" };

unsigned long activeStage1Ms   = DEMO_STAGE1_MS;
unsigned long activeStage2Ms   = DEMO_STAGE2_MS;
unsigned long activeStage3Ms   = DEMO_STAGE3_MS;
unsigned long activeSleepingMs = 15000UL;

#define MAX_SESSION_TOPICS 12
char sessionTopicNames[MAX_SESSION_TOPICS][MAX_TOPIC_LEN];
char sessionTopicProfiles[MAX_SESSION_TOPICS][12];
int  sessionTopicCount = 0;
int  sessionTopicIdx   = 0;
char activeTopicName[MAX_TOPIC_LEN] = "";  // "" = Ulangkaji Bebas
```

### 5.2 New forward declarations

```cpp
void fetchSessionTopics(int subjectId);
void applyProfileTimings();
void renderSessionTopic();
```

### 5.3 New function: `applyProfileTimings()`

Called once per `startSession()` after profile is resolved. Scales S1/S2/S3/Sleep
timings. Floors: 2000 ms for stage timings, 5000 ms for sleeping. Serial log shows
the profile name, scale factor, and all four computed values in seconds.

### 5.4 Modified: `startSession(int subjectIndex)`

- Resets `activeProfile = PROFILE_CAMPURAN` at start.
- POST body now `StaticJsonDocument<256>` (was 128) — adds `"topic": activeTopicName`.
- Response parsed with `StaticJsonDocument<96>` (was 64) — reads `"profile"` string,
  sets `activeProfile` enum.
- Calls `applyProfileTimings()` at the end, even if not in Companion mode (ensures
  active* variables are always valid; Campuran = 1.0× = unchanged behaviour).
- Serial log includes topic name and profile.

### 5.5 Modified: warning ladder (in `loop()`)

All four `demoMode ? DEMO_STAGE1_MS : REAL_STAGE1_MS` (and equivalent for S2, S3)
replaced with `activeStage1Ms`, `activeStage2Ms`, `activeStage3Ms`.

The `SLEEPING_TRIGGER_MS` comparison in the WARN_S3 sleeping escalation replaced
with `activeSleepingMs`. The same variable used in the Stage 3 overlay countdown
(`renderHome`).

**Membaca orientation guard** added inside `if (!snoozeActive && currentMotion == MOTION_STATIK)`:

```cpp
if (activeProfile == PROFILE_MEMBACA && currentOrientation == ORIENT_TEGAK) {
    stageEnteredMs = millis();  // reset clock — never advance while arm is raised
} else {
    // ... existing switch(currentWarnStage) { ... } unchanged
}
```

### 5.6 New `AppScreen` value: `SCREEN_SESSION_TOPIC`

Inserted immediately after `SCREEN_START_SESSION` in the enum.

### 5.7 New function: `fetchSessionTopics(int subjectId)`

- GETs `/api/quiz/topics?subject_id=X&device_id=Y` (sends own `deviceId`).
- Parses new object format: `t["topic"]` and `t["profile"]` from each element.
- Stores into `sessionTopicNames[]` and `sessionTopicProfiles[]`.
- Offline fallback: if not `companionReady` or HTTP error, inserts single
  "Ulangkaji Bebas" / "Campuran" entry so the flow always has something to show.
- Uses `StaticJsonDocument<2048>` to accommodate up to ~12 topics.

### 5.8 Modified: `fetchQuizTopics(int subjectId)` (Quiz Mode)

Updated to parse the new object format — extracts `t["topic"]` only (profile
not needed for Quiz Mode). `StaticJsonDocument` increased to 1536 bytes.

### 5.9 New function: `renderSessionTopic()`

Mirrors the Quiz Mode `renderQuizTopic()` UX. When an item is selected:
- Shows topic name (truncated to 14 chars).
- Shows profile name at the right edge in small COL_DIM text.
- "Ulangkaji Bebas" appears last (server-appended sentinel).
- Footer: `[A] Kitar` / `[B] Mula Sesi`.
- If `sessionTopicCount == 0` (server error): shows "Tiada topik / Tambah topik dalam perancangan dahulu."

### 5.10 Modified: `SCREEN_START_SESSION` BtnB handler

Previously called `startSession()` directly. Now:
1. Sets `sessionTopicIdx = 0`.
2. Calls `fetchSessionTopics(subjectSelectIdx + 1)`.
3. Sets `currentScreen = SCREEN_SESSION_TOPIC`.

### 5.11 New: `SCREEN_SESSION_TOPIC` BtnA and BtnB handlers

- **BtnA:** cycles `sessionTopicIdx`.
- **BtnB:** reads `sessionTopicNames[sessionTopicIdx]`; if "Ulangkaji Bebas",
  sets `activeTopicName[0] = '\0'` (empty); else copies to `activeTopicName`.
  Then calls `startSession(subjectSelectIdx)` and goes to `SCREEN_HOME`.

### 5.12 Modified: active session screen (`renderHome`)

Profile label added to Row 1 (subject name row). When `activeProfile !=
PROFILE_CAMPURAN`, prints the profile name in `textSize(1)` / `COL_DIM` at the
right edge of the display (right-aligned by char count). Campuran is silent —
it is the default/unchanged behaviour.

### 5.13 Modified: `renderCurrentScreen()`

Added `case SCREEN_SESSION_TOPIC: renderSessionTopic(); break;` alongside existing
screen cases.

---

## 6. DEMO FLOW (v11.0)

**Setup:**
1. Add topic in planner (spins ~2–5 s while Gemini classifies + generates quiz).
2. Verify profile badge appears (e.g. "📖 Membaca" in teal for Sejarah / Kemerdekaan).
3. Teacher can override with the inline dropdown if AI misclassified.

**On device:**
1. Home → Mula Sesi → pick subject (e.g. Sejarah).
2. SCREEN_SESSION_TOPIC appears — shows "Kemerdekaan Malaysia [Membaca]" + "Ulangkaji Bebas".
3. Pick "Kemerdekaan Malaysia" → BtnB → session starts. Serial shows:
   `[v11] Profil: Membaca — skala 2.0x (S1=10s S2=10s S3=10s Tidur=...)`
4. Raise arm (Tegak) and hold still → ladder does NOT advance (Membaca trust rule).
5. Lower arm flat (Lintang) and hold still → ladder advances at 2× pace.
6. Session screen shows subject name on left, "Membaca" dim label on right of Row 1.

**Demo contrast (Campuran vs Membaca same device):**
- Start session with "Ulangkaji Bebas" (Campuran, 1×) — propped reading trips the
  alarm at 15 s (demo).
- Start session with the Sejarah topic (Membaca, 2×+Tegak rule) — same propped
  reading stays calm.

---

## 7. HARDWARE AND TECHNICAL FACTS CARRIED FORWARD

- `MOTION_AKTIF_THRESHOLD = 0.015f` (actual code value — NOT 0.03f as docs stated)
- `ORIENT_TEGAK_THRESHOLD = 0.4f` (flat ax~0, vertical ax~0.75+)
- `POSTURE_HYSTERESIS = 5` samples
- Gemini model: `gemini-2.5-flash-lite` (confirmed working)
- `sleepingCount * -8` penalty still live in focus formula (not retired despite docs)
- ESP32 has no RTC — all timestamps from `datetime.utcnow()` server-side
- `printWrapped()` helper in firmware for word-wrap on 240×135 display
- Unified palette: COL_ACCENT/GOOD/WARN/DANGER/INFO/TEXT/DIM
- `QuizAnswer.session_id` is `nullable=True`
- Arduino IDE requires explicit forward declarations for non-void return functions
- ESPAsyncWebServer: mathieucarbou fork required
- Git on Windows: use separate commands, not `&&` chaining (PowerShell 5.x)
- DB schema changes require deleting `studyaid.db` and reseeding

---

## 8. QUIZ MODE (MOD KUIZ) — COMPATIBILITY NOTE

`GET /api/quiz/topics` response format changed from `{"topics": ["string",...]}` to
`{"topics": [{"topic":"...","profile":"..."},...]}`  in v11. The firmware's
`fetchQuizTopics()` (used by Mod Kuiz SCREEN_QUIZ_TOPIC) was updated to parse
the new object format (extracts `t["topic"]` only). Both session-start and
Quiz Mode topic pickers are compatible with the new format on v11-dev.

---

## 9. OPEN ITEMS / POSSIBLE NEXT STEPS

- **Hardware test:** Confirm the Campuran vs Membaca contrast is visually dramatic
  on real hardware. The Tegak orientation guard is the main differentiator — verify
  `axWindow` reliably reads Tegak when the arm is genuinely raised with material.
- **Seed demo data update:** The existing `seed_demo.py` inserts `TopicDeadline`
  rows without a `profile` field — it will crash after the schema change. Update
  `seed_demo.py` to include `profile="Campuran"` (or the correct profile) on each
  seeded deadline row, OR reseed via the planner UI to get real AI classifications.
- **`session.topic` column:** Session table currently does not store which topic was
  studied. Adding this would allow per-topic accuracy tracking on the dashboard.
  Not in v11 scope — evaluate post-demo.
- **Mod Kuiz + session_id in Quiz Mode:** The Quiz Mode `SCREEN_QUIZ_TOPIC` BtnB
  handler sends a `session/start` POST without a `topic` field — will receive
  `profile: "Campuran"` which is ignored (Quiz Mode doesn't use profiles). No bug,
  but the payload could be cleaned up later.
- **Raspberry Pi migration** — still deferred, unchanged from v10.
- **`SLEEPING_TRIGGER_MS` in focus formula denominator:** The `-8` penalty is still
  live (Discrepancy 2). Decide before competition whether to document honestly or
  fix the formula. Currently the docs are simply wrong about "retired."

---

## 10. USER PREFERENCES (unchanged)

- Accumulate change requests; confirm written change list before generating code.
- Present full outline before writing any document.
- Explanation-focused: reasoning behind decisions, not just steps.
- Hardware-driven iteration: test on real hardware, report observations, accumulate.
- Scope discipline: evaluate features against judge scrutiny before building.
- Bahasa Malaysia for UI / student-facing / demo content; English for code,
  comments, README, technical docs.
- Git: master = stable, v9-dev = v9, v10-dev = v10 final, v11-dev = active.
- No placeholder branding — use SMK Gudang Rasau (SEGRA), Kuantan, Pahang.

---

## 11. HOW TO START THE NEXT CHAT

1. Paste v1 handover, v2 handover, HANDOVER_V10.md, HANDOVER_V11.md (design),
   and this document (v11 built delta).
2. Add: *"These are the StudyAid handover documents. v11-dev is the active branch.
   v11.0 is the last commit. Please confirm understanding before we proceed."*
3. First actions: clone/checkout v11-dev, verify the key globals and functions
   listed in this document exist in the actual code before scoping any new work.

---

*Generated at the close of the v11.0 development session.*
*SMK Gudang Rasau (SEGRA), Kuantan, Pahang.*
