"""
Device-facing HTTP API.
Week 3: /api/session/end — real DB write + adherence.
Week 5: /api/session/start — real Session row creation.
        /api/session/drift — real DriftEvent logging + quiz trigger.
        /api/session/answer — real QuizAnswer logging.
        /api/quiz/questions — fetch question set for Quiz Mode.
"""

import json
import random
from datetime import datetime, timedelta
from flask import Blueprint, jsonify, request
from models.schema import (db, Student, Subject, Session, WeeklySlot,
                            DriftEvent, QuizBank, QuizQuestion, QuizAnswer,
                            TopicDeadline)

api_bp = Blueprint("api", __name__, url_prefix="/api")

# ISO datetime format sent by device
_DT_FMT = "%Y-%m-%dT%H:%M:%S"

# ── In-memory drift tracking ─────────────────────────────────────────────────
# Tracks recent drift timestamps per session_id.
# Structure: { session_id: [timestamp, timestamp, ...] }
# Kept in memory (not DB) — resets if server restarts, which is acceptable
# since the device will re-establish context on next drift POST.
# Quiz cooldown tracked separately: { session_id: last_quiz_ts }
_drift_log   = {}   # session_id -> list of datetime
_quiz_cooldown = {}  # session_id -> datetime of last quiz trigger

QUIZ_COOLDOWN_SECONDS = 15   # v10.5: loose server backstop only — the device is
                             # the primary cooldown authority (mode-aware: 30s demo /
                             # 10min real). This short guard only prevents a
                             # malfunctioning device from spamming quiz triggers.


def _log(tag, payload):
    print(f"[API] {tag}: {payload}")


def _parse_ts(val):
    """Accept epoch int/float or ISO string, return datetime."""
    if val is None:
        return datetime.utcnow()
    if isinstance(val, (int, float)):
        return datetime.utcfromtimestamp(val)
    try:
        return datetime.strptime(str(val), _DT_FMT)
    except ValueError:
        return datetime.utcnow()


def _pick_question(subject_id, exclude_ids=None, difficulty=None,
                   allow_cross_subject=False):
    """
    Pick one random question for a drift quiz, scoped to subject_id.

    By default this is STRICT: only questions belonging to subject_id's banks are
    eligible. If the subject has no questions, returns None (no quiz) rather than
    serving an unrelated subject's question. Set allow_cross_subject=True only if
    you explicitly want the any-bank fallback.

    If difficulty is given (1=easy, 2=hard), prefer it, falling back to any
    difficulty within the same subject.
    Returns a compact dict or None.
    """
    exclude_ids = exclude_ids or []

    def _from_banks(banks):
        all_qs = []
        for bank in banks:
            qs = QuizQuestion.query.filter_by(bank_id=bank.id).all()
            all_qs.extend(qs)
        if not all_qs:
            return None

        if difficulty is not None:
            candidates = [q for q in all_qs
                          if q.difficulty == difficulty and q.id not in exclude_ids]
            if not candidates:
                candidates = [q for q in all_qs if q.difficulty == difficulty]
            if not candidates:
                candidates = [q for q in all_qs if q.id not in exclude_ids]
        else:
            candidates = [q for q in all_qs if q.id not in exclude_ids]

        if not candidates:
            candidates = all_qs  # all answered already — allow repeats within subject
        return _format_question(random.choice(candidates)) if candidates else None

    # Strict: only this subject's banks
    if subject_id is not None:
        banks = QuizBank.query.filter_by(subject_id=subject_id).all()
        result = _from_banks(banks)
        if result:
            return result

    # Optional cross-subject fallback (off by default)
    if allow_cross_subject:
        return _from_banks(QuizBank.query.all())

    return None


def _format_question(q):
    """Return compact question dict for device JSON response."""
    return {
        "id": q.id,
        "q":  q.question_text,
        "o":  json.loads(q.options_json),
        "c":  q.correct_index,
    }


