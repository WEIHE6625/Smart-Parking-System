import math
import time
import json
import ssl
import threading
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion

from datetime import datetime
from firebase_admin import db
from lcd import lcd_display

parking_state = {
    "TOTAL_AVAILABLE": 4,
    "ZONE_AVAILABLE": {
        "Zone_A": 2,
        "Zone_B": 2
    },
    "SLOTS": {
        "Zone_A": {
            "Slot_A1": {"status": "empty", "distance": 0},
            "Slot_A2": {"status": "empty", "distance": 0}
        },
        "Zone_B": {
            "Slot_B1": {"status": "empty", "distance": 0},
            "Slot_B2": {"status": "empty", "distance": 0}
        }
    },
    "ILLEGAL_PARKING": {
        "Zone_A": {
            "detected": False,
            "distance": 0,
            "message": ""
        },
        "Zone_B": {
            "detected": False,
            "distance": 0,
            "message": ""
        }
    },
    "ALERT": {
        "type": "",
        "zone": "",
        "message": "",
        "timestamp": ""
    }
}

MQTT_BROKER = "dd35fd08fd3e4f6bbd826d8384d43855.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_USER = "zceping"
MQTT_PW = "Tzp014588!"
MQTT_TOPIC = "parking/+/+/status"
MQTT_WEB_TOPIC = "parking/full_state"  # The new topic we publish to for the web frontend

mqtt_client = None  # Global MQTT client variable

# RFID module
def rfid_validate_entry(card_data) -> tuple[bool, str]: 
    # RULE 1: Does the card exist?
    if card_data is not None: user_id = card_data.get('User_ID', None)

    if card_data is None or user_id is None:
        print("Result: DENIED - Unregistered Card")
        return False, "DENIED_UNREGISTERED"

    # RULE 2: Is the user currently outside?
    if card_data.get('is_Entry') == True:
        print("Result: DENIED - Repeated Entry")
        return False, "DENIED_REPEATED"

    # RULE 3: Is the balance >= RM 20?
    balance = card_data.get('Balance', 0.0)
    if balance < 20.0:
        print(f"Result: DENIED - Insufficient Funds (RM {balance})")
        return False, "DENIED_ISUFF_FUNDS"
    
    # RULE 5: Is the card expired?
    expiry_str = card_data.get('Expiry_Date')
    if expiry_str:            
        expiry_date = datetime.strptime(expiry_str, "%m-%Y")
        current_date = datetime.now()

        if current_date.year > expiry_date.year or (current_date.year == expiry_date.year and current_date.month > expiry_date.month):
            print(f"Result: DENIED - Card Expired ({expiry_str})")
            return False, "DENIED_EXPIRED"
    
    return True, "APPROVED"

def rfid_validate_exit(card_data) -> tuple[bool, str]:
    # RULE 1: Does the card exist?
    if card_data is None:
        print("Result: DENIED - Unregistered Card")
        return False, "DENIED_UNREGISTERED"

    # RULE 2: Is the user inside?
    if card_data.get('is_Entry') == False:
        print("Result: DENIED - Illegal Exit")
        return False, "DENIED_ILLEGAL_EXIT"
    
    return True, "APPROVED"

def add_new_parking_history(scanned_uid, entry_time, exit_time, calculated_fee):
    history_ref = db.reference('Parking_History')
    history_data = history_ref.get()
    
    if history_data is None:
        # If the history is completely empty, start at 1
        next_session_num = 1
    else:
        # Count the existing sessions and add 1
        next_session_num = len(history_data) + 1
        
    new_session_id = f"Session_{next_session_num:06d}"
    new_record = {
        "Card_ID": scanned_uid,
        "Entry_Time": entry_time, 
        "Exit_Time": exit_time,
        "Fee_Paid": calculated_fee     
    }

    db.reference(f'Parking_History/{new_session_id}').set(new_record)

def get_user_name(card_data):
    user_id = card_data.get('User_ID') if card_data else None
    if user_id:
        user_data = db.reference(f'User/{user_id}').get()
        if user_data:
            return user_data.get('Name', "")
    return ""

