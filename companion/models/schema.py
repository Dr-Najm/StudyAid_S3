"""
StudyAid Companion App - Database Schema

Nine tables covering v9 scope:
    students, subjects, weekly_slots, topic_deadlines,
    quiz_banks, quiz_questions, sessions, drift_events, quiz_answers

Plus two helpers:
    init_db(app)           - create all tables if they don't exist
    seed_initial_data()    - populate 8 SPM subjects + 2 demo students
"""

from datetime import datetime
from flask_sqlalchemy import SQLAlchemy

db = SQLAlchemy()


# ----------------------------------------------------------------------------
# Identity
# ----------------------------------------------------------------------------
class Student(db.Model):
    __tablename__ = "students"
    id = db.Column(db.Integer, primary_key=True)
    name = db.Column(db.String(80), nullable=False)
    class_name = db.Column(db.String(20), nullable=False)
    device_id = db.Column(db.String(40), unique=True, nullable=False)

    def to_dict(self):
        return {
            "id": self.id,
            "name": self.name,
            "class_name": self.class_name,
            "device_id": self.device_id,
        }


# ----------------------------------------------------------------------------
# Catalog
# ----------------------------------------------------------------------------
class Subject(db.Model):
    __tablename__ = "subjects"
    id = db.Column(db.Integer, primary_key=True)
    name_bm = db.Column(db.String(60), nullable=False)
    name_en = db.Column(db.String(60), nullable=False)
    # default_lang values: "bm" or "en"
    default_lang = db.Column(db.String(2), nullable=False, default="bm")

    def to_dict(self):
        return {
            "id": self.id,
            "name_bm": self.name_bm,
            "name_en": self.name_en,
            "default_lang": self.default_lang,
        }


# ----------------------------------------------------------------------------
# Planner
# ----------------------------------------------------------------------------
class WeeklySlot(db.Model):
    __tablename__ = "weekly_slots"
    id = db.Column(db.Integer, primary_key=True)
    student_id = db.Column(db.Integer, db.ForeignKey("students.id"), nullable=False)
    subject_id = db.Column(db.Integer, db.ForeignKey("subjects.id"), nullable=False)
    # 0=Monday ... 6=Sunday (ISO weekday minus 1)
    day_of_week = db.Column(db.Integer, nullable=False)
    # Start time stored as "HH:MM" string for simplicity
    start_time = db.Column(db.String(5), nullable=False)
    duration_min = db.Column(db.Integer, nullable=False)


class TopicDeadline(db.Model):
    __tablename__ = "topic_deadlines"
    id = db.Column(db.Integer, primary_key=True)
    student_id = db.Column(db.Integer, db.ForeignKey("students.id"), nullable=False)
    subject_id = db.Column(db.Integer, db.ForeignKey("subjects.id"), nullable=False)
    topic = db.Column(db.String(120), nullable=False)
    deadline = db.Column(db.Date, nullable=False)
    # status values: "pending", "in_progress", "done"
    status = db.Column(db.String(20), nullable=False, default="pending")


# ----------------------------------------------------------------------------
# Quiz bank
# ----------------------------------------------------------------------------
class QuizBank(db.Model):
    __tablename__ = "quiz_banks"
    id = db.Column(db.Integer, primary_key=True)
    subject_id = db.Column(db.Integer, db.ForeignKey("subjects.id"), nullable=False)
    topic = db.Column(db.String(120), nullable=False)
    generated_at = db.Column(db.DateTime, nullable=False, default=datetime.utcnow)
    # source values: "gemini" or "manual"
    source = db.Column(db.String(20), nullable=False, default="gemini")


class QuizQuestion(db.Model):
    __tablename__ = "quiz_questions"
    id = db.Column(db.Integer, primary_key=True)
    bank_id = db.Column(db.Integer, db.ForeignKey("quiz_banks.id"), nullable=False)
    question_text = db.Column(db.Text, nullable=False)
    # options_json is a JSON-encoded array of 4 option strings
    options_json = db.Column(db.Text, nullable=False)
    correct_index = db.Column(db.Integer, nullable=False)
    # language values: "bm" or "en"
    language = db.Column(db.String(2), nullable=False, default="bm")


