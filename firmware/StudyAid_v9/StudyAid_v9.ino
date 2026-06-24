/*
 * StudyAid Firmware v12.0
 * Hardware: M5StickS3 + M5 Unit NFC (ST25R3916, I2C via Grove Port A)
 *
 * Button mapping:
 *   BtnA (front) : Kitar menu / parameter / bangun dari tidur / kitar pilihan kuiz
 *   BtnB (side)  : Sahkan / Pilih / tukar nilai / sahkan jawapan kuiz
 *
 * Wiring:
 *   Unit NFC -> Grove Port A (HY2.0-4P) on M5StickS3
 *   (SDA/SCL assigned automatically via M5.getPin)
 *
 * WiFi (Solo mode)     : AP  SSID=StudyAid  Password=studyaid123  IP=192.168.4.1
 * WiFi (Companion mode): STA SSID=studyaid-pi  connects to Raspberry Pi AP
 *
 * Libraries required:
 *   - M5Unified          (replaces M5StickCPlus2.h)
 *   - M5UnitUnified      (Unit manager framework)
 *   - M5UnitUnifiedNFC   (ST25R3916 NFC driver)
 *   - Preferences        (NVS storage)
 *   - WiFi               (built-in ESP32-S3)
 *   - HTTPClient         (built-in ESP32, used in Companion mode)
 *   - ESPAsyncWebServer  (web dashboard, Solo mode only)
 *   - ArduinoJson        (JSON API)
 *
 * Version history:
 *   v1    - initial POC
 *   v2    - revised state model, new UX, audio state feedback
 *   v3    - revised focus score, screen layout, sleeping exit via BtnA
 *   v4    - WiFi AP, web dashboard, session history
 *   v5    - settings screen, reset, web settings tab
 *   v6    - rest tag, back navigation, home NFC auto-start, optional calibration
 *   v7    - Bahasa Malaysia UI, RETURN nav item, tag registration status,
 *           rest in timeline, battery level, post-session dashboard summary
 *   v8.1  - Ported to M5StickS3 + M5 Unit NFC (ST25R3916 via Grove Port A)
 *   v8.2  - Fixed missing Units.begin() call causing NFC tag registration failure
 *   v8.3  - Fixed speaker volume and sleeping/rest-expired buzz not sounding
 *   v8.4  - Sharper frequencies for sleeping and rest-expired alerts
 *           Uses M5Unified, M5UnitUnified, BMI270 IMU, M5PM1 power management,
 *           M5.Speaker for audio. IMU thresholds marked for hardware retuning.
 *   v9.0  - Solo/Companion mode toggle (Settings screen)
 *           Companion mode: WiFi STA, HTTP POST session summary to Flask server
 *           Updated subjects: 8 core SPM subjects matching companion app DB
 *           Companion mode defaults: SSID=StudyAid-Laptop, IP=192.168.137.1:5000
 *   v9.1  - Quiz Mode: student-initiated quiz from home menu (Companion only)
 *           Drift quiz: triggered by 2 warnings within configurable window
 *           Quiz window added to Settings (1/3/5 min options)
 *           Session start now POSTs to server, gets server-side session_id
 *           Session end sends server session_id for proper DB update
 *   v9.2  - Periodic focus score POST every 15 seconds during active session
 *           Enables live session monitor on companion app dashboard
 *   v9.3  - Student name shown on Home menu for easier device identification
 *           Subject list updated: Fizik, Kimia, Biologi replace Bahasa Inggeris,
 *           Sains, Pendidikan Moral
 *           Quiz Mode: topic picker screen added between subject and question
 *           New API call: GET /api/quiz/topics?subject_id=X
 *   v12.0 - Raspberry Pi migration: companion server moves from Windows laptop to
 *           Raspberry Pi 5. WiFi constants updated to Pi AP (studyaid-pi / 192.168.4.1).
 *           Focus formula fix: removed sleepingCount*8 penalty — only Stage 3 warning
 *           penalises (-3), matching the documented behaviour since v10. Session summary
 *           breakdown updated: T:- column dropped, now shows A:-  P:+  B:+ only.
 *   v11.0 - Activity-aware sensing via three study profiles (Menulis/Membaca/Campuran).
 *           Profiles are AI-classified at planning time (server-side) and fetched at
 *           session start. Each profile scales the warning-ladder timings (0.5x/1.0x/2.0x)
 *           and Membaca adds an orientation trust rule: Statik Tegak (arm raised, holding
 *           material) does NOT advance the warning ladder — only Statik Lintang escalates.
 *           New SCREEN_SESSION_TOPIC topic picker inserted between subject selection and
 *           session start. GET /api/quiz/topics now returns {topic, profile} objects and
 *           accepts device_id for student-scoped planned topics. POST /api/session/start
 *           now sends "topic" and receives "profile" in the response. applyProfileTimings()
 *           scales all four ladder timings (S1/S2/S3 + Sleeping) at session start.
 *   v10.8 - Fixes: "Salah" result screen now wraps the correct answer (was cut
 *           off when long). Quiz question selection strictly scoped to subject —
 *           removed cross-subject fallback in both _pick_question (drift) and
 *           /api/quiz/questions (manual Mod Kuiz) that leaked other subjects' Qs.
 *   v10.7 - UI polish pass: unified palette (COL_ACCENT/GOOD/WARN/DANGER/INFO/
 *           TEXT/DIM). drawBatteryIcon() — graphical battery on every screen.
 *           drawHeader/Footer rewritten: COL_ACCENT teal frame, WHITE text.
 *           Consistent COL_INFO selection bands on all list screens. Left margin
 *           4px throughout. Posture dot icon on session screen. Thin focus score
 *           progress bar. Boot splash includes school name + tagline. ">" removed
 *           from menu items (band is the cursor). All 0x1F5F / NAVY / DARKGREY
 *           ad-hoc colors replaced with palette names.
 *   v10.6 - Quiz question screen redesigned to prevent option text cutoff.
 *           Question wraps (capped 3 lines); all four options listed compact, the
 *           SELECTED option expands into a highlighted band showing full text
 *           (wraps up to 2 lines). New printWrapped() word-wrap helper. resultShowTime
 *           race fixed (stamp after POST, not before) in both drift + manual quiz.
 *   v10.5 - Drift quiz fixes: non-blocking buzz_driftAlert() (was swallowing
 *           button presses via M5.update() in speakerBeep loop). Mode-aware
 *           trigger: every-S3 in demo, 2-within-window in real. Quiz timeout
 *           (25s demo / 90s real) silently dismisses unanswered quiz and restores
 *           S3/WARNING so Tidak Aktif escalation still fires for a sleeping student.
 *           Non-blocking result screen (no delay() freeze). Server cooldown lowered
 *           to 15s backstop; server now honours device quiz_trigger decision.
 *   v10.4 - Drift quiz behaviour fixed: driftQuizActive flag pauses IMU monitoring
 *           and warning ladder entirely while quiz is on screen. BtnA cycles
 *           options; BtnB submits answer inline (no separate result screen).
 *           Repeating buzz_driftAlert() every 1.5s until student responds.
 *           Monitoring resumes from WARN_S0 after quiz. urlEncode() added to
 *           fix Mod Kuiz 400 error (spaces in topic name). "Tidak Pasti" →
 *           "Inaktif" in engagementNames. Drift quiz reverted to random question.
 *   v10.3 - Part 3: Adaptive drift quiz. Device sends engagement_state in
 *           /api/session/drift POST. Server picks difficulty=2 (hard) when
 *           ENGAGED, difficulty=1 (easy) when UNCERTAIN, with random fallback.
 *           StaticJsonDocument for drift POST increased to 192 bytes.
 *           QuizQuestion schema adds difficulty column (DB reseed required).
 *   v10.2 - UI overhaul: textSize(2) throughout, scrollable lists, new layout
 *           constants (HDR_H/CONTENT_Y/FTR_Y/ROW_H). Demo/Real mode converted
 *           from compile-time #define to NVS-persisted runtime toggle in Settings.
 *           Active session screen redesigned: subject / time / Fokus (large) /
 *           posture / stage indicator. All list screens scrollable at textSize(2).
 *   v10.1 - Four-stage warning ladder (Part 2):
 *           WarnStage enum: WARN_S0/S1/S2/S3
 *           Stage 0: engaged (no alert)
 *           Stage 1: Watching — silent amber dot, no sound, no penalty
 *           Stage 2: Nudge — soft single chime + inline prompt, no penalty
 *           Stage 3: Full alert — full-screen flash + repeating buzz + -3 penalty
 *           Escalation gated by MOTION_STATIK; MOTION_AKTIF drops to Stage 0
 *           BtnA cycles: Stage 1/2 -> snooze; Stage 3 -> recovery
 *           Snooze suppresses escalation for SNOOZE_MS then resumes from S0
 *           Score penalty only at Stage 3 (warningCount++, -3)
 *           sleepingCount kept for summary display but -8 penalty retired
 *           Drift quiz buffer now gated to Stage 3 entries only
 *           Demo/Real presets: #define DEMO_MODE at compile time
 *             Demo: 5s/5s/5s/30s snooze
 *             Real: 2min/2min/2min/5min snooze
 *           Two-axis posture model (v10.0c):
 *             MOTION axis: STATIK (still) / AKTIF (moving) from magnitude
 *             ORIENT axis: LINTANG (flat) / TEGAK (vertical) from X-axis gravity
 *           Displayed together e.g. "Aktif Lintang" (writing), "Statik Tegak"
 *           Orientation read from accelerometer ax (gravity projection) —
 *           hardware-measured: flat ax~0, vertical ax~0.75+; threshold 0.4
 *           Motion threshold 0.015 (above BMI270 resting noise floor 0.002-0.008)
 *           EngagementState (advisory): AKTIF->Fokus, STATIK->Tidak Pasti
 *             Option A: motion-based only. Wrist IMU cannot detect silent phone
 *             scrolling (thumb moves, wrist still) so no phone-use claim made.
 *             DISENGAGED reserved for Part 2 (adds time dimension).
 *           Per-axis hysteresis (POSTURE_HYSTERESIS) prevents display flicker
 *           Calibration Phase 2 captures variance baseline (retained for Part 2)
 *           On-screen: posture prominent, engagement soft/secondary
 *           Honest relabeling: "Tertidur!" -> "Tidak Aktif"
 *           NOTE: posture is additive in Part 1 — existing IMUState warning
 *           logic unchanged. Posture gates the warning ladder in Part 2.
 */

// ─── Core Libraries ────────────────────────────────────────────────────────
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// No namespace imports needed — use fully qualified types below

// ─── v9: HTTPClient for Companion mode uploads ─────────────────────────────
#include <HTTPClient.h>

// ─── WiFi (Solo / AP mode — unchanged from v8.4) ───────────────────────────
#define WIFI_SSID     "StudyAid"
#define WIFI_PASSWORD "studyaid123"
#define WIFI_IP       "192.168.4.1"

// ─── v9: Companion mode WiFi + server defaults ─────────────────────────────
// These are the defaults baked into the firmware.
// COMP_SSID / COMP_PASS : Raspberry Pi AP credentials (v12: Pi replaces Windows hotspot)
// COMP_SERVER_IP        : Pi fixed AP IP (192.168.4.1 — NetworkManager default range)
// COMP_SERVER_PORT      : Flask companion app port
#define COMP_SSID       "studyaid-pi"
#define COMP_PASS       "studyaid123"
#define COMP_SERVER_IP  "192.168.4.1"
#define COMP_SERVER_PORT 5000
#define COMP_TIMEOUT_MS  3000   // HTTP request timeout — fail fast, don't block loop

// ─── v10.2: Display layout constants ───────────────────────────────────────
// All pixel positions derived from these so a single change reflows everything.
// M5StickS3 display: 240×135px landscape.
#define HDR_H       22    // header bar height (px)
#define CONTENT_Y   24    // first content row y (just below header)
#define FTR_Y       114   // footer bar top y
#define FTR_H       21    // footer bar height (px)
#define VISIBLE_ITEMS 5   // items shown per scroll viewport
#define ROW_H       18    // px per row at textSize(2); 5 rows fit in content area

// ─── v10.7: Unified UI palette ─────────────────────────────────────────────
// Single source of truth for all colours. Replaces ad-hoc literals throughout.
#define COL_ACCENT  0x1F5F  // navy-teal — universal header/footer/brand colour
#define COL_GOOD    GREEN   // focus score, confirmed tags, correct answer
#define COL_WARN    ORANGE  // Stage 2/3 warning, caution
#define COL_DANGER  RED     // Stage 3 full alert, delete actions
#define COL_INFO    CYAN    // selection bands, companion-mode indicator
#define COL_TEXT    WHITE   // primary text
#define COL_DIM     DARKGREY // secondary/hint text

// ─── Subjects + Rest slot ──────────────────────────────────────────────────
// v9.3: Updated subject list — Bahasa Inggeris, Sains, Pendidikan Moral removed;
//       Fizik, Kimia, Biologi added. Order must match companion/models/schema.py:
//   0=Bahasa Melayu, 1=Matematik, 2=Sejarah, 3=Geografi,
//   4=Pendidikan Islam, 5=Fizik, 6=Kimia, 7=Biologi
#define NUM_SUBJECTS 8
#define REST_SLOT    8
const char* subjects[NUM_SUBJECTS] = {
  "Bhs Melayu", "Matematik", "Sejarah", "Geografi",
  "Pend Islam", "Fizik", "Kimia", "Biologi"
};

// ─── IMU States ────────────────────────────────────────────────────────────
enum IMUState { STATE_ACTIVE, STATE_READING, STATE_WARNING, STATE_SLEEPING };
const char*    stateNames[]     = { "Aktif", "Membaca", "Amaran!", "Tidak Aktif" };  // v10.0: Tertidur -> Tidak Aktif
const char*    stateColorsHex[] = { "#4CAF50","#00BCD4","#FF9800","#F44336" };
const uint16_t lcdStateColors[] = { COL_GOOD, COL_INFO, COL_WARN, COL_DANGER };

// ─── v10.0: Posture & Engagement States ────────────────────────────────────
// v10.0c: Two-axis posture model. Posture is described by two independent axes:
//   MOTION axis      : STATIK (still) vs AKTIF (moving)
//   ORIENTATION axis : LINTANG (forearm flat) vs TEGAK (forearm vertical)
// These combine into readouts like "Aktif Lintang" (writing) or
// "Statik Tegak" (phone held still). Both are honest direct sensor readings.
//
// Orientation is read from the accelerometer's X axis (gravity projection):
//   flat on desk -> gravity on Z (ax near 0)  -> LINTANG
//   forearm up   -> gravity on X (ax high)     -> TEGAK
// This is far more robust than pitch angle, which barely moved on the BMI270.
//
// EngagementState: inferred from MOTION only (Option A — no phone detection).
//   AKTIF  -> ENGAGED   (student is physically doing something)
//   STATIK -> UNCERTAIN (still does not mean disengaged — reading/thinking)
// Advisory in Part 1: drives display only, not IMUState/warning/focusScore.
enum MotionState      { MOTION_STATIK, MOTION_AKTIF };
enum OrientationState { ORIENT_LINTANG, ORIENT_TEGAK };
const char* motionNames[]      = { "Statik", "Aktif" };
const char* orientationNames[] = { "Lintang", "Tegak" };

enum EngagementState { ENGAGED, UNCERTAIN, DISENGAGED };
const char* engagementNames[] = { "Fokus", "Inaktif", "Tidak Fokus" };
#define TIMELINE_ACTIVE   0
#define TIMELINE_READING  1
#define TIMELINE_WARNING  2
#define TIMELINE_SLEEPING 3
#define TIMELINE_RESTING  4

// ─── App Screens ───────────────────────────────────────────────────────────
enum AppScreen {
  SCREEN_HOME,
  SCREEN_START_SESSION,
  SCREEN_SESSION_TOPIC,   // v11: topic picker inserted between subject select and session start
  SCREEN_REGISTER_TAG,
  SCREEN_CALIBRATE,
  SCREEN_SETTINGS,
  SCREEN_CONFIRM_RESET,
  // v9.1: Quiz Mode screens
  SCREEN_QUIZ_SUBJECT,    // subject picker before quiz starts
  SCREEN_QUIZ_TOPIC,      // v9.3: topic picker after subject selected
  SCREEN_QUIZ_QUESTION,   // question + options display
  SCREEN_QUIZ_RESULT,     // brief correct/incorrect feedback
  SCREEN_QUIZ_SUMMARY     // end-of-set score summary
};

// ─── NFC Tag Registry ──────────────────────────────────────────────────────
#define MAX_TAGS 10
#define TAG_TYPE_SUBJECT 0
#define TAG_TYPE_REST    1
struct NFCTag {
  uint8_t uid[7];
  uint8_t uidLen;
  uint8_t subjectIndex;
  uint8_t tagType;
  bool    registered;
};

// ─── Session Record ────────────────────────────────────────────────────────
#define MAX_HISTORY 20
struct SessionRecord {
  uint32_t timestamp;
  uint8_t  subjectIndex;
  uint32_t durationMs;
  uint32_t restTimeMs;
  uint8_t  restBreaks;
  int16_t  focusScore;
  uint8_t  warningCount;
  uint8_t  sleepingCount;
  int8_t   recoveryBonus;
  int8_t   streakBonus;
  int8_t   durationBonus;
};

// ─── Timeline Entry ────────────────────────────────────────────────────────
#define MAX_TIMELINE_ENTRIES 200
struct TimelineEntry {
  unsigned long timestamp;
  uint8_t       state;
};

// ─── Distraction Log ───────────────────────────────────────────────────────
#define MAX_DISTRACTION_LOG 50
struct DistractionEntry {
  unsigned long timestamp;
  uint8_t       type;
  int           scoreImpact;
};

// ─── Settings ──────────────────────────────────────────────────────────────
struct Settings {
  uint8_t warningTrigger;
  uint8_t sleepTrigger;
  uint8_t sleepBuzzInterval;
  uint8_t distrThreshold;
  uint8_t deepFocusInterval;
  uint8_t imuSensitivity;
  uint8_t restDuration;
  uint8_t quizWindow;      // v9.1: drift quiz trigger window (0=1min,1=3min,2=5min)
};

const char* settingLabels[] = {
  "Pencetus amaran",
  "Pencetus tidur",
  "Getar tidur",
  "Gangguan",
  "Selang fokus",
  "Kepekaan IMU",
  "Tempoh rehat",
  "Tetingkap Kuiz"   // v9.1
};

const char* settingOptions[8][3] = {
  { "Singkat (30s)",  "Sedang (60s)",  "Panjang (120s)" },
  { "Singkat (15s)",  "Sedang (30s)",  "Panjang (60s)"  },
  { "Singkat (2s)",   "Sedang (5s)",   "Panjang (10s)"  },
  { "Singkat (15s)",  "Sedang (30s)",  "Panjang (60s)"  },
  { "Singkat (5min)", "Sedang (10min)","Panjang (25min)"},
  { "Rendah (0.15)",  "Sedang (0.30)", "Tinggi (0.50)"  },
  { "Singkat (1min)", "Sedang (5min)", "Panjang (10min)"},
  { "1 minit",        "3 minit",       "5 minit"        }  // v9.1 quiz window
};

const unsigned long warningTriggerVals[]    = { 30000,   60000,   120000  };
const unsigned long sleepTriggerVals[]      = { 15000,   30000,   60000   };
const unsigned long sleepBuzzIntervalVals[] = { 2000,    5000,    10000   };
const unsigned long distrThresholdVals[]    = { 15000,   30000,   60000   };
const unsigned long deepFocusIntervalVals[] = { 300000,  600000,  1500000 };
const float         imuSensitivityVals[]    = { 0.15f,   0.30f,   0.50f   };
const unsigned long restDurationVals[]      = { 60000,   300000,  600000  };

// ─── v10.1: Demo / Real timing presets ─────────────────────────────────────
// Previously a compile-time #define DEMO_MODE flag — now a runtime NVS-persisted
// toggle (demoMode bool, Settings index 11). All timing constants remain defined;
// the active set is selected at runtime via demoMode ? DEMO_* : REAL_*.

// Real preset — humane, minutes-scale escalation
#define REAL_STAGE1_MS  120000UL   // 2 min still before Stage 1 (Watching)
#define REAL_STAGE2_MS  120000UL   // 2 min more before Stage 2 (Nudge)
#define REAL_STAGE3_MS  120000UL   // 2 min more before Stage 3 (Full alert)
#define REAL_SNOOZE_MS  300000UL   // 5 min snooze duration

// Demo preset — fast, all stages visible in ~15 seconds
#define DEMO_STAGE1_MS    5000UL   // 5s before Stage 1
#define DEMO_STAGE2_MS    5000UL   // 5s more before Stage 2
#define DEMO_STAGE3_MS    5000UL   // 5s more before Stage 3
#define DEMO_SNOOZE_MS   30000UL   // 30s snooze

// v10.5: drift quiz INACTIVITY timeout — if no button press for this long, the
// quiz auto-dismisses and monitoring resumes (so a sleeping student still gets the
// Tidak Aktif alarm). Resets on every BtnA press, so an active student is never cut off.
#define DEMO_QUIZ_TIMEOUT_MS  25000UL   // 25s in demo — comfortable read+answer time
#define REAL_QUIZ_TIMEOUT_MS  90000UL   // 90s in real use

// companionMode  : false = Solo (WiFi AP, same as v8.4)
//                  true  = Companion (WiFi STA, uploads to Flask server)
// companionReady : set true after successful WiFi STA connection at boot
// deviceId       : identifies this device in session uploads (matches DB seed)
// studentName    : displayed on Home menu so devices are easy to tell apart
bool        companionMode  = false;
bool        companionReady = false;
bool        demoMode       = false;   // v10.2: runtime Demo/Real toggle (NVS: "demo_mode")
// v10.4: drift quiz state — pauses monitoring while student answers
bool        driftQuizActive  = false;
unsigned long lastDriftAlertMs = 0;  // tracks repeating alert interval
unsigned long driftQuizStartMs = 0;  // v10.5: when quiz opened (for timeout)
bool        driftResultActive = false; // v10.5: showing drift quiz result (non-blocking)
const char* deviceId       = "studyaid-01";   // change to "studyaid-02" for second device
const char* studentName    = "M. Khalish";    // change to "Rania Batrisyia" for second device

// ─── v11: Study profiles ───────────────────────────────────────────────────
// Three profiles change (a) warning-ladder timings and (b) for Membaca, an
// orientation trust rule (Statik Tegak = holding material = trusted, not warned).
// Profiles are AI-classified at planning time and fetched from the server at
// session start; teacher-overridable via the dashboard.
enum StudyProfile { PROFILE_CAMPURAN, PROFILE_MENULIS, PROFILE_MEMBACA };
StudyProfile activeProfile = PROFILE_CAMPURAN;
const char*  profileNames[] = { "Campuran", "Menulis", "Membaca" };

