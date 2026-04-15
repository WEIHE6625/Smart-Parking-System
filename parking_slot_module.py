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

        # Send initial state
        yield f"data: {json.dumps(parking_state)}\n\n"

        try:
            while True:
                try:
                    data = q.get(timeout=5)
                    yield f"data: {data}\n\n"
                except Empty:
                    # If 10 seconds pass with no car movement, send an invisible heartbeat.
                    # This prevents proxy buffering and forces the server to check 
                    # if the user has closed their browser tab!
                    yield ": keep-alive\n\n"
                    
        except GeneratorExit:
            if q in clients:
                clients.remove(q)

    # Headers to stop Render from buffering
    return Response(event_stream(), mimetype="text/event-stream", direct_passthrough=True, headers={
        'X-Accel-Buffering': 'no',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive'
    })