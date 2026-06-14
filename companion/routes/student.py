"""
Student-facing routes.
Week 3: study planner.
Week 4: progress dashboard.
Week 7: quiz accuracy per subject, deadline countdown, subject balance warning.
"""

from datetime import date, datetime, timedelta
from flask import Blueprint, flash, jsonify, redirect, render_template, request, url_for
from models.schema import (db, Student, Subject, WeeklySlot, TopicDeadline,
                            Session, QuizAnswer, QuizQuestion, QuizBank)

student_bp = Blueprint("student", __name__, url_prefix="/student")


def _week_bounds(today=None):
    if today is None:
        today = date.today()
    monday = today - timedelta(days=today.weekday())
    return monday, monday + timedelta(days=6)


def _day_name(i):
    return ["Isnin","Selasa","Rabu","Khamis","Jumaat","Sabtu","Ahad"][i]


def _build_dashboard(student):
    today          = date.today()
    monday, sunday = _week_bounds(today)
    monday_dt      = datetime.combine(monday, datetime.min.time())
    sunday_dt      = datetime.combine(sunday, datetime.max.time())

    # ── Sessions this week + all sessions ───────────────────────────────────
    week_sessions = (Session.query
        .filter(Session.student_id==student.id,
                Session.start_ts>=monday_dt,
                Session.start_ts<=sunday_dt)
        .all())
    all_sessions = Session.query.filter_by(student_id=student.id).all()

    # ── Weekly slots ─────────────────────────────────────────────────────────
    slots = WeeklySlot.query.filter_by(student_id=student.id).all()
    planned_per_day = [0]*7
    for s in slots:
        planned_per_day[s.day_of_week] += s.duration_min

    actual_per_day = [0]*7
    for s in week_sessions:
        actual_per_day[s.start_ts.weekday()] += s.active_min

    chart1 = {
        "labels":  [_day_name(i) for i in range(7)],
        "planned": planned_per_day,
        "actual":  actual_per_day,
    }

    # ── Subject balance (doughnut) ───────────────────────────────────────────
    subject_minutes = {}
    for s in week_sessions:
        subj = db.session.get(Subject, s.subject_id)
        name = subj.name_bm if subj else f"Subjek {s.subject_id}"
        subject_minutes[name] = subject_minutes.get(name, 0) + s.active_min

    chart2 = {
        "labels":  list(subject_minutes.keys()),
        "minutes": list(subject_minutes.values()),
    }

    # ── Subject balance warning ──────────────────────────────────────────────
    # v9 Week 7: flag if one subject >50% of total study time this week
    balance_warning = None
    total_min = sum(subject_minutes.values())
    if total_min > 0:
        for subj_name, mins in subject_minutes.items():
            pct = mins / total_min * 100
            if pct > 50:
                balance_warning = {
                    "subject": subj_name,
                    "pct":     round(pct),
                }
                break

    # ── Adherence this week ──────────────────────────────────────────────────
    adherence_vals = []
    for i in range(7):
        if planned_per_day[i] > 0:
            adherence_vals.append(min(actual_per_day[i]/planned_per_day[i], 1.0))
    adherence_pct = round(sum(adherence_vals)/len(adherence_vals)*100) \
                    if adherence_vals else None

    # ── Streak ───────────────────────────────────────────────────────────────
    streak = 0
    check  = today
    while True:
        s = datetime.combine(check, datetime.min.time())
        e = datetime.combine(check, datetime.max.time())
        if Session.query.filter(Session.student_id==student.id,
                                Session.start_ts>=s,
                                Session.start_ts<=e).first():
            streak += 1; check -= timedelta(days=1)
        else:
            break

    # ── Metrics ──────────────────────────────────────────────────────────────
    total_sessions  = len(all_sessions)
    active_subjects = len(set(s.subject_id for s in week_sessions))

    # ── Recent sessions (last 5) ─────────────────────────────────────────────
    recent_raw = (Session.query.filter_by(student_id=student.id)
        .order_by(Session.start_ts.desc()).limit(5).all())
    recent = []
    for s in recent_raw:
        subj = db.session.get(Subject, s.subject_id)
        slot = WeeklySlot.query.filter_by(
            student_id=student.id, subject_id=s.subject_id,
            day_of_week=s.start_ts.weekday()).first()
        adh = None
        if slot and slot.duration_min > 0:
            adh = min(round(s.active_min*(s.focus_score/100)/slot.duration_min*100), 100)
        recent.append({
            "subject":    subj.name_bm if subj else "—",
            "date":       s.start_ts.strftime("%d/%m/%Y"),
            "active_min": s.active_min,
            "focus":      round(s.focus_score),
            "adherence":  adh,
        })

    # ── v9 Week 7: Quiz accuracy per subject ─────────────────────────────────
    # Join quiz_answers → quiz_questions → quiz_banks → subjects
    quiz_accuracy = []
    subjects_all  = Subject.query.order_by(Subject.name_bm).all()
    for subj in subjects_all:
        banks = QuizBank.query.filter_by(subject_id=subj.id).all()
        bank_ids = [b.id for b in banks]
        if not bank_ids:
            continue
        q_ids = [q.id for q in QuizQuestion.query.filter(
            QuizQuestion.bank_id.in_(bank_ids)).all()]
        if not q_ids:
            continue
        answers = QuizAnswer.query.filter(
            QuizAnswer.question_id.in_(q_ids)
        ).all()
        # Include answers from this student's sessions OR unlinked quiz-mode answers
        student_session_ids = {s.id for s in all_sessions}
        answers = [a for a in answers if
                   a.session_id is None or
                   a.session_id in student_session_ids]
        if not answers:
            continue
        correct = sum(1 for a in answers if a.correct)
        quiz_accuracy.append({
            "subject": subj.name_bm,
            "correct": correct,
            "total":   len(answers),
            "pct":     round(correct/len(answers)*100),
        })

    chart3 = {
        "labels": [q["subject"] for q in quiz_accuracy],
        "pct":    [q["pct"]     for q in quiz_accuracy],
    }

    # ── v9 Week 7: Deadline countdown ────────────────────────────────────────
    raw_deadlines = (
        db.session.query(TopicDeadline, Subject.name_bm)
        .join(Subject, TopicDeadline.subject_id==Subject.id)
        .filter(TopicDeadline.student_id==student.id,
                TopicDeadline.status != "done")
        .order_by(TopicDeadline.deadline)
        .all()
    )
    deadlines_dash = []
    urgent_deadline = None   # first deadline within 3 days (for banner)
    for d, subj_name in raw_deadlines:
        days_left = (d.deadline - today).days
        if days_left < 0:
            badge = "overdue"
        elif days_left <= 7:
            badge = "soon"
        else:
            badge = "ok"
        if days_left <= 3 and not urgent_deadline:
            urgent_deadline = {"topic": d.topic, "subject": subj_name,
                               "days": days_left}
        deadlines_dash.append({
            "topic":     d.topic,
            "subject":   subj_name,
            "deadline":  d.deadline.strftime("%d/%m/%Y"),
            "days_left": days_left,
            "badge":     badge,
        })

    return {
        "adherence_pct":   adherence_pct,
        "streak":          streak,
        "total_sessions":  total_sessions,
        "active_subjects": active_subjects,
        "chart1":          chart1,
        "chart2":          chart2,
        "chart3":          chart3,
        "quiz_accuracy":   quiz_accuracy,
        "recent":          recent,
        "deadlines_dash":  deadlines_dash,
        "urgent_deadline": urgent_deadline,
        "balance_warning": balance_warning,
        "week_start":      monday.strftime("%d/%m/%Y"),
        "week_end":        sunday.strftime("%d/%m/%Y"),
    }


