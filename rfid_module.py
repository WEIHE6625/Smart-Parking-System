import threading
from flask import Blueprint, request, jsonify
from datetime import datetime
from firebase_admin import db

from lcd import LCD
from parking_logic import *
from parking_logic import parking_state

rfid_bp = Blueprint('rfid', __name__)
display_lcd = LCD()

def reset_lcd_to_default():
    try:
        zone_ref = db.reference('Parking_Zone')
        zone_data = zone_ref.get()
        total_available = zone_data.get('Total_Available_Parking', 0) if zone_data else 0
        display_lcd.show_default(total_available)
    except Exception as e:
        print(f"Error resetting LCD: {e}")

@rfid_bp.route('/rfid_entry', methods=['POST'])
def rfid_entry():
    data = request.get_json()
    
    if not data or 'rfid_uid' not in data or data['rfid_uid'].strip() == "":
        return jsonify({"status": "ERROR", "message": "No RFID provided"}), 400

    scanned_uid = data['rfid_uid']
    print(f"Scanned card: {scanned_uid}")

    card_ref = db.reference(f'Card/{scanned_uid}')
    card_data = card_ref.get()
    zone_ref = db.reference('Parking_Zone')
    zone_data = zone_ref.get()
    total_available = zone_data.get('Total_Available_Parking', 0) if zone_data else 0

    is_Valid, message = rfid_validate_entry(card_data, total_available)
    balance = float(card_data.get('Balance', 0.0)) if card_data else 0.0

    if is_Valid:
        display_lcd.show_success(balance, True, get_user_name(card_data))
        current_time = datetime.now().strftime("%Y-%m-%d:%H:%M:%S") 

        card_ref.update({
            'is_Entry': True,
            'Last_Entry_Time': current_time 
        })
        
        zone_ref.update({
            'Total_Available_Parking': total_available - 1
        })
        
        print("Result: APPROVED - Gate Opening")
    else:
        display_lcd.show_denied(message)
    
    # Start a background timer to reset the LCD to default after 3 seconds
    threading.Timer(3.0, reset_lcd_to_default).start()

    return jsonify({"status": message}), 200


@rfid_bp.route('/rfid_exit', methods=['POST'])
def rfid_exit():
    data = request.get_json()
    
    if not data or 'rfid_uid' not in data or data['rfid_uid'].strip() == "":
        return jsonify({"status": "ERROR", "message": "No RFID provided"}), 400

    scanned_uid = data['rfid_uid']
    print(f"Scanned card: {scanned_uid}")

    card_ref = db.reference(f'Card/{scanned_uid}')
    card_data = card_ref.get()
    zone_ref = db.reference('Parking_Zone')
    zone_data = zone_ref.get()

    isValid, message = rfid_validate_exit(card_data)
    
    if isValid:
        card_ref.update({
            'is_Entry': False
        })

        entry_time = card_data.get('Last_Entry_Time', 0)
        exit_time = datetime.now().strftime("%Y-%m-%d:%H:%M:%S")
        calculated_fee = calculate_parking_fee(entry_time, exit_time)

        # deduct the balance
        current_balance = float(card_data.get('Balance', 0.0)) if card_data else 0.0
        new_balance = current_balance - calculated_fee
        card_ref.update({'Balance': new_balance})
        
        add_new_parking_history(
            scanned_uid=scanned_uid, 
            entry_time=entry_time,
            exit_time=exit_time,
            calculated_fee=calculated_fee
        )

        # Fixed: Pass the newly calculated balance and capitalized False
        display_lcd.show_success(new_balance, False, get_user_name(card_data), parking_state["TOTAL_AVAILABLE"])
        
        total_available = zone_data.get('Total_Available_Parking', 0) if zone_data else 0
        zone_ref.update({
            'Total_Available_Parking': total_available + 1
        })
        
        print("Result: APPROVED - Gate Opening")
        
    else:
        display_lcd.show_denied(message, parking_state["TOTAL_AVAILABLE"])

    # Start a background timer to reset the LCD to default after 3 seconds
    threading.Timer(3.0, reset_lcd_to_default).start()

    return jsonify({"status": message}), 200