# ----------------------------------------------------------------------------
# Session telemetry from device
# ----------------------------------------------------------------------------
class Session(db.Model):
    __tablename__ = "sessions"
    id = db.Column(db.Integer, primary_key=True)
    student_id = db.Column(db.Integer, db.ForeignKey("students.id"), nullable=False)
    subject_id = db.Column(db.Integer, db.ForeignKey("subjects.id"), nullable=False)
    start_ts = db.Column(db.DateTime, nullable=False)
    end_ts = db.Column(db.DateTime, nullable=True)
    active_min = db.Column(db.Integer, nullable=False, default=0)
    idle_min = db.Column(db.Integer, nullable=False, default=0)
    focus_score = db.Column(db.Float, nullable=False, default=0.0)


class DriftEvent(db.Model):
    __tablename__ = "drift_events"
    id = db.Column(db.Integer, primary_key=True)
    session_id = db.Column(db.Integer, db.ForeignKey("sessions.id"), nullable=False)
    ts = db.Column(db.DateTime, nullable=False, default=datetime.utcnow)
    # severity values: "warning", "quiz_trigger", "escalated"
    severity = db.Column(db.String(20), nullable=False)


class QuizAnswer(db.Model):
    __tablename__ = "quiz_answers"
    id = db.Column(db.Integer, primary_key=True)
    # nullable=True: quiz-mode answers may not be linked to a study session
    session_id = db.Column(db.Integer, db.ForeignKey("sessions.id"), nullable=True)
    question_id = db.Column(db.Integer, db.ForeignKey("quiz_questions.id"), nullable=False)
    chosen_index = db.Column(db.Integer, nullable=False)
    correct = db.Column(db.Boolean, nullable=False)
    ts = db.Column(db.DateTime, nullable=False, default=datetime.utcnow)


# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------
def init_db(app):
    """Bind SQLAlchemy to the Flask app and create all tables if missing."""
    db.init_app(app)
    with app.app_context():
        db.create_all()


# 8 core SPM subjects to seed on first run.
# Order must match firmware subjects[] array (device index 0-7 = DB ID 1-8).
_SEED_SUBJECTS = [
    {"name_bm": "Bahasa Melayu",    "name_en": "Malay Language",  "default_lang": "bm"},
    {"name_bm": "Matematik",        "name_en": "Mathematics",     "default_lang": "bm"},
    {"name_bm": "Sejarah",          "name_en": "History",         "default_lang": "bm"},
    {"name_bm": "Geografi",         "name_en": "Geography",       "default_lang": "bm"},
    {"name_bm": "Pendidikan Islam", "name_en": "Islamic Studies", "default_lang": "bm"},
    {"name_bm": "Fizik",            "name_en": "Physics",         "default_lang": "bm"},
    {"name_bm": "Kimia",            "name_en": "Chemistry",       "default_lang": "bm"},
    {"name_bm": "Biologi",          "name_en": "Biology",         "default_lang": "bm"},
]

# 2 demo students for prototype testing.
_SEED_STUDENTS = [
    {"name": "Muhammad Khalish", "class_name": "5A", "device_id": "studyaid-01"},
    {"name": "Rania Batrisyia",  "class_name": "5A", "device_id": "studyaid-02"},
]


def seed_initial_data(app):
    """
    Populate subjects and students if their tables are empty.
    Safe to call on every boot - does nothing once data exists.
    """
    with app.app_context():
        if Subject.query.count() == 0:
            for s in _SEED_SUBJECTS:
                db.session.add(Subject(**s))
            print(f"[DB] Seeded {len(_SEED_SUBJECTS)} subjects")
        if Student.query.count() == 0:
            for s in _SEED_STUDENTS:
                db.session.add(Student(**s))
            print(f"[DB] Seeded {len(_SEED_STUDENTS)} demo students")
        db.session.commit()
