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
                            DriftEvent, QuizBank, QuizQuestion, QuizAnswer)

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

QUIZ_COOLDOWN_SECONDS = 600   # 10 minutes between drift quizzes per session


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


def _pick_question(subject_id, exclude_ids=None):
    """
    Pick one random question for a drift quiz.
    Tries subject_id first; falls back to any available bank if none found.
    Returns a compact dict or None.
    """
    exclude_ids = exclude_ids or []

    def _from_banks(banks):
        all_qs = []
        for bank in banks:
            qs = QuizQuestion.query.filter_by(bank_id=bank.id).all()
            all_qs.extend(qs)
        candidates = [q for q in all_qs if q.id not in exclude_ids]
        if not candidates:
            candidates = all_qs  # ignore exclusions if pool exhausted
        if not candidates:
            return None
        q = random.choice(candidates)
        return _format_question(q)

    # Try matching subject first
    banks = QuizBank.query.filter_by(subject_id=subject_id).all()
    if banks:
        result = _from_banks(banks)
        if result:
            return result

    # Fallback: any bank
    all_banks = QuizBank.query.all()
    return _from_banks(all_banks)


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
    )
    db.session.add(session)
    db.session.commit()
    _log("session/start",
         f"Sesi ID {session.id} dimulakan untuk {student.name} "
         f"(subjek {subject_id})")

    # Initialise drift log for this session
    _drift_log[session.id] = []

    return jsonify({"session_id": session.id})


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

    if recent_count >= 2 and cooldown_ok:
        # Determine subject from session
        subject_id = session.subject_id if session else None

        # Find questions already answered this session (avoid repeats)
        answered_ids = [
            a.question_id for a in
            QuizAnswer.query.filter_by(session_id=session_id).all()
        ]

        question = _pick_question(subject_id, exclude_ids=answered_ids)
        if question:
            _quiz_cooldown[session_id] = now
            _drift_log[session_id] = []   # reset window after trigger
            _log("session/drift",
                 f"Kuiz dicetuskan untuk sesi {session_id}: soalan {question['id']}")
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


# ── Quiz questions — Quiz Mode fetch ─────────────────────────────────────────
@api_bp.route("/quiz/questions", methods=["GET"])
def quiz_questions():
    """
    Fetch up to 10 random questions for Quiz Mode.
    Query params:
        subject_id  : int  (required)
        session_id  : int  (optional — used to exclude already-answered Qs)
    Returns compact JSON to minimise device parse time.
    """
    subject_id = request.args.get("subject_id", type=int)
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

    # Try subject-specific banks first, fall back to any bank
    banks = QuizBank.query.filter_by(subject_id=subject_id).all()
    if not banks:
        banks = QuizBank.query.all()

    all_qs = []
    for bank in banks:
        qs = QuizQuestion.query.filter_by(bank_id=bank.id).all()
        all_qs.extend(qs)

    # Prefer unanswered questions
    unanswered = [q for q in all_qs if q.id not in answered_ids]
    pool = unanswered if unanswered else all_qs

    if not pool:
        _log("quiz/questions",
             f"Tiada soalan untuk subjek {subject_id}")
        return jsonify({"questions": [], "error": "no questions found"})

    selected = random.sample(pool, min(10, len(pool)))
    questions = [_format_question(q) for q in selected]

    _log("quiz/questions",
         f"Subjek {subject_id}: {len(questions)} soalan dihantar "
         f"(sesi {session_id})")

    return jsonify({"questions": questions})


# ── Session end ──────────────────────────────────────────────────────────────
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

    # Update existing Session row if session_id provided, else create new
    session = db.session.get(Session, session_id) if session_id else None
    if session:
        if session.start_ts and session.start_ts.year > 1970:
            # Keep original start_ts if it was set correctly
            start_dt = session.start_ts
        else:
            session.start_ts = start_dt
        session.end_ts      = end_dt
        session.active_min  = active_min
        session.idle_min    = idle_min
        session.focus_score = focus_score
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
