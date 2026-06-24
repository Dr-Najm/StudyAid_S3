"""
Student-facing routes.
Week 3: study planner.
Week 4: progress dashboard.
Week 7: quiz accuracy per subject, deadline countdown, subject balance warning.
Week 8: student switcher via ?student_id=X query parameter.
"""

import json
from datetime import date, datetime, timedelta
from flask import Blueprint, flash, jsonify, redirect, render_template, request, url_for
from models.schema import (db, Student, Subject, WeeklySlot, TopicDeadline,
                            Session, QuizAnswer, QuizQuestion, QuizBank)

student_bp = Blueprint("student", __name__, url_prefix="/student")


# ── Helpers ───────────────────────────────────────────────────────────────────

def _get_student():
    """
    Return the active student based on ?student_id= query param.
    Falls back to the first student in the DB if not provided or invalid.
    """
    sid = request.args.get("student_id", type=int)
    if sid:
        student = db.session.get(Student, sid)
        if student:
            return student
    return Student.query.order_by(Student.id).first()


def _get_student_from_form():
    """
    Read student_id from a hidden form field (used in POST routes).
    Falls back to first student.
    """
    sid = request.form.get("student_id", type=int)
    if sid:
        student = db.session.get(Student, sid)
        if student:
            return student
    return Student.query.order_by(Student.id).first()


def _all_students():
    return Student.query.order_by(Student.name).all()


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

    week_sessions = (Session.query
        .filter(Session.student_id==student.id,
                Session.start_ts>=monday_dt,
                Session.start_ts<=sunday_dt)
        .all())
    all_sessions = Session.query.filter_by(student_id=student.id).all()

    slots = WeeklySlot.query.filter_by(student_id=student.id).all()
    planned_per_day = [0]*7
    for s in slots:
        planned_per_day[s.day_of_week] += s.duration_min

    actual_per_day = [0]*7
    for s in week_sessions:
        actual_per_day[s.start_ts.weekday()] += s.active_min

    # Build subject label per day: show subject name(s) planned for each day
    subject_per_day = []
    for i in range(7):
        day_slots = [s for s in slots if s.day_of_week == i]
        if day_slots:
            names = []
            for sl in day_slots:
                subj = db.session.get(Subject, sl.subject_id)
                if subj:
                    # Use short form for display (first word only if multi-word)
                    short = subj.name_bm.split()[0] if subj else ""
                    names.append(short)
            subject_per_day.append(", ".join(names))
        else:
            subject_per_day.append("")

    chart1 = {
        "labels":      [_day_name(i) for i in range(7)],
        "subjects":    subject_per_day,
        "planned":     planned_per_day,
        "actual":      actual_per_day,
    }

    subject_minutes = {}
    for s in week_sessions:
        subj = db.session.get(Subject, s.subject_id)
        name = subj.name_bm if subj else f"Subjek {s.subject_id}"
        subject_minutes[name] = subject_minutes.get(name, 0) + s.active_min

    chart2 = {
        "labels":  list(subject_minutes.keys()),
        "minutes": list(subject_minutes.values()),
    }

    balance_warning = None
    total_min = sum(subject_minutes.values())
    if total_min > 0:
        for subj_name, mins in subject_minutes.items():
            pct = mins / total_min * 100
            if pct > 50:
                balance_warning = {"subject": subj_name, "pct": round(pct)}
                break

    adherence_vals = []
    for i in range(7):
        if planned_per_day[i] > 0:
            adherence_vals.append(min(actual_per_day[i]/planned_per_day[i], 1.0))
    adherence_pct = round(sum(adherence_vals)/len(adherence_vals)*100) \
                    if adherence_vals else None

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

    total_sessions  = len(all_sessions)
    active_subjects = len(set(s.subject_id for s in week_sessions))

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

    quiz_accuracy = []
    subjects_all  = Subject.query.order_by(Subject.name_bm).all()
    student_session_ids = {s.id for s in all_sessions}
    for subj in subjects_all:
        banks    = QuizBank.query.filter_by(subject_id=subj.id).all()
        bank_ids = [b.id for b in banks]
        if not bank_ids:
            continue
        q_ids = [q.id for q in QuizQuestion.query.filter(
            QuizQuestion.bank_id.in_(bank_ids)).all()]
        if not q_ids:
            continue
        answers = QuizAnswer.query.filter(
            QuizAnswer.question_id.in_(q_ids)).all()
        answers = [a for a in answers if
                   a.session_id is None or a.session_id in student_session_ids]
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

    raw_deadlines = (
        db.session.query(TopicDeadline, Subject.name_bm)
        .join(Subject, TopicDeadline.subject_id==Subject.id)
        .filter(TopicDeadline.student_id==student.id,
                TopicDeadline.status != "done")
        .order_by(TopicDeadline.deadline)
        .all()
    )
    deadlines_dash  = []
    urgent_deadline = None
    for d, subj_name in raw_deadlines:
        days_left = (d.deadline - today).days
        badge = "overdue" if days_left < 0 else ("soon" if days_left <= 7 else "ok")
        if days_left <= 3 and not urgent_deadline:
            urgent_deadline = {"topic": d.topic, "subject": subj_name, "days": days_left}
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


