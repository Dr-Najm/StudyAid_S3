"""
Admin routes.
Week 3: Gemini quiz bank generation + bank viewer.
Week 8: multi-class teacher view (stub for now).
"""

import json
from datetime import datetime
from flask import Blueprint, flash, render_template, request, redirect, url_for
from models.schema import db, Subject, QuizBank, QuizQuestion
import config

admin_bp = Blueprint("admin", __name__, url_prefix="/admin")


# ── Helper: call Gemini API ─────────────────────────────────────────────────
def _generate_questions_gemini(subject_name_bm, subject_name_en,
                                default_lang, topic, n=10):
    """
    Call Gemini to generate n multiple-choice questions for a given topic.
    Returns a list of dicts: {question_text, options (list of 4), correct_index, language}
    Raises RuntimeError on any failure so the caller can flash a user-friendly message.
    """
    import google.generativeai as genai

    if not config.GEMINI_API_KEY or config.GEMINI_API_KEY == "YOUR_GEMINI_API_KEY_HERE":
        raise RuntimeError(
            "Kunci API Gemini belum ditetapkan. "
            "Isi 'api_key' dalam config.ini bahagian [gemini]."
        )

    genai.configure(api_key=config.GEMINI_API_KEY)
    model = genai.GenerativeModel(config.GEMINI_MODEL)

    # Quiz language follows subject default — BM or English
    if default_lang == "en":
        lang_instruction = "Generate the questions and options in English."
        subject_label = subject_name_en
    else:
        lang_instruction = "Jana soalan dan pilihan jawapan dalam Bahasa Malaysia."
        subject_label = subject_name_bm

    prompt = f"""You are an exam question generator for Malaysian SPM students.
Generate exactly {n} multiple-choice questions about the topic: "{topic}"
Subject: {subject_label}

Rules:
- {lang_instruction}
- Each question must have exactly 4 options (A, B, C, D).
- Only one option is correct.
- Questions must be factually accurate and appropriate for SPM level.
- Assign a difficulty: 1 = easy (direct recall of a single fact), 2 = hard (requires understanding, analysis, or less obvious knowledge). Generate an equal mix.
- Return ONLY a valid JSON array. No preamble, no markdown, no explanation.

Required JSON format:
[
  {{
    "question_text": "...",
    "options": ["option A", "option B", "option C", "option D"],
    "correct_index": 0,
    "language": "{default_lang}",
    "difficulty": 1
  }}
]"""

    response = model.generate_content(prompt)
    raw = response.text.strip()

    # Strip markdown fences if Gemini wraps the output anyway
    if raw.startswith("```"):
        lines = raw.splitlines()
        raw = "\n".join(
            line for line in lines
            if not line.strip().startswith("```")
        )

    try:
        questions = json.loads(raw)
    except json.JSONDecodeError as e:
        raise RuntimeError(f"Gemini mengembalikan JSON tidak sah: {e}\n\nRaw: {raw[:300]}")

    if not isinstance(questions, list) or len(questions) == 0:
        raise RuntimeError("Gemini mengembalikan senarai kosong.")

    # Validate shape of each question
    for i, q in enumerate(questions):
        if not all(k in q for k in ("question_text", "options", "correct_index")):
            raise RuntimeError(f"Soalan #{i+1} tidak lengkap: {q}")
        if len(q["options"]) != 4:
            raise RuntimeError(f"Soalan #{i+1} tidak mempunyai 4 pilihan.")
        if not (0 <= q["correct_index"] <= 3):
            raise RuntimeError(f"Soalan #{i+1} mempunyai correct_index tidak sah.")
        # v10.3: coerce missing/invalid difficulty to 1
        if q.get("difficulty") not in (1, 2):
            q["difficulty"] = 1

    return questions