# ── Home / Dashboard ─────────────────────────────────────────────────────────
@student_bp.route("/")
def home():
    student   = Student.query.first()
    dashboard = _build_dashboard(student)
    return render_template("student/home.html", student=student, **dashboard)


# ── Planner ──────────────────────────────────────────────────────────────────
@student_bp.route("/plan")
def plan():
    student  = Student.query.first()
    subjects = Subject.query.order_by(Subject.name_bm).all()
    raw_slots = (db.session.query(WeeklySlot, Subject.name_bm)
        .join(Subject, WeeklySlot.subject_id==Subject.id)
        .filter(WeeklySlot.student_id==student.id)
        .order_by(WeeklySlot.day_of_week, WeeklySlot.start_time).all())
    slots = [{"id":sl.id,"day_of_week":sl.day_of_week,"start_time":sl.start_time,
               "duration_min":sl.duration_min,"subject_name":name}
             for sl,name in raw_slots]
    raw_deadlines = (db.session.query(TopicDeadline, Subject.name_bm)
        .join(Subject, TopicDeadline.subject_id==Subject.id)
        .filter(TopicDeadline.student_id==student.id)
        .order_by(TopicDeadline.deadline).all())
    deadlines = [{"id":d.id,"subject_name":name,"topic":d.topic,
                   "deadline":d.deadline.strftime("%d/%m/%Y"),"status":d.status}
                 for d,name in raw_deadlines]
    return render_template("student/plan.html", student=student,
                           subjects=subjects, slots=slots, deadlines=deadlines)