# ── Live session monitor ──────────────────────────────────────────────────────
@student_bp.route("/live")
def live():
    student  = _get_student()
    students = _all_students()
    return render_template("student/live.html",
                           student=student,
                           students=students)


# ── Home / Dashboard ──────────────────────────────────────────────────────────
@student_bp.route("/")
def home():
    student   = _get_student()
    students  = _all_students()
    dashboard = _build_dashboard(student)
    return render_template("student/home.html",
                           student=student,
                           students=students,
                           **dashboard)


# ── Planner ───────────────────────────────────────────────────────────────────
@student_bp.route("/plan")
def plan():
    student  = _get_student()
    students = _all_students()
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
                   "deadline":d.deadline.strftime("%d/%m/%Y"),"status":d.status,
                   "profile":d.profile}
                 for d,name in raw_deadlines]

    return render_template("student/plan.html",
                           student=student,
                           students=students,
                           subjects=subjects,
                           slots=slots,
                           deadlines=deadlines)


# ── POST routes — all read student_id from hidden form field ──────────────────

@student_bp.route("/plan/slot/add", methods=["POST"])
def slot_add():
    student = _get_student_from_form()
    try:
        db.session.add(WeeklySlot(
            student_id=student.id,
            subject_id=int(request.form["subject_id"]),
            day_of_week=int(request.form["day_of_week"]),
            start_time=request.form["start_time"],
            duration_min=int(request.form["duration_min"])))
        db.session.commit()
        flash("Slot berjaya ditambah.", "ok")
    except Exception as e:
        db.session.rollback()
        flash(f"Ralat: {e}", "err")
    return redirect(url_for("student.plan", student_id=student.id))


@student_bp.route("/plan/slot/delete/<int:slot_id>", methods=["POST"])
def slot_delete(slot_id):
    student = _get_student_from_form()
    slot = db.session.get(WeeklySlot, slot_id)
    if slot:
        db.session.delete(slot)
        db.session.commit()
        flash("Slot dipadam.", "ok")
    return redirect(url_for("student.plan", student_id=student.id))


