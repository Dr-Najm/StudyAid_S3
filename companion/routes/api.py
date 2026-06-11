"""
Device-facing HTTP API.
Week 3: /api/session/end now writes a real Session row and calculates adherence.
Other endpoints remain stubs until Week 5.
"""

from datetime import datetime
from flask import Blueprint, jsonify, request
from models.schema import db, Student, Session, WeeklySlot, DriftEvent

api_bp = Blueprint("api", __name__, url_prefix="/api")

# ISO datetime format sent by device
_DT_FMT = "%Y-%m-%dT%H:%M:%S"


def _log(tag, payload):
    print(f"[API] {tag}: {payload}")


# ── Session start (stub — real insert in Week 5 when drift is also handled) ──
@api_bp.route("/session/start", methods=["POST"])
def session_start():
    payload = request.get_json(silent=True) or {}
    _log("session/start", payload)
    return jsonify({"session_id": 1})


# ── Drift event (stub until Week 5) ─────────────────────────────────────────
@api_bp.route("/session/drift", methods=["POST"])
def session_drift():
    payload = request.get_json(silent=True) or {}
    _log("session/drift", payload)
    return jsonify({"quiz": None})


# ── Quiz answer (stub until Week 5) ─────────────────────────────────────────
@api_bp.route("/session/answer", methods=["POST"])
def session_answer():
    payload = request.get_json(silent=True) or {}
    _log("session/answer", payload)
    return jsonify({"correct": True})


# ── Session end — real logic ─────────────────────────────────────────────────
@api_bp.route("/session/end", methods=["POST"])
def session_end():
    payload = request.get_json(silent=True) or {}
    _log("session/end", payload)

    device_id   = payload.get("device_id", "")
    subject_id  = payload.get("subject_id")
    start_ts    = payload.get("start_ts")
    end_ts      = payload.get("end_ts")
    active_min  = int(payload.get("active_min", 0))
    idle_min    = int(payload.get("idle_min", 0))
    focus_score = float(payload.get("focus_score", 0))

    # ── Look up student by device_id ────────────────────────────────────────
    student = Student.query.filter_by(device_id=device_id).first()
    if not student:
        _log("session/end", f"AMARAN: device_id '{device_id}' tidak dijumpai")
        return jsonify({"ok": False, "error": "device not found"}), 404

    # ── Parse timestamps ────────────────────────────────────────────────────
    # Device sends epoch seconds (int). Accept both epoch and ISO string.
    def _parse_ts(val):
        if val is None:
            return datetime.utcnow()
        if isinstance(val, (int, float)):
            return datetime.utcfromtimestamp(val)
        try:
            return datetime.strptime(str(val), _DT_FMT)
        except ValueError:
            return datetime.utcnow()

    start_dt = _parse_ts(start_ts)
    end_dt   = _parse_ts(end_ts)

    # ── Write Session row ────────────────────────────────────────────────────
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

    # ── Adherence calculation ────────────────────────────────────────────────
    # Formula: actual_min × (focus_score / 100) / planned_min
    # Look for a matching weekly slot: same student, same subject, same day of week.
    # focus_score from device is 0–100 integer.
    adherence = None
    day_of_week = start_dt.weekday()  # 0=Monday … 6=Sunday
    matching_slot = WeeklySlot.query.filter_by(
        student_id=student.id,
        subject_id=subject_id,
        day_of_week=day_of_week,
    ).first()

    if matching_slot and matching_slot.duration_min > 0:
        quality_min = active_min * (focus_score / 100.0)
        adherence   = round(quality_min / matching_slot.duration_min, 3)
        adherence   = min(adherence, 1.0)  # cap at 100%
        _log("session/end",
             f"Pematuhan: {adherence:.1%} "
             f"(aktif={active_min}min, fokus={focus_score}%, "
             f"dirancang={matching_slot.duration_min}min)")
    else:
        _log("session/end", "Tiada slot mingguan sepadan — pematuhan tidak dikira")

    return jsonify({"ok": True, "session_id": session.id, "adherence": adherence})
