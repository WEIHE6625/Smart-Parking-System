from flask import Blueprint, jsonify
import parking_logic

slot_bp = Blueprint('slot', __name__)

@slot_bp.route('/api/parking_state')
def get_parking_state():
    return jsonify(parking_logic.parking_state)


# @slot_bp.route('/stream')
# def stream():
#     @stream_with_context
#     def event_stream():
#         q = Queue()

#         # ✅ Thread-safe add
#         with parking_logic.clients_lock:
#             parking_logic.clients.append(q)

#         print(f"SSE client connected. Total clients: {len(parking_logic.clients)}", flush=True)

#         # ✅ Tell browser to reconnect faster
#         yield "retry: 1000\n\n"

#         # ✅ Send initial state
#         yield f"data: {json.dumps(parking_logic.parking_state)}\n\n"

#         try:
#             while True:
#                 try:
#                     # 🔥 shorter timeout = more stable on Render
#                     data = q.get(timeout=5)
#                     yield f"data: {data}\n\n"

#                 except Empty:
#                     # 🔥 CRITICAL: always send data (prevents Render killing connection)
#                     yield f"data: {json.dumps(parking_logic.parking_state)}\n\n"

#         except GeneratorExit:
#             print("SSE client disconnected.", flush=True)

#             # ✅ Thread-safe remove
#             with parking_logic.clients_lock:
#                 if q in parking_logic.clients:
#                     parking_logic.clients.remove(q)

#     return Response(
#         event_stream(),
#         mimetype="text/event-stream",
#         headers={
#             'Cache-Control': 'no-cache',
#             'Connection': 'keep-alive',
#             'X-Accel-Buffering': 'no',
#             'Keep-Alive': 'timeout=5, max=1000'
#         }
#     )