// Scaled timing variables — computed by applyProfileTimings() at session start.
// Multipliers: Menulis 0.5x, Campuran 1.0x, Membaca 2.0x vs the demo/real base.
// All warning-ladder comparisons use these instead of the raw DEMO_*/REAL_* macros.
unsigned long activeStage1Ms   = DEMO_STAGE1_MS;   // default to demo until first session
unsigned long activeStage2Ms   = DEMO_STAGE2_MS;
unsigned long activeStage3Ms   = DEMO_STAGE3_MS;
unsigned long activeSleepingMs = 15000UL;           // default; overwritten at session start

// v9: Forward declaration — postToServer() body is defined later in the file,
// after setup(). Without this the compiler rejects the call inside endSession().
bool postToServer(const char* path, const String& jsonBody);
String postToServerWithResponse(const char* path, const String& jsonBody);  // v9.1

// ─── v9.1: Server-assigned session ID ──────────────────────────────────────
// Returned by /api/session/start. Sent with every subsequent API call so the
// server can associate drift events, answers and session end with the right row.
int serverSessionId = -1;   // -1 = not yet assigned by server

// ─── v9.1: Quiz Mode state ──────────────────────────────────────────────────
#define MAX_QUIZ_QUESTIONS 10
#define MAX_Q_TEXT    256
#define MAX_OPT_TEXT   80

struct QuizQuestion_t {
  int  id;
  char text[MAX_Q_TEXT];
  char opts[4][MAX_OPT_TEXT];
  int  correctIndex;
};

struct QuizState_t {
  QuizQuestion_t questions[MAX_QUIZ_QUESTIONS];
  int  totalLoaded;         // questions fetched from server
  int  currentIdx;          // current question index (0-based)
  int  selectedOpt;         // BtnA cursor (0–3)
  int  score;               // correct answers so far
  bool isDriftQuiz;         // triggered by drift vs manual
  bool resultCorrect;       // for SCREEN_QUIZ_RESULT display
  unsigned long resultShowTime; // millis() when result shown
};
QuizState_t quizState;

int quizSubjectIdx = 0;     // subject picker cursor

// v9.3: Topic picker state
#define MAX_QUIZ_TOPICS 10
#define MAX_TOPIC_LEN   80
char quizTopics[MAX_QUIZ_TOPICS][MAX_TOPIC_LEN];
int  quizTopicCount = 0;
int  quizTopicIdx   = 0;    // topic picker cursor

// v11: Session topic picker state (separate from Quiz Mode topic picker).
// MAX_TOPIC_LEN must be defined above before this block.
// Populated by fetchSessionTopics() when student confirms subject at session start.
#define MAX_SESSION_TOPICS 12   // planned topics (up to ~10) + Ulangkaji Bebas sentinel
char sessionTopicNames[MAX_SESSION_TOPICS][MAX_TOPIC_LEN];
char sessionTopicProfiles[MAX_SESSION_TOPICS][12];  // "Menulis","Membaca","Campuran"
int  sessionTopicCount = 0;
int  sessionTopicIdx   = 0;
char activeTopicName[MAX_TOPIC_LEN] = "";  // "" = Ulangkaji Bebas (free study)

// ─── v9.2: Periodic focus report ───────────────────────────────────────────
// POSTs current focus score to /api/session/update every 15 seconds during
// an active Companion mode session. Enables live monitor on dashboard.
#define FOCUS_REPORT_INTERVAL_MS 15000UL

unsigned long lastFocusReportMs = 0;

// ─── v12.1: Booth hello ping ─────────────────────────────────────────────────
// POSTs /api/hello immediately on WiFi connect, then every 30 seconds.
// This tells the booth kiosk (/booth) that a device is present so it can
// show the welcome screen with the student's name and study plan.
// Runs independently of any session — works in companion mode even at idle.
#define HELLO_INTERVAL_MS 30000UL

unsigned long lastHelloMs = 0;
#define MAX_WARN_BUF 8
unsigned long warnTimestamps[MAX_WARN_BUF];
int           warnBufCount    = 0;
unsigned long lastDriftQuizMs = 0;
#define DRIFT_QUIZ_COOLDOWN_MS 600000UL  // 10 min cooldown between drift quizzes

// Quiz window durations matching settings index 0/1/2 → 1/3/5 min
const unsigned long quizWindowVals[] = { 60000UL, 180000UL, 300000UL };

// Forward declarations for quiz functions defined later in the file
void fetchQuizTopics(int subjectId);   // v9.3: topic picker
void fetchQuizQuestions(int subjectId, const char* topic);
void renderQuizSubject();
void renderQuizTopic();                // v9.3: topic picker screen
void renderQuizQuestion();
void renderQuizResult();
void renderQuizSummary();
void handleDriftQuizResponse(const String& responseBody);

// v11: Forward declarations for session topic picker and profile timing functions
void fetchSessionTopics(int subjectId);  // GET /api/quiz/topics with device_id
void applyProfileTimings();              // scale S1/S2/S3/Sleep timings by active profile
void renderSessionTopic();               // SCREEN_SESSION_TOPIC render function
int  printWrapped(const char* s, int x, int y, int maxChars, int maxLines, int lineH);

// v10.0: Forward declarations for posture/engagement functions
float            getMagVariance();
MotionState      classifyMotion();
OrientationState classifyOrientation();
void             updateEngagement();

// ─── NFC (M5UnitUnifiedNFC) ────────────────────────────────────────────────
//
// The M5 Unit NFC uses the ST25R3916 chip via I2C at address 0x50.
// Managed via M5UnitUnified framework.
// Uses native UnitST25R3916 NFC-A methods (nfcaRequest, nfcaSelectWithAnticollision, nfcaHlt).
// Supports NTAG213/215 (Type2 tags). UID read via m5::nfc::a::PICC.uid[] / .size.
//
m5::unit::UnitUnified  Units;
m5::unit::UnitNFC      unitNFC;

bool nfcReady = false;

// ─── Global Variables ──────────────────────────────────────────────────────
Preferences    prefs;
AsyncWebServer server(80);

// Runtime settings
Settings settings = { 1, 1, 1, 1, 1, 1, 1 };
unsigned long WARNING_TRIGGER_MS       = 60000;
unsigned long SLEEPING_TRIGGER_MS      = 30000;
unsigned long SLEEPING_BUZZ_INTERVAL   = 5000;
unsigned long DISTRACTION_THRESHOLD_MS = 30000;
unsigned long DEEP_FOCUS_INTERVAL_MS   = 600000;
unsigned long REST_DURATION_MS         = 300000;

// ─── IMU Thresholds ────────────────────────────────────────────────────────
// NOTE: These initial values are placeholders for the BMI270 on M5StickS3.
// The MPU6886 (v7) used 0.30f as the "medium" threshold.
// The BMI270 has different sensitivity characteristics — tune these values
// on the actual hardware after first flash by observing Serial output
// ([IMU] mag=X.XXX) during typical study movements (writing, reading, typing).
// Good starting point: set HIGH = ~0.25 and LOW = ~0.07 then adjust.
float calMagnitudeThresholdHigh = 0.30f; // TODO: retune for BMI270
float calMagnitudeThresholdLow  = 0.08f; // TODO: retune for BMI270
bool  calibrated                = false;

// Screen
AppScreen currentScreen    = SCREEN_HOME;
int       homeMenuIdx      = 0;
int       subjectSelectIdx = 0;
int       registerSelectIdx= 0;
int       settingsParamIdx = 0;
int       confirmResetType = 0;

// Session
bool          sessionActive    = false;
bool          sessionPaused    = false;
int           currentSubject   = -1;
unsigned long sessionStartTime = 0;
unsigned long pauseStartTime   = 0;
unsigned long totalPausedMs    = 0;
unsigned long totalRestMs      = 0;
int           restBreakCount   = 0;
unsigned long restTimerStart   = 0;
bool          restTimerExpired = false;
unsigned long lastRestBuzz     = 0;

// Post-session summary
bool          lastSessionValid    = false;
int           lastFocusScore      = 0;
int           lastWarningCount    = 0;
int           lastSleepingCount   = 0;
int           lastRecoveryBonus   = 0;
int           lastStreakBonus     = 0;
int           lastDurationBonus   = 0;
int           lastRestBreaks      = 0;
unsigned long lastDurationMs      = 0;
unsigned long lastRestTimeMs      = 0;
int           lastSubject         = 0;
int           lastDistractionCount= 0;

// Focus score
int           focusScore        = 100;
int           warningCount      = 0;
int           sleepingCount     = 0;
int           recoveryBonus     = 0;
int           streakBonus       = 0;
int           durationBonus     = 0;
int           streakBonusEarned = 0;
int           distractionCount  = 0;
unsigned long streakStartTime   = 0;

// IMU
IMUState      currentState      = STATE_READING;
IMUState      lastState         = STATE_READING;
unsigned long lastMovementTime  = 0;
unsigned long warningStartTime  = 0;
bool          inWarning         = false;  // shim: true when currentWarnStage >= STAGE_1
unsigned long lastSleepingBuzz  = 0;
bool          flashState        = false;

// ─── v10.1: Four-stage warning ladder ──────────────────────────────────────
// Replaces the two-step Warning -> Sleeping escalation.
// Stage 0: engaged / no alert
// Stage 1: Watching (silent — subtle on-screen indicator only)
// Stage 2: Nudge (soft chime + small prompt, no takeover, no penalty)
// Stage 3: Full alert (full-screen flash + repeating buzz + score penalty)
// Escalation gated by MOTION_STATIK; any MOTION_AKTIF drops to Stage 0.
// inWarning kept as a compatibility shim for existing code.
enum WarnStage { WARN_S0, WARN_S1, WARN_S2, WARN_S3 };
WarnStage     currentWarnStage = WARN_S0;
unsigned long stageEnteredMs   = 0;   // when current stage started
bool          snoozeActive     = false;
unsigned long snoozeStartMs    = 0;

// IMU window
#define IMU_WINDOW_SIZE 15
float magWindow[IMU_WINDOW_SIZE]   = {0};
float pitchWindow[IMU_WINDOW_SIZE] = {0};
float axWindow[IMU_WINDOW_SIZE]    = {0};  // v10.0c: X-axis for orientation (gravity)
int   imuWindowIdx  = 0;
bool  imuWindowFull = false;

// ─── v10.0: Posture & Engagement globals ───────────────────────────────────
// v10.0c: Two-axis model — motion and orientation tracked independently.
MotionState      currentMotion      = MOTION_STATIK;
OrientationState currentOrientation = ORIENT_LINTANG;
EngagementState  currentEngagement  = UNCERTAIN;

// Hysteresis: each axis only commits after POSTURE_HYSTERESIS consecutive
// samples agree. Prevents flickering on the display during brief transitions.
// TODO: tune on BMI270 hardware — increase if label flickers, decrease if sluggish
#define POSTURE_HYSTERESIS 5
int              motionHoldCount     = 0;
int              orientHoldCount     = 0;
MotionState      motionCandidate     = MOTION_STATIK;
OrientationState orientCandidate     = ORIENT_LINTANG;

// Productive motion variance baseline — learned during calibration Phase 2.
// Retained for Part 2 adaptive use; not used by Part 1 engagement (Option A).
float calVarianceBaseline = 0.020f;  // starting value; overwritten by calibration

// Engagement hold timer: how long a motion state must persist before
// engagement commits. Prevents noise from instant transitions.
#define ENGAGEMENT_HOLD_MS 3000UL
unsigned long engagementHoldStart = 0;
EngagementState engagementCandidate = UNCERTAIN;

// Timeline
TimelineEntry timeline[MAX_TIMELINE_ENTRIES];
int           timelineCount = 0;

// Distraction log
DistractionEntry distractionLog[MAX_DISTRACTION_LOG];
int              distractionLogCount = 0;

// History
SessionRecord sessionHistory[MAX_HISTORY];
int           historyCount = 0;

// NFC tag registry
NFCTag tagRegistry[MAX_TAGS];
int    tagCount   = 0;
bool   hasRestTag = false;
bool   subjectHasTag[NUM_SUBJECTS] = {false};

// Notification
char          notificationMsg[32] = "";
unsigned long notificationStart   = 0;
bool          notificationActive  = false;

// Timing
unsigned long lastStateSample    = 0;
unsigned long lastDisplayRefresh = 0;
#define DISPLAY_REFRESH_MS       500
#define CALIBRATION_DURATION_MS  20000
#define NOTIFICATION_DURATION_MS 2000

// ─── Apply Settings ────────────────────────────────────────────────────────
void applySettings() {
  WARNING_TRIGGER_MS       = warningTriggerVals   [settings.warningTrigger];
  SLEEPING_TRIGGER_MS      = sleepTriggerVals     [settings.sleepTrigger];
  SLEEPING_BUZZ_INTERVAL   = sleepBuzzIntervalVals[settings.sleepBuzzInterval];
  DISTRACTION_THRESHOLD_MS = distrThresholdVals   [settings.distrThreshold];
  DEEP_FOCUS_INTERVAL_MS   = deepFocusIntervalVals[settings.deepFocusInterval];
  REST_DURATION_MS         = restDurationVals     [settings.restDuration];
  if (!calibrated) calMagnitudeThresholdHigh = imuSensitivityVals[settings.imuSensitivity];
}

// ─── NVS ───────────────────────────────────────────────────────────────────
void saveSettingsToNVS() { prefs.putBytes("settings",&settings,sizeof(Settings)); }

void loadSettingsFromNVS() {
  if (prefs.isKey("settings")) {
    prefs.getBytes("settings",&settings,sizeof(Settings));
    settings.warningTrigger    = constrain(settings.warningTrigger,   0,2);
    settings.sleepTrigger      = constrain(settings.sleepTrigger,     0,2);
    settings.sleepBuzzInterval = constrain(settings.sleepBuzzInterval,0,2);
    settings.distrThreshold    = constrain(settings.distrThreshold,   0,2);
    settings.deepFocusInterval = constrain(settings.deepFocusInterval,0,2);
    settings.imuSensitivity    = constrain(settings.imuSensitivity,   0,2);
    settings.restDuration      = constrain(settings.restDuration,     0,2);
    settings.quizWindow        = constrain(settings.quizWindow,       0,2);  // v9.1
  }
  applySettings();
  // v9: load companion mode flag
  companionMode = prefs.getBool("comp_mode", false);
  Serial.printf("[v9] Mod: %s\n", companionMode ? "Rakan (Companion)" : "Solo");
  // v10.2: load Demo/Real toggle
  demoMode = prefs.getBool("demo_mode", false);
  Serial.printf("[v10.2] Mod masa: %s\n", demoMode ? "DEMO" : "Nyata");
  // v10.0: load productive variance baseline from calibration Phase 2
  if (prefs.isKey("cal_var")) {
    calVarianceBaseline = prefs.getFloat("cal_var", 0.020f);
    Serial.printf("[v10] varBaseline=%.4f\n", calVarianceBaseline);
  }
}

void loadHistoryFromNVS() {
  historyCount = constrain(prefs.getInt("histCount",0),0,MAX_HISTORY);
  for (int i=0;i<historyCount;i++) {
    char key[16]; sprintf(key,"sess%d",i);
    prefs.getBytes(key,&sessionHistory[i],sizeof(SessionRecord));
  }
}

void saveHistoryToNVS() {
  prefs.putInt("histCount",historyCount);
  for (int i=0;i<historyCount;i++) {
    char key[16]; sprintf(key,"sess%d",i);
    prefs.putBytes(key,&sessionHistory[i],sizeof(SessionRecord));
  }
}

void clearHistoryNVS() {
  historyCount=0; prefs.putInt("histCount",0);
  for (int i=0;i<MAX_HISTORY;i++) {
    char key[16]; sprintf(key,"sess%d",i); prefs.remove(key);
  }
}

void clearAllNVS() { prefs.clear(); delay(500); ESP.restart(); }

void appendSessionRecord(SessionRecord& rec) {
  if (historyCount<MAX_HISTORY) sessionHistory[historyCount++]=rec;
  else {
    memmove(sessionHistory,sessionHistory+1,(MAX_HISTORY-1)*sizeof(SessionRecord));
    sessionHistory[MAX_HISTORY-1]=rec;
  }
  saveHistoryToNVS();
}

void rebuildTagStatus() {
  hasRestTag=false;
  for (int i=0;i<NUM_SUBJECTS;i++) subjectHasTag[i]=false;
  for (int i=0;i<tagCount;i++) {
    if (tagRegistry[i].tagType==TAG_TYPE_REST) hasRestTag=true;
    else if (tagRegistry[i].subjectIndex<NUM_SUBJECTS)
      subjectHasTag[tagRegistry[i].subjectIndex]=true;
  }
}

void saveTagsToNVS() {
  prefs.putInt("tagCount",tagCount);
  for (int i=0;i<tagCount;i++) {
    char key[16]; sprintf(key,"tag%d",i);
    prefs.putBytes(key,&tagRegistry[i],sizeof(NFCTag));
  }
}

void loadTagsFromNVS() {
  tagCount=prefs.getInt("tagCount",0);
  for (int i=0;i<tagCount;i++) {
    char key[16]; sprintf(key,"tag%d",i);
    prefs.getBytes(key,&tagRegistry[i],sizeof(NFCTag));
  }
  rebuildTagStatus();
}

// ─── Timeline & Log ────────────────────────────────────────────────────────
void addTimelineEntry(uint8_t state) {
  if (!sessionActive) return;
  if (timelineCount>=MAX_TIMELINE_ENTRIES) {
    memmove(timeline,timeline+1,(MAX_TIMELINE_ENTRIES-1)*sizeof(TimelineEntry));
    timelineCount=MAX_TIMELINE_ENTRIES-1;
  }
  unsigned long paused=totalPausedMs+(sessionPaused?millis()-pauseStartTime:0);
  timeline[timelineCount].timestamp=millis()-sessionStartTime-paused;
  timeline[timelineCount].state=state;
  timelineCount++;
}

void addDistractionEntry(uint8_t type,int impact) {
  if (!sessionActive||distractionLogCount>=MAX_DISTRACTION_LOG) return;
  unsigned long paused=totalPausedMs+(sessionPaused?millis()-pauseStartTime:0);
  distractionLog[distractionLogCount].timestamp=millis()-sessionStartTime-paused;
  distractionLog[distractionLogCount].type=type;
  distractionLog[distractionLogCount].scoreImpact=impact;
  distractionLogCount++;
}

// ─── Helpers ───────────────────────────────────────────────────────────────
void showNotification(const char* msg) {
  strncpy(notificationMsg,msg,sizeof(notificationMsg)-1);
  notificationStart=millis(); notificationActive=true;
}

String formatTime(unsigned long ms) {
  unsigned long s=ms/1000,m=s/60,h=m/60; s%=60; m%=60;
  char buf[12];
  if (h>0) sprintf(buf,"%02lu:%02lu:%02lu",h,m,s);
  else     sprintf(buf,"%02lu:%02lu",m,s);
  return String(buf);
}

unsigned long getActiveSessionMs() {
  if (!sessionActive) return 0;
  unsigned long paused=totalPausedMs+(sessionPaused?millis()-pauseStartTime:0);
  return millis()-sessionStartTime-paused;
}

int getBatteryLevel() {
  // M5Unified provides battery level via M5.Power
  return M5.Power.getBatteryLevel();
}

// ─── Audio (M5StickS3 onboard speaker via ES8311 + AW8737) ────────────────
//
// v7 used ledcWriteTone() / ledcAttach() on the passive buzzer (GPIO2).
// v8 uses M5.Speaker.tone() which drives the ES8311 codec on the S3.
//
// Key behaviour: M5.Speaker.tone(freq, durationMs) is asynchronous — it
// queues the tone and returns immediately. M5.update() must be called
// periodically to pump the speaker driver. Using a plain delay() without
// M5.update() inside the wait causes the speaker to stay silent.
//
// speakerBeep() pumps M5.update() during the wait so the tone actually plays,
// then waits for the speaker to finish before returning.
//
void speakerBeep(uint16_t freq, uint32_t durationMs) {
  M5.Speaker.tone(freq, durationMs);
  unsigned long end = millis() + durationMs + 10;
  while (millis() < end) { M5.update(); delay(5); }
  M5.Speaker.stop();
}

void buzz_nfcScanned()    { speakerBeep(1200,80); }
void buzz_tagRegistered() { speakerBeep(1000,100); delay(60); speakerBeep(1300,150); }
void buzz_sessionStart()  { speakerBeep(800,120); delay(60); speakerBeep(1000,120); delay(60); speakerBeep(1200,180); }
void buzz_sessionEnd()    { speakerBeep(1200,120); delay(60); speakerBeep(1000,120); delay(60); speakerBeep(800,180); }
void buzz_calibration()   { speakerBeep(800,120); delay(60); speakerBeep(1000,120); delay(60); speakerBeep(1200,180); }
void buzz_warning()       { speakerBeep(800,180); delay(80); speakerBeep(1100,180); delay(80); speakerBeep(1400,250); }
void buzz_nudge()         { speakerBeep(900,120); }  // v10.1: Stage 2 soft single chime
void buzz_sleeping()      { speakerBeep(1400,120); delay(60); speakerBeep(1100,120); delay(60); speakerBeep(1400,120); delay(60); speakerBeep(1100,200); }
void buzz_recovery()      { speakerBeep(800,120); delay(60); speakerBeep(1000,120); delay(60); speakerBeep(1200,180); }
void buzz_streakBonus()   { speakerBeep(1000,100); delay(60); speakerBeep(1200,100); delay(60); speakerBeep(1400,200); }
void buzz_restStart()     { speakerBeep(800,200); delay(100); speakerBeep(600,300); }
void buzz_restEnd()       { speakerBeep(600,200); delay(100); speakerBeep(800,200); delay(100); speakerBeep(1000,300); }
void buzz_restExpired()   { speakerBeep(1300,150); delay(60); speakerBeep(900,150); delay(60); speakerBeep(1300,150); delay(60); speakerBeep(900,250); }
void buzz_error()         { speakerBeep(400,400); }
void buzz_confirm()       { speakerBeep(1000,80); delay(40); speakerBeep(1000,80); }
// v10.5: NON-BLOCKING drift alert — uses M5.Speaker.tone() directly with NO
// M5.update() wait loop. The blocking speakerBeep() pumps M5.update() internally,
// which consumes edge-triggered button events (wasPressed) before loop() sees them,
// making buttons feel unresponsive. This fires the tone and returns immediately so
// the main loop keeps reading buttons every cycle.
void buzz_driftAlert()    { M5.Speaker.tone(1000, 150); }

