"""
demo_seed.py — Offline demo seeder for the StudyAid booth.

This module is imported by the /admin/settings "Isi Data Demo" button. Unlike
seed_demo.py (the CLI tool that calls Gemini and is used to BUILD the bundle),
this seeder is fully OFFLINE:

  - Sessions, weekly slots, topic deadlines, profiles and quiz answers are
    generated algorithmically (deterministic, no network).
  - Quiz banks are loaded from data/demo_quiz_bundle.json (pre-captured Gemini
    output) instead of calling Gemini live.

It is safe to call from inside a Flask request: it never calls sys.exit() and
returns a summary dict instead of printing-and-exiting.

IMPORTANT: the topic lists below MUST stay in sync with seed_demo.py. If you
change a topic string in one file, change it in the other, or the bundle keys
will no longer match.
"""

import json
import os
import random
from datetime import date, datetime, timedelta

from models.schema import (db, Student, Subject, WeeklySlot, Session,
                            TopicDeadline, QuizBank, QuizQuestion, QuizAnswer)
from models.schema import init_db, seed_initial_data  # noqa: F401  (re-exported)

_BUNDLE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "data", "demo_quiz_bundle.json")

# ── Demo topic plan (keep in sync with seed_demo.py) ─────────────────────────
# (subject_name_bm, topic, profile, deadline_days, status)
KHALISH_TOPICS = [
    ("Bahasa Melayu",    "Penulisan Karangan — Jenis dan Format",      "Campuran", 10, "in_progress"),
    ("Matematik",        "Ungkapan Algebra dan Persamaan Linear",       "Menulis",  14, "pending"),
    ("Sejarah",          "Bab 5 — Kemerdekaan dan Pembangunan Negara",  "Membaca",  5,  "in_progress"),
    ("Geografi",         "Bentuk Muka Bumi Malaysia",                   "Membaca",  21, "pending"),
    ("Pendidikan Islam", "Akhlak Mahmudah dalam Kehidupan",             "Membaca",  18, "pending"),
    ("Fizik",            "Daya dan Tekanan",                            "Menulis",  25, "pending"),
    ("Kimia",            "Ikatan Kimia dan Struktur Sebatian",          "Menulis",  28, "pending"),
    ("Biologi",          "Sistem Respirasi Manusia",                    "Campuran", 20, "pending"),
]

RANIA_TOPICS = [
    ("Bahasa Melayu",    "Karangan Jenis Perbahasan",                   "Campuran", 3,  "in_progress"),
    ("Matematik",        "Statistik — Min, Mod dan Median",             "Menulis",  12, "pending"),
    ("Sejarah",          "Bab 3 — Nasionalisme di Malaysia",            "Membaca",  9,  "pending"),
    ("Geografi",         "Iklim dan Cuaca di Malaysia",                 "Membaca",  8,  "pending"),
    ("Pendidikan Islam", "Fardhu Ain — Solat dan Puasa",                "Membaca",  16, "pending"),
    ("Fizik",            "Daya dan Hukum Newton",                       "Menulis",  15, "pending"),
    ("Kimia",            "Asid, Bes dan Garam",                         "Menulis",  22, "pending"),
    ("Biologi",          "Sistem Penghadaman Manusia",                  "Campuran", 19, "pending"),
]