@student_bp.route("/plan/deadline/add", methods=["POST"])
def deadline_add():
    student = _get_student_from_form()
    try:
        subject_id = int(request.form["subject_id"])
        topic      = request.form["topic"].strip()
        subject    = db.session.get(Subject, subject_id)

        # v11: classify profile BEFORE saving so the row is written with the
        # correct value in a single commit. If Gemini fails, _classify_topic_profile
        # returns "Campuran" (safe default) and the topic is still saved.
        profile = "Campuran"
        if subject:
            from routes.admin import _classify_topic_profile
            profile = _classify_topic_profile(subject.name_bm, topic)

        db.session.add(TopicDeadline(
            student_id = student.id,
            subject_id = subject_id,
            topic      = topic,
            deadline   = date.fromisoformat(request.form["deadline"]),
            status     = "pending",
            profile    = profile,
        ))
        db.session.commit()

        # v11: generate quiz bank synchronously in a separate try/except so
        # the topic save above is never rolled back if Gemini quiz-gen fails.
        quiz_msg = ""
        if subject:
            from routes.admin import _generate_questions_gemini
            try:
                raw_questions = _generate_questions_gemini(
                    subject.name_bm, subject.name_en, subject.default_lang, topic
                )
                bank = QuizBank(
                    subject_id   = subject_id,
                    topic        = topic,
                    generated_at = datetime.utcnow(),
                    source       = "gemini",
                )
                db.session.add(bank)
                db.session.flush()
                for q in raw_questions:
                    db.session.add(QuizQuestion(
                        bank_id       = bank.id,
                        question_text = q["question_text"],
                        options_json  = json.dumps(q["options"], ensure_ascii=False),
                        correct_index = q["correct_index"],
                        language      = q.get("language", subject.default_lang),
                        difficulty    = q.get("difficulty", 1),
                    ))
                db.session.commit()
                quiz_msg = f" {len(raw_questions)} soalan dijana."
            except RuntimeError as e:
                db.session.rollback()
                quiz_msg = " (Jana soalan gagal — gunakan halaman Admin untuk jana secara manual.)"

        flash(f"Sasaran ditambah. Profil: {profile}.{quiz_msg}", "ok")
    except Exception as e:
        db.session.rollback()
        flash(f"Ralat: {e}", "err")
    return redirect(url_for("student.plan", student_id=student.id))


@student_bp.route("/plan/deadline/status/<int:deadline_id>", methods=["POST"])
def deadline_status(deadline_id):
    student = _get_student_from_form()
    d = db.session.get(TopicDeadline, deadline_id)
    if d and request.form.get("status") in ("pending","in_progress","done"):
        d.status = request.form["status"]
        db.session.commit()
    return redirect(url_for("student.plan", student_id=student.id))


@student_bp.route("/plan/deadline/delete/<int:deadline_id>", methods=["POST"])
def deadline_delete(deadline_id):
    student = _get_student_from_form()
    d = db.session.get(TopicDeadline, deadline_id)
    if d:
        db.session.delete(d)
        db.session.commit()
        flash("Sasaran dipadam.", "ok")
    return redirect(url_for("student.plan", student_id=student.id))


@student_bp.route("/plan/topic/profile", methods=["POST"])
def deadline_profile_override():
    """
    v11: Teacher override for AI-classified study profile.
    Accepts deadline_id + profile from the planner topic table.
    This is the explainability anchor: the AI classification is visible and
    correctable by the teacher in one click.
    """
    student    = _get_student_from_form()
    deadline_id = request.form.get("deadline_id", type=int)
    new_profile = request.form.get("profile", "").strip()

    _VALID_PROFILES = {"Menulis", "Membaca", "Campuran"}
    if new_profile not in _VALID_PROFILES:
        flash("Profil tidak sah.", "err")
        return redirect(url_for("student.plan", student_id=student.id))

    d = db.session.get(TopicDeadline, deadline_id)
    if d:
        d.profile = new_profile
        db.session.commit()
        flash(f"Profil '{d.topic}' dikemaskini kepada {new_profile}.", "ok")
    return redirect(url_for("student.plan", student_id=student.id))


@student_bp.route("/plan/json")
def plan_json():
    student   = _get_student()
    slots     = WeeklySlot.query.filter_by(student_id=student.id).all()
    deadlines = TopicDeadline.query.filter_by(student_id=student.id).all()
    return jsonify({
        "slots":     [{"id":s.id,"day":s.day_of_week,"start":s.start_time,
                       "dur":s.duration_min,"subject_id":s.subject_id} for s in slots],
        "deadlines": [{"id":d.id,"topic":d.topic,"deadline":str(d.deadline),
                       "status":d.status} for d in deadlines],
    })


# ── Booth kiosk page ──────────────────────────────────────────────────────────
@student_bp.route("/booth")
def booth():
    """
    Dedicated kiosk page for the competition booth.
    Polls /api/booth/state every 3s and transitions between:
      idle    — no device nearby
      welcome — device connected, no active session
      live    — session in progress
    Only this page reacts to device hello-pings.
    Laptops on other pages (planner, teacher, dashboard) are unaffected.
    """
    return render_template("student/booth.html")