def calculate_parking_fee(entry_time, exit_time, first_hour_fee=2, fee_per_hour=1, max_fee=20):
    time_format = "%Y-%m-%d:%H:%M:%S"
    
    try:
        entry_dt = datetime.strptime(entry_time, time_format)
        exit_dt = datetime.strptime(exit_time, time_format)
        
        duration = exit_dt - entry_dt
        
        total_seconds = duration.total_seconds()
        hours_parked = math.floor(total_seconds / 30) # 30 seconds free
        
        final_fee = 0.0

        if hours_parked <= 0:
            return final_fee
        else:
            calculated_fee = first_hour_fee + ((hours_parked - 1) * fee_per_hour)
            final_fee = min(calculated_fee, max_fee)
            return final_fee
            
    except ValueError as e:
        print(f"Error calculating time: {e}")
        return 0.0
    
# Parking slot module
def recalculate_available():
    total = 0

    for zone, slots in parking_state["SLOTS"].items():
        available = 0
        for _, slot_data in slots.items():
            if slot_data["status"] == "empty":
                available += 1

        parking_state["ZONE_AVAILABLE"][zone] = available
        total += available

    old_total = parking_state["TOTAL_AVAILABLE"]
    parking_state["TOTAL_AVAILABLE"] = total

    if old_total != total:
        lcd_display.show_default()

def broadcast_state():
    # Publish the updated parking state to MQTT instead of using SSE queues
    global mqtt_client
    data = json.dumps(parking_state)

    if mqtt_client is not None:
        try:
            # retain=True ensures newly connected dashboards instantly receive the latest state
            mqtt_client.publish(MQTT_WEB_TOPIC, data, retain=True)
            print(f"State broadcasted to MQTT topic: {MQTT_WEB_TOPIC}", flush=True)
        except Exception as e:
            print(f"MQTT Broadcast error: {e}", flush=True)

def set_alert(alert_type, zone, message):
    parking_state["ALERT"] = {
        "type": alert_type,
        "zone": zone,
        "message": message,
        "timestamp": datetime.now().isoformat()
    }

def update_slot(zone, slot_id, status, distance):
    if zone not in parking_state["SLOTS"]:
        return

    if slot_id not in parking_state["SLOTS"][zone]:
        return

    parking_state["SLOTS"][zone][slot_id]["status"] = status
    parking_state["SLOTS"][zone][slot_id]["distance"] = distance

    recalculate_available()
    broadcast_state()

def update_illegal_parking(zone, illegal_detected, distance):
    if zone not in parking_state["ILLEGAL_PARKING"]:
        parking_state["ILLEGAL_PARKING"][zone] = {
            "detected": False,
            "distance": 0,
            "message": ""
        }

    previous_detected = parking_state["ILLEGAL_PARKING"][zone]["detected"]

    parking_state["ILLEGAL_PARKING"][zone]["detected"] = illegal_detected
    parking_state["ILLEGAL_PARKING"][zone]["distance"] = distance

    if illegal_detected and not previous_detected:
        zone_label = zone.replace("_", " ")
        set_alert("illegal_parking", zone, f"{zone_label} have illegal parking detected.")
    elif (not illegal_detected) and previous_detected:
        zone_label = zone.replace("_", " ")
        set_alert("illegal_parking_cleared", zone, f"{zone_label} illegal parking cleared.")

    broadcast_state()

def on_connect(client, userdata, flags, rc, properties=None):
    print("Connected to MQTT broker with code:", rc)
    client.subscribe(MQTT_TOPIC)
    print("Subscribed to:", MQTT_TOPIC)

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    
        zone = payload.get("zone")
        if not zone:
            print("Invalid MQTT payload, missing zone:", payload)
            return

        if "illegal_parking" in payload:
            illegal_detected = bool(payload.get("illegal_parking", False))
            distance = payload.get("distance", 0)

            update_illegal_parking(zone, illegal_detected, distance) 
            return

        slot_id = payload.get("slot_id")
        status = payload.get("status")
        distance = payload.get("distance", 0)

        if not slot_id or not status:
            print("Invalid MQTT payload:", payload)
            return

        update_slot(zone, slot_id, status, distance)

    except Exception as e:
        print("MQTT message error:", e)

def start_mqtt_listener():
    global mqtt_client
    mqtt_client = mqtt.Client(CallbackAPIVersion.VERSION2)
    mqtt_client.username_pw_set(MQTT_USER, MQTT_PW)
    mqtt_client.tls_set()  # Required for HiveMQ port 8883
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_forever()

def get_current_parking_total_available():
    return parking_state["TOTAL_AVAILABLE"]

lcd_display.set_available_provider(get_current_parking_total_available())