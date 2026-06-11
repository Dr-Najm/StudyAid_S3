"""
Student-facing routes.
Week 3: full study planner — weekly slots + topic deadlines.
Week 4: student progress dashboard — adherence, streak, charts.
"""

from datetime import date, datetime, timedelta
from flask import Blueprint, flash, jsonify, redirect, render_template, request, url_for
from models.schema import db, Student, Subject, WeeklySlot, TopicDeadline, Session

student_bp = Blueprint("student", __name__, url_prefix="/student")

# ── Helpers ──────────────────────────────────────────────────────────────────

def _week_bounds(today=None):
    """Return (monday, sunday) for the calendar week containing today."""
    if today is None:
        today = date.today()
    monday = today - timedelta(days=today.weekday())
    sunday = monday + timedelta(days=6)
    return monday, sunday


def _day_name(weekday_int):
    """Return BM day name for weekday integer (0=Monday)."""
    return ["Isnin","Selasa","Rabu","Khamis","Jumaat","Sabtu","Ahad"][weekday_int]


def _build_dashboard(student):
    """
    Compute all dashboard metrics for the given student.
    Returns a dict consumed directly by the template.
    """
    today      = date.today()
    monday, sunday = _week_bounds(today)
    monday_dt  = datetime.combine(monday, datetime.min.time())
    sunday_dt  = datetime.combine(sunday, datetime.max.time())

    # ── All sessions this week ───────────────────────────────────────────────
    week_sessions = (
        Session.query
        .filter(
            Session.student_id == student.id,
            Session.start_ts >= monday_dt,
            Session.start_ts <= sunday_dt,
        )
        .all()
    )

    # ── All sessions ever ────────────────────────────────────────────────────
    all_sessions = Session.query.filter_by(student_id=student.id).all()

    # ── Weekly slots (planned minutes per day) ───────────────────────────────
    slots = WeeklySlot.query.filter_by(student_id=student.id).all()
    planned_per_day = [0] * 7   # index = day_of_week (0=Mon)
    for s in slots:
        planned_per_day[s.day_of_week] += s.duration_min

    # ── Actual minutes per day this week ─────────────────────────────────────
    actual_per_day = [0] * 7
    for s in week_sessions:
        dow = s.start_ts.weekday()
        actual_per_day[dow] += s.active_min

    # ── Chart 1 data — Dirancang vs Aktual ───────────────────────────────────
    day_labels   = [_day_name(i) for i in range(7)]
    chart1 = {
        "labels":   day_labels,
        "planned":  planned_per_day,
        "actual":   actual_per_day,
    }

    # ── Chart 2 data — Masa per Subjek (doughnut) ────────────────────────────
    subject_minutes = {}
    for s in week_sessions:
        subj = db.session.get(Subject, s.subject_id)
        name = subj.name_bm if subj else f"Subjek {s.subject_id}"
        subject_minutes[name] = subject_minutes.get(name, 0) + s.active_min

    chart2 = {
        "labels": list(subject_minutes.keys()),
        "minutes": list(subject_minutes.values()),
    }

    # ── Metric: adherence this week ───────────────────────────────────────────
    # For each day, if planned > 0: adherence = min(actual/planned, 1.0)
    # Average across days that had a plan.
    adherence_vals = []
    for i in range(7):
        if planned_per_day[i] > 0:
            adherence_vals.append(min(actual_per_day[i] / planned_per_day[i], 1.0))
    adherence_pct = round(sum(adherence_vals) / len(adherence_vals) * 100) \
                    if adherence_vals else None

    # ── Metric: streak (consecutive days with >= 1 session, ending today) ────
    streak = 0
    check_date = today
    while True:
        check_start = datetime.combine(check_date, datetime.min.time())
        check_end   = datetime.combine(check_date, datetime.max.time())
        had_session = Session.query.filter(
            Session.student_id == student.id,
            Session.start_ts >= check_start,
            Session.start_ts <= check_end,
        ).first()
        if had_session:
            streak += 1
            check_date -= timedelta(days=1)
        else:
            break

    # ── Metric: total sessions ────────────────────────────────────────────────
    total_sessions = len(all_sessions)

    # ── Metric: active subjects this week ────────────────────────────────────
    active_subjects = len(set(s.subject_id for s in week_sessions))

    # ── Recent sessions (last 5) ──────────────────────────────────────────────
    recent_raw = (
        Session.query
        .filter_by(student_id=student.id)
        .order_by(Session.start_ts.desc())
        .limit(5)
        .all()
    )
    recent = []
    for s in recent_raw:
        subj = db.session.get(Subject, s.subject_id)

        # Adherence for this session — compare to matching weekly slot
        sesh_adherence = None
        matching_slot = WeeklySlot.query.filter_by(
            student_id=student.id,
            subject_id=s.subject_id,
            day_of_week=s.start_ts.weekday(),
        ).first()
        if matching_slot and matching_slot.duration_min > 0:
            quality_min   = s.active_min * (s.focus_score / 100.0)
            sesh_adherence = min(
                round(quality_min / matching_slot.duration_min * 100), 100
            )

        recent.append({
            "subject":    subj.name_bm if subj else "—",
            "date":       s.start_ts.strftime("%d/%m/%Y"),
            "active_min": s.active_min,
            "focus":      round(s.focus_score),
            "adherence":  sesh_adherence,
        })

    return {
        "adherence_pct":   adherence_pct,
        "streak":          streak,
        "total_sessions":  total_sessions,
        "active_subjects": active_subjects,
        "chart1":          chart1,
        "chart2":          chart2,
        "recent":          recent,
        "week_start":      monday.strftime("%d/%m/%Y"),
        "week_end":        sunday.strftime("%d/%m/%Y"),
    }