// v10.4: URL-encode a string — replaces spaces and reserved chars with %XX
// Used by fetchQuizQuestions() to safely encode topic names in the query string.
String urlEncode(const char* src) {
  String out;
  while (*src) {
    char c = *src++;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;  // unreserved — pass through
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

// ─── Focus Score ───────────────────────────────────────────────────────────
void recalcFocusScore() {
  if (!sessionActive) return;
  durationBonus=min((int)(getActiveSessionMs()/600000),20);
  streakBonus=streakBonusEarned*5;
  focusScore=constrain(100-(warningCount*3)
    +recoveryBonus+streakBonus+durationBonus,0,120);
}

// ─── IMU (BMI270 via M5Unified) ────────────────────────────────────────────
//
// v7 used M5.Imu.getAccel() with the MPU6886.
// v8 uses the same M5Unified API — M5.Imu.getAccel() works transparently
// with the BMI270 on the S3 through M5Unified's unified IMU abstraction.
// The axis layout is the same (x, y, z in g), so the magnitude calculation
// is unchanged. Only the threshold values need hardware retuning.
//
float getRawMag() {
  float ax,ay,az; M5.Imu.getAccel(&ax,&ay,&az);
  return abs(sqrt(ax*ax+ay*ay+az*az)-1.0f);
}

void updateIMUWindow() {
  float ax,ay,az; M5.Imu.getAccel(&ax,&ay,&az);
  axWindow[imuWindowIdx]=ax;  // v10.0c: X axis tracked for orientation (gravity projection)
  magWindow[imuWindowIdx]=abs(sqrt(ax*ax+ay*ay+az*az)-1.0f);
  pitchWindow[imuWindowIdx]=atan2(ay,az)*180.0/PI;
  imuWindowIdx=(imuWindowIdx+1)%IMU_WINDOW_SIZE;
  if (imuWindowIdx==0) imuWindowFull=true;
}

float getSmoothedMag() {
  int count=imuWindowFull?IMU_WINDOW_SIZE:max(imuWindowIdx,1);
  float peak=0,sum=0;
  for (int i=0;i<count;i++) { if(magWindow[i]>peak) peak=magWindow[i]; sum+=magWindow[i]; }
  return (peak*0.6f)+((sum/count)*0.4f);
}

// ─── v10.0: Variance, Posture & Engagement ─────────────────────────────────

// getMagVariance — compute variance of magWindow samples.
// Low variance = steady/rhythmic motion (writing, typing).
// High variance = irregular motion (fidgeting, restless).
// TODO: observe Serial [IMU] var= during typical study activities on BMI270
//       then adjust calVarianceBaseline in calibration accordingly.
float getMagVariance() {
  int count = imuWindowFull ? IMU_WINDOW_SIZE : max(imuWindowIdx, 1);
  float sum = 0;
  for (int i = 0; i < count; i++) sum += magWindow[i];
  float mean = sum / count;
  float varSum = 0;
  for (int i = 0; i < count; i++) {
    float d = magWindow[i] - mean;
    varSum += d * d;
  }
  return varSum / count;
}

// getAvgAx — average X-axis accelerometer reading from axWindow.
// This is the gravity projection used for orientation detection.
// Hardware-measured reference values (BMI270 on M5StickS3 wrist mount):
//   flat on desk (Lintang)   : ax near 0    (-0.03 to 0.01)
//   forearm vertical (Tegak) : ax high      (0.75 to 0.86)
// Threshold of 0.4 sits in the clean gap between the two.
float getAvgAx() {
  int count = imuWindowFull ? IMU_WINDOW_SIZE : max(imuWindowIdx, 1);
  float sum = 0;
  for (int i = 0; i < count; i++) sum += axWindow[i];
  return sum / count;
}

// classifyMotion — STATIK (still) vs AKTIF (moving) from smoothed magnitude.
// MOTION_LOW threshold tuned to BMI270 resting noise floor (0.002-0.008).
#define MOTION_AKTIF_THRESHOLD 0.015f  // tuned: above resting noise floor

MotionState classifyMotion() {
  return (getSmoothedMag() >= MOTION_AKTIF_THRESHOLD) ? MOTION_AKTIF : MOTION_STATIK;
}

// classifyOrientation — LINTANG (flat) vs TEGAK (vertical) from X-axis gravity.
// ORIENT_TEGAK threshold sits in the gap between hardware-measured flat (~0)
// and vertical (~0.75+) positions.
#define ORIENT_TEGAK_THRESHOLD 0.4f   // tuned: ax above this = forearm vertical

OrientationState classifyOrientation() {
  return (getAvgAx() >= ORIENT_TEGAK_THRESHOLD) ? ORIENT_TEGAK : ORIENT_LINTANG;
}

// updateEngagement — Option A: engagement inferred from MOTION only.
// The wrist IMU cannot detect silent phone scrolling (thumb moves, wrist
// stays still), so we make no phone-use claim. Orientation does not affect
// engagement — only whether the student is actively moving.
//
//   AKTIF  -> ENGAGED   (writing, working — the reliable signal)
//   STATIK -> UNCERTAIN (reading/thinking is legitimate; not penalised)
//
// DISENGAGED is not asserted in Part 1; the warning ladder in Part 2 adds
// the TIME dimension (still for too long) that justifies it.
// Advisory only: drives display, not IMUState/warning/focusScore.
void updateEngagement() {
  EngagementState candidate =
    (currentMotion == MOTION_AKTIF) ? ENGAGED : UNCERTAIN;

  // Hold-timer: candidate must persist for ENGAGEMENT_HOLD_MS before committing.
  if (candidate != engagementCandidate) {
    engagementCandidate = candidate;
    engagementHoldStart = millis();
  }
  if (millis() - engagementHoldStart >= ENGAGEMENT_HOLD_MS) {
    currentEngagement = engagementCandidate;
  }
}

IMUState detectRawState() {
  return (getSmoothedMag()>calMagnitudeThresholdHigh)?STATE_ACTIVE:STATE_READING;
}

// ─── Recovery ──────────────────────────────────────────────────────────────
// v10.1: stage-aware recovery.
// Stage 3 recovery: full buzz + possible bonus (same as before).
// Stage 1/2 recovery: silent reset — no buzz, no bonus, no penalty was earned.
// Stage 0: nothing to recover from.
void handleRecovery() {
  if (currentWarnStage == WARN_S0) return;

  bool fromStage3 = (currentWarnStage == WARN_S3);
  bool quick = fromStage3 && (millis() - stageEnteredMs <= 10000);

  // Reset stage
  currentWarnStage = WARN_S0;
  stageEnteredMs   = 0;
  snoozeActive     = false;
  snoozeStartMs    = 0;

  // Compatibility shim
  inWarning        = false;
  warningStartTime = 0;

  lastMovementTime = millis();
  streakStartTime  = millis();

  if (fromStage3) {
    // Full recovery from Stage 3 — mirrors old STATE_WARNING/SLEEPING recovery
    currentState = STATE_READING;
    buzz_recovery();
    addTimelineEntry(TIMELINE_READING);
    if (quick) {
      recoveryBonus += 2;
      recalcFocusScore();
      showNotification("+2 Bonus Pemulihan!");
    }
    Serial.println("[WARN] Pulih dari Tahap 3");
  } else {
    // Silent recovery from Stage 1 or 2 — student re-engaged before alert
    currentState = STATE_READING;
    addTimelineEntry(TIMELINE_READING);
    Serial.printf("[WARN] Pulih senyap dari Tahap %d\n",
                  currentWarnStage == WARN_S1 ? 1 : 2);
  }
}

// ─── Rest ──────────────────────────────────────────────────────────────────
void startRest() {
  sessionPaused=true; pauseStartTime=millis();
  restTimerStart=millis(); restTimerExpired=false;
  restBreakCount++;
  addTimelineEntry(TIMELINE_RESTING);
  buzz_restStart(); showNotification("Sedang berehat...");
  Serial.printf("[REST] Rehat ke-%d\n",restBreakCount);
}

void endRest() {
  unsigned long thisPause=millis()-pauseStartTime;
  totalPausedMs+=thisPause; totalRestMs+=thisPause;
  sessionPaused=false; restTimerExpired=false;
  lastMovementTime=millis(); streakStartTime=millis();
  addTimelineEntry(TIMELINE_READING);
  buzz_restEnd(); showNotification("Selamat kembali!");
  Serial.printf("[REST] Tamat. Jumlah rehat: %s\n",formatTime(totalRestMs).c_str());
}

// ─── State Machine ─────────────────────────────────────────────────────────
void updateIMUState() {
  updateIMUWindow();
  float mag=getSmoothedMag();
  bool  moving=(mag>calMagnitudeThresholdHigh);

  // ── v10.0c: Two-axis posture classification with hysteresis ────────────
  // Motion and orientation each classified independently, each committing
  // only after POSTURE_HYSTERESIS consecutive agreeing samples to prevent
  // display flicker from momentary sensor noise.
  MotionState rawMotion = classifyMotion();
  if (rawMotion == motionCandidate) {
    if (++motionHoldCount >= POSTURE_HYSTERESIS) currentMotion = rawMotion;
  } else { motionCandidate = rawMotion; motionHoldCount = 1; }

  OrientationState rawOrient = classifyOrientation();
  if (rawOrient == orientCandidate) {
    if (++orientHoldCount >= POSTURE_HYSTERESIS) currentOrientation = rawOrient;
  } else { orientCandidate = rawOrient; orientHoldCount = 1; }

  // ── v10.0: Engagement inference (advisory — no effect on scoring yet) ──
  updateEngagement();

  // ── Enhanced Serial debug log every 2 seconds ──────────────────────────
  static unsigned long lastMagLog=0;
  if (millis()-lastMagLog>=2000) {
    lastMagLog=millis();
    Serial.printf("[IMU] mag=%.3f ax=%.3f postur=%s %s engage=%s state=%s stage=%d%s\n",
      mag, getAvgAx(),
      motionNames[currentMotion], orientationNames[currentOrientation],
      engagementNames[currentEngagement], stateNames[currentState],
      (int)currentWarnStage, snoozeActive ? " [SNUZ]" : "");
  }

  if (moving) {
    lastMovementTime=millis();
    // Any genuine motion drops the warning stage and recovers
    if (currentWarnStage > WARN_S0) {
      handleRecovery(); return;
    }
  }

  // ── v10.1: Four-stage warning ladder ─────────────────────────────────────
  // Escalation only advances when student is STATIK (not moving) and session
  // is active. Snooze suppresses escalation entirely while active.
  // Stage timings compile-time selected by DEMO_MODE flag (master switch later).
  if (sessionActive && !sessionPaused) {

    // Check snooze expiry
    if (snoozeActive && (millis() - snoozeStartMs >= (demoMode?DEMO_SNOOZE_MS:REAL_SNOOZE_MS))) {
      snoozeActive   = false;
      snoozeStartMs  = 0;
      stageEnteredMs = millis();  // restart stage timer after snooze
      Serial.println("[WARN] Snuz tamat, pantauan disambung semula");
    }

    if (!snoozeActive && currentMotion == MOTION_STATIK) {

      // v11: Membaca profile trusts a raised arm — Statik Tegak means the student
      // is holding their book/notes, not drifting. Reset the stage clock on every
      // Tegak sample so the ladder never advances while the arm is up.
      // Only Statik Lintang (flat, resting arm) escalates in Membaca mode.
      if (activeProfile == PROFILE_MEMBACA && currentOrientation == ORIENT_TEGAK) {
        stageEnteredMs = millis();  // keep resetting; S0 timer never fires
      } else {

      unsigned long timeInStage = millis() - stageEnteredMs;

      switch (currentWarnStage) {
        case WARN_S0:
          // Begin timing disengagement from first still moment
          if (stageEnteredMs == 0) stageEnteredMs = millis();
          if (millis() - stageEnteredMs >= activeStage1Ms) {
            currentWarnStage = WARN_S1;
            stageEnteredMs   = millis();
            inWarning        = true;  // shim
            streakStartTime  = millis();
            Serial.println("[WARN] Tahap 1 — Memerhati (senyap)");
          }
          break;

        case WARN_S1:
          if (timeInStage >= activeStage2Ms) {
            currentWarnStage = WARN_S2;
            stageEnteredMs   = millis();
            buzz_nudge();
            Serial.println("[WARN] Tahap 2 — Dorongan lembut");
          }
          break;

        case WARN_S2:
          if (timeInStage >= activeStage3Ms) {
            currentWarnStage  = WARN_S3;
            stageEnteredMs    = millis();
            warningStartTime  = millis();  // shim for overlay countdown

            // Score penalty — only at Stage 3
            warningCount++; distractionCount++; streakStartTime = millis();
            buzz_warning();
            addTimelineEntry(TIMELINE_WARNING);
            addDistractionEntry(0, -3);
            recalcFocusScore();

            // Update IMUState for overlay rendering compatibility
            currentState = STATE_WARNING;

            // v9.1: drift quiz warning buffer — only Stage 3 counts
            if (warnBufCount < MAX_WARN_BUF) {
              warnTimestamps[warnBufCount++] = millis();
            } else {
              for (int i=0;i<MAX_WARN_BUF-1;i++) warnTimestamps[i]=warnTimestamps[i+1];
              warnTimestamps[MAX_WARN_BUF-1] = millis();
            }

            // Check drift quiz trigger
            unsigned long windowMs  = quizWindowVals[settings.quizWindow];
            unsigned long now       = millis();
            int recentCount = 0;
            for (int i=0;i<warnBufCount;i++) {
              if (now - warnTimestamps[i] <= windowMs) recentCount++;
            }
            bool cooldownOk = (lastDriftQuizMs == 0 ||
                               (now - lastDriftQuizMs) >= (demoMode?30000UL:DRIFT_QUIZ_COOLDOWN_MS));
            // v10.5: mode-aware trigger rule.
            //   Demo  → fire on EVERY Stage 3 (cooldown-gated) for predictable demos
            //   Real  → require 2 Stage-3 events within the window (humane)
            bool triggerRuleMet = demoMode ? (recentCount >= 1) : (recentCount >= 2);
            if (triggerRuleMet && cooldownOk && companionReady && sessionActive) {
              Serial.printf("[WARN] Drift kuiz dicetuskan (%d amaran)\n", recentCount);
              // v10.4: engagement_state removed — drift always fires when still
              StaticJsonDocument<128> driftDoc;
              driftDoc["session_id"]     = serverSessionId;
              driftDoc["severity"]       = "quiz_trigger";
              driftDoc["ts"]             = (long)(millis() / 1000);
              driftDoc["quiz_window_ms"] = (long)windowMs;
              String driftBody; serializeJson(driftDoc, driftBody);
              String resp = postToServerWithResponse("/api/session/drift", driftBody);
              if (resp.length() > 0) handleDriftQuizResponse(resp);
              lastDriftQuizMs = now;
              warnBufCount    = 0;
            } else if (companionReady && serverSessionId >= 0) {
              StaticJsonDocument<128> driftDoc;
              driftDoc["session_id"]     = serverSessionId;
              driftDoc["severity"]       = "warning";
              driftDoc["ts"]             = (long)(millis() / 1000);
              driftDoc["quiz_window_ms"] = (long)windowMs;
              String driftBody; serializeJson(driftDoc, driftBody);
              postToServer("/api/session/drift", driftBody);
            }
            Serial.println("[WARN] Tahap 3 — Amaran penuh");
          }
          break;

        case WARN_S3:
          // Remain in Stage 3 — keep IMUState as WARNING or SLEEPING for overlay
          // Escalate to SLEEPING after activeSleepingMs (profile-scaled)
          if (currentState == STATE_WARNING &&
              (millis() - warningStartTime >= activeSleepingMs)) {
            currentState = STATE_SLEEPING;
            sleepingCount++;  // kept for session summary display
            lastSleepingBuzz = millis();
            buzz_sleeping();
            addTimelineEntry(TIMELINE_SLEEPING);
            Serial.println("[WARN] Tahap 3 — Tidak Aktif (tidur)");
          }
          // Repeating sleeping buzz
          if (currentState == STATE_SLEEPING &&
              (millis() - lastSleepingBuzz >= SLEEPING_BUZZ_INTERVAL)) {
            lastSleepingBuzz = millis();
            buzz_sleeping();
          }
          break;
      }

      }  // end else (Membaca+Tegak guard)

    } else if (!snoozeActive && currentMotion == MOTION_AKTIF) {
      // Moving — reset stage 0 timer so it only starts counting from stillness
      if (currentWarnStage == WARN_S0) stageEnteredMs = 0;
    }
  }

  // Streak bonus (unchanged)
  if (currentState==STATE_ACTIVE||currentState==STATE_READING) {
    if (millis()-streakStartTime>=DEEP_FOCUS_INTERVAL_MS) {
      streakBonusEarned++; streakStartTime=millis();
      recalcFocusScore(); buzz_streakBonus(); showNotification("+5 Fokus Berterusan!");
    }
  }
  recalcFocusScore();
}

// ─── NFC Helpers ───────────────────────────────────────────────────────────
bool uidMatch(uint8_t* a,uint8_t la,uint8_t* b,uint8_t lb) {
  if (la!=lb) return false;
  for (int i=0;i<la;i++) if(a[i]!=b[i]) return false;
  return true;
}

int findTag(uint8_t* uid,uint8_t uidLen) {
  for (int i=0;i<tagCount;i++)
    if (tagRegistry[i].registered&&uidMatch(uid,uidLen,tagRegistry[i].uid,tagRegistry[i].uidLen))
      return i;
  return -1;
}

// ─── NFC Polling (M5UnitUnifiedNFC) ────────────────────────────────────────
//
// v7 used nfc.readPassiveTargetID() from the Elechouse PN532 library (blocking).
// v8 uses UnitST25R3916's native NFC-A methods directly on unitNFC:
//   nfcaRequest(atqa)                          — REQA to wake idle tags
//   nfcaSelectWithAnticollision(ok, picc, lv)  — anticollision + select
//   nfcaHlt()                                  — halt the tag after reading
//
// m5::nfc::a::PICC holds:
//   picc.uid[]  — UID bytes (valid up to picc.size)
//   picc.size   — UID length (4, 7, or 10)
//   picc.type   — tag type (NTAG_213, NTAG_215, etc.)
//
// scanNFC() performs a single poll cycle within timeoutMs.
// Returns true and fills uid/uidLen if a tag is detected and selected.
//
bool scanNFC(uint8_t* uid, uint8_t* uidLen, uint32_t timeoutMs=200) {
  if (!nfcReady) return false;
  unsigned long start = millis();
  while (millis()-start < timeoutMs) {
    Units.update();
    uint16_t atqa = 0;
    if (unitNFC.nfcaRequest(atqa)) {
      m5::nfc::a::PICC picc{};
      bool completed = false;
      // Cascade levels 1-3 handle 4, 7, and 10-byte UIDs respectively
      for (uint8_t lv = 1; lv <= 3 && !completed; lv++) {
        if (!unitNFC.nfcaSelectWithAnticollision(completed, picc, lv)) break;
      }
      if (completed && picc.size > 0) {
        *uidLen = picc.size;
        memcpy(uid, picc.uid, picc.size);
        unitNFC.nfcaHlt();
        return true;
      }
    }
    delay(10);
  }
  return false;
}

// ─── NFC Session Handler ───────────────────────────────────────────────────
void handleNFCDuringSession() {
  uint8_t uid[7]; uint8_t uidLen=0;
  if (!scanNFC(uid,&uidLen,100)) return;
  buzz_nfcScanned();
  int idx=findTag(uid,uidLen);
  if (idx<0) return;
  if (tagRegistry[idx].tagType==TAG_TYPE_REST) {
    if (!sessionPaused) startRest(); else endRest();
    return;
  }
  if (sessionPaused) return;
  int newSubject=tagRegistry[idx].subjectIndex;
  if (newSubject!=currentSubject) {
    currentSubject=newSubject;
    showNotification("Subjek ditukar!");
  }
}

// ─── NFC Home Screen Handler ───────────────────────────────────────────────
void checkNFCOnHome() {
  if (sessionActive) return;
  if (currentScreen!=SCREEN_HOME&&currentScreen!=SCREEN_START_SESSION) return;
  uint8_t uid[7]; uint8_t uidLen=0;
  if (!scanNFC(uid,&uidLen,100)) return;
  buzz_nfcScanned();
  int idx=findTag(uid,uidLen);
  if (idx<0) return;
  if (tagRegistry[idx].tagType==TAG_TYPE_SUBJECT) {
    startSession(tagRegistry[idx].subjectIndex);
    currentScreen=SCREEN_HOME; homeMenuIdx=0;
  }
}

// ─── Register Tag ──────────────────────────────────────────────────────────
void registerTagFlow(int slotIndex) {
  bool isRest=(slotIndex==REST_SLOT);
  if (isRest&&hasRestTag) {
    M5.Display.fillScreen(BLACK); M5.Display.setCursor(0,30);
    M5.Display.println("Tag REHAT sudah\ndidaftar!\nPadam Semua untuk\nset semula.");
    buzz_error(); delay(2500); return;
  }
  if (tagCount>=MAX_TAGS) {
    M5.Display.fillScreen(BLACK); M5.Display.setCursor(0,30);
    M5.Display.println("Pendaftaran penuh!"); buzz_error(); delay(2000); return;
  }
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0,10); M5.Display.println("Daftar Tag");
  M5.Display.setCursor(0,30);
  M5.Display.printf("Jenis: %s\n\nSentuh tag NFC...(5s)",
    isRest?"REHAT":subjects[slotIndex]);

  uint8_t uid[7]; uint8_t uidLen=0;
  if (scanNFC(uid,&uidLen,5000)) {
    buzz_nfcScanned();
    int ex=findTag(uid,uidLen);
    if (ex>=0) {
      M5.Display.fillScreen(BLACK); M5.Display.setCursor(0,30);
      M5.Display.printf("Sudah didaftar!\n%s",
        tagRegistry[ex].tagType==TAG_TYPE_REST?"REHAT":subjects[tagRegistry[ex].subjectIndex]);
      buzz_error(); delay(2000); return;
    }
    memcpy(tagRegistry[tagCount].uid,uid,uidLen);
    tagRegistry[tagCount].uidLen=uidLen;
    tagRegistry[tagCount].subjectIndex=slotIndex;
    tagRegistry[tagCount].tagType=isRest?TAG_TYPE_REST:TAG_TYPE_SUBJECT;
    tagRegistry[tagCount].registered=true;
    tagCount++;
    rebuildTagStatus();
    saveTagsToNVS();
    buzz_tagRegistered();
    M5.Display.fillScreen(BLACK); M5.Display.setCursor(0,30);
    M5.Display.printf("Tag Berjaya Didaftar!\n%s\nJumlah tag: %d",
      isRest?"REHAT":subjects[slotIndex],tagCount);
    delay(2500);
  } else {
    M5.Display.fillScreen(BLACK); M5.Display.setCursor(0,40);
    M5.Display.println("Tag Tidak Dikesan."); buzz_error(); delay(1500);
  }
}

// ─── Calibration ───────────────────────────────────────────────────────────
void runCalibration() {
  buzz_calibration();

  // ── Phase 1: Magnitude threshold (existing) ──────────────────────────────
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0,10); M5.Display.println("Kalibrasi — Fasa 1");
  M5.Display.setCursor(0,30); M5.Display.println("Gerak semula jadi 20s.\nTulis, baca, isyarat.");

  unsigned long start=millis();
  float maxMag=0,sumMag=0; int samples=0;
  while (millis()-start<CALIBRATION_DURATION_MS) {
    float mag=getRawMag();
    if (mag>maxMag) maxMag=mag;
    sumMag+=mag; samples++;
    int remaining=(CALIBRATION_DURATION_MS-(millis()-start))/1000;
    M5.Display.fillRect(80,100,80,16,BLACK);
    M5.Display.setTextSize(2); M5.Display.setCursor(90,100);
    M5.Display.printf("%ds",remaining);
    delay(250); M5.update();
  }
  M5.Display.setTextSize(1);
  float avgMag=sumMag/samples;
  calMagnitudeThresholdHigh=constrain(avgMag*0.7f,0.15f,1.5f);
  calMagnitudeThresholdLow =constrain(avgMag*0.2f,0.05f,0.3f);
  calibrated=true;
  prefs.putFloat("cal_high",calMagnitudeThresholdHigh);
  prefs.putFloat("cal_low", calMagnitudeThresholdLow);
  prefs.putBool("calibrated",true);
  buzz_calibration();
  M5.Display.fillScreen(BLACK); M5.Display.setCursor(0,20);
  M5.Display.printf("Fasa 1 Selesai!\nTinggi: %.3f\nRendah: %.3f\n\nSiap untuk fasa 2...",
    calMagnitudeThresholdHigh,calMagnitudeThresholdLow);
  delay(2000);

  // ── Phase 2: Productive variance baseline (v10.0) ────────────────────────
  // Capture variance during natural writing/studying motion.
  // This baseline distinguishes rhythmic productive motion (writing) from
  // irregular restless motion — used by updateEngagement() in Part 1.
  // TODO: if variance baseline seems wrong after testing, run calibration again
  //       while doing typical writing/studying — not fidgeting or large gestures.
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0,10); M5.Display.println("Kalibrasi — Fasa 2");
  M5.Display.setCursor(0,30); M5.Display.println("Tulis atau belajar\nseperti biasa...\n20 saat.");

  start = millis();
  float varSum = 0; int varSamples = 0;
  while (millis() - start < CALIBRATION_DURATION_MS) {
    updateIMUWindow();  // keep window fresh
    varSum += getMagVariance();
    varSamples++;
    int remaining = (CALIBRATION_DURATION_MS - (millis() - start)) / 1000;
    M5.Display.fillRect(80,100,80,16,BLACK);
    M5.Display.setTextSize(2); M5.Display.setCursor(90,100);
    M5.Display.printf("%ds", remaining);
    delay(250); M5.update();
  }
  M5.Display.setTextSize(1);

  // Store the average variance during productive motion as the baseline.
  // Multiply by 1.5 to give a tolerance margin above typical writing variance.
  float avgVar = varSum / max(varSamples, 1);
  calVarianceBaseline = constrain(avgVar * 1.5f, 0.005f, 0.5f);
  prefs.putFloat("cal_var", calVarianceBaseline);

  Serial.printf("[KALIBRASI] Fasa 2 selesai. varBaseline=%.4f\n", calVarianceBaseline);

  buzz_calibration();
  M5.Display.fillScreen(BLACK); M5.Display.setCursor(0,20);
  M5.Display.printf("Kalibrasi Selesai!\n\nFasa 1:\nTinggi: %.3f\nRendah: %.3f\n\nFasa 2:\nVar: %.4f",
    calMagnitudeThresholdHigh, calMagnitudeThresholdLow, calVarianceBaseline);
  delay(3000);
}