# ── Session start ────────────────────────────────────────────────────────────
@api_bp.route("/session/start", methods=["POST"])
def session_start():
    payload    = request.get_json(silent=True) or {}
    _log("session/start", payload)

    device_id  = payload.get("device_id", "")
    subject_id = payload.get("subject_id")
    start_ts   = payload.get("start_ts")
    # v11: device sends topic name chosen at session start (empty string = free study)
    topic      = payload.get("topic", "").strip()

    # v12.1: Mod Kuiz sends is_quiz=true so the server can exclude these
    # from the live monitor and booth state (they are not real study sessions)
    is_quiz = bool(payload.get("is_quiz", False))

    student = Student.query.filter_by(device_id=device_id).first()
    if not student:
        _log("session/start", f"AMARAN: device_id '{device_id}' tidak dijumpai")
        return jsonify({"session_id": None, "error": "device not found"}), 404

    session = Session(
        student_id  = student.id,
        subject_id  = subject_id,
        start_ts    = datetime.utcnow(),  # Option A: server timestamp, device has no RTC
        end_ts      = None,
        active_min  = 0,
        idle_min    = 0,
        focus_score = 0.0,
        is_quiz     = is_quiz,
    )
    db.session.add(session)
    db.session.commit()

    # v11: look up the pre-computed profile for this topic.
    # Falls back to "Campuran" if no topic provided or topic not found in
    # this student's planner (e.g. free-study / Ulangkaji Bebas).
    profile = "Campuran"
    if topic:
        td = TopicDeadline.query.filter_by(
            student_id=student.id,
            subject_id=subject_id,
            topic=topic,
        ).first()
        if td:
            profile = td.profile

    _log("session/start",
         f"Sesi ID {session.id} dimulakan untuk {student.name} "
         f"(subjek {subject_id}, topik='{topic}', profil={profile})")

    # Initialise drift log for this session
    _drift_log[session.id] = []

    return jsonify({"session_id": session.id, "profile": profile})


# ── Drift event ──────────────────────────────────────────────────────────────
@api_bp.route("/session/drift", methods=["POST"])
def session_drift():
    payload    = request.get_json(silent=True) or {}
    _log("session/drift", payload)

    session_id = payload.get("session_id")
    severity   = payload.get("severity", "warning")
    ts         = payload.get("ts")
    # quiz_window_ms sent by device: 60000 / 180000 / 300000 (1/3/5 min)
    quiz_window_ms = int(payload.get("quiz_window_ms", 180000))

    # ── Log DriftEvent to DB ─────────────────────────────────────────────────
    session = db.session.get(Session, session_id)
    if session:
        db.session.add(DriftEvent(
            session_id=session_id,
            ts=_parse_ts(ts),
            severity=severity,
        ))
        db.session.commit()

    # ── Quiz trigger logic ───────────────────────────────────────────────────
    # Maintain per-session drift timestamp list in memory.
    # Count warnings within quiz_window_ms; trigger if >= 2.
    now = datetime.utcnow()
    if session_id not in _drift_log:
        _drift_log[session_id] = []

    _drift_log[session_id].append(now)

    # Prune timestamps outside the window
    window_secs = quiz_window_ms / 1000.0
    _drift_log[session_id] = [
        t for t in _drift_log[session_id]
        if (now - t).total_seconds() <= window_secs
    ]

    recent_count = len(_drift_log[session_id])
    _log("session/drift",
         f"Sesi {session_id}: {recent_count} amaran dalam "
         f"{window_secs:.0f}s tetingkap")

    # Check cooldown
    last_quiz = _quiz_cooldown.get(session_id)
    cooldown_ok = (
        last_quiz is None or
        (now - last_quiz).total_seconds() >= QUIZ_COOLDOWN_SECONDS
    )

    # v10.5: The DEVICE decides when to trigger (mode-aware: every-S3 in demo,
    # 2-within-window in real). When severity == "quiz_trigger", the device has
    # already made that decision — the server simply honours it, gated only by
    # the loose backstop cooldown. "warning" severity is logged but never fires.
    device_requested_quiz = (severity == "quiz_trigger")

    if device_requested_quiz and cooldown_ok:
        # Determine subject from session
        subject_id = session.subject_id if session else None

        # Find questions already answered this session (avoid repeats)
        answered_ids = [
            a.question_id for a in
            QuizAnswer.query.filter_by(session_id=session_id).all()
        ]

        # v10.4: random question (drift fires only when still; difficulty not applicable)
        question = _pick_question(subject_id, exclude_ids=answered_ids)
        if question:
            _quiz_cooldown[session_id] = now
            _drift_log[session_id] = []   # reset window after trigger
            _log("session/drift",
                 f"Kuiz dicetuskan sesi {session_id}: soalan {question['id']} (rawak)")
            return jsonify({"quiz": question})

    return jsonify({"quiz": None})


