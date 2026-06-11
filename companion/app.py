"""
StudyAid Companion App - Flask entry point.

Usage:
    python app.py

Then open http://localhost:5000 in a browser.
"""

from flask import Flask

import config
from models.schema import db, init_db, seed_initial_data, Student, Subject
from routes import student_bp, teacher_bp, admin_bp, api_bp


def create_app():
    app = Flask(__name__)
    app.config["SQLALCHEMY_DATABASE_URI"] = config.SQLALCHEMY_DATABASE_URI
    app.config["SQLALCHEMY_TRACK_MODIFICATIONS"] = False
    app.config["SECRET_KEY"] = "studyaid-v9-dev-key"  # needed for flash()

    init_db(app)
    if config.SEED_DEMO_DATA:
        seed_initial_data(app)

    app.register_blueprint(student_bp)
    app.register_blueprint(teacher_bp)
    app.register_blueprint(admin_bp)
    app.register_blueprint(api_bp)

    # Jinja2 filter: parse JSON string in templates
    # Usage: {{ q.options_json | fromjson }}
    import json as _json
    app.jinja_env.filters["fromjson"] = _json.loads

    @app.route("/")
    def root():
        with app.app_context():
            n_students = Student.query.count()
            n_subjects = Subject.query.count()
        return (
            "<!doctype html><html><head><title>StudyAid Companion</title>"
            "<style>body{font-family:system-ui,Arial,sans-serif;max-width:800px;"
            "margin:2em auto;padding:0 1em;color:#1a1a1a}"
            "header{border-bottom:2px solid #0D7C7C;padding-bottom:.5em;"
            "margin-bottom:1.5em}h1{margin:0;color:#0B1D3A}"
            "ul{line-height:1.8}a{color:#0D7C7C}"
            "code{background:#F0F5F6;padding:.1em .4em;border-radius:3px}</style>"
            "</head><body><header><h1>StudyAid Companion</h1>"
            "<div style='color:#5A7080;font-size:.9em'>v9 development build</div>"
            "</header>"
            f"<p>Status: DB connected. "
            f"<strong>{n_students}</strong> students, "
            f"<strong>{n_subjects}</strong> subjects.</p>"
            "<h3>Routes</h3>"
            "<ul>"
            "<li><a href='/student/'>Student dashboard</a></li>"
            "<li><a href='/teacher/'>Teacher dashboard</a></li>"
            "<li><a href='/admin/'>Admin overview</a></li>"
            "<li><code>POST /api/session/start</code> (device endpoint)</li>"
            "<li><code>POST /api/session/drift</code> (device endpoint)</li>"
            "<li><code>POST /api/session/answer</code> (device endpoint)</li>"
            "<li><code>POST /api/session/end</code> (device endpoint)</li>"
            "</ul>"
            "</body></html>"
        )

    return app


if __name__ == "__main__":
    print("[STARTUP]", config.summary())
    app = create_app()
    app.run(
        host=config.SERVER_HOST,
        port=config.SERVER_PORT,
        debug=config.SERVER_DEBUG,
    )
