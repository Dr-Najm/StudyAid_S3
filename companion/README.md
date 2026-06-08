# StudyAid Companion App

A laptop-side Flask app that pairs with the StudyAid wearable device in
Companion Mode. It hosts the study planner, Gemini-generated quiz bank,
live quiz popup, progress dashboard, and teacher classroom view.

This is the v9 development build. Week 1 scope = Flask skeleton + SQLite
schema only. No real UI or device communication yet.

---

## Prerequisites

- **Python 3.12** (other 3.10+ versions should work but are untested)
- **pip** (bundled with Python)
- A modern browser (Chrome, Edge, Firefox)
- A Google AI Studio Gemini API key (free tier is fine) - **only needed
  from Week 3 onwards**, you can leave the placeholder for now

---

## First-time setup

Open a terminal in this `companion/` folder and run:

```
python -m venv venv
```

Activate the virtual environment:

- **Windows (PowerShell):**  `venv\Scripts\Activate.ps1`
- **Windows (cmd.exe):**     `venv\Scripts\activate.bat`
- **macOS / Linux:**         `source venv/bin/activate`

Install dependencies:

```
pip install -r requirements.txt
```

Create your config file from the template:

- **Windows:** `copy config.example.ini config.ini`
- **macOS / Linux:** `cp config.example.ini config.ini`

Edit `config.ini` and replace `YOUR_GEMINI_API_KEY_HERE` with your key.
(Not required until Week 3.)

---

## Running the app

With the virtual environment active:

```
python app.py
```

You should see startup output like:

```
[STARTUP] Config source: config.ini | Server: 0.0.0.0:5000 (debug=True) | ...
[DB] Seeded 8 subjects
[DB] Seeded 2 demo students
 * Running on http://0.0.0.0:5000
```

Open <http://localhost:5000> in your browser. You should see the
StudyAid home page with status counts and links to the four route groups.

---

## What to verify in Week 1

1. Home page loads and shows `2 students, 8 subjects`.
2. `/student/` shows "Hi Ali bin Ahmad".
3. `/teacher/` shows "Class 5A: 2 students seeded".
4. `/teacher/students` returns a JSON array with Ali and Siti.
5. `/admin/` shows the placeholder.
6. The `/api/session/*` endpoints respond to POSTs (test with curl or
   Postman):

   ```
   curl -X POST http://localhost:5000/api/session/start \
        -H "Content-Type: application/json" \
        -d "{\"device_id\":\"studyaid-01\",\"subject_id\":3,\"start_ts\":\"2026-05-26T10:00:00\"}"
   ```

   Expected response: `{"session_id": 1}` and a line in the server console
   logging the payload.

---

## Troubleshooting

**Port 5000 already in use.** Edit `config.ini` and change `port = 5000`
to e.g. `5001`. Then open <http://localhost:5001>.

**`ModuleNotFoundError: No module named 'flask'`.** The virtual
environment is not activated, or dependencies are not installed. Re-run
`pip install -r requirements.txt` with `venv` active.

**`config.ini` not found warning.** The app falls back to
`config.example.ini`. Copy the example to `config.ini` to silence the
warning.

**Database errors after schema changes.** During development you can wipe
the database by deleting `studyaid.db`. It will be recreated and reseeded
on next launch. Do not do this once you have real session data.

---

## Folder structure

```
companion/
  app.py                    Flask entry point
  config.py                 Config loader
  config.example.ini        Template config (committed)
  config.ini                Real config (gitignored)
  requirements.txt          Pinned dependencies
  studyaid.db               SQLite file (auto-created, gitignored)
  models/
    schema.py               9-table schema + seed helpers
  routes/
    student.py              /student/* routes
    teacher.py              /teacher/* routes
    admin.py                /admin/* routes
    api.py                  /api/* device endpoints
  templates/
    base.html
    student/home.html
  static/                   CSS/JS (used from Week 3)
```