# ── Helper: classify topic into a study profile (v11) ──────────────────────
def _classify_topic_profile(subject_name_bm, topic):
    """
    Call Gemini to classify a (subject, topic) pair into one of three study
    profiles: "Menulis", "Membaca", or "Campuran".

    Returns one of those three strings. On any API failure or unrecognised
    response, returns "Campuran" (safe default — same as current v10 behaviour).

    Kept strict and small by design: the prompt accepts ONLY a single word.
    Any value outside the three legal labels is coerced to "Campuran" rather
    than raising so that topic save is never blocked by a Gemini error.
    """
    import google.generativeai as genai

    if not config.GEMINI_API_KEY or config.GEMINI_API_KEY == "YOUR_GEMINI_API_KEY_HERE":
        print("[Gemini] Kunci API tidak ditetapkan — profil Campuran digunakan.")
        return "Campuran"

    try:
        genai.configure(api_key=config.GEMINI_API_KEY)
        model = genai.GenerativeModel(config.GEMINI_MODEL)

        prompt = f"""You are a study behaviour classifier for Malaysian SPM students.

Classify the following subject and topic into exactly ONE of these three study profiles:

- Menulis  : topic primarily involving active writing, solving problems, or exercises
             (e.g. Matematik problem sets, essay drafting, Fizik calculations)
- Membaca  : topic primarily involving reading, memorising, or reviewing content
             while holding a book or notes (e.g. Sejarah revision, Pendidikan Islam
             memorisation, Geografi recall)
- Campuran : topic involving a roughly equal mix of reading and writing, or does not
             clearly fit either profile

Subject: {subject_name_bm}
Topic: {topic}

Reply with ONLY one word — Menulis, Membaca, or Campuran. No punctuation, no explanation."""

        response = model.generate_content(prompt)
        raw = response.text.strip().strip(".")

        if raw in ("Menulis", "Membaca", "Campuran"):
            print(f"[Gemini] Profil untuk '{subject_name_bm} / {topic}': {raw}")
            return raw

        # Coerce unrecognised response (e.g. mixed-case, extra words)
        print(f"[Gemini] Respons profil tidak dikenali: '{raw}' — Campuran digunakan.")
        return "Campuran"

    except Exception as e:
        print(f"[Gemini] Ralat klasifikasi profil: {e} — Campuran digunakan.")
        return "Campuran"


# ── Helper: seed demo bank (offline fallback) ───────────────────────────────
def _seed_demo_bank():
    """
    Seed a pre-written Sejarah quiz bank for offline demo use.
    Only runs once — skipped if the bank already exists.
    """
    subject = Subject.query.filter_by(name_bm="Sejarah").first()
    if not subject:
        return

    topic = "Kemerdekaan Malaysia"
    existing = QuizBank.query.filter_by(
        subject_id=subject.id, topic=topic, source="manual"
    ).first()
    if existing:
        return  # Already seeded

    bank = QuizBank(
        subject_id=subject.id,
        topic=topic,
        generated_at=datetime(2026, 1, 1),
        source="manual",
    )
    db.session.add(bank)
    db.session.flush()  # Get bank.id before adding questions

    demo_questions = [
        {
            "question_text": "Pada tarikh berapakah Persekutuan Tanah Melayu mencapai kemerdekaan?",
            "options": ["31 Ogos 1957", "16 September 1963", "31 Ogos 1960", "1 Januari 1958"],
            "correct_index": 0,
            "language": "bm",
            "difficulty": 1,  # easy: direct date recall
        },
        {
            "question_text": "Siapakah Perdana Menteri pertama Malaysia?",
            "options": ["Tun Abdul Razak", "Tun Hussein Onn",
                        "Tunku Abdul Rahman", "Tun Dr. Mahathir Mohamad"],
            "correct_index": 2,
            "language": "bm",
            "difficulty": 1,  # easy: well-known fact
        },
        {
            "question_text": "Apakah nama perjanjian yang membawa kepada kemerdekaan Tanah Melayu?",
            "options": ["Perjanjian Pangkor", "Perjanjian London",
                        "Perjanjian Persekutuan", "Perjanjian Bangkok"],
            "correct_index": 1,
            "language": "bm",
            "difficulty": 2,  # hard: less obvious, requires specific knowledge
        },
        {
            "question_text": "Malaysia ditubuhkan pada tahun?",
            "options": ["1957", "1960", "1963", "1965"],
            "correct_index": 2,
            "language": "bm",
            "difficulty": 1,  # easy: direct year recall
        },
        {
            "question_text": "Siapakah Yang di-Pertuan Agong pertama Malaysia?",
            "options": ["Sultan Hisamuddin", "Tuanku Abdul Halim",
                        "Tuanku Abdul Rahman", "Sultan Ismail Nasiruddin"],
            "correct_index": 2,
            "language": "bm",
            "difficulty": 2,  # hard: easily confused with other figures
        },
        {
            "question_text": "Apakah nama parti yang mengetuai kemerdekaan Tanah Melayu?",
            "options": ["DAP", "MIC", "MCA", "UMNO"],
            "correct_index": 3,
            "language": "bm",
            "difficulty": 1,  # easy: prominent fact
        },
        {
            "question_text": "Berapakah bilangan negeri yang membentuk Malaysia pada 1963?",
            "options": ["11", "13", "14", "12"],
            "correct_index": 1,
            "language": "bm",
            "difficulty": 2,  # hard: specific number, easily confused
        },
        {
            "question_text": "Negara manakah yang berpisah daripada Malaysia pada tahun 1965?",
            "options": ["Brunei", "Singapura", "Sarawak", "Sabah"],
            "correct_index": 1,
            "language": "bm",
            "difficulty": 1,  # easy: well-known historical event
        },
        {
            "question_text": "Apakah slogan yang digunakan semasa perayaan kemerdekaan pertama?",
            "options": ["Malaysia Boleh", "Merdeka", "Bersatu Teguh", "Satu Malaysia"],
            "correct_index": 1,
            "language": "bm",
            "difficulty": 1,  # easy: iconic word
        },
        {
            "question_text": "Di manakah pengisytiharan kemerdekaan Tanah Melayu dibacakan?",
            "options": ["Stadium Merdeka", "Dataran Merdeka",
                        "Padang Kelab Selangor", "Bangunan Sultan Abdul Samad"],
            "correct_index": 0,
            "language": "bm",
            "difficulty": 2,  # hard: specific venue, easily confused
        },
    ]

    for q in demo_questions:
        db.session.add(QuizQuestion(
            bank_id=bank.id,
            question_text=q["question_text"],
            options_json=json.dumps(q["options"], ensure_ascii=False),
            correct_index=q["correct_index"],
            language=q["language"],
            difficulty=q["difficulty"],  # v10.3
        ))

    db.session.commit()
    print(f"[DB] Demo bank seeded: Sejarah — {topic} ({len(demo_questions)} soalan)")


