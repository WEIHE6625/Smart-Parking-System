from flask import Blueprint, jsonify
import parking_logic

slot_bp = Blueprint('slot', __name__)

@slot_bp.route('/api/parking_state')
def get_parking_state():
    return jsonify(parking_logic.parking_state)