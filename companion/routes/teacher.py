"""
Teacher dashboard routes.
Week 7: real multi-student view — session summary, adherence, subject balance.
"""

import json
from datetime import date, datetime, timedelta
from flask import Blueprint, render_template
from models.schema import db, Student, Subject, Session, WeeklySlot, QuizAnswer, QuizQuestion, QuizBank

teacher_bp = Blueprint("teacher", __name__, url_prefix="/teacher")


def _week_bounds(today=None):
    if today is None:
        today = date.today()
    monday = today - timedelta(days=today.weekday())
    return monday, monday + timedelta(days=6)


def _student_summary(student, week_sessions, all_sessions):
    """Compute per-student summary metrics for teacher view."""
    slots = WeeklySlot.query.filter_by(student_id=student.id).all()
    planned_per_day = [0]*7
    for s in slots:
        planned_per_day[s.day_of_week] += s.duration_min

    total_planned = sum(planned_per_day)
    total_actual  = sum(s.active_min for s in week_sessions)
    avg_focus     = (sum(s.focus_score for s in week_sessions) / len(week_sessions)
                     if week_sessions else 0)

    # Adherence per day then averaged
    actual_per_day = [0]*7
    for s in week_sessions:
        actual_per_day[s.start_ts.weekday()] += s.active_min
    adherence_vals = []
    for i in range(7):
        if planned_per_day[i] > 0:
            adherence_vals.append(min(actual_per_day[i]/planned_per_day[i], 1.0))
    adherence_pct = round(sum(adherence_vals)/len(adherence_vals)*100) \
                    if adherence_vals else None

    # Subject balance this week
    subject_minutes = {}
    for s in week_sessions:
        subj = db.session.get(Subject, s.subject_id)
        name = subj.name_bm if subj else f"Subjek {s.subject_id}"
        subject_minutes[name] = subject_minutes.get(name, 0) + s.active_min

    # Quiz accuracy overall
    banks    = QuizBank.query.all()
    bank_ids = [b.id for b in banks]
    q_ids    = [q.id for q in QuizQuestion.query.filter(
                    QuizQuestion.bank_id.in_(bank_ids)).all()] if bank_ids else []
    all_answers = (QuizAnswer.query.filter(
                    QuizAnswer.question_id.in_(q_ids))
                .all()) if q_ids else []
    student_session_ids = {s.id for s in all_sessions}
    answers = [a for a in all_answers if
               a.session_id is None or a.session_id in student_session_ids]
    quiz_pct = (round(sum(1 for a in answers if a.correct)/len(answers)*100)
                if answers else None)

    return {
        "name":           student.name,
        "device_id":      student.device_id,
        "class_name":     student.class_name,
        "total_sessions": len(week_sessions),
        "total_active":   total_actual,
        "total_planned":  total_planned,
        "avg_focus":      round(avg_focus),
        "adherence_pct":  adherence_pct,
        "quiz_pct":       quiz_pct,
        "chart_labels":   list(subject_minutes.keys()),
        "chart_minutes":  list(subject_minutes.values()),
    }


@teacher_bp.route("/")
def home():
    today          = date.today()
    monday, sunday = _week_bounds(today)
    monday_dt      = datetime.combine(monday, datetime.min.time())
    sunday_dt      = datetime.combine(sunday, datetime.max.time())

    students = Student.query.order_by(Student.name).all()
    summaries = []
    all_week_sessions = []

    for student in students:
        week_sess = (Session.query
            .filter(Session.student_id==student.id,
                    Session.start_ts>=monday_dt,
                    Session.start_ts<=sunday_dt)
            .order_by(Session.start_ts.desc()).all())
        all_sess = Session.query.filter_by(student_id=student.id).all()
        summary  = _student_summary(student, week_sess, all_sess)
        summaries.append(summary)

        # Collect sessions for the combined log
        for s in week_sess:
            subj = db.session.get(Subject, s.subject_id)
            slot = WeeklySlot.query.filter_by(
                student_id=student.id, subject_id=s.subject_id,
                day_of_week=s.start_ts.weekday()).first()
            adh = None
            if slot and slot.duration_min > 0:
                adh = min(round(s.active_min*(s.focus_score/100)
                                /slot.duration_min*100), 100)
            all_week_sessions.append({
                "student":    student.name,
                "subject":    subj.name_bm if subj else "—",
                "date":       s.start_ts.strftime("%d/%m/%Y"),
                "time":       s.start_ts.strftime("%H:%M"),
                "active_min": s.active_min,
                "focus":      round(s.focus_score),
                "adherence":  adh,
            })

    # Sort combined log by date+time descending
    all_week_sessions.sort(key=lambda x: (x["date"], x["time"]), reverse=True)

    return render_template("teacher/home.html",
        summaries=summaries,
        all_week_sessions=all_week_sessions,
        week_start=monday.strftime("%d/%m/%Y"),
        week_end=sunday.strftime("%d/%m/%Y"),
        student_count=len(students),
    )


@teacher_bp.route("/students")
def students():
    from flask import jsonify
    rows = [{"id":s.id,"name":s.name,"class_name":s.class_name,
             "device_id":s.device_id}
            for s in Student.query.order_by(Student.id).all()]
    return jsonify(rows)
