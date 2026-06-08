"""
Device-facing HTTP API. The StudyAid wearable POSTs to these endpoints in
Companion Mode. Week 1 stubs lock in the request/response shapes so Week 2's
firmware can target a stable contract.

Endpoints:
    POST /api/session/start    -> {session_id}
    POST /api/session/drift    -> {quiz: null}     (no quiz logic until W5)
    POST /api/session/answer   -> {correct: true}  (always true stub)
    POST /api/session/end      -> {ok: true}
"""

from flask import Blueprint, jsonify, request

api_bp = Blueprint("api", __name__, url_prefix="/api")


def _log(tag, payload):
    print(f"[API] {tag}: {payload}")


@api_bp.route("/session/start", methods=["POST"])
def session_start():
    payload = request.get_json(silent=True) or {}
    _log("session/start", payload)
    # Real logic in Week 2: insert Session row, return its real id.
    return jsonify({"session_id": 1})


@api_bp.route("/session/drift", methods=["POST"])
def session_drift():
    payload = request.get_json(silent=True) or {}
    _log("session/drift", payload)
    # Real logic in Week 5: decide whether to push a quiz back.
    return jsonify({"quiz": None})


@api_bp.route("/session/answer", methods=["POST"])
def session_answer():
    payload = request.get_json(silent=True) or {}
    _log("session/answer", payload)
    # Real logic in Week 5: look up correct_index, store QuizAnswer row.
    return jsonify({"correct": True})


@api_bp.route("/session/end", methods=["POST"])
def session_end():
    payload = request.get_json(silent=True) or {}
    _log("session/end", payload)
    # Real logic in Week 2: update Session row with end_ts and totals.
    return jsonify({"ok": True})
