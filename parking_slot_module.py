from flask import Blueprint, jsonify, Response, stream_with_context
import json
from queue import Queue, Empty
import parking_logic

slot_bp = Blueprint('slot', __name__)

@slot_bp.route('/api/parking_state')
def get_parking_state():
    return jsonify(parking_logic.parking_state)

@slot_bp.route('/stream')
def stream():
    @stream_with_context
    def event_stream():
        q = Queue()
        parking_logic.clients.append(q)
        print(f"SSE client connected. Total clients: {len(parking_logic.clients)}", flush=True)

        # Send CURRENT state immediately (this will have real data if MQTT already fired)
        yield f"data: {json.dumps(parking_logic.parking_state)}\n\n"

        try:
            while True:
                try:
                    data = q.get(timeout=25)
                    yield f"data: {data}\n\n"
                except Empty:
                    # Heartbeat to keep connection alive through proxies/load balancers
                    yield ": heartbeat\n\n"
        except GeneratorExit:
            print("SSE client disconnected.", flush=True)
            if q in parking_logic.clients:
                parking_logic.clients.remove(q)

    return Response(
        event_stream(),
        mimetype="text/event-stream",
        headers={
            'X-Accel-Buffering': 'no',
            'Cache-Control': 'no-cache',
            'Connection': 'keep-alive',
            'Transfer-Encoding': 'chunked'
        }
    )