from flask import Blueprint, jsonify, Response
import json
from queue import Queue

from parking_logic import parking_state, clients

slot_bp = Blueprint('slot', __name__)

@slot_bp.route('/api/parking_state')
def get_parking_state():
    return jsonify(parking_state)

@slot_bp.route('/stream')
def stream():
    def event_stream():
        q = Queue()
        clients.append(q)

        yield f"data: {json.dumps(parking_state)}\n\n"

        try:
            while True:
                data = q.get()
                yield f"data: {data}\n\n"
        except GeneratorExit:
            if q in clients:
                clients.remove(q)

    return Response(event_stream(), mimetype="text/event-stream")