// ─── Session ───────────────────────────────────────────────────────────────

// v11: applyProfileTimings — compute scaled stage timings for the active profile.
// Called once at session start after the profile is resolved from the server.
// Keeps demo/real orthogonal: profile scales whatever base set demoMode selects.
// Minimum floors prevent the ladder from firing instantly at 0.5x in demo mode.
void applyProfileTimings() {
  unsigned long s1 = demoMode ? DEMO_STAGE1_MS : REAL_STAGE1_MS;
  unsigned long s2 = demoMode ? DEMO_STAGE2_MS : REAL_STAGE2_MS;
  unsigned long s3 = demoMode ? DEMO_STAGE3_MS : REAL_STAGE3_MS;
  unsigned long sl = SLEEPING_TRIGGER_MS;   // runtime value from settings

  float scale = 1.0f;
  if      (activeProfile == PROFILE_MENULIS) scale = 0.5f;
  else if (activeProfile == PROFILE_MEMBACA) scale = 2.0f;
  // PROFILE_CAMPURAN: scale = 1.0 (unchanged)

  activeStage1Ms   = max((unsigned long)(s1 * scale), 2000UL);
  activeStage2Ms   = max((unsigned long)(s2 * scale), 2000UL);
  activeStage3Ms   = max((unsigned long)(s3 * scale), 2000UL);
  activeSleepingMs = max((unsigned long)(sl * scale), 5000UL);

  Serial.printf("[v11] Profil: %s — skala %.1fx "
                "(S1=%lus S2=%lus S3=%lus Tidur=%lus)\n",
                profileNames[activeProfile], scale,
                activeStage1Ms/1000, activeStage2Ms/1000,
                activeStage3Ms/1000, activeSleepingMs/1000);
}

void startSession(int subjectIndex) {
  sessionActive=true; sessionPaused=false;
  currentSubject=subjectIndex;
  sessionStartTime=millis(); streakStartTime=millis(); lastMovementTime=millis();
  totalPausedMs=0; totalRestMs=0; restBreakCount=0; restTimerExpired=false;
  focusScore=100; warningCount=0; sleepingCount=0;
  recoveryBonus=0; streakBonus=0; durationBonus=0;
  streakBonusEarned=0; distractionCount=0;
  currentState=STATE_READING; lastState=STATE_READING;
  inWarning=false; warningStartTime=0; lastSleepingBuzz=0;
  timelineCount=0; distractionLogCount=0;
  lastSessionValid=false;
  addTimelineEntry(TIMELINE_READING);

  // v10.1: Reset four-stage warning state
  currentWarnStage = WARN_S0;
  stageEnteredMs   = 0;
  snoozeActive     = false;
  snoozeStartMs    = 0;

  // v9.1: Reset drift quiz window for this session
  warnBufCount      = 0;
  lastDriftQuizMs   = 0;
  serverSessionId   = -1;
  lastFocusReportMs = 0;  // v9.2: reset focus report timer

  // v11: Reset profile to Campuran default — will be overwritten by server response below
  activeProfile = PROFILE_CAMPURAN;

  // v9: POST session start to companion server, capture server-side session ID.
  // v11: Also send the selected topic name; receive and apply profile from response.
  if (companionReady) {
    StaticJsonDocument<256> doc;
    doc["device_id"]  = deviceId;
    doc["subject_id"] = currentSubject + 1;  // DB is 1-indexed
    doc["start_ts"]   = (long)(millis() / 1000);
    doc["topic"]      = activeTopicName;      // v11: "" = Ulangkaji Bebas
    String body; serializeJson(doc, body);
    String resp = postToServerWithResponse("/api/session/start", body);
    if (resp.length() > 0) {
      StaticJsonDocument<96> rdoc;
      if (!deserializeJson(rdoc, resp)) {
        serverSessionId = rdoc["session_id"] | -1;
        // v11: parse and apply profile
        const char* profStr = rdoc["profile"] | "Campuran";
        if      (strcmp(profStr, "Menulis") == 0) activeProfile = PROFILE_MENULIS;
        else if (strcmp(profStr, "Membaca") == 0) activeProfile = PROFILE_MEMBACA;
        else                                       activeProfile = PROFILE_CAMPURAN;
        Serial.printf("[v11] Server session ID: %d | profil: %s\n",
                      serverSessionId, profStr);
      }
    }
  }

  // v11: Apply scaled timings for the resolved profile.
  // Also runs when not in Companion mode — applies Campuran (1.0x) timings so
  // the active* variables are always valid throughout the session.
  applyProfileTimings();

  buzz_sessionStart();
  Serial.printf("[SESI] Mula: %s | topik: '%s'\n",
                subjects[subjectIndex],
                activeTopicName[0] ? activeTopicName : "Ulangkaji Bebas");
}

void endSession() {
  if (!sessionActive) return;
  if (sessionPaused) endRest();
  recalcFocusScore();
  unsigned long elapsed=getActiveSessionMs();
  buzz_sessionEnd();

  lastSessionValid    = true;
  lastFocusScore      = focusScore;
  lastWarningCount    = warningCount;
  lastSleepingCount   = sleepingCount;
  lastRecoveryBonus   = recoveryBonus;
  lastStreakBonus      = streakBonus;
  lastDurationBonus   = durationBonus;
  lastRestBreaks      = restBreakCount;
  lastDurationMs      = elapsed;
  lastRestTimeMs      = totalRestMs;
  lastSubject         = currentSubject;
  lastDistractionCount= distractionCount;

  SessionRecord rec;
  rec.timestamp=(uint32_t)(millis()/1000);
  rec.subjectIndex=currentSubject;
  rec.durationMs=(uint32_t)elapsed;
  rec.restTimeMs=(uint32_t)totalRestMs;
  rec.restBreaks=(uint8_t)restBreakCount;
  rec.focusScore=(int16_t)focusScore;
  rec.warningCount=(uint8_t)warningCount;
  rec.sleepingCount=(uint8_t)sleepingCount;
  rec.recoveryBonus=(int8_t)recoveryBonus;
  rec.streakBonus=(int8_t)streakBonus;
  rec.durationBonus=(int8_t)durationBonus;
  appendSessionRecord(rec);

  Serial.println("[SESI] ===== RINGKASAN =====");
  Serial.printf("Subjek: %s | Tempoh: %s | Markah: %d\n",
    subjects[currentSubject],formatTime(elapsed).c_str(),focusScore);
  Serial.printf("Rehat: %dx %s\n",restBreakCount,formatTime(totalRestMs).c_str());

  // v9: Upload session summary to Flask companion app (Companion mode only)
  // Subject ID uses 1-based index matching the seeded DB order:
  //   1=Bhs Melayu, 2=Bhs Inggeris, 3=Matematik, 4=Sains,
  //   5=Sejarah, 6=Geografi, 7=Pend Islam, 8=Pend Moral
  if (companionReady) {
    StaticJsonDocument<256> upload;
    upload["device_id"]   = deviceId;
    upload["session_id"]  = serverSessionId;    // v9.1: update existing DB row
    upload["subject_id"]  = currentSubject + 1;
    upload["start_ts"]    = sessionStartTime / 1000;
    upload["end_ts"]      = (sessionStartTime + elapsed) / 1000;
    upload["active_min"]  = (int)(elapsed / 60000);
    upload["idle_min"]    = (int)(totalPausedMs / 60000);
    upload["focus_score"] = focusScore;
    String body; serializeJson(upload, body);
    postToServer("/api/session/end", body);
  }

  M5.Display.fillScreen(BLACK);
  M5.Display.fillRect(0,0,240,HDR_H,COL_ACCENT);
  M5.Display.setTextColor(COL_TEXT,COL_ACCENT); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,7); M5.Display.print("Sesi Tamat");
  drawBatteryIcon(218,7);
  M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(2);
  M5.Display.setCursor(4,26); M5.Display.printf("%s\n",subjects[currentSubject]);
  M5.Display.setTextColor(COL_TEXT,BLACK); M5.Display.setTextSize(2);
  M5.Display.setCursor(4,44); M5.Display.printf("%s\n",formatTime(elapsed).c_str());
  M5.Display.setTextColor(COL_GOOD,BLACK);
  M5.Display.setTextSize(3); M5.Display.setCursor(4,62);
  M5.Display.printf("Fokus: %d",focusScore);
  M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,90);
  M5.Display.printf("A:-%d P:+%d B:+%d\n",warningCount*3,recoveryBonus,durationBonus);
  M5.Display.printf("Rehat: %dx %s\n",restBreakCount,formatTime(totalRestMs).c_str());
  delay(4000);

  sessionActive=false; currentSubject=-1;
  currentScreen=SCREEN_HOME; homeMenuIdx=0;
  notificationActive=false;
}

// ─── Display Helpers ───────────────────────────────────────────────────────
// All M5.Lcd.* calls replaced with M5.Display.* (M5Unified API)
void clearDisplay() { M5.Display.fillScreen(BLACK); M5.Display.setTextColor(COL_TEXT,BLACK); }

// v10.7: Battery icon — 18×8px at (x,y). Green >50%, amber >20%, red ≤20%.
void drawBatteryIcon(int x, int y) {
  int lvl = getBatteryLevel();
  uint16_t fc = (lvl>50) ? COL_GOOD : (lvl>20 ? COL_WARN : COL_DANGER);
  M5.Display.drawRect(x, y, 16, 8, COL_DIM);        // body outline
  M5.Display.fillRect(x+16, y+2, 2, 4, COL_DIM);    // terminal nub
  int fw = map(constrain(lvl,0,100), 0, 100, 0, 14);
  if (fw>0) M5.Display.fillRect(x+1, y+1, fw, 6, fc); // charge fill
}

// drawHeader — universal: COL_ACCENT bar, WHITE title, battery icon always on right
void drawHeader(const char* t, uint16_t c=COL_ACCENT) {
  M5.Display.fillRect(0,0,240,HDR_H,c);
  M5.Display.setTextColor(COL_TEXT,c); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,7); M5.Display.print(t);
  drawBatteryIcon(218,7);
  M5.Display.setTextColor(COL_TEXT,BLACK);
}

// drawFooter — COL_ACCENT bar to match header, creating a clean frame
void drawFooter(const char* l, const char* r) {
  M5.Display.fillRect(0,FTR_Y,240,FTR_H,COL_ACCENT);
  M5.Display.setTextColor(COL_TEXT,COL_ACCENT); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,FTR_Y+7); M5.Display.print(l);
  M5.Display.setCursor(240-strlen(r)*6-4,FTR_Y+7); M5.Display.print(r);
  M5.Display.setTextColor(COL_TEXT,BLACK);
}

// v10.2: Scroll helpers — used by all list screens ─────────────────────────
// Returns the first visible item for a given selected index and total count.
// Keeps the cursor roughly centred in the VISIBLE_ITEMS viewport.
int scrollStart(int selected, int total) {
  int s = selected - VISIBLE_ITEMS/2;
  if (s < 0) s = 0;
  if (s + VISIBLE_ITEMS > total) s = max(0, total - VISIBLE_ITEMS);
  return s;
}
// Draws a proportional 4px scrollbar on the right edge of the content area.
void drawScrollBar(int selected, int total) {
  if (total <= VISIBLE_ITEMS) return;
  int trackH = FTR_Y - CONTENT_Y;                         // 90px
  int barH   = max(8, trackH * VISIBLE_ITEMS / total);
  int scrollOff = scrollStart(selected, total);
  int barY   = CONTENT_Y + trackH * scrollOff / total;
  barY = min(barY, FTR_Y - barH);
  M5.Display.fillRect(236, CONTENT_Y, 4, trackH, 0x2104);  // dark track
  M5.Display.fillRect(236, barY,      4, barH,   COL_DIM);  // thumb
}

void drawProgressBar(int x,int y,int w,int h,float pct,uint16_t color) {
  M5.Display.drawRect(x,y,w,h,DARKGREY);
  int f=constrain((int)(pct*(w-2)),0,w-2);
  if (f>0) M5.Display.fillRect(x+1,y+1,f,h-2,color);
  if (f<w-2) M5.Display.fillRect(x+1+f,y+1,w-2-f,h-2,BLACK);
}

// ─── Screen Renderers ──────────────────────────────────────────────────────
void renderRestScreen() {
  clearDisplay();
  char hdr[28]; snprintf(hdr,28,"Berehat | %s",subjects[currentSubject]);
  drawHeader(hdr);

  unsigned long elapsed=millis()-restTimerStart;
  unsigned long remaining=(elapsed>=REST_DURATION_MS)?0:REST_DURATION_MS-elapsed;
  float pct=min((float)elapsed/REST_DURATION_MS,1.0f);

  if (!restTimerExpired) {
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,CONTENT_Y); M5.Display.println("Masa rehat:");
    M5.Display.setTextColor(COL_TEXT,BLACK);
    M5.Display.setTextSize(3); M5.Display.setCursor(20,44);
    M5.Display.printf("%s\n",formatTime(remaining).c_str());
    drawProgressBar(4,70,228,6,pct,COL_ACCENT);
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,82);
    M5.Display.printf("Rehat ke-%d\n",restBreakCount);
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.setCursor(4,FTR_Y-10);
    M5.Display.print("Sentuh tag REHAT untuk sambung");
  } else {
    uint16_t fc=flashState?COL_WARN:COL_DIM;
    M5.Display.fillRect(0,CONTENT_Y,240,32,fc);
    M5.Display.setTextColor(BLACK,fc); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,CONTENT_Y+8); M5.Display.println("Rehat Tamat!");
    M5.Display.setTextColor(COL_TEXT,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,62);
    M5.Display.println("Sentuh tag REHAT");
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.setCursor(4,82);
    M5.Display.printf("Rehat ke-%d selesai\n",restBreakCount);
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
}

void renderSessionOverlay() {
  if (currentState==STATE_WARNING) {
    M5.Display.fillScreen(COL_WARN);
    M5.Display.setTextColor(BLACK,COL_WARN);
    M5.Display.setTextSize(2); M5.Display.setCursor(10,10); M5.Display.println("! AMARAN !");
    M5.Display.setTextSize(2); M5.Display.setCursor(4,44);
    int rem=max(0,(int)((activeSleepingMs-(millis()-warningStartTime))/1000));
    M5.Display.printf("Gerak sekarang!\nTamat dalam: %ds",rem);
    M5.Display.setTextSize(1); M5.Display.setCursor(4,108); M5.Display.print("[A] Saya dah bangun!");
    return;
  }
  if (currentState==STATE_SLEEPING) {
    uint16_t fc=flashState?COL_DANGER:MAROON;
    M5.Display.fillScreen(BLACK); M5.Display.fillRect(0,0,240,32,fc);
    M5.Display.setTextColor(COL_TEXT,fc);
    M5.Display.setTextSize(2); M5.Display.setCursor(10,8); M5.Display.println("!! TIDAK AKTIF !!");
    M5.Display.setTextColor(COL_TEXT,BLACK);
    M5.Display.setTextSize(2); M5.Display.setCursor(4,40);
    M5.Display.printf("Bangun!\nGerak atau tekan [A].");
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.setCursor(4,108); M5.Display.print("[A] Bangun!");
    M5.Display.setTextColor(COL_TEXT,BLACK);
  }
}

void renderHome() {
  if (sessionActive&&sessionPaused) { renderRestScreen(); return; }
  if (sessionActive && currentWarnStage == WARN_S3) {
    renderSessionOverlay(); return;
  }
  clearDisplay();

  // ─── Header ───────────────────────────────────────────────────────────────
  uint16_t hc = sessionActive ? lcdStateColors[currentState] : COL_ACCENT;
  M5.Display.fillRect(0,0,240,HDR_H,hc);
  M5.Display.setTextColor(COL_TEXT,hc); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,7);
  if (sessionActive) {
    M5.Display.printf("StudyAid | %s",stateNames[currentState]);
    if (demoMode) {
      M5.Display.setTextColor(COL_WARN,hc); M5.Display.setCursor(130,7);
      M5.Display.print("[DEMO]");
      M5.Display.setTextColor(COL_TEXT,hc);
    }
  } else {
    M5.Display.print(studentName);
  }
  drawBatteryIcon(218,7);
  M5.Display.setTextColor(COL_TEXT,BLACK);

  if (sessionActive) {
    // ─── Active session: 5-row layout ────────────────────────────────────
    unsigned long elapsed=getActiveSessionMs();

    // Row 1 (y=26): Subject name — dimmed (secondary info)
    // v11: Profile label shown at right of same row when not Campuran (default)
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,26); M5.Display.print(subjects[currentSubject]);
    if (activeProfile != PROFILE_CAMPURAN) {
      M5.Display.setTextSize(1); M5.Display.setTextColor(COL_DIM,BLACK);
      const char* pn = profileNames[activeProfile];
      M5.Display.setCursor(240 - (int)strlen(pn)*6 - 4, 28);
      M5.Display.print(pn);
      M5.Display.setTextSize(2);
    }

    // Row 2 (y=44): Elapsed time
    M5.Display.setTextColor(COL_TEXT,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,44); M5.Display.print(formatTime(elapsed).c_str());

    // Row 3 (y=62): Focus score — hero number in COL_GOOD
    M5.Display.setTextColor(COL_GOOD,BLACK);
    M5.Display.setTextSize(3); M5.Display.setCursor(4,62);
    M5.Display.printf("Fokus: %d",focusScore);
    // Thin progress bar under score (proportional to score value)
    drawProgressBar(4,86,140,3,focusScore/100.0f,COL_GOOD);

    // Row 4 (y=91): Posture — small motion icon + text
    // Motion icon: filled circle=AKTIF (green dot), filled dash=STATIK (dim dash)
    if (currentMotion==MOTION_AKTIF)
      M5.Display.fillCircle(8,98,4,COL_GOOD);
    else
      M5.Display.fillRect(4,97,8,3,COL_DIM);
    M5.Display.setTextColor(COL_TEXT,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(18,91);
    M5.Display.printf("%s %s",motionNames[currentMotion],orientationNames[currentOrientation]);
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.printf(" [%s]",engagementNames[currentEngagement]);
    M5.Display.setTextColor(COL_TEXT,BLACK);

    // Row 5 (y=106): Stage indicator with small drawn glyph
    M5.Display.setTextSize(1);
    if (snoozeActive) {
      unsigned long snoozeLeft=(demoMode?DEMO_SNOOZE_MS:REAL_SNOOZE_MS)-(millis()-snoozeStartMs);
      M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setCursor(4,106);
      M5.Display.printf("[Rehat sebentar... %lus]",snoozeLeft/1000);
    } else if (currentWarnStage==WARN_S1) {
      M5.Display.fillCircle(6,109,4,COL_WARN);         // watching dot
      M5.Display.setTextColor(COL_WARN,BLACK); M5.Display.setCursor(14,106);
      M5.Display.print("Memerhati...");
    } else if (currentWarnStage==WARN_S2) {
      M5.Display.fillTriangle(4,112,8,106,12,112,COL_WARN); // warning triangle
      M5.Display.setTextColor(COL_WARN,BLACK); M5.Display.setCursor(16,106);
      M5.Display.print("Fokus semula  [A] Snuz");
    } else {
      M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setCursor(4,106);
      if (companionReady&&serverSessionId>=0)
        M5.Display.printf("Sesi #%d",serverSessionId);
      else
        M5.Display.print("WiFi: " WIFI_IP);
    }
    M5.Display.setTextColor(COL_TEXT,BLACK);
    drawFooter("[A] -","[B] Tamat Sesi");

  } else {
    // ─── Idle home: 5 menu items with unified COL_INFO selection band ────
    const char* opts[]={"Mula Sesi","Daftar Tag","Kalibrasi","Tetapan","Mod Kuiz"};
    for (int i=0;i<5;i++) {
      int rowY = CONTENT_Y + i*ROW_H;
      bool sel = (i==homeMenuIdx);
      if (sel) M5.Display.fillRect(0,rowY-1,232,ROW_H,COL_INFO);
      M5.Display.setTextColor(sel?BLACK:COL_TEXT, sel?COL_INFO:BLACK);
      M5.Display.setCursor(4,rowY+1); M5.Display.setTextSize(2);
      M5.Display.print(opts[i]);
    }
    M5.Display.setTextColor(COL_TEXT,BLACK);
    drawFooter("[A] Kitar","[B] Pilih");
  }
}


