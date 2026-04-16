from flask import Blueprint, jsonify, Response
import json
from queue import Queue, Empty

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

        # Send initial state as a normal string (Flask converts it safely)
        yield f"data: {json.dumps(parking_state)}\n\n"

        try:
            while True:
                try:
                    data = q.get(timeout=2)
                    # Force double-quotes on data so JavaScript doesn't crash
                    data_str = json.dumps(data) if isinstance(data, dict) else data
                    yield f"data: {data_str}\n\n"
                except Empty:
                    yield ": keep-alive\n\n"
                    
        except GeneratorExit:
            if q in clients:
                clients.remove(q)

    # REMOVED direct_passthrough=True. Kept Nginx buffering bypass headers.
    return Response(event_stream(), mimetype="text/event-stream", direct_passthrough=True, headers={
        'X-Accel-Buffering': 'no',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive'
    })