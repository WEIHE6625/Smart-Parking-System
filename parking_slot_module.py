from flask import Blueprint, jsonify, Response
import json
from queue import Queue, Empty

# Import the whole module so the 'clients' list connects properly
import parking_logic 

slot_bp = Blueprint('slot', __name__)

@slot_bp.route('/api/parking_state')
def get_parking_state():
    return jsonify(parking_logic.parking_state)

@slot_bp.route('/stream')
def stream():
    def event_stream():
        q = Queue()
        parking_logic.clients.append(q)

        # Send initial state when dashboard first connects
        yield f"data: {json.dumps(parking_logic.parking_state)}\n\n".encode('utf-8')

        try:
            while True:
                try:
                    # Wait for broadcast_state() to put data in the queue
                    data = q.get(timeout=2)
                    
                    # 'data' is already perfect JSON sent from broadcast_state!
                    yield f"data: {data}\n\n".encode('utf-8')
                    
                except Empty:
                    yield ": keep-alive\n\n".encode('utf-8')
                    
        except GeneratorExit:
            if q in parking_logic.clients:
                parking_logic.clients.remove(q)

    return Response(event_stream(), mimetype="text/event-stream", direct_passthrough=True, headers={
        'X-Accel-Buffering': 'no',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive'
    })