# ── Home / Dashboard ─────────────────────────────────────────────────────────
@student_bp.route("/")
def home():
    student = Student.query.first()
    dashboard = _build_dashboard(student)
    return render_template("student/home.html", student=student, **dashboard)


# ── Planner ──────────────────────────────────────────────────────────────────
@student_bp.route("/plan")
def plan():
    student  = Student.query.first()
    subjects = Subject.query.order_by(Subject.name_bm).all()

    raw_slots = (
        db.session.query(WeeklySlot, Subject.name_bm)
        .join(Subject, WeeklySlot.subject_id == Subject.id)
        .filter(WeeklySlot.student_id == student.id)
        .order_by(WeeklySlot.day_of_week, WeeklySlot.start_time)
        .all()
    )
    slots = [{"id": sl.id, "day_of_week": sl.day_of_week,
               "start_time": sl.start_time, "duration_min": sl.duration_min,
               "subject_name": name} for sl, name in raw_slots]

    raw_deadlines = (
        db.session.query(TopicDeadline, Subject.name_bm)
        .join(Subject, TopicDeadline.subject_id == Subject.id)
        .filter(TopicDeadline.student_id == student.id)
        .order_by(TopicDeadline.deadline)
        .all()
    )
    deadlines = [{"id": d.id, "subject_name": name, "topic": d.topic,
                   "deadline": d.deadline.strftime("%d/%m/%Y"),
                   "status": d.status} for d, name in raw_deadlines]

    return render_template("student/plan.html", student=student,
                           subjects=subjects, slots=slots, deadlines=deadlines)


# ── Weekly slot: add ─────────────────────────────────────────────────────────
@student_bp.route("/plan/slot/add", methods=["POST"])
def slot_add():
    student = Student.query.first()
    try:
        db.session.add(WeeklySlot(
            student_id=student.id,
            subject_id=int(request.form["subject_id"]),
            day_of_week=int(request.form["day_of_week"]),
            start_time=request.form["start_time"],
            duration_min=int(request.form["duration_min"]),
        ))
        db.session.commit()
        flash("Slot berjaya ditambah.", "ok")
    except Exception as e:
        db.session.rollback()
        flash(f"Ralat: {e}", "err")
    return redirect(url_for("student.plan"))


# ── Weekly slot: delete ──────────────────────────────────────────────────────
@student_bp.route("/plan/slot/delete/<int:slot_id>", methods=["POST"])
def slot_delete(slot_id):
    slot = db.session.get(WeeklySlot, slot_id)
    if slot:
        db.session.delete(slot)
        db.session.commit()
        flash("Slot dipadam.", "ok")
    return redirect(url_for("student.plan"))


# ── Deadline: add ────────────────────────────────────────────────────────────
@student_bp.route("/plan/deadline/add", methods=["POST"])
def deadline_add():
    student = Student.query.first()
    try:
        db.session.add(TopicDeadline(
            student_id=student.id,
            subject_id=int(request.form["subject_id"]),
            topic=request.form["topic"].strip(),
            deadline=date.fromisoformat(request.form["deadline"]),
            status="pending",
        ))
        db.session.commit()
        flash("Sasaran berjaya ditambah.", "ok")
    except Exception as e:
        db.session.rollback()
        flash(f"Ralat: {e}", "err")
    return redirect(url_for("student.plan"))


# ── Deadline: update status ──────────────────────────────────────────────────
@student_bp.route("/plan/deadline/status/<int:deadline_id>", methods=["POST"])
def deadline_status(deadline_id):
    d = db.session.get(TopicDeadline, deadline_id)
    if d and request.form.get("status") in ("pending", "in_progress", "done"):
        d.status = request.form["status"]
        db.session.commit()
    return redirect(url_for("student.plan"))


# ── Deadline: delete ─────────────────────────────────────────────────────────
@student_bp.route("/plan/deadline/delete/<int:deadline_id>", methods=["POST"])
def deadline_delete(deadline_id):
    d = db.session.get(TopicDeadline, deadline_id)
    if d:
        db.session.delete(d)
        db.session.commit()
        flash("Sasaran dipadam.", "ok")
    return redirect(url_for("student.plan"))


# ── JSON stub (device compatibility) ─────────────────────────────────────────
@student_bp.route("/plan/json")
def plan_json():
    student   = Student.query.first()
    slots     = WeeklySlot.query.filter_by(student_id=student.id).all()
    deadlines = TopicDeadline.query.filter_by(student_id=student.id).all()
    return jsonify({
        "slots":     [{"id": s.id, "day": s.day_of_week, "start": s.start_time,
                       "dur": s.duration_min, "subject_id": s.subject_id} for s in slots],
        "deadlines": [{"id": d.id, "topic": d.topic, "deadline": str(d.deadline),
                       "status": d.status} for d in deadlines],
    })
