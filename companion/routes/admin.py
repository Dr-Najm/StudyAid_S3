"""
Admin (multi-class) routes. Week 1 stub - real view in Week 8.
"""

from flask import Blueprint

admin_bp = Blueprint("admin", __name__, url_prefix="/admin")


@admin_bp.route("/")
def home():
    return (
        "<h1>Admin overview placeholder</h1>"
        "<p>Multi-class view coming in Week 8.</p>"
        '<p><a href="/">Back home</a></p>'
    )
