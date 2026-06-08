"""
Teacher dashboard routes. Week 1 stubs - real multi-device view in Week 8.
"""

from flask import Blueprint, jsonify
from models.schema import Student

teacher_bp = Blueprint("teacher", __name__, url_prefix="/teacher")


@teacher_bp.route("/")
def home():
    count = Student.query.count()
    return (
        f"<h1>Teacher dashboard placeholder</h1>"
        f"<p>Class 5A: {count} students seeded.</p>"
        f'<p><a href="/teacher/students">View students JSON</a></p>'
        f'<p><a href="/">Back home</a></p>'
    )


@teacher_bp.route("/students")
def students():
    rows = [s.to_dict() for s in Student.query.order_by(Student.id).all()]
    return jsonify(rows)