# ── Quiz answer ──────────────────────────────────────────────────────────────
@api_bp.route("/session/answer", methods=["POST"])
def session_answer():
    payload    = request.get_json(silent=True) or {}
    _log("session/answer", payload)

    session_id    = payload.get("session_id")
    question_id   = payload.get("question_id")
    chosen_index  = payload.get("chosen_index", 0)

    # Look up correct answer
    question = db.session.get(QuizQuestion, question_id)
    if not question:
        _log("session/answer", f"Soalan ID {question_id} tidak dijumpai")
        return jsonify({"correct": False, "error": "question not found"}), 404

    correct = (chosen_index == question.correct_index)

    # Store QuizAnswer row.
    # session_id may be None for drift quizzes outside a tracked session —
    # store the answer anyway so quiz accuracy stats are captured.
    # session_id=-1 from firmware means server session not yet assigned; treat as None.
    stored_session_id = session_id if (session_id and session_id > 0) else None
    db.session.add(QuizAnswer(
        session_id    = stored_session_id,
        question_id   = question_id,
        chosen_index  = chosen_index,
        correct       = correct,
        ts            = datetime.utcnow(),
    ))
    db.session.commit()

    _log("session/answer",
         f"Sesi {session_id} soalan {question_id}: "
         f"pilihan={chosen_index} betul={correct}")

    return jsonify({"correct": correct})


# ── Quiz topics — topic list for a subject ────────────────────────────────────
@api_bp.route("/quiz/topics", methods=["GET"])
def quiz_topics():
    """
    Return topics available for a subject, for display on the device topic picker.

    v11 behaviour: if device_id is provided, returns this student's planned topics
    from TopicDeadline (which carry a pre-computed profile). Always appends a
    "Ulangkaji Bebas" sentinel as the last item so the device always offers a
    free-study fallback with no specific topic.

    If no device_id is provided, falls back to QuizBank topics (all banks for the
    subject) — preserves backward compatibility for contexts where device_id is
    not available.

    Query params:
        subject_id : int (required)
        device_id  : str (optional — if given, filters to this student's planned topics)
    Response:
        {"topics": [{"topic": "Kemerdekaan Malaysia", "profile": "Membaca"}, ...,
                    {"topic": "Ulangkaji Bebas", "profile": "Campuran"}]}
    """
    subject_id = request.args.get("subject_id", type=int)
    device_id  = request.args.get("device_id",  type=str)

    if subject_id is None:
        return jsonify({"error": "subject_id required"}), 400

    topics = []

    if device_id:
        # v11: return this student's planned topics with their classified profiles
        student = Student.query.filter_by(device_id=device_id).first()
        if student:
            planned = (TopicDeadline.query
                .filter_by(student_id=student.id, subject_id=subject_id)
                .order_by(TopicDeadline.topic)
                .all())
            topics = [{"topic": td.topic, "profile": td.profile} for td in planned]
        # else: student not found — topics stays [], sentinel still appended below

    if not topics:
        # Fallback: all QuizBank topics for this subject (no profile info available
        # at this level — Campuran is the safe default)
        banks  = QuizBank.query.filter_by(subject_id=subject_id)\
            .order_by(QuizBank.topic).all()
        topics = [{"topic": b.topic, "profile": "Campuran"} for b in banks]

    # Always append the free-study sentinel so the device always has a fallback
    topics.append({"topic": "Ulangkaji Bebas", "profile": "Campuran"})

    _log("quiz/topics",
         f"Subjek {subject_id} device '{device_id}': {len(topics)} topik "
         f"(termasuk Ulangkaji Bebas)")
    return jsonify({"topics": topics})


