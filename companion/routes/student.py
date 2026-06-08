"""
Student-facing routes. Week 1 stubs only - real planner/dashboard UI in W3+.
"""

from flask import Blueprint, jsonify, render_template
from models.schema import Student

student_bp = Blueprint("student", __name__, url_prefix="/student")


@student_bp.route("/")
def home():
    # For the prototype, treat the first student in the DB as the active user.
    student = Student.query.first()
    return render_template("student/home.html", student=student)


@student_bp.route("/plan")
def plan():
    # Empty stub - real planner data comes in Week 3.
    return jsonify({"slots": [], "deadlines": []})