void renderStartSession() {
  int total = NUM_SUBJECTS+1;
  int vis0  = scrollStart(subjectSelectIdx, total);
  clearDisplay(); drawHeader("Mula Sesi");
  drawScrollBar(subjectSelectIdx, total);
  for (int i=vis0; i<vis0+VISIBLE_ITEMS && i<total; i++) {
    int rowY = CONTENT_Y + (i-vis0)*ROW_H;
    bool sel = (i==subjectSelectIdx);
    if (sel) M5.Display.fillRect(0,rowY-1,232,ROW_H,COL_INFO);
    M5.Display.setCursor(4,rowY); M5.Display.setTextSize(2);
    if (i<NUM_SUBJECTS) {
      M5.Display.setTextColor(sel?BLACK:COL_TEXT, sel?COL_INFO:BLACK);
      M5.Display.print(subjects[i]);
    } else {
      M5.Display.setTextColor(sel?BLACK:COL_DIM, sel?COL_INFO:BLACK);
      M5.Display.print("< Kembali");
    }
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("[A] Kitar","[B] Pilih");
}

void renderRegisterTag() {
  int total = NUM_SUBJECTS+2;
  int vis0  = scrollStart(registerSelectIdx, total);
  char hdrBuf[24]; snprintf(hdrBuf,24,"Daftar Tag  %d/%d",tagCount,MAX_TAGS);
  clearDisplay(); drawHeader(hdrBuf);
  drawScrollBar(registerSelectIdx, total);
  for (int i=vis0; i<vis0+VISIBLE_ITEMS && i<total; i++) {
    int rowY = CONTENT_Y + (i-vis0)*ROW_H;
    bool sel = (i==registerSelectIdx);
    if (sel) M5.Display.fillRect(0,rowY-1,232,ROW_H,COL_INFO);
    M5.Display.setCursor(4,rowY); M5.Display.setTextSize(2);
    if (i<NUM_SUBJECTS) {
      bool hasTag=subjectHasTag[i];
      M5.Display.setTextColor(sel?BLACK:COL_TEXT, sel?COL_INFO:BLACK);
      M5.Display.printf("%s%s",subjects[i],hasTag?" [OK]":"");
    } else if (i==NUM_SUBJECTS) {
      M5.Display.setTextColor(sel?BLACK:COL_INFO, sel?COL_INFO:BLACK);
      M5.Display.printf("REHAT%s",hasRestTag?" [OK]":"");
    } else {
      M5.Display.setTextColor(sel?BLACK:COL_DIM, sel?COL_INFO:BLACK);
      M5.Display.print("< Kembali");
    }
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("[A] Kitar","[B] Pilih");
}

void renderCalibrate() {
  clearDisplay(); drawHeader("Kalibrasi IMU");
  M5.Display.setTextSize(2); M5.Display.setCursor(4,CONTENT_Y);
  if (calibrated) {
    M5.Display.setTextColor(COL_GOOD,BLACK); M5.Display.println("Telah kalibrasi");
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.printf("Tinggi: %.3f  Rendah: %.3f\n",
      calMagnitudeThresholdHigh,calMagnitudeThresholdLow);
  } else {
    M5.Display.setTextColor(COL_WARN,BLACK); M5.Display.println("Belum dikalibrasi");
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.printf("Kepekaan: %s\n",settingOptions[5][settings.imuSensitivity]);
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  M5.Display.setTextSize(2); M5.Display.setCursor(4,72);
  M5.Display.println("[B] Kalibrasi 20s");
  M5.Display.setTextSize(2); M5.Display.setCursor(4,92);
  M5.Display.println("[A] Kembali");
  drawFooter("[A] Kembali","[B] Kalibrasi");
}

void renderSettings() {
  const char* sLabels[] = {
    "Amaran","Tidur","Getar","Gangguan",
    "Selang Fokus","IMU","Rehat","Tingkap Kuiz"
  };
  int total = 13;
  int vis0  = scrollStart(settingsParamIdx, total);
  clearDisplay(); drawHeader("Tetapan");
  drawScrollBar(settingsParamIdx, total);

  for (int i=vis0; i<vis0+VISIBLE_ITEMS && i<total; i++) {
    int rowY = CONTENT_Y + (i-vis0)*ROW_H;
    bool sel = (i==settingsParamIdx);
    if (sel) M5.Display.fillRect(0,rowY-1,232,ROW_H,COL_INFO);
    M5.Display.setCursor(4,rowY); M5.Display.setTextSize(2);

    if (i<8) {
      uint8_t val=0;
      switch(i){
        case 0: val=settings.warningTrigger;    break;
        case 1: val=settings.sleepTrigger;      break;
        case 2: val=settings.sleepBuzzInterval; break;
        case 3: val=settings.distrThreshold;    break;
        case 4: val=settings.deepFocusInterval; break;
        case 5: val=settings.imuSensitivity;    break;
        case 6: val=settings.restDuration;      break;
        case 7: val=settings.quizWindow;        break;
      }
      M5.Display.setTextColor(sel?BLACK:COL_TEXT, sel?COL_INFO:BLACK);
      M5.Display.printf("%-12s[%d]", sLabels[i], val+1);
    } else if (i==8) {
      M5.Display.setTextColor(sel?BLACK:COL_WARN, sel?COL_INFO:BLACK);
      M5.Display.print("Padam Sejarah");
    } else if (i==9) {
      M5.Display.setTextColor(sel?BLACK:COL_DANGER, sel?COL_INFO:BLACK);
      M5.Display.print("Padam Semua");
    } else if (i==10) {
      uint16_t mc = companionMode ? COL_GOOD : COL_DIM;
      M5.Display.setTextColor(sel?BLACK:mc, sel?COL_INFO:BLACK);
      M5.Display.printf("Mod Rakan  [%s]", companionMode?"ON":"OFF");
    } else if (i==11) {
      uint16_t dc = demoMode ? COL_WARN : COL_DIM;
      M5.Display.setTextColor(sel?BLACK:dc, sel?COL_INFO:BLACK);
      M5.Display.printf("Mod Demo   [%s]", demoMode?"ON":"OFF");
    } else if (i==12) {
      M5.Display.setTextColor(sel?BLACK:COL_DIM, sel?COL_INFO:BLACK);
      M5.Display.print("< Kembali");
    }
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);

  // Custom footer with hint on left — overrides standard footer
  M5.Display.fillRect(0,FTR_Y,240,FTR_H,COL_ACCENT);
  M5.Display.setTextColor(COL_INFO,COL_ACCENT); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,FTR_Y+7);
  if (settingsParamIdx<8) {
    uint8_t val=0;
    switch(settingsParamIdx){
      case 0: val=settings.warningTrigger;    break;
      case 1: val=settings.sleepTrigger;      break;
      case 2: val=settings.sleepBuzzInterval; break;
      case 3: val=settings.distrThreshold;    break;
      case 4: val=settings.deepFocusInterval; break;
      case 5: val=settings.imuSensitivity;    break;
      case 6: val=settings.restDuration;      break;
      case 7: val=settings.quizWindow;        break;
    }
    M5.Display.print(settingOptions[settingsParamIdx][val]);
  } else if (settingsParamIdx==10) {
    M5.Display.print("[B] togol mod rakan");
  } else if (settingsParamIdx==11) {
    M5.Display.print("[B] togol mod demo");
  }
  M5.Display.setTextColor(COL_TEXT,COL_ACCENT);
  M5.Display.setCursor(192,FTR_Y+7); M5.Display.print("[B]Pilih");
  M5.Display.setTextColor(COL_TEXT,BLACK);
}


void renderConfirmReset() {
  clearDisplay();
  uint16_t hc=(confirmResetType==1)?COL_DANGER:COL_WARN;
  drawHeader(confirmResetType==1?"Padam Semua?":"Padam Sejarah?",hc);
  M5.Display.setTextSize(2); M5.Display.setCursor(4,CONTENT_Y);
  if (confirmResetType==1) {
    M5.Display.setTextColor(COL_DANGER,BLACK);
    M5.Display.println("Padam semua data");
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.println("Sejarah, kalibrasi,\ntag & tetapan.\nPeranti akan restart.");
  } else {
    M5.Display.setTextColor(COL_WARN,BLACK);
    M5.Display.println("Padam sejarah sesi");
    M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
    M5.Display.println("Kalibrasi dan tag\ndikekalkan.");
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("[A] Batal","[B] Sahkan");
}

void renderCurrentScreen() {
  flashState=!flashState;
  switch(currentScreen) {
    case SCREEN_HOME:          renderHome();          break;
    case SCREEN_START_SESSION: renderStartSession();  break;
    case SCREEN_SESSION_TOPIC: renderSessionTopic();  break;  // v11
    case SCREEN_REGISTER_TAG:  renderRegisterTag();   break;
    case SCREEN_CALIBRATE:     renderCalibrate();     break;
    case SCREEN_SETTINGS:      renderSettings();      break;
    case SCREEN_CONFIRM_RESET: renderConfirmReset();  break;
    // v9.1: Quiz screens
    case SCREEN_QUIZ_SUBJECT:  renderQuizSubject();   break;
    case SCREEN_QUIZ_TOPIC:    renderQuizTopic();     break;  // v9.3
    case SCREEN_QUIZ_QUESTION: renderQuizQuestion();  break;
    case SCREEN_QUIZ_RESULT:   renderQuizResult();    break;
    case SCREEN_QUIZ_SUMMARY:  renderQuizSummary();   break;
  }
}

// ─── Button Handlers ───────────────────────────────────────────────────────
// M5Unified button API: M5.BtnA / M5.BtnB / M5.BtnPWR
// BtnA = front large button, BtnB = side small button
// wasPressed() behaviour is identical to v7
void handleBtnA() {
  // v10.4: drift quiz owns BtnA — cycle options, silence repeating alert
  if (driftQuizActive && currentScreen == SCREEN_QUIZ_QUESTION) {
    quizState.selectedOpt = (quizState.selectedOpt + 1) % 4;
    lastDriftAlertMs = millis();  // reset alert timer on any interaction
    driftQuizStartMs = millis();  // v10.5: reset timeout — active student stays alive
    lastDisplayRefresh = 0;
    return;
  }
  // v10.1: Stage-aware BtnA during active session (not paused, not in quiz)
  if (sessionActive && !sessionPaused) {
    if (currentWarnStage == WARN_S3) {
      // Stage 3: full recovery (same as before)
      handleRecovery(); lastDisplayRefresh=0; return;
    } else if (currentWarnStage == WARN_S1 || currentWarnStage == WARN_S2) {
      // Stage 1 or 2: snooze — student says "I'm thinking, hold off"
      snoozeActive     = true;
      snoozeStartMs    = millis();
      currentWarnStage = WARN_S0;
      stageEnteredMs   = 0;
      inWarning        = false;
      showNotification("Rehat sebentar...");
      Serial.printf("[WARN] Snuz diaktif (%lus)\n", (demoMode?DEMO_SNOOZE_MS:REAL_SNOOZE_MS) / 1000);
      lastDisplayRefresh=0; return;
    }
    // Stage 0: BtnA falls through to normal home menu handling below
  }
  switch(currentScreen) {
    case SCREEN_HOME:
      if (!sessionActive) homeMenuIdx=(homeMenuIdx+1)%5;
      break;
    case SCREEN_START_SESSION:
      subjectSelectIdx=(subjectSelectIdx+1)%(NUM_SUBJECTS+1); break;
    case SCREEN_SESSION_TOPIC:  // v11: cycle through session topics
      if (sessionTopicCount > 0)
        sessionTopicIdx=(sessionTopicIdx+1)%sessionTopicCount;
      break;
    case SCREEN_REGISTER_TAG:
      registerSelectIdx=(registerSelectIdx+1)%(NUM_SUBJECTS+2); break;
    case SCREEN_CALIBRATE:
      currentScreen=SCREEN_HOME; break;
    case SCREEN_SETTINGS:
      settingsParamIdx=(settingsParamIdx+1)%13; break;
    case SCREEN_CONFIRM_RESET:
      currentScreen=SCREEN_SETTINGS; break;
    // v9.1: Quiz BtnA handlers
    case SCREEN_QUIZ_SUBJECT:
      quizSubjectIdx=(quizSubjectIdx+1)%NUM_SUBJECTS; break;
    case SCREEN_QUIZ_TOPIC:    // v9.3: cycle through available topics
      if (quizTopicCount > 0)
        quizTopicIdx=(quizTopicIdx+1)%quizTopicCount;
      break;
    case SCREEN_QUIZ_QUESTION:
      quizState.selectedOpt=(quizState.selectedOpt+1)%4; break;
    case SCREEN_QUIZ_RESULT:
      break;  // no BtnA action on result screen
    case SCREEN_QUIZ_SUMMARY:
      // BtnA = restart — close current session, go back to topic picker
      if (companionReady && !sessionActive && serverSessionId >= 0) {
        StaticJsonDocument<256> qEndDoc;
        qEndDoc["device_id"]  = deviceId;
        qEndDoc["session_id"] = serverSessionId;
        qEndDoc["subject_id"] = quizSubjectIdx + 1;
        qEndDoc["start_ts"]   = (long)(millis() / 1000);
        qEndDoc["end_ts"]     = (long)(millis() / 1000);
        qEndDoc["active_min"] = 0; qEndDoc["idle_min"] = 0;
        qEndDoc["focus_score"]= 0;
        String qEndBody; serializeJson(qEndDoc, qEndBody);
        postToServer("/api/session/end", qEndBody);
        serverSessionId = -1;
      }
      // Return to topic picker for same subject
      quizTopicIdx  = 0;
      currentScreen = SCREEN_QUIZ_TOPIC;
      break;
  }
  lastDisplayRefresh=0;
}

void handleBtnB() {
  // v10.4: drift quiz owns BtnB — submit answer, then show result (non-blocking)
  if (driftQuizActive && currentScreen == SCREEN_QUIZ_QUESTION) {
    int qIdx = quizState.currentIdx;
    int chosen = quizState.selectedOpt;
    int correct = quizState.questions[qIdx].correctIndex;
    quizState.resultCorrect = (chosen == correct);

    // POST answer to server (blocking — can take up to COMP_TIMEOUT_MS)
    if (companionReady && serverSessionId >= 0) {
      StaticJsonDocument<128> aDoc;
      aDoc["session_id"]  = serverSessionId;
      aDoc["question_id"] = quizState.questions[qIdx].id;
      aDoc["chosen_index"]= chosen;
      String aBody; serializeJson(aDoc, aBody);
      postToServer("/api/session/answer", aBody);
    }

    // v10.5: non-blocking result — switch to result screen, loop() auto-returns
    // after 2s. Stamp resultShowTime AFTER the blocking POST and beep so the
    // 2-second display window starts when the result actually appears on screen
    // (otherwise slow network eats into the window and the result flashes by).
    buzz_confirm();
    quizState.resultShowTime = millis();
    driftResultActive = true;
    currentScreen     = SCREEN_QUIZ_RESULT;
    lastDisplayRefresh = 0;
    Serial.println("[QUIZ] Kuiz drift dijawab — papar keputusan");
    return;
  }

  if (sessionActive&&!sessionPaused&&
      (currentState==STATE_SLEEPING||currentState==STATE_WARNING)) return;

  switch(currentScreen) {
    case SCREEN_HOME:
      if (sessionActive) endSession();
      else {
        switch(homeMenuIdx) {
          case 0: currentScreen=SCREEN_START_SESSION; subjectSelectIdx=0; homeMenuIdx=0; break;
          case 1: currentScreen=SCREEN_REGISTER_TAG;  registerSelectIdx=0; homeMenuIdx=0; break;
          case 2: currentScreen=SCREEN_CALIBRATE;     homeMenuIdx=0; break;
          case 3: currentScreen=SCREEN_SETTINGS; settingsParamIdx=0; homeMenuIdx=0; break;
          case 4:  // v9.1: Mod Kuiz — do NOT reset homeMenuIdx here
            if (!companionMode || !companionReady) {
              showNotification("Hanya Mod Rakan");
              homeMenuIdx=0;
            } else {
              quizSubjectIdx = 0;
              currentScreen  = SCREEN_QUIZ_SUBJECT;
              // homeMenuIdx intentionally not reset — preserved for return
            }
            break;
        }
      }
      break;

    // v9.1/v9.3: Quiz subject picker — BtnB fetches topics then shows topic screen
    case SCREEN_QUIZ_SUBJECT:
      quizTopicIdx = 0;
      fetchQuizTopics(quizSubjectIdx + 1);  // GET /api/quiz/topics?subject_id=X
      if (quizTopicCount > 0) {
        currentScreen = SCREEN_QUIZ_TOPIC;
      } else {
        // No topics available for this subject
        showNotification("Tiada topik tersedia");
        currentScreen = SCREEN_HOME;
      }
      break;

    // v9.3: Quiz topic picker — BtnB confirms topic and fetches questions
    case SCREEN_QUIZ_TOPIC: {
      memset(&quizState, 0, sizeof(quizState));
      quizState.isDriftQuiz = false;

      // Start a quiz-only session on the server so answers are stored properly.
      if (companionReady && !sessionActive) {
        StaticJsonDocument<128> qStartDoc;
        qStartDoc["device_id"]  = deviceId;
        qStartDoc["subject_id"] = quizSubjectIdx + 1;
        qStartDoc["start_ts"]   = (long)(millis() / 1000);
        String qStartBody; serializeJson(qStartDoc, qStartBody);
        String qStartResp = postToServerWithResponse("/api/session/start", qStartBody);
        if (qStartResp.length() > 0) {
          StaticJsonDocument<64> rdoc;
          if (!deserializeJson(rdoc, qStartResp)) {
            serverSessionId = rdoc["session_id"] | -1;
            Serial.printf("[QUIZ] Sesi kuiz dimulakan, ID: %d\n", serverSessionId);
          }
        }
      }

      // Fetch questions filtered by the selected topic
      fetchQuizQuestions(quizSubjectIdx + 1, quizTopics[quizTopicIdx]);
      if (quizState.totalLoaded > 0) {
        currentScreen = SCREEN_QUIZ_QUESTION;
      } else {
        showNotification("Tiada soalan tersedia");
        currentScreen = SCREEN_HOME;
      }
      break;
    }

    // v9.1: Quiz question — BtnB confirms selected option
    case SCREEN_QUIZ_QUESTION: {
      int qIdx = quizState.currentIdx;
      bool correct = (quizState.selectedOpt ==
                      quizState.questions[qIdx].correctIndex);
      quizState.resultCorrect   = correct;
      if (correct) quizState.score++;
      // POST answer to server (blocking)
      if (companionReady && serverSessionId >= 0) {
        StaticJsonDocument<128> aDoc;
        aDoc["session_id"]   = serverSessionId;
        aDoc["question_id"]  = quizState.questions[qIdx].id;
        aDoc["chosen_index"] = quizState.selectedOpt;
        String aBody; serializeJson(aDoc, aBody);
        postToServer("/api/session/answer", aBody);
      }
      // v10.5: stamp AFTER the POST so the 2s window starts when result renders
      quizState.resultShowTime  = millis();
      currentScreen = SCREEN_QUIZ_RESULT;
      break;
    }

    // v9.1: Quiz result — BtnB skips 2-second wait and advances
    case SCREEN_QUIZ_RESULT:
      quizState.currentIdx++;
      if (quizState.currentIdx >= quizState.totalLoaded) {
        currentScreen = SCREEN_QUIZ_SUMMARY;
      } else {
        quizState.selectedOpt = 0;
        currentScreen = SCREEN_QUIZ_QUESTION;
      }
      break;

    // v9.1: Quiz summary — BtnB exits to home
    case SCREEN_QUIZ_SUMMARY:
      // Close the quiz-only session on the server (if we opened one)
      if (companionReady && !sessionActive && serverSessionId >= 0) {
        StaticJsonDocument<256> qEndDoc;
        qEndDoc["device_id"]   = deviceId;
        qEndDoc["session_id"]  = serverSessionId;
        qEndDoc["subject_id"]  = quizSubjectIdx + 1;
        qEndDoc["start_ts"]    = (long)(millis() / 1000);
        qEndDoc["end_ts"]      = (long)(millis() / 1000);
        qEndDoc["active_min"]  = 0;
        qEndDoc["idle_min"]    = 0;
        qEndDoc["focus_score"] = 0;
        String qEndBody; serializeJson(qEndDoc, qEndBody);
        postToServer("/api/session/end", qEndBody);
        serverSessionId = -1;  // reset for next quiz
        Serial.println("[QUIZ] Sesi kuiz ditamatkan.");
      }
      currentScreen = SCREEN_HOME; homeMenuIdx = 0;
      break;

    case SCREEN_START_SESSION:
      if (subjectSelectIdx==NUM_SUBJECTS) {
        currentScreen=SCREEN_HOME;
      } else if (!sessionActive) {
        // v11: after subject confirmed, show topic picker before starting session
        sessionTopicIdx = 0;
        fetchSessionTopics(subjectSelectIdx + 1);  // GET /api/quiz/topics?subject_id=X&device_id=Y
        currentScreen = SCREEN_SESSION_TOPIC;
      }
      break;

    // v11: Session topic picker — BtnB confirms topic and starts session
    case SCREEN_SESSION_TOPIC:
      if (sessionTopicCount > 0) {
        // Store selected topic name (empty string if "Ulangkaji Bebas")
        const char* sel = sessionTopicNames[sessionTopicIdx];
        if (strcmp(sel, "Ulangkaji Bebas") == 0) {
          activeTopicName[0] = '\0';  // empty = free study
        } else {
          strlcpy(activeTopicName, sel, MAX_TOPIC_LEN);
        }
        startSession(subjectSelectIdx);
        currentScreen=SCREEN_HOME; homeMenuIdx=0;
      }
      break;

    case SCREEN_REGISTER_TAG:
      if (registerSelectIdx==NUM_SUBJECTS+1) {
        currentScreen=SCREEN_HOME;
      } else {
        registerTagFlow(registerSelectIdx);
      }
      break;

    case SCREEN_CALIBRATE:
      runCalibration(); currentScreen=SCREEN_HOME; break;

    case SCREEN_SETTINGS:
      if (settingsParamIdx<8) {
        uint8_t* val=nullptr;
        switch(settingsParamIdx){
          case 0: val=&settings.warningTrigger;    break;
          case 1: val=&settings.sleepTrigger;      break;
          case 2: val=&settings.sleepBuzzInterval; break;
          case 3: val=&settings.distrThreshold;    break;
          case 4: val=&settings.deepFocusInterval; break;
          case 5: val=&settings.imuSensitivity;    break;
          case 6: val=&settings.restDuration;      break;
          case 7: val=&settings.quizWindow;        break;  // v9.1
        }
        *val=(*val+1)%3; applySettings(); saveSettingsToNVS(); buzz_confirm();
      } else if (settingsParamIdx==8) {
        confirmResetType=0; currentScreen=SCREEN_CONFIRM_RESET;
      } else if (settingsParamIdx==9) {
        confirmResetType=1; currentScreen=SCREEN_CONFIRM_RESET;
      } else if (settingsParamIdx==10) {
        // v9: toggle Companion / Solo mode and persist to NVS
        companionMode = !companionMode;
        prefs.putBool("comp_mode", companionMode);
        buzz_confirm();
        Serial.printf("[v9] Mod ditukar: %s (berkuat kuasa selepas restart)\n",
          companionMode ? "Rakan" : "Solo");
        showNotification(companionMode ? "Mod: Rakan (restart)" : "Mod: Solo (restart)");
      } else if (settingsParamIdx==11) {
        // v10.2: toggle Demo/Real timing and persist to NVS
        demoMode = !demoMode;
        prefs.putBool("demo_mode", demoMode);
        buzz_confirm();
        Serial.printf("[v10.2] Mod masa ditukar: %s\n", demoMode ? "DEMO" : "Nyata");
        showNotification(demoMode ? "Mod Demo: AKTIF" : "Mod Demo: Nyata");
      } else if (settingsParamIdx==12) {
        currentScreen=SCREEN_HOME;
      }
      break;

    case SCREEN_CONFIRM_RESET:
      if (confirmResetType==0) {
        clearHistoryNVS(); historyCount=0;
        lastSessionValid=false;
        currentScreen=SCREEN_HOME;
        showNotification("Sejarah dipadam!");
        buzz_confirm();
      } else {
        clearAllNVS();
      }
      break;
  }
  lastDisplayRefresh=0;
}

// ─── Web Server ────────────────────────────────────────────────────────────
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200,"text/html",getDashboardHTML());
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<2048> doc;
    doc["sessionActive"]     = sessionActive;
    doc["sessionPaused"]     = sessionPaused;
    doc["lastSessionValid"]  = lastSessionValid;
    doc["batteryLevel"]      = getBatteryLevel();

    if (sessionActive) {
      doc["state"]           = sessionPaused?"Berehat":stateNames[currentState];
      doc["stateColor"]      = sessionPaused?"#3F51B5":stateColorsHex[currentState];
      doc["focusScore"]      = focusScore;
      doc["distractionCount"]= distractionCount;
      doc["warningCount"]    = warningCount;
      doc["sleepingCount"]   = sleepingCount;
      doc["recoveryBonus"]   = recoveryBonus;
      doc["streakBonus"]     = streakBonus;
      doc["durationBonus"]   = durationBonus;
      doc["streakBonusEarned"]=streakBonusEarned;
      doc["restBreakCount"]  = restBreakCount;
      doc["totalRestMs"]     = totalRestMs+(sessionPaused?millis()-pauseStartTime:0);
      unsigned long el=getActiveSessionMs();
      unsigned long st=millis()-streakStartTime;
      doc["subject"]         = subjects[currentSubject];
      doc["elapsedMs"]       = el;
      doc["elapsedStr"]      = formatTime(el);
      doc["streakPct"]       = min((float)st/DEEP_FOCUS_INTERVAL_MS,1.0f);
      doc["warningSecsLeft"] = inWarning
        ?max(0,(int)((SLEEPING_TRIGGER_MS-(millis()-warningStartTime))/1000)):-1;
      if (sessionPaused) {
        unsigned long restEl=millis()-restTimerStart;
        unsigned long restRem=(restEl>=REST_DURATION_MS)?0:REST_DURATION_MS-restEl;
        doc["restSecsLeft"]    = (int)(restRem/1000);
        doc["restTimerExpired"]= restTimerExpired;
      }
    } else if (lastSessionValid) {
      doc["state"]           = "Tamat";
      doc["stateColor"]      = "#9E9E9E";
      doc["focusScore"]      = lastFocusScore;
      doc["distractionCount"]= lastDistractionCount;
      doc["warningCount"]    = lastWarningCount;
      doc["sleepingCount"]   = lastSleepingCount;
      doc["recoveryBonus"]   = lastRecoveryBonus;
      doc["streakBonus"]     = lastStreakBonus;
      doc["durationBonus"]   = lastDurationBonus;
      doc["streakBonusEarned"]=lastStreakBonus/5;
      doc["restBreakCount"]  = lastRestBreaks;
      doc["totalRestMs"]     = lastRestTimeMs;
      doc["subject"]         = subjects[lastSubject];
      doc["elapsedMs"]       = lastDurationMs;
      doc["elapsedStr"]      = formatTime(lastDurationMs);
      doc["streakPct"]       = 0;
      doc["warningSecsLeft"] = -1;
    } else {
      doc["state"]           = "--";
      doc["stateColor"]      = "#9E9E9E";
    }

    JsonArray tl=doc.createNestedArray("timeline");
    for (int i=0;i<timelineCount;i++) {
      JsonObject e=tl.createNestedObject();
      e["t"]=timeline[i].timestamp; e["s"]=timeline[i].state;
    }
    JsonArray dl=doc.createNestedArray("distractions");
    for (int i=0;i<distractionLogCount;i++) {
      JsonObject e=dl.createNestedObject();
      e["t"]=distractionLog[i].timestamp;
      e["type"]=distractionLog[i].type;
      e["impact"]=distractionLog[i].scoreImpact;
    }
    String out; serializeJson(doc,out);
    req->send(200,"application/json",out);
  });

  server.on("/history", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<4096> doc;
    JsonArray arr=doc.createNestedArray("sessions");
    for (int i=0;i<historyCount;i++) {
      JsonObject s=arr.createNestedObject();
      s["subject"]      = subjects[sessionHistory[i].subjectIndex];
      s["durationMs"]   = sessionHistory[i].durationMs;
      s["durationStr"]  = formatTime(sessionHistory[i].durationMs);
      s["restTimeMs"]   = sessionHistory[i].restTimeMs;
      s["restTimeStr"]  = formatTime(sessionHistory[i].restTimeMs);
      s["restBreaks"]   = sessionHistory[i].restBreaks;
      s["focusScore"]   = sessionHistory[i].focusScore;
      s["warningCount"] = sessionHistory[i].warningCount;
      s["sleepingCount"]= sessionHistory[i].sleepingCount;
      s["recoveryBonus"]= sessionHistory[i].recoveryBonus;
      s["streakBonus"]  = sessionHistory[i].streakBonus;
      s["durationBonus"]= sessionHistory[i].durationBonus;
    }
    String out; serializeJson(doc,out);
    req->send(200,"application/json",out);
  });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    doc["warningTrigger"]   = settings.warningTrigger;
    doc["sleepTrigger"]     = settings.sleepTrigger;
    doc["sleepBuzzInterval"]= settings.sleepBuzzInterval;
    doc["distrThreshold"]   = settings.distrThreshold;
    doc["deepFocusInterval"]= settings.deepFocusInterval;
    doc["imuSensitivity"]   = settings.imuSensitivity;
    doc["restDuration"]     = settings.restDuration;
    String out; serializeJson(doc,out);
    req->send(200,"application/json",out);
  });

  server.on("/settings",HTTP_POST,
    [](AsyncWebServerRequest* req){},nullptr,
    [](AsyncWebServerRequest* req,uint8_t* data,size_t len,size_t,size_t){
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc,data,len)==DeserializationError::Ok) {
        settings.warningTrigger    =constrain((int)doc["warningTrigger"],   0,2);
        settings.sleepTrigger      =constrain((int)doc["sleepTrigger"],     0,2);
        settings.sleepBuzzInterval =constrain((int)doc["sleepBuzzInterval"],0,2);
        settings.distrThreshold    =constrain((int)doc["distrThreshold"],   0,2);
        settings.deepFocusInterval =constrain((int)doc["deepFocusInterval"],0,2);
        settings.imuSensitivity    =constrain((int)doc["imuSensitivity"],   0,2);
        settings.restDuration      =constrain((int)doc["restDuration"],     0,2);
        applySettings(); saveSettingsToNVS();
        req->send(200,"application/json","{\"ok\":true}");
      } else req->send(400,"application/json","{\"error\":\"json error\"}");
    }
  );

  server.on("/reset/history",HTTP_POST,[](AsyncWebServerRequest* req){
    clearHistoryNVS(); historyCount=0; lastSessionValid=false;
    req->send(200,"application/json","{\"ok\":true}");
  });

  server.on("/reset/all",HTTP_POST,[](AsyncWebServerRequest* req){
    req->send(200,"application/json","{\"ok\":true}");
    delay(300); clearAllNVS();
  });

  server.begin();
}