def _load_bundle():
    """Return {(subject_name_bm, topic): {profile, questions}} from the bundle.
    Returns an empty dict if the bundle file is missing or unreadable."""
    if not os.path.exists(_BUNDLE_PATH):
        return {}
    try:
        with open(_BUNDLE_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        out = {}
        for bank in data.get("banks", []):
            key = (bank["subject_name_bm"], bank["topic"])
            out[key] = bank
        return out
    except (json.JSONDecodeError, KeyError, OSError):
        return {}


def _make_session(student_id, subject_id, day, hour, active_min, focus_score):
    start = datetime.combine(day, datetime.min.time()) + timedelta(hours=hour)
    end   = start + timedelta(minutes=active_min)
    idle  = max(0, int(active_min * random.uniform(0.05, 0.20)))
    return Session(
        student_id=student_id, subject_id=subject_id,
        start_ts=start, end_ts=end,
        active_min=active_min, idle_min=idle, focus_score=focus_score,
    )


def run_demo_seed(wipe=True):
    """
    Populate the database with offline demo data.

    wipe=True  -> drop all tables, recreate, reseed subjects + students first.
    wipe=False -> keep existing rows; only add demo data if no sessions exist.

    Returns a summary dict. Never raises on 'already seeded' — returns counts.
    Caller is responsible for running inside an app context.
    """
    if wipe:
        db.drop_all()
        db.create_all()
        # reseed subjects + students directly (avoid nested app contexts)
        from models.schema import _SEED_SUBJECTS, _SEED_STUDENTS
        for s in _SEED_SUBJECTS:
            db.session.add(Subject(**s))
        for s in _SEED_STUDENTS:
            db.session.add(Student(**s))
        db.session.commit()

    khalish = Student.query.filter_by(device_id="studyaid-01").first()
    rania   = Student.query.filter_by(device_id="studyaid-02").first()
    if not khalish or not rania:
        return {"ok": False, "msg": "Pelajar demo tidak dijumpai."}

    if not wipe and Session.query.filter_by(student_id=khalish.id).count() > 0:
        return {"ok": False, "msg": "Data sesi sudah wujud. Kosongkan dahulu."}

    random.seed(42)
    today       = date.today()
    monday_this = today - timedelta(days=today.weekday())
    monday_last = monday_this - timedelta(weeks=1)

    S_obj = {s.name_bm: s for s in Subject.query.all()}
    bundle = _load_bundle()

    # ── Weekly slots ─────────────────────────────────────────────────────────
    def slots_for(student, plan):
        for (subj_name, day, hh, dur) in plan:
            db.session.add(WeeklySlot(
                student_id=student.id, subject_id=S_obj[subj_name].id,
                day_of_week=day, start_time=hh, duration_min=dur))

    slots_for(khalish, [
        ("Matematik", 0, "16:00", 60), ("Sejarah", 1, "16:00", 60),
        ("Biologi", 2, "15:30", 45), ("Bahasa Melayu", 3, "16:00", 60),
        ("Fizik", 4, "17:00", 45),
    ])
    slots_for(rania, [
        ("Bahasa Melayu", 0, "20:00", 45), ("Matematik", 1, "20:00", 60),
        ("Geografi", 3, "19:30", 45), ("Sejarah", 4, "20:00", 60),
    ])
    db.session.commit()

    # ── Sessions (2 weeks) ───────────────────────────────────────────────────
    session_count = 0
    # Khalish: consistent, good focus (80-91)
    for wk_monday in (monday_last, monday_this):
        for (subj, day, hour, dur) in [
            ("Matematik", 0, 16, 55), ("Sejarah", 1, 16, 60),
            ("Biologi", 2, 15, 45), ("Bahasa Melayu", 3, 16, 50),
            ("Fizik", 4, 17, 40), ("Matematik", 5, 10, 65), ("Sejarah", 6, 11, 50),
        ]:
            d = wk_monday + timedelta(days=day)
            if d > today:
                continue
            db.session.add(_make_session(
                khalish.id, S_obj[subj].id, d, hour, dur,
                random.randint(80, 91)))
            session_count += 1

    # Rania: weaker focus (60-74), misses Wednesdays
    for wk_monday in (monday_last, monday_this):
        for (subj, day, hour, dur) in [
            ("Bahasa Melayu", 0, 20, 50), ("Matematik", 1, 20, 45),
            ("Geografi", 3, 19, 40), ("Sejarah", 4, 20, 55),
            ("Bahasa Melayu", 5, 15, 60),
        ]:
            d = wk_monday + timedelta(days=day)
            if d > today:
                continue
            db.session.add(_make_session(
                rania.id, S_obj[subj].id, d, hour, dur,
                random.randint(60, 74)))
            session_count += 1
    db.session.commit()

    # ── Topic deadlines + quiz banks (from bundle) ───────────────────────────
    all_rows = ([(khalish, t) for t in KHALISH_TOPICS] +
                [(rania,   t) for t in RANIA_TOPICS])

    made_banks = {}          # (subject_id, topic) -> QuizBank
    bank_count = q_count = 0
    missing_banks = []

    for student, (subj_name, topic, profile, dd, status) in all_rows:
        s_obj = S_obj[subj_name]
        key   = (s_obj.id, topic)

        if key not in made_banks:
            entry = bundle.get((subj_name, topic))
            if entry and entry.get("questions"):
                bank = QuizBank(subject_id=s_obj.id, topic=topic,
                                generated_at=datetime.utcnow(), source="bundle")
                db.session.add(bank)
                db.session.flush()
                for q in entry["questions"]:
                    db.session.add(QuizQuestion(
                        bank_id=bank.id,
                        question_text=q["question_text"],
                        options_json=json.dumps(q["options"], ensure_ascii=False),
                        correct_index=q["correct_index"],
                        language=q.get("language", s_obj.default_lang),
                        difficulty=q.get("difficulty", 1)))
                    q_count += 1
                made_banks[key] = bank
                bank_count += 1
            else:
                made_banks[key] = None
                missing_banks.append(f"{subj_name} / {topic}")

        db.session.add(TopicDeadline(
            student_id=student.id, subject_id=s_obj.id, topic=topic,
            deadline=today + timedelta(days=dd), status=status, profile=profile))
    db.session.commit()

    # ── Quiz answers (history) for banks that have questions ─────────────────
    ans_count = 0
    for (student, rate) in [(khalish, 0.75), (rania, 0.60)]:
        for bank in [b for b in made_banks.values() if b]:
            qs = QuizQuestion.query.filter_by(bank_id=bank.id).all()
            for q in random.sample(qs, min(3, len(qs))):
                opts = json.loads(q.options_json)
                correct = random.random() < rate
                chosen = q.correct_index if correct else \
                    random.choice([i for i in range(len(opts)) if i != q.correct_index])
                db.session.add(QuizAnswer(
                    session_id=None, question_id=q.id,
                    chosen_index=chosen, correct=correct,
                    ts=datetime.utcnow()))
                ans_count += 1
    db.session.commit()

    return {
        "ok": True,
        "sessions": session_count,
        "banks": bank_count,
        "questions": q_count,
        "answers": ans_count,
        "topics": len(all_rows),
        "missing_banks": missing_banks,
    }