@student_bp.route("/plan/slot/add", methods=["POST"])
def slot_add():
    student = Student.query.first()
    try:
        db.session.add(WeeklySlot(
            student_id=student.id, subject_id=int(request.form["subject_id"]),
            day_of_week=int(request.form["day_of_week"]),
            start_time=request.form["start_time"],
            duration_min=int(request.form["duration_min"])))
        db.session.commit(); flash("Slot berjaya ditambah.", "ok")
    except Exception as e:
        db.session.rollback(); flash(f"Ralat: {e}", "err")
    return redirect(url_for("student.plan"))


@student_bp.route("/plan/slot/delete/<int:slot_id>", methods=["POST"])
def slot_delete(slot_id):
    slot = db.session.get(WeeklySlot, slot_id)
    if slot: db.session.delete(slot); db.session.commit(); flash("Slot dipadam.", "ok")
    return redirect(url_for("student.plan"))


@student_bp.route("/plan/deadline/add", methods=["POST"])
def deadline_add():
    student = Student.query.first()
    try:
        db.session.add(TopicDeadline(
            student_id=student.id, subject_id=int(request.form["subject_id"]),
            topic=request.form["topic"].strip(),
            deadline=date.fromisoformat(request.form["deadline"]), status="pending"))
        db.session.commit(); flash("Sasaran berjaya ditambah.", "ok")
    except Exception as e:
        db.session.rollback(); flash(f"Ralat: {e}", "err")
    return redirect(url_for("student.plan"))


@student_bp.route("/plan/deadline/status/<int:deadline_id>", methods=["POST"])
def deadline_status(deadline_id):
    d = db.session.get(TopicDeadline, deadline_id)
    if d and request.form.get("status") in ("pending","in_progress","done"):
        d.status = request.form["status"]; db.session.commit()
    return redirect(url_for("student.plan"))


@student_bp.route("/plan/deadline/delete/<int:deadline_id>", methods=["POST"])
def deadline_delete(deadline_id):
    d = db.session.get(TopicDeadline, deadline_id)
    if d: db.session.delete(d); db.session.commit(); flash("Sasaran dipadam.", "ok")
    return redirect(url_for("student.plan"))


@student_bp.route("/plan/json")
def plan_json():
    student   = Student.query.first()
    slots     = WeeklySlot.query.filter_by(student_id=student.id).all()
    deadlines = TopicDeadline.query.filter_by(student_id=student.id).all()
    return jsonify({
        "slots":     [{"id":s.id,"day":s.day_of_week,"start":s.start_time,
                       "dur":s.duration_min,"subject_id":s.subject_id} for s in slots],
        "deadlines": [{"id":d.id,"topic":d.topic,"deadline":str(d.deadline),
                       "status":d.status} for d in deadlines],
    })
