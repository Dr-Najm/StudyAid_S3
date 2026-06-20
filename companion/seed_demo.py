"""
StudyAid Demo Seed Data — v11
Run from inside companion/ with venv active:
    python seed_demo.py

Populates 2 weeks of realistic session history for Muhammad Khalish (studyaid-01)
and Rania Batrisyia (studyaid-02), one topic deadline per subject per student with
AI-classified profiles, and a Gemini-generated quiz bank per topic.

v11 changes vs previous seed:
  - Subject list updated to v11 (BM, Matematik, Sejarah, Geografi, Pend Islam,
    Fizik, Kimia, Biologi)
  - 8 topics per student (one per subject) instead of 3
  - TopicDeadline rows include profile= matching Gemini classification
  - Quiz bank generated via Gemini for each topic at seed time
    (~16 calls, expect 60-90 seconds total)
  - Profiles are hardcoded to match _classify_topic_profile() output —
    avoids 16 extra classification calls while keeping results consistent

Run time: ~60-90 seconds (Gemini quiz generation).
Safe to re-run — checks for existing sessions before inserting.
"""

import json
import random
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from datetime import date, datetime, timedelta
from app import create_app
from models.schema import (db, Student, Subject, WeeklySlot, Session,
                            TopicDeadline, QuizBank, QuizQuestion, QuizAnswer)

app = create_app()
random.seed(42)

TODAY       = date.today()
MONDAY_THIS = TODAY - timedelta(days=TODAY.weekday())
MONDAY_LAST = MONDAY_THIS - timedelta(weeks=1)


# ── Helpers ──────────────────────────────────────────────────────────────────
def make_session(student_id, subject_id, day, hour, active_min, focus_score):
    start = datetime.combine(day, datetime.min.time()).replace(hour=hour, minute=0)
    end   = start + timedelta(minutes=active_min + random.randint(0, 10))
    return Session(
        student_id  = student_id,
        subject_id  = subject_id,
        start_ts    = start,
        end_ts      = end,
        active_min  = active_min,
        idle_min    = random.randint(3, 12),
        focus_score = focus_score + random.uniform(-5, 5),
    )


def add_quiz_answers(session_id, bank_questions, correct_rate):
    if not bank_questions:
        return
    sampled = random.sample(bank_questions, min(5, len(bank_questions)))
    for q in sampled:
        correct = random.random() < correct_rate
        chosen  = q.correct_index if correct else (q.correct_index + 1) % 4
        db.session.add(QuizAnswer(
            session_id   = session_id,
            question_id  = q.id,
            chosen_index = chosen,
            correct      = correct,
            ts           = datetime.utcnow(),
        ))