# ── Quiz questions — Quiz Mode fetch ─────────────────────────────────────────
@api_bp.route("/quiz/questions", methods=["GET"])
def quiz_questions():
    """
    Fetch up to 10 random questions for Quiz Mode.
    Query params:
        subject_id  : int  (required)
        topic       : str  (optional — filter to a specific quiz bank topic)
        session_id  : int  (optional — used to exclude already-answered Qs)
    Returns compact JSON to minimise device parse time.
    """
    subject_id = request.args.get("subject_id", type=int)
    topic      = request.args.get("topic",      type=str)
    session_id = request.args.get("session_id", type=int)

    if subject_id is None:
        return jsonify({"error": "subject_id required"}), 400

    # Exclude questions already answered this session
    answered_ids = []
    if session_id:
        answered_ids = [
            a.question_id for a in
            QuizAnswer.query.filter_by(session_id=session_id).all()
        ]

    # Filter banks by subject, and by topic if provided
    bank_query = QuizBank.query.filter_by(subject_id=subject_id)
    if topic:
        bank_query = bank_query.filter_by(topic=topic)
    banks = bank_query.all()

    # If no topic-specific bank found, fall back to all banks FOR THIS SUBJECT only.
    # v10.7: do NOT fall back to other subjects' banks — that leaked unrelated
    # questions into a subject's quiz. Strict subject scoping.
    if not banks:
        banks = QuizBank.query.filter_by(subject_id=subject_id).all()

    all_qs = []
    for bank in banks:
        qs = QuizQuestion.query.filter_by(bank_id=bank.id).all()
        all_qs.extend(qs)

    # Prefer unanswered questions
    unanswered = [q for q in all_qs if q.id not in answered_ids]
    pool = unanswered if unanswered else all_qs

    if not pool:
        _log("quiz/questions",
             f"Tiada soalan untuk subjek {subject_id} topik '{topic}'")
        return jsonify({"questions": [], "error": "no questions found"})

    selected = random.sample(pool, min(10, len(pool)))
    questions = [_format_question(q) for q in selected]

    _log("quiz/questions",
         f"Subjek {subject_id} topik '{topic}': {len(questions)} soalan dihantar "
         f"(sesi {session_id})")

    return jsonify({"questions": questions})