// ─── Dashboard HTML ────────────────────────────────────────────────────────
// Identical to v7 — web dashboard is hardware-agnostic
String getDashboardHTML() {
  return R"rawhtml(
<!DOCTYPE html>
<html lang="ms">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>StudyAid Dashboard</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f5f5f5;color:#333}
.header{background:#fff;border-bottom:2px solid #e0e0e0;padding:14px 24px;display:flex;align-items:center;justify-content:space-between}
.header h1{font-size:20px;font-weight:700}
.header-right{display:flex;align-items:center;gap:12px}
.state-badge{padding:5px 14px;border-radius:20px;font-size:12px;font-weight:600;color:#fff;transition:background .3s}
.battery{font-size:12px;color:#666;font-weight:500}
.tabs{display:flex;background:#fff;border-bottom:1px solid #e0e0e0}
.tab{padding:11px 20px;cursor:pointer;font-size:13px;font-weight:500;color:#666;border-bottom:3px solid transparent}
.tab.active{color:#1976D2;border-bottom-color:#1976D2}
.page{display:none;padding:16px;max-width:900px;margin:0 auto}
.page.active{display:block}
.session-ended-banner{background:#E8F5E9;border:1px solid #4CAF50;border-radius:8px;padding:10px 16px;margin-bottom:16px;font-size:13px;color:#2E7D32;font-weight:500}
.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-bottom:16px}
.card{background:#fff;border-radius:10px;padding:16px;box-shadow:0 1px 4px rgba(0,0,0,.08)}
.card .label{font-size:11px;color:#888;text-transform:uppercase;letter-spacing:.5px;margin-bottom:6px}
.card .value{font-size:32px;font-weight:700;line-height:1}
.card .sub{font-size:11px;color:#aaa;margin-top:3px}
.green{color:#4CAF50}.yellow{color:#FF9800}.red{color:#F44336}
.section{background:#fff;border-radius:10px;padding:16px;box-shadow:0 1px 4px rgba(0,0,0,.08);margin-bottom:16px}
.section h3{font-size:12px;font-weight:600;color:#555;text-transform:uppercase;letter-spacing:.5px;margin-bottom:12px}
.score-bar{display:flex;height:26px;border-radius:5px;overflow:hidden;margin-bottom:8px}
.score-bar div{display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:600;color:#fff;min-width:0;overflow:hidden;transition:width .5s}
.bar-base{background:#4CAF50}.bar-warn{background:#FF9800}.bar-sleep{background:#F44336}
.bar-recovery{background:#2196F3}.bar-streak{background:#9C27B0}.bar-duration{background:#00BCD4}
.legend{display:flex;flex-wrap:wrap;gap:8px;margin-top:6px}
.legend-item{display:flex;align-items:center;gap:4px;font-size:11px;color:#666}
.legend-dot{width:8px;height:8px;border-radius:50%}
.streak-track{background:#e0e0e0;border-radius:4px;height:8px;overflow:hidden;margin:6px 0}
.streak-fill{background:#9C27B0;height:100%;border-radius:4px;transition:width .5s}
.rest-card{background:#E8EAF6;border:1px solid #3F51B5;border-radius:10px;padding:12px;margin-bottom:16px;text-align:center}
.rest-card h3{color:#3F51B5;font-size:14px;margin-bottom:8px}
.rest-timer{font-size:32px;font-weight:700;color:#3F51B5}
.rest-bar-track{background:#e0e0e0;border-radius:4px;height:8px;overflow:hidden;margin:8px 0}
.rest-bar-fill{background:#3F51B5;height:100%;border-radius:4px;transition:width .5s}
table{width:100%;border-collapse:collapse;font-size:12px}
th{text-align:left;padding:8px 10px;font-size:11px;color:#888;text-transform:uppercase;border-bottom:2px solid #f0f0f0}
td{padding:8px 10px;border-bottom:1px solid #f5f5f5}
tr:hover td{background:#fafafa}
.badge{display:inline-block;padding:2px 7px;border-radius:8px;font-size:10px;font-weight:600}
.badge-warn{background:#FFF3E0;color:#E65100}.badge-sleep{background:#FFEBEE;color:#B71C1C}
.no-session{text-align:center;padding:32px;color:#aaa}
.chart-wrap{position:relative;height:180px}
canvas.chart{width:100%!important;height:180px!important}
.timeline-canvas{width:100%;height:50px;border-radius:4px;background:#f9f9f9;display:block}
.refresh-dot{width:7px;height:7px;border-radius:50%;background:#4CAF50;display:inline-block;margin-right:5px;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
.setting-row{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid #f5f5f5;flex-wrap:wrap;gap:8px}
.setting-label{font-size:13px;font-weight:500}
.setting-options{display:flex;gap:6px;flex-wrap:wrap}
.opt-btn{padding:4px 10px;border-radius:12px;font-size:11px;font-weight:600;cursor:pointer;border:1.5px solid #e0e0e0;background:#fff;color:#666}
.opt-btn.active{background:#1976D2;color:#fff;border-color:#1976D2}
.save-btn{display:block;width:100%;padding:10px;background:#1976D2;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer;margin-top:14px}
.reset-section{margin-top:16px;padding-top:16px;border-top:2px solid #f0f0f0;display:flex;gap:10px;flex-wrap:wrap}
.reset-btn{padding:8px 18px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;border:none}
.reset-hist{background:#FFF3E0;color:#E65100}.reset-all{background:#FFEBEE;color:#B71C1C}
@media(max-width:600px){.cards{grid-template-columns:1fr 1fr}.card .value{font-size:26px}}
</style>
</head>
<body>
<div class="header">
  <div>
    <h1>📚 StudyAid</h1>
    <div style="font-size:11px;color:#aaa;margin-top:1px">
      <span class="refresh-dot"></span>Papan Pemuka Langsung
    </div>
  </div>
  <div class="header-right">
    <div class="battery" id="batteryDisplay">🔋 --%</div>
    <div id="stateBadge" class="state-badge" style="background:#9E9E9E">--</div>
  </div>
</div>
<div class="tabs">
  <div class="tab active" onclick="switchTab('live')">Sesi Langsung</div>
  <div class="tab" onclick="switchTab('history')">Sejarah</div>
  <div class="tab" onclick="switchTab('settings')">Tetapan</div>
</div>
<div id="pageLive" class="page active">
  <div id="sessionEndedBanner" class="session-ended-banner" style="display:none">
    ✅ Sesi tamat — menunjukkan ringkasan sesi terakhir
  </div>
  <div id="noSession" class="section no-session" style="display:none">
    <div style="font-size:40px;margin-bottom:8px">⏸️</div>
    <div>Tiada sesi aktif</div>
    <div style="font-size:12px;margin-top:4px;color:#bbb">Mulakan sesi pada peranti atau sentuh tag NFC berdaftar</div>
  </div>
  <div id="restCard" class="rest-card" style="display:none">
    <h3>🛋️ Berehat</h3>
    <div class="rest-timer" id="restTimer">--</div>
    <div style="font-size:12px;color:#3F51B5;margin-top:4px" id="restStatus"></div>
    <div class="rest-bar-track"><div class="rest-bar-fill" id="restBarFill" style="width:0%"></div></div>
  </div>
  <div id="sessionContent">
    <div class="cards">
      <div class="card">
        <div class="label">Markah Fokus</div>
        <div class="value" id="focusScore">--</div>
        <div class="sub" id="scoreChange"></div>
      </div>
      <div class="card">
        <div class="label">Masa Sesi</div>
        <div class="value" style="font-size:26px" id="sessionTime">--</div>
        <div class="sub" id="subjectName"></div>
      </div>
      <div class="card">
        <div class="label">Gangguan</div>
        <div class="value" id="distrCount">--</div>
        <div class="sub" id="distrBreakdown"></div>
      </div>
    </div>
    <div class="cards" style="grid-template-columns:1fr 1fr">
      <div class="card">
        <div class="label">Rehat</div>
        <div class="value" style="font-size:28px" id="restBreaks">0</div>
        <div class="sub" id="restTotal"></div>
      </div>
      <div class="card">
        <div class="label">Fokus Berterusan</div>
        <div class="value" style="font-size:28px" id="streakCount">0x</div>
        <div class="sub" id="streakLabel"></div>
      </div>
    </div>
    <div class="section">
      <h3>Pecahan Markah</h3>
      <div class="score-bar" id="scoreBar"></div>
      <div class="legend">
        <div class="legend-item"><div class="legend-dot" style="background:#4CAF50"></div>Asas</div>
        <div class="legend-item"><div class="legend-dot" style="background:#FF9800"></div>Amaran</div>
        <div class="legend-item"><div class="legend-dot" style="background:#F44336"></div>Tertidur</div>
        <div class="legend-item"><div class="legend-dot" style="background:#2196F3"></div>Pemulihan</div>
        <div class="legend-item"><div class="legend-dot" style="background:#9C27B0"></div>Streak</div>
        <div class="legend-item"><div class="legend-dot" style="background:#00BCD4"></div>Tempoh</div>
      </div>
    </div>
    <div class="section">
      <h3>Kemajuan Streak Fokus — +5 Seterusnya</h3>
      <div class="streak-track"><div class="streak-fill" id="streakFill" style="width:0%"></div></div>
    </div>
    <div class="section">
      <h3>Garis Masa Keadaan</h3>
      <canvas class="timeline-canvas" id="timelineCanvas"></canvas>
      <div class="legend" style="margin-top:8px">
        <div class="legend-item"><div class="legend-dot" style="background:#4CAF50"></div>Aktif</div>
        <div class="legend-item"><div class="legend-dot" style="background:#00BCD4"></div>Membaca</div>
        <div class="legend-item"><div class="legend-dot" style="background:#FF9800"></div>Amaran</div>
        <div class="legend-item"><div class="legend-dot" style="background:#F44336"></div>Tertidur</div>
        <div class="legend-item"><div class="legend-dot" style="background:#3F51B5"></div>Berehat</div>
      </div>
    </div>
    <div class="section">
      <h3>Log Gangguan</h3>
      <table><thead><tr><th>Masa</th><th>Jenis</th><th>Kesan Markah</th></tr></thead>
      <tbody id="distrTable"></tbody></table>
    </div>
  </div>
</div>
<div id="pageHistory" class="page">
  <div class="section">
    <h3>Sesi Lepas</h3>
    <table><thead><tr><th>#</th><th>Subjek</th><th>Tempoh</th><th>Markah</th><th>Amaran</th><th>Tidur</th><th>Rehat</th></tr></thead>
    <tbody id="historyTable"></tbody></table>
  </div>
  <div class="section">
    <h3>Masa Belajar Mengikut Subjek (minit)</h3>
    <div class="chart-wrap"><canvas class="chart" id="subjectChart"></canvas></div>
  </div>
  <div class="section">
    <h3>Trend Markah Fokus</h3>
    <div class="chart-wrap"><canvas class="chart" id="scoreChart"></canvas></div>
  </div>
</div>
<div id="pageSettings" class="page">
  <div class="section">
    <h3>Parameter</h3>
    <div id="settingsRows"></div>
    <button class="save-btn" onclick="saveSettings()">Simpan Tetapan</button>
  </div>
  <div class="section">
    <h3>Set Semula</h3>
    <div class="reset-section">
      <button class="reset-btn reset-hist" onclick="resetHistory()">Padam Sejarah Sesi</button>
      <button class="reset-btn reset-all"  onclick="resetAll()">Padam Semua Data</button>
    </div>
    <div style="font-size:11px;color:#aaa;margin-top:10px">
      Padam Semua akan memadam sejarah, kalibrasi, tag dan tetapan. Peranti akan restart.
    </div>
  </div>
</div>
<script>
const timelineColorMap=['#4CAF50','#00BCD4','#FF9800','#F44336','#3F51B5'];
function switchTab(t){
  document.querySelectorAll('.tab').forEach((el,i)=>
    el.classList.toggle('active',['live','history','settings'][i]===t));
  ['pageLive','pageHistory','pageSettings'].forEach((id,i)=>
    document.getElementById(id).classList.toggle('active',['live','history','settings'][i]===t));
  if(t==='history') loadHistory();
  if(t==='settings') loadSettings();
}
function msToStr(ms){
  let s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60);
  s%=60;m%=60;
  return h>0
    ?`${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`
    :`${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
}
function scoreClass(s){return s>=80?'green':s>=50?'yellow':'red'}
let prevScore=null;
async function fetchLive(){
  try{
    const d=await(await fetch('/data')).json();
    document.getElementById('batteryDisplay').textContent=`🔋 ${d.batteryLevel}%`;
    const badge=document.getElementById('stateBadge');
    badge.textContent=d.state; badge.style.background=d.stateColor;
    const endedBanner=document.getElementById('sessionEndedBanner');
    const noSession=document.getElementById('noSession');
    if (!d.sessionActive && !d.lastSessionValid) {
      noSession.style.display='block';
      endedBanner.style.display='none';
      document.getElementById('sessionContent').style.display='none';
      document.getElementById('restCard').style.display='none';
      return;
    }
    noSession.style.display='none';
    endedBanner.style.display=(!d.sessionActive&&d.lastSessionValid)?'block':'none';
    const restCard=document.getElementById('restCard');
    if(d.sessionActive&&d.sessionPaused){
      restCard.style.display='block';
      const secsLeft=d.restSecsLeft||0;
      document.getElementById('restTimer').textContent=
        d.restTimerExpired?'Rehat Tamat!':msToStr(secsLeft*1000);
      document.getElementById('restStatus').textContent=
        d.restTimerExpired?'Sentuh tag REHAT untuk sambung semula':'Sentuh tag REHAT untuk sambung awal';
      document.getElementById('restBarFill').style.width=
        d.restTimerExpired?'100%':
        Math.round((1-(secsLeft/(secsLeft+((d.totalRestMs||0)/1000))))*100)+'%';
    } else { restCard.style.display='none'; }
    document.getElementById('sessionContent').style.display='block';
    const sc=document.getElementById('focusScore');
    sc.textContent=d.focusScore; sc.className='value '+scoreClass(d.focusScore);
    if(prevScore!==null&&d.focusScore!==prevScore){
      const diff=d.focusScore-prevScore;
      document.getElementById('scoreChange').textContent=(diff>0?'+':'')+diff+' mata';
    }
    prevScore=d.focusScore;
    document.getElementById('sessionTime').textContent=d.elapsedStr||'--';
    document.getElementById('subjectName').textContent=d.subject||'';
    document.getElementById('distrCount').textContent=d.distractionCount;
    document.getElementById('distrBreakdown').textContent=`Amaran: ${d.warningCount}  Tidur: ${d.sleepingCount}`;
    document.getElementById('restBreaks').textContent=d.restBreakCount||0;
    document.getElementById('restTotal').textContent=d.totalRestMs?msToStr(d.totalRestMs):'Tiada rehat';
    document.getElementById('streakCount').textContent=(d.streakBonusEarned||0)+'x';
    const pct=Math.round((d.streakPct||0)*100);
    document.getElementById('streakLabel').textContent=pct+'% ke +5 seterusnya';
    document.getElementById('streakFill').style.width=pct+'%';
    const total=120,base=Math.max(0,100-d.warningCount*3);
    const wW=Math.min(d.warningCount*3,50);
    const rW=d.recoveryBonus,stW=d.streakBonus,dW=d.durationBonus;
    document.getElementById('scoreBar').innerHTML=`
      <div class="bar-base" style="width:${base/total*100}%">${base>10?'Asas':''}</div>
      ${wW?`<div class="bar-warn" style="width:${wW/total*100}%">${wW>5?'-'+wW:''}</div>`:''}
      ${sW?`<div class="bar-sleep" style="width:${sW/total*100}%">${sW>5?'-'+sW:''}</div>`:''}
      ${rW?`<div class="bar-recovery" style="width:${rW/total*100}%">${rW>3?'+'+rW:''}</div>`:''}
      ${stW?`<div class="bar-streak" style="width:${stW/total*100}%">${stW>3?'+'+stW:''}</div>`:''}
      ${dW?`<div class="bar-duration" style="width:${dW/total*100}%">${dW>3?'+'+dW:''}</div>`:''}`;
    drawTimeline(d.timeline,d.elapsedMs);
    const tbody=document.getElementById('distrTable');
    tbody.innerHTML='';
    if(!d.distractions?.length){
      tbody.innerHTML='<tr><td colspan="3" style="color:#aaa;text-align:center">Tiada lagi</td></tr>';
    } else {
      d.distractions.slice().reverse().forEach(e=>{
        const tr=document.createElement('tr');
        tr.innerHTML=`<td>${msToStr(e.t)}</td>
          <td><span class="badge ${e.type===0?'badge-warn':'badge-sleep'}">${e.type===0?'Amaran':'Tertidur'}</span></td>
          <td style="color:#F44336">${e.impact}</td>`;
        tbody.appendChild(tr);
      });
    }
  }catch(e){console.warn(e)}
}
function drawTimeline(entries,totalMs){
  const canvas=document.getElementById('timelineCanvas');
  if(!canvas||!entries||entries.length<1) return;
  const ctx=canvas.getContext('2d');
  const W=canvas.offsetWidth,H=canvas.offsetHeight;
  canvas.width=W*devicePixelRatio; canvas.height=H*devicePixelRatio;
  ctx.scale(devicePixelRatio,devicePixelRatio);
  const dur=totalMs||1;
  ctx.clearRect(0,0,W,H);
  for(let i=0;i<entries.length-1;i++){
    ctx.fillStyle=timelineColorMap[entries[i].s]||'#9E9E9E';
    ctx.fillRect(entries[i].t/dur*W,3,Math.max((entries[i+1].t-entries[i].t)/dur*W,2),H-6);
  }
  if(entries.length>0){
    const last=entries[entries.length-1];
    ctx.fillStyle=timelineColorMap[last.s]||'#9E9E9E';
    ctx.fillRect(last.t/dur*W,3,W-last.t/dur*W,H-6);
  }
}
if(!CanvasRenderingContext2D.prototype.roundRect){
  CanvasRenderingContext2D.prototype.roundRect=function(x,y,w,h,r){
    this.beginPath();this.moveTo(x+r,y);this.lineTo(x+w-r,y);
    this.arcTo(x+w,y,x+w,y+r,r);this.lineTo(x+w,y+h-r);
    this.arcTo(x+w,y+h,x+w-r,y+h,r);this.lineTo(x+r,y+h);
    this.arcTo(x,y+h,x,y+h-r,r);this.lineTo(x,y+r);
    this.arcTo(x,y,x+r,y,r);this.closePath();return this;
  };
}
function drawBarChart(id,labels,data,colors){
  const canvas=document.getElementById(id);
  if(!canvas||!data.length) return;
  const ctx=canvas.getContext('2d');
  const W=canvas.offsetWidth,H=canvas.offsetHeight;
  canvas.width=W*devicePixelRatio; canvas.height=H*devicePixelRatio;
  ctx.scale(devicePixelRatio,devicePixelRatio);
  ctx.clearRect(0,0,W,H);
  const max=Math.max(...data,1);
  const pad={top:10,right:10,bottom:30,left:40};
  const chartW=W-pad.left-pad.right,chartH=H-pad.top-pad.bottom;
  const gap=chartW/data.length,barW=Math.max(gap*0.6,4);
  ctx.strokeStyle='#e0e0e0';ctx.lineWidth=1;
  ctx.beginPath();ctx.moveTo(pad.left,pad.top);ctx.lineTo(pad.left,pad.top+chartH);ctx.stroke();
  data.forEach((v,i)=>{
    const x=pad.left+gap*i+(gap-barW)/2,barH=v/max*chartH,y=pad.top+chartH-barH;
    ctx.fillStyle=colors[i%colors.length];
    ctx.roundRect(x,y,barW,barH,3);ctx.fill();
    ctx.fillStyle='#666';ctx.font='10px sans-serif';ctx.textAlign='center';
    ctx.fillText(labels[i].substring(0,6),x+barW/2,H-pad.bottom+14);
    if(barH>14){ctx.fillStyle='#fff';ctx.font='bold 10px sans-serif';ctx.fillText(v,x+barW/2,y+12);}
  });
  ctx.fillStyle='#999';ctx.font='9px sans-serif';ctx.textAlign='right';
  [0,0.5,1].forEach(f=>{
    const y=pad.top+chartH*(1-f);
    ctx.fillText(Math.round(max*f),pad.left-4,y+3);
    ctx.strokeStyle='#f0f0f0';ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(pad.left,y);ctx.lineTo(W-pad.right,y);ctx.stroke();
  });
}
function drawLineChart(id,labels,data){
  const canvas=document.getElementById(id);
  if(!canvas||data.length<2) return;
  const ctx=canvas.getContext('2d');
  const W=canvas.offsetWidth,H=canvas.offsetHeight;
  canvas.width=W*devicePixelRatio; canvas.height=H*devicePixelRatio;
  ctx.scale(devicePixelRatio,devicePixelRatio);
  ctx.clearRect(0,0,W,H);
  const max=120,min=0;
  const pad={top:10,right:10,bottom:30,left:40};
  const chartW=W-pad.left-pad.right,chartH=H-pad.top-pad.bottom;
  const xStep=chartW/(data.length-1),yScale=chartH/(max-min);
  const px=i=>pad.left+i*xStep,py=v=>pad.top+chartH-(v-min)*yScale;
  [0,50,80,120].forEach(v=>{
    ctx.strokeStyle=v===0?'#e0e0e0':'#f5f5f5';ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(pad.left,py(v));ctx.lineTo(W-pad.right,py(v));ctx.stroke();
    ctx.fillStyle='#999';ctx.font='9px sans-serif';ctx.textAlign='right';
    ctx.fillText(v,pad.left-4,py(v)+3);
  });
  ctx.beginPath();ctx.moveTo(px(0),py(data[0]));
  data.forEach((v,i)=>{if(i>0)ctx.lineTo(px(i),py(v));});
  ctx.lineTo(px(data.length-1),py(min));ctx.lineTo(px(0),py(min));ctx.closePath();
  ctx.fillStyle='rgba(25,118,210,.08)';ctx.fill();
  ctx.beginPath();ctx.moveTo(px(0),py(data[0]));
  data.forEach((v,i)=>{if(i>0)ctx.lineTo(px(i),py(v));});
  ctx.strokeStyle='#1976D2';ctx.lineWidth=2;ctx.stroke();
  data.forEach((v,i)=>{
    ctx.beginPath();ctx.arc(px(i),py(v),4,0,Math.PI*2);
    ctx.fillStyle='#1976D2';ctx.fill();
    ctx.fillStyle='#666';ctx.font='9px sans-serif';ctx.textAlign='center';
    ctx.fillText(labels[i],px(i),H-pad.bottom+12);
  });
}
async function loadHistory(){
  try{
    const d=await(await fetch('/history')).json();
    const sessions=d.sessions||[];
    const tbody=document.getElementById('historyTable');
    tbody.innerHTML='';
    if(!sessions.length){
      tbody.innerHTML='<tr><td colspan="7" style="color:#aaa;text-align:center">Tiada sesi lagi</td></tr>';
    } else {
      sessions.slice().reverse().forEach((s,i)=>{
        const sc=s.focusScore>=80?'#4CAF50':s.focusScore>=50?'#FF9800':'#F44336';
        const tr=document.createElement('tr');
        tr.innerHTML=`<td>${sessions.length-i}</td><td>${s.subject}</td>
          <td>${s.durationStr}</td>
          <td style="color:${sc};font-weight:700">${s.focusScore}</td>
          <td>${s.warningCount}</td><td>${s.sleepingCount}</td>
          <td>${s.restBreaks}x ${s.restTimeStr}</td>`;
        tbody.appendChild(tr);
      });
    }
    const totals={};
    sessions.forEach(s=>totals[s.subject]=(totals[s.subject]||0)+s.durationMs/60000);
    const sLabels=Object.keys(totals),sData=sLabels.map(k=>Math.round(totals[k]));
    drawBarChart('subjectChart',sLabels,sData,['#4CAF50','#2196F3','#FF9800','#9C27B0','#00BCD4']);
    const scLabels=sessions.map((_,i)=>`#${i+1}`),scData=sessions.map(s=>s.focusScore);
    drawLineChart('scoreChart',scLabels,scData);
  }catch(e){console.warn(e)}
}
const PARAM_DEFS=[
  {key:'warningTrigger',   label:'Pencetus Amaran',    opts:['Singkat (30s)','Sedang (60s)','Panjang (120s)']},
  {key:'sleepTrigger',     label:'Pencetus Tidur',      opts:['Singkat (15s)','Sedang (30s)','Panjang (60s)']},
  {key:'sleepBuzzInterval',label:'Selang Getar Tidur',  opts:['Singkat (2s)','Sedang (5s)','Panjang (10s)']},
  {key:'distrThreshold',   label:'Ambang Gangguan',     opts:['Singkat (15s)','Sedang (30s)','Panjang (60s)']},
  {key:'deepFocusInterval',label:'Selang Fokus',        opts:['Singkat (5min)','Sedang (10min)','Panjang (25min)']},
  {key:'imuSensitivity',   label:'Kepekaan IMU',        opts:['Rendah (0.15)','Sedang (0.30)','Tinggi (0.50)']},
  {key:'restDuration',     label:'Tempoh Rehat',         opts:['Singkat (1min)','Sedang (5min)','Panjang (10min)']},
];
let currentSettings={};
async function loadSettings(){
  try{
    currentSettings=await(await fetch('/settings')).json();
    const container=document.getElementById('settingsRows');
    container.innerHTML='';
    PARAM_DEFS.forEach(p=>{
      const val=currentSettings[p.key]??1;
      const row=document.createElement('div');
      row.className='setting-row';
      row.innerHTML=`<div class="setting-label">${p.label}</div>
        <div class="setting-options">
          ${p.opts.map((o,i)=>`<button class="opt-btn${val===i?' active':''}"
            onclick="setSetting('${p.key}',${i},this)">${o}</button>`).join('')}
        </div>`;
      container.appendChild(row);
    });
  }catch(e){console.warn(e)}
}
function setSetting(key,val,btn){
  currentSettings[key]=val;
  btn.parentElement.querySelectorAll('.opt-btn').forEach((b,i)=>b.classList.toggle('active',i===val));
}
async function saveSettings(){
  try{
    const r=await fetch('/settings',{method:'POST',
      headers:{'Content-Type':'application/json'},body:JSON.stringify(currentSettings)});
    if((await r.json()).ok) alert('Tetapan disimpan!');
  }catch(e){alert('Simpan gagal')}
}
async function resetHistory(){
  if(!confirm('Padam semua sejarah sesi? Tindakan ini tidak boleh dibuat alik.')) return;
  await fetch('/reset/history',{method:'POST'});
  alert('Sejarah dipadam.'); loadHistory();
}
async function resetAll(){
  if(!confirm('Padam SEMUA data? Peranti akan restart.')) return;
  await fetch('/reset/all',{method:'POST'});
  alert('Semua data dipadam. Peranti sedang restart...');
}
fetchLive();
setInterval(fetchLive,2000);
</script>
</body>
</html>
)rawhtml";
}

// ─── v9: Companion Mode — HTTP POST Helper ─────────────────────────────────
//
// postToServer(path, jsonBody)
//   Sends a JSON POST to the Flask companion app.
//   Only executes when companionReady is true (Companion mode + WiFi connected).
//   Times out after COMP_TIMEOUT_MS so a missing server never blocks the loop.
//   Returns true on HTTP 200, false on any error.
//
// Why not use ESPAsyncWebServer for this?
//   ESPAsyncWebServer is a *server* library — it handles incoming requests.
//   To make *outgoing* requests (device → laptop) we need HTTPClient,
//   which is the ESP32 Arduino HTTP client. Both can coexist; in Companion mode
//   the AsyncWebServer is simply not started.
//
bool postToServer(const char* path, const String& jsonBody) {
  if (!companionReady) return false;

  HTTPClient http;
  char url[64];
  snprintf(url, sizeof(url), "http://%s:%d%s", COMP_SERVER_IP, COMP_SERVER_PORT, path);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(COMP_TIMEOUT_MS);

  int code = http.POST(jsonBody);
  bool ok  = (code == 200);

  Serial.printf("[HTTP] POST %s -> %d %s\n", path, code, ok ? "OK" : "GAGAL");
  if (!ok && code > 0) {
    Serial.printf("[HTTP] Response: %s\n", http.getString().c_str());
  }

  http.end();
  return ok;
}

// v9.1: Like postToServer but returns response body as String.
// Used when we need to parse the server's reply (session/start, session/drift).
String postToServerWithResponse(const char* path, const String& jsonBody) {
  if (!companionReady) return "";

  HTTPClient http;
  char url[64];
  snprintf(url, sizeof(url), "http://%s:%d%s", COMP_SERVER_IP, COMP_SERVER_PORT, path);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(COMP_TIMEOUT_MS);

  int code = http.POST(jsonBody);
  String resp = "";
  if (code == 200) {
    resp = http.getString();
  }
  Serial.printf("[HTTP] POST %s -> %d\n", path, code);
  http.end();
  return resp;
}

// ─── v9: Companion WiFi Init ────────────────────────────────────────────────
//
// Called from setup() when companionMode is true.
// Connects to the laptop's Mobile Hotspot as a WiFi client (STA mode).
// Times out after 10 seconds and falls back to Solo mode if unreachable.
// This is separate from the v8.4 softAP setup which only runs in Solo mode.
//
void initCompanionWiFi() {
  M5.Display.setCursor(0, 80);
  M5.Display.printf("Mod Rakan...\nSSID: %s\n", COMP_SSID);
  Serial.printf("[v9] Menghubung ke %s ...\n", COMP_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(COMP_SSID, COMP_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    M5.Display.print(".");
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    companionReady = true;
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.printf("\nTerhubung!\nIP: %s\n", WiFi.localIP().toString().c_str());
    M5.Display.setTextColor(WHITE, BLACK);
    Serial.printf("[v9] WiFi terhubung. IP peranti: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[v9] Pelayan: http://%s:%d\n", COMP_SERVER_IP, COMP_SERVER_PORT);
    // v12.1: Send initial hello ping so booth kiosk knows this device is present
    {
      StaticJsonDocument<64> hDoc;
      hDoc["device_id"] = deviceId;
      String hBody; serializeJson(hDoc, hBody);
      postToServer("/api/hello", hBody);
      lastHelloMs = millis();
      Serial.printf("[v12.1] Hello ping dihantar untuk %s\n", deviceId);
    }
  } else {
    // Connection failed — fall back to Solo mode so the device remains usable
    companionMode  = false;
    companionReady = false;
    M5.Display.setTextColor(ORANGE, BLACK);
    M5.Display.println("\nGagal! Kembali ke Solo.");
    M5.Display.setTextColor(WHITE, BLACK);
    Serial.println("[v9] AMARAN: Gagal hubung WiFi. Kembali ke mod Solo.");
    buzz_error();
    delay(2000);
  }
}

// ─── v9.1/v9.3: Quiz Functions ─────────────────────────────────────────────

// fetchQuizTopics — GET /api/quiz/topics?subject_id=X
// Populates quizTopics[] and sets quizTopicCount.
// v11: server now returns [{topic, profile}] objects — extract topic string only
// (Quiz Mode doesn't use profiles). Called after subject is confirmed in Mod Kuiz.
void fetchQuizTopics(int subjectId) {
  quizTopicCount = 0;
  if (!companionReady) return;

  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url),
    "http://%s:%d/api/quiz/topics?subject_id=%d",
    COMP_SERVER_IP, COMP_SERVER_PORT, subjectId);

  http.begin(url);
  http.setTimeout(COMP_TIMEOUT_MS);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("[QUIZ] fetchQuizTopics gagal: HTTP %d\n", code);
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  // v11: parse response: {"topics": [{"topic": "...", "profile": "..."}, ...]}
  StaticJsonDocument<1536> doc;
  if (deserializeJson(doc, body)) {
    Serial.println("[QUIZ] fetchQuizTopics: JSON parse gagal");
    return;
  }

  JsonArray arr = doc["topics"].as<JsonArray>();
  for (JsonObject t : arr) {
    if (quizTopicCount >= MAX_QUIZ_TOPICS) break;
    strlcpy(quizTopics[quizTopicCount], t["topic"] | "", MAX_TOPIC_LEN);
    quizTopicCount++;
  }
  Serial.printf("[QUIZ] %d topik dimuatkan (subjek %d)\n", quizTopicCount, subjectId);
}

// v11: fetchSessionTopics — GET /api/quiz/topics?subject_id=X&device_id=Y
// Populates sessionTopicNames[] and sessionTopicProfiles[] for the session
// topic picker (SCREEN_SESSION_TOPIC). Sends device_id so the server returns
// only this student's planned topics (with profiles) + Ulangkaji Bebas sentinel.
void fetchSessionTopics(int subjectId) {
  sessionTopicCount = 0;
  if (!companionReady) {
    // Offline fallback: single "Ulangkaji Bebas" option
    strlcpy(sessionTopicNames[0], "Ulangkaji Bebas", MAX_TOPIC_LEN);
    strlcpy(sessionTopicProfiles[0], "Campuran", 12);
    sessionTopicCount = 1;
    return;
  }

  HTTPClient http;
  char url[160];
  snprintf(url, sizeof(url),
    "http://%s:%d/api/quiz/topics?subject_id=%d&device_id=%s",
    COMP_SERVER_IP, COMP_SERVER_PORT, subjectId, deviceId);

  http.begin(url);
  http.setTimeout(COMP_TIMEOUT_MS);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("[v11] fetchSessionTopics gagal: HTTP %d\n", code);
    http.end();
    // Offline fallback
    strlcpy(sessionTopicNames[0], "Ulangkaji Bebas", MAX_TOPIC_LEN);
    strlcpy(sessionTopicProfiles[0], "Campuran", 12);
    sessionTopicCount = 1;
    return;
  }

  String body = http.getString();
  http.end();

  // Parse: {"topics": [{"topic": "...", "profile": "..."}, ..., {"topic": "Ulangkaji Bebas", "profile": "Campuran"}]}
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, body)) {
    Serial.println("[v11] fetchSessionTopics: JSON parse gagal");
    strlcpy(sessionTopicNames[0], "Ulangkaji Bebas", MAX_TOPIC_LEN);
    strlcpy(sessionTopicProfiles[0], "Campuran", 12);
    sessionTopicCount = 1;
    return;
  }

  JsonArray arr = doc["topics"].as<JsonArray>();
  for (JsonObject t : arr) {
    if (sessionTopicCount >= MAX_SESSION_TOPICS) break;
    strlcpy(sessionTopicNames[sessionTopicCount], t["topic"] | "Ulangkaji Bebas", MAX_TOPIC_LEN);
    strlcpy(sessionTopicProfiles[sessionTopicCount], t["profile"] | "Campuran", 12);
    sessionTopicCount++;
  }
  Serial.printf("[v11] %d topik sesi dimuatkan (subjek %d)\n", sessionTopicCount, subjectId);
}

// fetchQuizQuestions — GET /api/quiz/questions?subject_id=X&topic=Y&session_id=Z
// v9.3: now accepts a topic string to filter questions from a specific bank.
void fetchQuizQuestions(int subjectId, const char* topic) {
  quizState.totalLoaded = 0;
  quizState.currentIdx  = 0;
  quizState.selectedOpt = 0;
  quizState.score       = 0;

  if (!companionReady) return;

  HTTPClient http;
  char url[192];
  // v10.4: URL-encode topic so spaces/special chars don't break the request line
  String encodedTopic = topic ? urlEncode(topic) : "";
  snprintf(url, sizeof(url),
    "http://%s:%d/api/quiz/questions?subject_id=%d&topic=%s&session_id=%d",
    COMP_SERVER_IP, COMP_SERVER_PORT, subjectId,
    encodedTopic.c_str(), serverSessionId);

  http.begin(url);
  http.setTimeout(COMP_TIMEOUT_MS);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("[QUIZ] Gagal ambil soalan: HTTP %d\n", code);
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  // Parse response: {"questions":[{"id":1,"q":"...","o":["A","B","C","D"],"c":2},...]}
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[QUIZ] JSON parse gagal: %s\n", err.c_str());
    return;
  }

  JsonArray arr = doc["questions"].as<JsonArray>();
  int count = 0;
  for (JsonObject q : arr) {
    if (count >= MAX_QUIZ_QUESTIONS) break;
    quizState.questions[count].id           = q["id"] | 0;
    quizState.questions[count].correctIndex = q["c"]  | 0;
    strlcpy(quizState.questions[count].text,
            q["q"] | "", MAX_Q_TEXT);
    JsonArray opts = q["o"].as<JsonArray>();
    for (int i=0;i<4&&i<(int)opts.size();i++) {
      strlcpy(quizState.questions[count].opts[i],
              opts[i] | "", MAX_OPT_TEXT);
    }
    count++;
  }
  quizState.totalLoaded = count;
  Serial.printf("[QUIZ] %d soalan dimuatkan (subjek %d)\n", count, subjectId);
}

// handleDriftQuizResponse — parse server drift response and enter quiz if quiz present
void handleDriftQuizResponse(const String& body) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, body)) return;
  if (doc["quiz"].isNull()) return;

  JsonObject q = doc["quiz"].as<JsonObject>();
  memset(&quizState, 0, sizeof(quizState));
  quizState.isDriftQuiz             = true;
  quizState.totalLoaded             = 1;
  quizState.questions[0].id         = q["id"] | 0;
  quizState.questions[0].correctIndex = q["c"] | 0;
  strlcpy(quizState.questions[0].text, q["q"] | "", MAX_Q_TEXT);
  JsonArray opts = q["o"].as<JsonArray>();
  for (int i=0;i<4&&i<(int)opts.size();i++) {
    strlcpy(quizState.questions[0].opts[i], opts[i] | "", MAX_OPT_TEXT);
  }
  quizState.selectedOpt = 0;
  driftQuizActive  = true;       // v10.4: pause monitoring, own buttons
  lastDriftAlertMs = millis();   // start repeating alert immediately
  driftQuizStartMs = millis();   // v10.5: start timeout clock
  buzz_driftAlert();             // first alert on entry
  currentScreen = SCREEN_QUIZ_QUESTION;
  Serial.printf("[QUIZ] Kuiz drift: soalan ID %d\n", quizState.questions[0].id);
}

// renderQuizSubject — subject picker screen
void renderQuizSubject() {
  int total = NUM_SUBJECTS;
  int vis0  = scrollStart(quizSubjectIdx, total);
  clearDisplay(); drawHeader("Mod Kuiz");
  drawScrollBar(quizSubjectIdx, total);
  for (int i=vis0; i<vis0+VISIBLE_ITEMS && i<total; i++) {
    int rowY = CONTENT_Y + (i-vis0)*ROW_H;
    bool sel = (i==quizSubjectIdx);
    if (sel) M5.Display.fillRect(0,rowY-1,232,ROW_H,COL_INFO);
    M5.Display.setCursor(4,rowY); M5.Display.setTextSize(2);
    M5.Display.setTextColor(sel?BLACK:COL_TEXT, sel?COL_INFO:BLACK);
    M5.Display.print(subjects[i]);
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("[A] Kitar","[B] Pilih");
}

void renderQuizTopic() {
  char hdr[28]; snprintf(hdr,28,"Topik — %s",subjects[quizSubjectIdx]);
  clearDisplay(); drawHeader(hdr);

  if (quizTopicCount == 0) {
    M5.Display.setTextColor(COL_WARN,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,CONTENT_Y+10);
    M5.Display.println("Tiada topik");
    M5.Display.setTextColor(COL_TEXT,BLACK);
  } else {
    int vis0 = scrollStart(quizTopicIdx, quizTopicCount);
    drawScrollBar(quizTopicIdx, quizTopicCount);
    // textSize(1) — same fix as renderSessionTopic; topic names can be long
    M5.Display.setTextSize(1);
    for (int i=vis0; i<vis0+VISIBLE_ITEMS && i<quizTopicCount; i++) {
      int  rowY = CONTENT_Y + (i-vis0)*ROW_H;
      bool sel  = (i==quizTopicIdx);
      if (sel) M5.Display.fillRect(0,rowY-1,232,ROW_H,COL_INFO);
      M5.Display.setTextColor(sel?BLACK:COL_TEXT, sel?COL_INFO:BLACK);
      if (sel) {
        // Selected: word-wrap up to 2 lines within ROW_H (9px × 2 = 18px)
        printWrapped(quizTopics[i], 4, rowY+1, 36, 2, 9);
      } else {
        // Unselected: single line vertically centred, up to 36 chars
        char trunc[37]; strlcpy(trunc, quizTopics[i], 37);
        M5.Display.setCursor(4, rowY+5);
        M5.Display.print(trunc);
      }
    }
    M5.Display.setTextSize(2);  // restore
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("[A] Kitar","[B] Pilih");
}

// v11: renderSessionTopic — topic picker for session start (SCREEN_SESSION_TOPIC).
// Shows this student's planned topics for the chosen subject + "Ulangkaji Bebas"
// sentinel at the bottom. The selected item also shows its profile in COL_DIM
// so the student sees what sensing mode they're about to use.
void renderSessionTopic() {
  char hdr[28]; snprintf(hdr,28,"Topik — %s",subjects[subjectSelectIdx]);
  clearDisplay(); drawHeader(hdr);

  if (sessionTopicCount == 0) {
    M5.Display.setTextColor(COL_WARN,BLACK); M5.Display.setTextSize(2);
    M5.Display.setCursor(4,CONTENT_Y+10);
    M5.Display.println("Tiada topik");
    M5.Display.setTextSize(1); M5.Display.setTextColor(COL_DIM,BLACK);
    M5.Display.setCursor(4,CONTENT_Y+34);
    M5.Display.println("Tambah topik dalam");
    M5.Display.println("perancangan dahulu.");
    M5.Display.setTextColor(COL_TEXT,BLACK);
  } else {
    int vis0 = scrollStart(sessionTopicIdx, sessionTopicCount);
    drawScrollBar(sessionTopicIdx, sessionTopicCount);
    // textSize(1) throughout — topic names can be long; 36 chars × 6px = 216px
    // fits the display and avoids the old 14-char hard cutoff.
    M5.Display.setTextSize(1);
    for (int i=vis0; i<vis0+VISIBLE_ITEMS && i<sessionTopicCount; i++) {
      int  rowY = CONTENT_Y + (i-vis0)*ROW_H;
      bool sel  = (i==sessionTopicIdx);
      if (sel) M5.Display.fillRect(0,rowY-1,232,ROW_H,COL_INFO);
      M5.Display.setTextColor(sel?BLACK:COL_TEXT, sel?COL_INFO:BLACK);
      if (sel) {
        // Selected: word-wrap up to 2 lines (9px × 2 = 18px = ROW_H exactly)
        printWrapped(sessionTopicNames[i], 4, rowY+1, 36, 2, 9);
        // Profile label at bottom-right — right-aligned so it clears line-2 text
        const char* pn = sessionTopicProfiles[i];
        M5.Display.setTextColor(COL_ACCENT, COL_INFO);
        M5.Display.setCursor(232 - (int)strlen(pn)*6, rowY+9);
        M5.Display.print(pn);
      } else {
        // Unselected: single line vertically centred in ROW_H, up to 36 chars
        char trunc[37]; strlcpy(trunc, sessionTopicNames[i], 37);
        M5.Display.setCursor(4, rowY+5);
        M5.Display.print(trunc);
      }
    }
    M5.Display.setTextSize(2);  // restore
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("[A] Kitar","[B] Mula Sesi");
}

// renderQuizQuestion — question + options screen
// v10.6: Quiz question screen redesigned to prevent option text cutoff.
// Question wraps at top (capped). All four options listed compact; the SELECTED
// option expands into a highlighted band showing its full text (wrapping up to
// 2 lines), so whatever the student is about to pick is always fully readable.
// Helper: print a string with word-wrap starting at (x,y), max `maxChars` per
// line and up to `maxLines` lines; returns the y after the last line drawn.
int printWrapped(const char* s, int x, int y, int maxChars, int maxLines, int lineH) {
  int len = strlen(s);
  int pos = 0, line = 0;
  while (pos < len && line < maxLines) {
    int take = len - pos;
    if (take > maxChars) {
      // try to break at the last space within the window
      take = maxChars;
      int brk = -1;
      for (int i = pos; i < pos + maxChars && i < len; i++)
        if (s[i] == ' ') brk = i;
      if (brk > pos) take = brk - pos;
    }
    char buf[48];
    int n = (take < 47) ? take : 47;
    strncpy(buf, s + pos, n); buf[n] = '\0';
    M5.Display.setCursor(x, y + line * lineH);
    M5.Display.print(buf);
    pos += take;
    while (pos < len && s[pos] == ' ') pos++;  // skip the space we broke on
    line++;
  }
  return y + line * lineH;
}

void renderQuizQuestion() {
  clearDisplay();
  int qIdx = quizState.currentIdx;
  char header[24];
  snprintf(header, sizeof(header), "Soalan %d/%d", qIdx+1, quizState.totalLoaded);
  drawHeader(header, COL_ACCENT);

  // Question text — wraps, capped at 3 lines (y=24..50, 9px line height)
  M5.Display.setTextSize(1); M5.Display.setTextColor(WHITE,BLACK);
  printWrapped(quizState.questions[qIdx].text, 0, CONTENT_Y, 40, 3, 9);

  // Divider under question
  M5.Display.drawFastHLine(0, 52, 240, COL_ACCENT);

  // Options region: y=55..113. Selected option gets an expanded highlighted band.
  const char* optLabels[4] = {"A","B","C","D"};
  int y = 55;
  for (int i=0;i<4;i++) {
    bool sel = (i==quizState.selectedOpt);
    const char* opt = quizState.questions[qIdx].opts[i];
    if (sel) {
      // Expanded: highlighted band, full text wrapped up to 2 lines
      int lines = (strlen(opt) > 36) ? 2 : 1;
      int bandH = lines*9 + 4;
      M5.Display.fillRect(0, y-1, 232, bandH, CYAN);
      M5.Display.setTextColor(BLACK, CYAN);
      char lead[3]; snprintf(lead,3,"%s.",optLabels[i]);
      M5.Display.setCursor(2, y+1); M5.Display.print(lead);
      printWrapped(opt, 16, y+1, 36, 2, 9);
      M5.Display.setTextColor(WHITE, BLACK);
      y += bandH + 1;
    } else {
      // Compact: one truncated line, dark grey
      M5.Display.setTextColor(DARKGREY, BLACK);
      M5.Display.setCursor(2, y);
      char buf[40]; snprintf(buf, sizeof(buf), "%s.%s", optLabels[i], opt);
      if (strlen(buf) > 39) buf[39] = '\0';
      M5.Display.print(buf);
      M5.Display.setTextColor(WHITE, BLACK);
      y += 10;
    }
  }
  drawFooter("[A] Kitar","[B] Jawab");
}

// renderQuizResult — brief correct/incorrect feedback (2 seconds auto-advance)
void renderQuizResult() {
  clearDisplay();
  int qIdx = quizState.currentIdx;
  if (quizState.resultCorrect) {
    drawHeader("Betul! ✓", COL_GOOD);
    M5.Display.setTextSize(3); M5.Display.setCursor(30,50);
    M5.Display.setTextColor(COL_GOOD,BLACK);
    M5.Display.println("BETUL!");
  } else {
    drawHeader("Salah ✗", COL_DANGER);
    M5.Display.setTextSize(2); M5.Display.setCursor(4,CONTENT_Y);
    M5.Display.setTextColor(COL_DANGER,BLACK);
    M5.Display.println("Salah.");
    M5.Display.setTextSize(1); M5.Display.setCursor(4,CONTENT_Y+20);
    M5.Display.setTextColor(COL_DIM,BLACK);
    M5.Display.println("Jawapan betul:");
    M5.Display.setTextColor(COL_GOOD,BLACK); M5.Display.setTextSize(1);
    const char* optLabels[4]={"A","B","C","D"};
    int ci = quizState.questions[qIdx].correctIndex;
    // Build "A. <full answer>" and wrap so long answers are not cut off
    char line[80];
    snprintf(line, sizeof(line), "%s. %s", optLabels[ci], quizState.questions[qIdx].opts[ci]);
    printWrapped(line, 4, CONTENT_Y+32, 40, 4, 10);
  }
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("","[B] Terus");

  // Auto-advance after 2 seconds
  if (millis() - quizState.resultShowTime >= 2000) {
    // v10.5: drift quiz result — resume monitoring from clean Stage 0
    if (driftResultActive) {
      driftResultActive = false;
      driftQuizActive   = false;
      lastDriftAlertMs  = 0;
      driftQuizStartMs  = 0;
      currentWarnStage  = WARN_S0;
      stageEnteredMs    = 0;
      inWarning         = false;
      snoozeActive      = false;
      currentState      = STATE_READING;
      currentScreen     = SCREEN_HOME;
      lastDisplayRefresh = 0;
      Serial.println("[QUIZ] Kuiz drift selesai — pantauan disambung dari Tahap 0");
      return;
    }
    quizState.currentIdx++;
    if (quizState.currentIdx >= quizState.totalLoaded) {
      currentScreen = SCREEN_QUIZ_SUMMARY;
    } else {
      quizState.selectedOpt = 0;
      currentScreen = SCREEN_QUIZ_QUESTION;
    }
    lastDisplayRefresh = 0;
  }
}

void renderQuizSummary() {
  clearDisplay();
  drawHeader("Tamat Kuiz!");
  M5.Display.setTextSize(1); M5.Display.setCursor(4,CONTENT_Y);
  M5.Display.setTextColor(COL_DIM,BLACK);
  M5.Display.printf("%s", subjects[quizSubjectIdx]);
  M5.Display.setTextColor(COL_TEXT,BLACK);
  M5.Display.setTextSize(3); M5.Display.setCursor(20,36);
  M5.Display.printf("%d / %d", quizState.score, quizState.totalLoaded);
  int pct = (quizState.totalLoaded > 0)
            ? (quizState.score * 100 / quizState.totalLoaded) : 0;
  M5.Display.setTextSize(2); M5.Display.setCursor(4,76);
  M5.Display.setTextColor(pct>=70?COL_GOOD:(pct>=40?COL_WARN:COL_DANGER), BLACK);
  M5.Display.printf("Markah: %d%%", pct);
  M5.Display.setTextColor(COL_TEXT,BLACK);
  drawFooter("[A] Ulang","[B] Keluar");
}

// ─── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  // M5Unified init — handles display, IMU (BMI270), buttons, power (M5PM1)
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.println("[BOOT] StudyAid v10.8 starting...");
  Serial.println("[BOOT] Hardware: M5StickS3 + M5 Unit NFC (ST25R3916)");

  M5.Display.setRotation(3);
  clearDisplay();
  // Boot splash — school identity
  M5.Display.fillRect(0,0,240,HDR_H,COL_ACCENT);
  M5.Display.setTextColor(COL_TEXT,COL_ACCENT); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,7); M5.Display.print("SMK Gudang Rasau (SEGRA)");
  M5.Display.setTextColor(COL_TEXT,BLACK);
  M5.Display.setTextSize(2); M5.Display.setCursor(30,36); M5.Display.println("StudyAid");
  M5.Display.setTextColor(COL_DIM,BLACK); M5.Display.setTextSize(1);
  M5.Display.setCursor(4,60); M5.Display.print("Sistem Pemantauan Tabiat Belajar");
  M5.Display.setCursor(192,60); M5.Display.print("v10.8");
  M5.Display.setTextColor(COL_TEXT,BLACK);
  delay(2000);

  // Speaker volume — set once at boot
  // Adjust 128 (0-255) if volume is too loud or too quiet on hardware
  M5.Speaker.setVolume(220);  // Increased from 128; max is 255
  M5.Speaker.begin();

  prefs.begin("studyaid",false);
  loadSettingsFromNVS();
  loadTagsFromNVS();
  loadHistoryFromNVS();

  calibrated=prefs.getBool("calibrated",false);
  if (calibrated) {
    calMagnitudeThresholdHigh=prefs.getFloat("cal_high",0.30f);
    calMagnitudeThresholdLow =prefs.getFloat("cal_low", 0.08f);
    Serial.printf("[CAL] Dimuat: Tinggi=%.3f Rendah=%.3f\n",
      calMagnitudeThresholdHigh,calMagnitudeThresholdLow);
  } else {
    Serial.printf("[CAL] Belum dikalibrasi. Ambang IMU: %.2f (TODO: retune untuk BMI270)\n",
      calMagnitudeThresholdHigh);
  }

  // IMU init — M5Unified handles BMI270 automatically
  M5.Imu.init();
  Serial.println("[IMU] BMI270 diinisialisasi melalui M5Unified");

  // WiFi — v9: branch on Solo vs Companion mode
  // Solo mode    : start AP (same as v8.4, no change)
  // Companion mode: connect as STA to laptop hotspot, skip AP and web server
  if (!companionMode) {
    M5.Display.setCursor(0,80); M5.Display.println("Memulakan WiFi (Solo)...");
    WiFi.softAP(WIFI_SSID,WIFI_PASSWORD);
    WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
    M5.Display.setTextColor(GREEN,BLACK);
    M5.Display.printf("WiFi: %s\nIP: %s\n",WIFI_SSID,WIFI_IP);
    M5.Display.setTextColor(WHITE,BLACK);
    setupWebServer();
  } else {
    initCompanionWiFi();
    // Web server not started in Companion mode — device is a client, not a host
  }

  // NFC init — M5 Unit NFC via Grove Port A (hardware I2C)
  // M5.getPin() resolves the correct SDA/SCL for Port A on the S3
  M5.Display.println("Memulakan NFC...");
  auto sda = M5.getPin(m5::pin_name_t::port_a_sda);
  auto scl = M5.getPin(m5::pin_name_t::port_a_scl);
  Serial.printf("[NFC] Port A: SDA=%d SCL=%d\n", sda, scl);
  Wire.begin(sda, scl);

  // Register the Unit NFC with the UnitUnified manager
  // Units.add() registers the unit; Units.begin() initialises all registered units.
  // Both calls are required — missing Units.begin() causes nfcaRequest() to always fail.
  if (Units.add(unitNFC, Wire) && Units.begin()) {
    nfcReady = true;
    M5.Display.setTextColor(GREEN,BLACK); M5.Display.println("NFC OK!");
    Serial.println("[NFC] ST25R3916 diinisialisasi OK");
  } else {
    M5.Display.setTextColor(RED,BLACK); M5.Display.println("NFC GAGAL!");
    Serial.println("[NFC] RALAT: Unit NFC tidak dijumpai. Semak sambungan Grove.");
    buzz_error();
    // Not halting — device can still run without NFC for IMU testing
  }
  M5.Display.setTextColor(WHITE,BLACK);

  lastMovementTime=millis();
  buzz_calibration();
  delay(1500);
  Serial.println("[BOOT] Sedia.");
  currentScreen=SCREEN_HOME;
}

// ─── Main Loop ─────────────────────────────────────────────────────────────
void loop() {
  M5.update();   // Updates buttons, IMU, power via M5Unified

  // Button handling — M5Unified API, same wasPressed() semantics as v7
  if (M5.BtnA.wasPressed()) handleBtnA();
  if (M5.BtnB.wasPressed()) handleBtnB();

  if (sessionActive) {
    if (!sessionPaused) {
      // v10.4: freeze monitoring entirely while drift quiz is waiting for answer
      if (driftQuizActive) {
        // v10.5: timeout — if unanswered after the mode-dependent limit, silently
        // dismiss and restore S3/WARNING so the Tidak Aktif escalation resumes
        // (a sleeping student who ignores the quiz still gets the sleeping alarm).
        unsigned long quizTimeout = demoMode ? DEMO_QUIZ_TIMEOUT_MS : REAL_QUIZ_TIMEOUT_MS;
        if (millis() - driftQuizStartMs >= quizTimeout) {
          driftQuizActive  = false;
          lastDriftAlertMs = 0;
          driftQuizStartMs = 0;
          // Restore S3 warning state so escalation timer continues from now
          currentWarnStage = WARN_S3;
          currentState     = STATE_WARNING;
          warningStartTime  = millis();
          stageEnteredMs    = millis();
          currentScreen    = SCREEN_HOME;
          lastDisplayRefresh = 0;
          Serial.println("[QUIZ] Kuiz drift tamat masa (tiada jawapan) — pantauan disambung");
        } else {
          // Repeating alert every 1.5s until student presses a button
          if (millis() - lastDriftAlertMs >= 1500) {
            lastDriftAlertMs = millis();
            buzz_driftAlert();
          }
        }
      } else {
        if (millis()-lastStateSample>=200) { lastStateSample=millis(); updateIMUState(); }
        if (currentState==STATE_SLEEPING&&millis()-lastSleepingBuzz>=SLEEPING_BUZZ_INTERVAL) {
          lastSleepingBuzz=millis(); buzz_sleeping();
        }
      }
    } else {
      unsigned long restElapsed=millis()-restTimerStart;
      if (!restTimerExpired&&restElapsed>=REST_DURATION_MS) {
        restTimerExpired=true; lastRestBuzz=millis(); buzz_restExpired();
        Serial.println("[REST] Masa tamat, tunggu sentuhan");
      }
      if (restTimerExpired&&millis()-lastRestBuzz>=3000) {
        lastRestBuzz=millis(); buzz_restExpired();
      }
    }
    handleNFCDuringSession();
  } else {
    if (millis()-lastStateSample>=200) { lastStateSample=millis(); updateIMUWindow(); }
    checkNFCOnHome();
  }

  if (millis()-lastDisplayRefresh>=DISPLAY_REFRESH_MS) {
    lastDisplayRefresh=millis(); renderCurrentScreen();
  }

  // v9.2: Periodic focus score report to companion server
  // Fires every 15 seconds during an active Companion mode session.
  // Uses postToServer() (fire-and-forget) so it never blocks the loop.
  if (sessionActive && companionReady && serverSessionId >= 0) {
    if (millis() - lastFocusReportMs >= (demoMode?5000UL:FOCUS_REPORT_INTERVAL_MS)) {
      lastFocusReportMs = millis();
      unsigned long elapsed = millis() - sessionStartTime - totalPausedMs;
      StaticJsonDocument<96> upDoc;
      upDoc["session_id"]  = serverSessionId;
      upDoc["focus_score"] = focusScore;
      upDoc["elapsed_sec"] = (long)(elapsed / 1000);
      String upBody; serializeJson(upDoc, upBody);
      postToServer("/api/session/update", upBody);
      Serial.printf("[v9.2] Fokus dilaporkan: %d%% (sesi %d)\n",
                    focusScore, serverSessionId);
    }
  }

  // v12.1: Periodic hello ping — keeps booth kiosk welcome screen alive.
  // Fires every 30s in companion mode regardless of session state.
  if (companionReady && (millis() - lastHelloMs >= HELLO_INTERVAL_MS)) {
    lastHelloMs = millis();
    StaticJsonDocument<64> hDoc;
    hDoc["device_id"] = deviceId;
    String hBody; serializeJson(hDoc, hBody);
    postToServer("/api/hello", hBody);
  }
}