def generate_quiz_bank(subject, topic):
    """
    Generate a quiz bank for (subject, topic) via Gemini and store it.
    Skips silently if a bank for this (subject_id, topic) already exists.
    Returns the QuizBank object (existing or new), or None on failure.
    """
    from routes.admin import _generate_questions_gemini

    existing = QuizBank.query.filter_by(
        subject_id=subject.id, topic=topic
    ).first()
    if existing:
        q_count = QuizQuestion.query.filter_by(bank_id=existing.id).count()
        print(f"  [SKIP] Bank sedia ada: {subject.name_bm} / {topic} ({q_count} soalan)")
        return existing

    print(f"  [GEN]  Jana soalan: {subject.name_bm} / {topic} ...", end="", flush=True)
    try:
        raw_questions = _generate_questions_gemini(
            subject.name_bm, subject.name_en, subject.default_lang, topic
        )
        bank = QuizBank(
            subject_id   = subject.id,
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
        print(f" {len(raw_questions)} soalan OK")
        return bank
    except RuntimeError as e:
        db.session.rollback()
        print(f" GAGAL ({e})")
        return None


# ── Main ─────────────────────────────────────────────────────────────────────
with app.app_context():
    khalish = Student.query.filter_by(device_id="studyaid-01").first()
    rania   = Student.query.filter_by(device_id="studyaid-02").first()

    if not khalish or not rania:
        print("ERROR: Demo students not found. Run python app.py first.")
        sys.exit(1)

    existing = Session.query.filter_by(student_id=khalish.id).count()
    if existing > 0:
        print(f"Sessions already exist ({existing} rows). Delete studyaid.db to reseed.")
        sys.exit(0)

    # ── v11 Subject IDs and objects ──────────────────────────────────────────
    # 1=Bahasa Melayu, 2=Matematik, 3=Sejarah, 4=Geografi,
    # 5=Pendidikan Islam, 6=Fizik, 7=Kimia, 8=Biologi
    S_obj = {s.name_bm: s for s in Subject.query.all()}

    def subj(name): return S_obj[name]

    BM  = subj("Bahasa Melayu").id
    MTH = subj("Matematik").id
    SEJ = subj("Sejarah").id
    GEO = subj("Geografi").id
    PI  = subj("Pendidikan Islam").id
    FIZ = subj("Fizik").id
    KIM = subj("Kimia").id
    BIO = subj("Biologi").id

    print(f"[SEED] Subjek: BM={BM} MTH={MTH} SEJ={SEJ} GEO={GEO} "
          f"PI={PI} FIZ={FIZ} KIM={KIM} BIO={BIO}")

    # ── Weekly slots ─────────────────────────────────────────────────────────
    khalish_slots = [
        WeeklySlot(student_id=khalish.id, subject_id=MTH, day_of_week=0, start_time="16:00", duration_min=60),
        WeeklySlot(student_id=khalish.id, subject_id=SEJ, day_of_week=1, start_time="16:00", duration_min=60),
        WeeklySlot(student_id=khalish.id, subject_id=BIO, day_of_week=2, start_time="15:30", duration_min=45),
        WeeklySlot(student_id=khalish.id, subject_id=BM,  day_of_week=3, start_time="16:00", duration_min=60),
        WeeklySlot(student_id=khalish.id, subject_id=MTH, day_of_week=4, start_time="16:00", duration_min=60),
        WeeklySlot(student_id=khalish.id, subject_id=SEJ, day_of_week=5, start_time="10:00", duration_min=90),
    ]
    rania_slots = [
        WeeklySlot(student_id=rania.id, subject_id=BM,  day_of_week=0, start_time="15:00", duration_min=60),
        WeeklySlot(student_id=rania.id, subject_id=FIZ, day_of_week=1, start_time="16:00", duration_min=60),
        WeeklySlot(student_id=rania.id, subject_id=BM,  day_of_week=3, start_time="15:00", duration_min=60),
        WeeklySlot(student_id=rania.id, subject_id=GEO, day_of_week=4, start_time="16:00", duration_min=60),
        WeeklySlot(student_id=rania.id, subject_id=FIZ, day_of_week=5, start_time="09:00", duration_min=60),
    ]
    for sl in khalish_slots + rania_slots:
        db.session.add(sl)

    # ── Sessions — last week ─────────────────────────────────────────────────
    sessions_created = []

    khalish_last_week = [
        (0, MTH, 16, 58, 87),
        (1, SEJ, 16, 55, 83),
        (2, BIO, 15, 40, 79),
        (3, BM,  16, 52, 85),
        (4, MTH, 16, 60, 90),
        (5, SEJ, 10, 85, 88),
        (3, FIZ, 19, 30, 75),
    ]
    for dow, sid, hr, mins, focus in khalish_last_week:
        day = MONDAY_LAST + timedelta(days=dow)
        s   = make_session(khalish.id, sid, day, hr, mins, focus)
        db.session.add(s); db.session.flush()
        sessions_created.append((s.id, sid))

    rania_last_week = [
        (0, BM,  15, 45, 72),
        (1, FIZ, 16, 50, 68),
        (3, BM,  15, 40, 70),
        (4, GEO, 16, 30, 60),
        (6, PI,  14, 35, 65),
    ]
    for dow, sid, hr, mins, focus in rania_last_week:
        day = MONDAY_LAST + timedelta(days=dow)
        s   = make_session(rania.id, sid, day, hr, mins, focus)
        db.session.add(s); db.session.flush()
        sessions_created.append((s.id, sid))

    # ── Sessions — this week ─────────────────────────────────────────────────
    days_so_far = TODAY.weekday()

    khalish_this_week = [
        (0, MTH, 16, 60, 88),
        (1, SEJ, 16, 57, 84),
        (2, BIO, 15, 42, 80),
        (3, BM,  16, 55, 86),
        (4, MTH, 16, 62, 91),
        (5, SEJ, 10, 88, 89),
        (6, FIZ, 15, 40, 78),
    ]
    rania_this_week = [
        (0, BM,  15, 48, 74),
        (1, FIZ, 16, 52, 69),
        (3, BM,  15, 45, 71),
        (4, GEO, 16, 35, 62),
        (5, FIZ, 14, 55, 70),
        (6, BM,  10, 40, 68),
    ]

    for dow, sid, hr, mins, focus in khalish_this_week:
        if dow > days_so_far: break
        day = MONDAY_THIS + timedelta(days=dow)
        s   = make_session(khalish.id, sid, day, hr, mins, focus)
        db.session.add(s); db.session.flush()
        sessions_created.append((s.id, sid))

    for dow, sid, hr, mins, focus in rania_this_week:
        if dow > days_so_far: break
        day = MONDAY_THIS + timedelta(days=dow)
        s   = make_session(rania.id, sid, day, hr, mins, focus)
        db.session.add(s); db.session.flush()
        sessions_created.append((s.id, sid))

    db.session.commit()
    print(f"[SEED] {len(sessions_created)} sesi dicipta")

    # ── Quiz banks — one per topic via Gemini ────────────────────────────────
    # Profiles are hardcoded to match what _classify_topic_profile() returns,
    # saving 16 extra API calls while keeping results identical.
    #
    # Profile rationale:
    #   Menulis  — problem sets, calculations, exercises (Matematik, Fizik, Kimia)
    #   Membaca  — memorisation, recall, content review (Sejarah, Geografi, Pend Islam)
    #   Campuran — mixed reading and writing (Bahasa Melayu, Biologi)

    print("\n[SEED] Jana bank kuiz (ini mengambil masa ~60-90 saat) ...")

    # (subject_name_bm, topic, profile, deadline_days, student)
    # deadline_days: days from TODAY for TopicDeadline row
    KHALISH_TOPICS = [
        ("Bahasa Melayu",    "Penulisan Karangan — Jenis dan Format",          "Campuran", 10,  "in_progress"),
        ("Matematik",        "Ungkapan Algebra dan Persamaan Linear",           "Menulis",  14,  "pending"),
        ("Sejarah",          "Bab 5 — Kemerdekaan dan Pembangunan Negara",      "Membaca",  5,   "in_progress"),
        ("Geografi",         "Bentuk Muka Bumi Malaysia",                       "Membaca",  21,  "pending"),
        ("Pendidikan Islam", "Akhlak Mahmudah dalam Kehidupan",                 "Membaca",  18,  "pending"),
        ("Fizik",            "Daya dan Tekanan",                                "Menulis",  25,  "pending"),
        ("Kimia",            "Ikatan Kimia dan Struktur Sebatian",              "Menulis",  28,  "pending"),
        ("Biologi",          "Sistem Respirasi Manusia",                        "Campuran", 20,  "pending"),
    ]

    RANIA_TOPICS = [
        ("Bahasa Melayu",    "Karangan Jenis Perbahasan",                       "Campuran", 3,   "in_progress"),
        ("Matematik",        "Statistik — Min, Mod dan Median",                 "Menulis",  12,  "pending"),
        ("Sejarah",          "Bab 3 — Nasionalisme di Malaysia",                "Membaca",  9,   "pending"),
        ("Geografi",         "Iklim dan Cuaca di Malaysia",                     "Membaca",  8,   "pending"),
        ("Pendidikan Islam", "Fardhu Ain — Solat dan Puasa",                   "Membaca",  16,  "pending"),
        ("Fizik",            "Daya dan Hukum Newton",                           "Menulis",  15,  "pending"),
        ("Kimia",            "Asid, Bes dan Garam",                             "Menulis",  22,  "pending"),
        ("Biologi",          "Sistem Penghadaman Manusia",                      "Campuran", 19,  "pending"),
    ]

    bank_count    = 0
    question_count = 0

    # Generate banks for all unique (subject, topic) pairs across both students
    # Check for duplicates so shared topics (if any) aren't generated twice
    generated_banks = {}  # (subject_id, topic) -> QuizBank

    all_topic_rows = (
        [(khalish, t) for t in KHALISH_TOPICS] +
        [(rania,   t) for t in RANIA_TOPICS]
    )

    for student, (subj_name, topic, profile, dd, status) in all_topic_rows:
        s_obj  = S_obj[subj_name]
        key    = (s_obj.id, topic)

        if key not in generated_banks:
            bank = generate_quiz_bank(s_obj, topic)
            generated_banks[key] = bank
            if bank:
                bank_count    += 1
                question_count += QuizQuestion.query.filter_by(bank_id=bank.id).count()
        else:
            print(f"  [SKIP] Topik dikongsi: {subj_name} / {topic}")

        db.session.add(TopicDeadline(
            student_id = student.id,
            subject_id = s_obj.id,
            topic      = topic,
            deadline   = TODAY + timedelta(days=dd),
            status     = status,
            profile    = profile,
        ))

    db.session.commit()
    print(f"\n[SEED] {bank_count} bank kuiz dicipta, {question_count} soalan dijana")
    print(f"[SEED] {len(all_topic_rows)} sasaran topik dicipta")

    # ── Quiz answers ─────────────────────────────────────────────────────────
    all_questions = QuizQuestion.query.all()
    answer_count  = 0

    if all_questions:
        for sess_id, sid in random.sample(sessions_created,
                                          min(10, len(sessions_created))):
            sess = db.session.get(Session, sess_id)
            rate = 0.75 if sess.student_id == khalish.id else 0.60
            rate = max(0.2, min(0.95, rate + random.uniform(-0.15, 0.15)))
            add_quiz_answers(sess_id, all_questions, rate)
            answer_count += min(5, len(all_questions))

    db.session.commit()
    print(f"[SEED] ~{answer_count} jawapan kuiz dicipta")

    # ── Summary ──────────────────────────────────────────────────────────────
    print("\n[SEED] Selesai! Data demo berjaya dimasukkan.")
    print(f"\n  Ringkasan DB:")
    print(f"    Khalish sesi   : {Session.query.filter_by(student_id=khalish.id).count()}")
    print(f"    Rania sesi     : {Session.query.filter_by(student_id=rania.id).count()}")
    print(f"    Bank kuiz      : {QuizBank.query.count()}")
    print(f"    Soalan kuiz    : {QuizQuestion.query.count()}")
    print(f"    Jawapan kuiz   : {QuizAnswer.query.count()}")
    print(f"    Sasaran topik  : {TopicDeadline.query.count()}")
    print(f"\n  Profil topik Khalish:")
    for td in TopicDeadline.query.filter_by(student_id=khalish.id)\
              .order_by(TopicDeadline.subject_id).all():
        s = Subject.query.get(td.subject_id)
        print(f"    [{td.profile:8s}] {s.name_bm:20s} — {td.topic}")
    print(f"\n  Profil topik Rania:")
    for td in TopicDeadline.query.filter_by(student_id=rania.id)\
              .order_by(TopicDeadline.subject_id).all():
        s = Subject.query.get(td.subject_id)
        print(f"    [{td.profile:8s}] {s.name_bm:20s} — {td.topic}")