# ── Session update — periodic focus score from device ────────────────────────
@api_bp.route("/session/update", methods=["POST"])
def session_update():
    """
    Called by device every 15 seconds during an active session.
    Updates focus_score on the Session row so /api/live always returns
    the latest value without waiting for session end.
    """
    payload     = request.get_json(silent=True) or {}
    session_id  = payload.get("session_id")
    focus_score = payload.get("focus_score")
    elapsed_sec = payload.get("elapsed_sec", 0)

    _log("session/update", payload)

    session = db.session.get(Session, session_id) if session_id else None
    if session and focus_score is not None:
        session.focus_score = float(focus_score)
        # Update active_min from elapsed_sec for live display accuracy
        session.active_min  = max(session.active_min, int(elapsed_sec // 60))
        db.session.commit()
        return jsonify({"ok": True})

    return jsonify({"ok": False, "error": "session not found"}), 404


# ── Live session status — polled by browser every 3 seconds ──────────────────
@api_bp.route("/live", methods=["GET"])
def live():
    """
    Returns the current active session for a student.
    A session is "active" when end_ts IS NULL (started but not ended).
    Polled by /student/live every 3 seconds via JS fetch.
    """
    student_id = request.args.get("student_id", type=int)

    # Resolve student
    if student_id:
        student = db.session.get(Student, student_id)
    else:
        student = Student.query.order_by(Student.id).first()

    if not student:
        return jsonify({"active": False, "error": "student not found"})

    # Find active session (end_ts is NULL)
    # Guard: treat sessions open for more than 8 hours as stale (device crashed,
    # WiFi dropped, or session/end POST failed). Auto-close them.
    MAX_SESSION_HOURS = 8
    active_session = (Session.query
        .filter_by(student_id=student.id)
        .filter(Session.end_ts == None)   # noqa: E711
        .filter(Session.is_quiz == False)  # noqa: E712 — exclude Mod Kuiz sessions
        .order_by(Session.start_ts.desc())
        .first())

    if active_session:
        age_hours = (datetime.utcnow() - active_session.start_ts).total_seconds() / 3600
        if age_hours > MAX_SESSION_HOURS:
            # Auto-close stale session
            active_session.end_ts = datetime.utcnow()
            db.session.commit()
            _log("live", f"Sesi {active_session.id} ditutup secara automatik "
                          f"(terbuka {age_hours:.1f} jam)")
            active_session = None

    if not active_session:
        return jsonify({"active": False})

    # Compute elapsed time server-side
    now         = datetime.utcnow()
    elapsed_sec = int((now - active_session.start_ts).total_seconds())
    elapsed_min = elapsed_sec // 60
    elapsed_s   = elapsed_sec % 60

    # Subject name
    subj = db.session.get(Subject, active_session.subject_id)
    subject_name = subj.name_bm if subj else f"Subjek {active_session.subject_id}"

    # Drift events this session
    drift_events_raw = (DriftEvent.query
        .filter_by(session_id=active_session.id)
        .order_by(DriftEvent.ts.desc())
        .all())
    drift_count = len(drift_events_raw)

    # Last 5 drift events for the log
    drift_log = [{
        "ts":       d.ts.strftime("%H:%M:%S"),
        "severity": d.severity,
    } for d in drift_events_raw[:5]]

    # Quiz answers this session
    quiz_count = QuizAnswer.query.filter_by(
        session_id=active_session.id).count()

    return jsonify({
        "active":       True,
        "session_id":   active_session.id,
        "student_name": student.name,
        "subject":      subject_name,
        "start_ts":     active_session.start_ts.strftime("%H:%M"),
        "elapsed_min":  elapsed_min,
        "elapsed_sec":  elapsed_s,
        "focus_score":  round(active_session.focus_score),
        "drift_count":  drift_count,
        "quiz_count":   quiz_count,
        "drift_log":    drift_log,
    })


# ── Close stale sessions — call this if live monitor gets stuck ──────────────
@api_bp.route("/session/close_stale", methods=["POST"])
def close_stale():
    """
    Closes all sessions where end_ts IS NULL.
    Call this from browser console or curl if the live monitor shows a session
    that has already ended on the device:
        fetch('/api/session/close_stale', {method:'POST'})
    """
    stale = Session.query.filter(Session.end_ts == None).all()  # noqa: E711
    count = len(stale)
    now   = datetime.utcnow()
    for s in stale:
        s.end_ts = now
    if stale:
        db.session.commit()
    _log("close_stale", f"{count} sesi lapuk ditutup")
    return jsonify({"ok": True, "closed": count})


# ── Session end ───────────────────────────────────────────────────────────────
@api_bp.route("/session/end", methods=["POST"])
def session_end():
    payload     = request.get_json(silent=True) or {}
    _log("session/end", payload)

    device_id   = payload.get("device_id", "")
    subject_id  = payload.get("subject_id")
    start_ts    = payload.get("start_ts")
    end_ts      = payload.get("end_ts")
    active_min  = int(payload.get("active_min", 0))
    idle_min    = int(payload.get("idle_min", 0))
    focus_score = float(payload.get("focus_score", 0))
    session_id  = payload.get("session_id")  # v9.1: device sends session_id

    student = Student.query.filter_by(device_id=device_id).first()
    if not student:
        _log("session/end", f"AMARAN: device_id '{device_id}' tidak dijumpai")
        return jsonify({"ok": False, "error": "device not found"}), 404

    now      = datetime.utcnow()
    # Option A: server provides timestamps. Reconstruct start from end minus duration.
    duration = timedelta(minutes=active_min + idle_min)
    end_dt   = now
    start_dt = now - duration if duration.total_seconds() > 0 else now

    # Normalise session_id: device sends -1 if server session was never assigned.
    valid_sid = session_id if (session_id and session_id > 0) else None

    # Find the session to close.
    # 1. Try the explicit session_id from the device.
    # 2. Fall back to the most recent OPEN session for this student
    #    (handles the case where serverSessionId was lost/never assigned).
    session = db.session.get(Session, valid_sid) if valid_sid else None
    if not session:
        session = (Session.query
            .filter_by(student_id=student.id)
            .filter(Session.end_ts == None)  # noqa: E711
            .order_by(Session.start_ts.desc())
            .first())
        if session:
            _log("session/end",
                 f"session_id={session_id} tidak sah, menutup sesi terbuka "
                 f"terkini (ID {session.id})")

    if session:
        if session.start_ts and session.start_ts.year > 1970:
            start_dt = session.start_ts
        else:
            session.start_ts = start_dt
        session.end_ts      = end_dt
        session.active_min  = active_min
        session.idle_min    = idle_min
        session.focus_score = focus_score
        _log("session/end", f"Sesi {session.id} DITUTUP (end_ts={end_dt})")
    else:
        session = Session(
            student_id  = student.id,
            subject_id  = subject_id,
            start_ts    = start_dt,
            end_ts      = end_dt,
            active_min  = active_min,
            idle_min    = idle_min,
            focus_score = focus_score,
        )
        db.session.add(session)
        _log("session/end", "Tiada sesi terbuka — cipta sesi baharu yang sudah ditutup")

    db.session.commit()
    _log("session/end", f"Sesi ID {session.id} disimpan untuk {student.name}")

    # Clean up in-memory drift log
    _drift_log.pop(session.id, None)
    _quiz_cooldown.pop(session.id, None)

    # Adherence calculation
    adherence   = None
    day_of_week = start_dt.weekday()
    slot = WeeklySlot.query.filter_by(
        student_id=student.id,
        subject_id=subject_id,
        day_of_week=day_of_week,
    ).first()

    if slot and slot.duration_min > 0:
        quality_min = active_min * (focus_score / 100.0)
        adherence   = round(min(quality_min / slot.duration_min, 1.0), 3)
        _log("session/end",
             f"Pematuhan: {adherence:.1%} "
             f"(aktif={active_min}min, fokus={focus_score}%, "
             f"dirancang={slot.duration_min}min)")

    return jsonify({"ok": True, "session_id": session.id, "adherence": adherence})


# ── Booth presence ────────────────────────────────────────────────────────────
# In-memory: { device_id: datetime }  resets on server restart.
_booth_presence = {}
BOOTH_PRESENCE_TIMEOUT_S = 180   # 3 min no ping -> device gone

_MOTIVASI = [
    "Setiap minit belajar membina masa depan anda.",
    "Konsisten adalah kunci kejayaan SPM.",
    "Fokus hari ini, kejayaan esok.",
    "Pelajar terbaik bukan yang paling bijak, tapi yang paling gigih.",
    "Mulakan dengan langkah pertama — StudyAid akan pandu anda.",
]
_motivasi_idx = 0


@api_bp.route("/hello", methods=["POST"])
def device_hello():
    """
    Device calls this immediately after WiFi connects, then every 30s.
    Records presence so the /booth page can greet the student.
    """
    global _motivasi_idx
    payload   = request.get_json(silent=True) or {}
    device_id = payload.get("device_id", "")
    if not device_id:
        return jsonify({"ok": False, "error": "device_id required"}), 400
    _booth_presence[device_id] = datetime.utcnow()
    _log("hello", f"{device_id} pinged")
    return jsonify({"ok": True})


@api_bp.route("/booth/state", methods=["GET"])
def booth_state():
    """
    Kiosk polls this every 3s to decide what to display.
    Returns one of three states: 'live', 'welcome', 'idle'.
    """
    global _motivasi_idx
    now = datetime.utcnow()

    # ── State 1: live — any active session ───────────────────────────────────
    active = (Session.query
              .filter(Session.end_ts.is_(None))
              .filter(Session.is_quiz == False)   # noqa: E712 — exclude Mod Kuiz sessions
              .order_by(Session.start_ts.desc())
              .first())
    if active:
        student = db.session.get(Student, active.student_id)
        subject = db.session.get(Subject, active.subject_id)
        elapsed_sec = int((now - active.start_ts).total_seconds())
        drift_events = (DriftEvent.query
                        .filter_by(session_id=active.id)
                        .order_by(DriftEvent.ts.desc()).all())
        drift_log = [{"ts": d.ts.strftime("%H:%M:%S"), "severity": d.severity}
                     for d in drift_events[:5]]
        return jsonify({
            "state":        "live",
            "student_name": student.name if student else "",
            "subject":      subject.name_bm if subject else "",
            "start_ts":     active.start_ts.strftime("%H:%M"),
            "elapsed_min":  elapsed_sec // 60,
            "elapsed_sec":  elapsed_sec % 60,
            "focus_score":  round(active.focus_score or 0),
            "drift_count":  len(drift_events),
            "quiz_count":   QuizAnswer.query.filter_by(session_id=active.id).count(),
            "drift_log":    drift_log,
        })

    # ── State 2: welcome — device present but no session ─────────────────────
    # Find most recent ping within timeout window
    recent = {did: ts for did, ts in _booth_presence.items()
              if (now - ts).total_seconds() < BOOTH_PRESENCE_TIMEOUT_S}
    if recent:
        latest_did = max(recent, key=lambda d: recent[d])
        student = Student.query.filter_by(device_id=latest_did).first()
        if student:
            first_name = student.name.split()[0]   # "Muhammad Khalish" → "Muhammad"
            # Use first name only (first token); adjust if you prefer given name
            # Today's weekly slots
            today_dow = now.weekday()   # 0=Mon … 6=Sun
            slots = (WeeklySlot.query
                     .filter_by(student_id=student.id, day_of_week=today_dow)
                     .order_by(WeeklySlot.start_time).all())
            today_plan = [{"subject": db.session.get(Subject, s.subject_id).name_bm,
                           "time":    s.start_time,
                           "dur":     s.duration_min}
                          for s in slots]

            # Nearest upcoming deadline
            nearest = (TopicDeadline.query
                       .filter_by(student_id=student.id)
                       .filter(TopicDeadline.deadline >= now.date())
                       .order_by(TopicDeadline.deadline).first())
            deadline_info = None
            if nearest:
                days_left = (nearest.deadline - now.date()).days
                subj = db.session.get(Subject, nearest.subject_id)
                deadline_info = {
                    "subject": subj.name_bm if subj else "",
                    "topic":   nearest.topic,
                    "days":    days_left,
                }

            # Streak
            streak = 0
            check  = now.date() - timedelta(days=1)
            for _ in range(30):
                had = Session.query.filter_by(student_id=student.id).filter(
                    db.func.date(Session.start_ts) == check).first()
                if had:
                    streak += 1
                    check -= timedelta(days=1)
                else:
                    break
            if streak > 0:
                motivasi = f"{streak} hari berturut-turut — teruskan semangat!"
            else:
                motivasi = _MOTIVASI[_motivasi_idx % len(_MOTIVASI)]
                _motivasi_idx += 1

            return jsonify({
                "state":        "welcome",
                "first_name":   first_name,
                "full_name":    student.name,
                "today_plan":   today_plan,
                "deadline":     deadline_info,
                "motivasi":     motivasi,
                "streak":       streak,
            })

    # ── State 3: idle ─────────────────────────────────────────────────────────
    return jsonify({"state": "idle"})
