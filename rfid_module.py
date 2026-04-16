import threading
from flask import Blueprint, request, jsonify
from datetime import datetime
from firebase_admin import db

from lcd import lcd_display
from parking_logic import *

rfid_bp = Blueprint('rfid', __name__)

lcd_display.connect_cloud_mqtt(
    broker="dd35fd08fd3e4f6bbd826d8384d43855.s1.eu.hivemq.cloud",
    port=8883,
    user="zceping",
    pw="Tzp014588!"
)

def bg_entry_update(scanned_uid):
    try:
        card_ref = db.reference(f"Card/{scanned_uid}")
        current_time = datetime.now().strftime("%Y-%m-%d:%H:%M:%S")
        card_ref.update({
            'is_Entry': True,
            'Last_Entry_Time': current_time 
        })
    except Exception as e:
        print(e)

def bg_exit_update(scanned_uid, entry_time, exit_time, calculated_fee, new_balance):
    try:
        card_ref = db.reference(f"Card/{scanned_uid}")
        card_ref.update({
            'is_Entry': False,
            'Balance': new_balance 
        })
            
        add_new_parking_history(
            scanned_uid=scanned_uid, 
            entry_time=entry_time,
            exit_time=exit_time,
            calculated_fee=calculated_fee
        )
    
    except Exception as e:
        print(e)

@rfid_bp.route('/rfid_entry', methods=['POST'])
def rfid_entry():
    data = request.get_json()
    
    if not data or 'rfid_uid' not in data or data['rfid_uid'].strip() == "":
        return jsonify({"status": "ERROR", "message": "No RFID provided"}), 400

    scanned_uid = data['rfid_uid']
    print(f"Scanned card: {scanned_uid}")

    card_ref = db.reference(f'Card/{scanned_uid}')
    card_data = card_ref.get()

    is_Valid, message = rfid_validate_entry(card_data)
    balance = float(card_data.get('Balance', 0.0)) if card_data else 0.0

    if is_Valid:        
        threading.Thread(
            target=bg_entry_update,
            args=(scanned_uid,),
            daemon=True
        ).start()
        
        print("Result: APPROVED - Gate Opening")
        lcd_display.show_success(balance, True, get_user_name(card_data))
    else:
        lcd_display.show_denied(message)

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

    isValid, message = rfid_validate_exit(card_data)
    
    if isValid:
        entry_time = card_data.get('Last_Entry_Time', 0)
        exit_time = datetime.now().strftime("%Y-%m-%d:%H:%M:%S")
        calculated_fee = calculate_parking_fee(entry_time, exit_time)
        
        # deduct the balance
        current_balance = float(card_data.get('Balance', 0.0)) if card_data else 0.0
        new_balance = current_balance - calculated_fee
        
        threading.Thread(
            target=bg_exit_update,
            args=(scanned_uid,entry_time,exit_time,calculated_fee, new_balance),
            daemon=True
        ).start()

        lcd_display.show_success(new_balance, False, get_user_name(card_data))
        
        print("Result: APPROVED - Gate Opening")
        
    else:
        lcd_display.show_denied(message)

    return jsonify({"status": message}), 200
