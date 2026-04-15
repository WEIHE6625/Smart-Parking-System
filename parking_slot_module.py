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

        # 1. ADD .encode() HERE
        yield f"data: {json.dumps(parking_state)}\n\n".encode()

        try:
            while True:
                try:
                    data = q.get(timeout=2)
                    # 2. ADD .encode() HERE
                    yield f"data: {json.dumps(data) if isinstance(data, dict) else data}\n\n".encode()
                except Empty:
                    # 3. ADD .encode() HERE
                    yield ": keep-alive\n\n".encode()
                    
        except GeneratorExit:
            if q in clients:
                clients.remove(q)

    # Keep this exactly as you have it
    return Response(event_stream(), mimetype="text/event-stream", direct_passthrough=True, headers={
        'X-Accel-Buffering': 'no',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive'
    })