# ── Overview (stub for Week 8) ──────────────────────────────────────────────
@admin_bp.route("/")
def home():
    return redirect(url_for("admin.generate"))


# ── Generate page ───────────────────────────────────────────────────────────
@admin_bp.route("/generate", methods=["GET", "POST"])
def generate():
    # Seed demo bank on first visit if not already present
    _seed_demo_bank()

    subjects = Subject.query.order_by(Subject.name_bm).all()

    # Load all existing banks with subject name + question count
    banks_raw = (
        db.session.query(QuizBank, Subject.name_bm)
        .join(Subject, QuizBank.subject_id == Subject.id)
        .order_by(QuizBank.generated_at.desc())
        .all()
    )
    banks = []
    for bank, subject_name in banks_raw:
        q_count = QuizQuestion.query.filter_by(bank_id=bank.id).count()
        b = bank
        b.subject_name = subject_name
        b.question_count = q_count
        banks.append(b)

    # Default template context
    ctx = dict(
        subjects=subjects,
        banks=banks,
        questions=None,
        topic=None,
        selected_subject=None,
        bank_id=None,
    )

    if request.method == "POST":
        subject_id = int(request.form["subject_id"])
        topic = request.form["topic"].strip()
        subject = db.session.get(Subject, subject_id)
        ctx["topic"] = topic
        ctx["selected_subject"] = subject_id

        try:
            raw_questions = _generate_questions_gemini(
                subject.name_bm, subject.name_en, subject.default_lang, topic
            )

            # Store bank + questions
            bank = QuizBank(
                subject_id=subject_id,
                topic=topic,
                generated_at=datetime.utcnow(),
                source="gemini",
            )
            db.session.add(bank)
            db.session.flush()

            for q in raw_questions:
                db.session.add(QuizQuestion(
                    bank_id=bank.id,
                    question_text=q["question_text"],
                    options_json=json.dumps(q["options"], ensure_ascii=False),
                    correct_index=q["correct_index"],
                    language=q.get("language", subject.default_lang),
                    difficulty=q.get("difficulty", 1),  # v10.3
                ))
            db.session.commit()
            flash(f"{len(raw_questions)} soalan berjaya dijana dan disimpan.", "ok")

            # Attach display-friendly options to questions for template
            for q in raw_questions:
                q["options_json"] = json.dumps(q["options"])
            ctx["questions"] = raw_questions
            ctx["bank_id"] = bank.id

            # Refresh banks list to include the new one
            banks_raw2 = (
                db.session.query(QuizBank, Subject.name_bm)
                .join(Subject, QuizBank.subject_id == Subject.id)
                .order_by(QuizBank.generated_at.desc())
                .all()
            )
            banks2 = []
            for b2, sname in banks_raw2:
                b2.subject_name = sname
                b2.question_count = QuizQuestion.query.filter_by(bank_id=b2.id).count()
                banks2.append(b2)
            ctx["banks"] = banks2

        except RuntimeError as e:
            db.session.rollback()
            flash(str(e), "err")

    return render_template("admin/generate.html", **ctx)


# ── View existing bank ──────────────────────────────────────────────────────
@admin_bp.route("/generate/view/<int:bank_id>")
def view_bank(bank_id):
    bank = db.session.get(QuizBank, bank_id)
    if not bank:
        flash("Bank soalan tidak dijumpai.", "err")
        return redirect(url_for("admin.generate"))

    subject = db.session.get(Subject, bank.subject_id)
    bank.subject_name = subject.name_bm

    questions = QuizQuestion.query.filter_by(bank_id=bank_id).all()
    # Parse options_json for template use
    for q in questions:
        q.options = json.loads(q.options_json)

    return render_template("admin/view_bank.html", bank=bank, questions=questions)
