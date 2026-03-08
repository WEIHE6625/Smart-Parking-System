import math
from datetime import datetime
from firebase_admin import db

def rfid_validate_entry(card_data, total_available) -> tuple[bool, str]: 
    # RULE 1: Does the card exist?
    if card_data is None:
        print("Result: DENIED - Unregistered Card")
        return False, "DENIED_UNREGISTERED"

    # RULE 2: Is the parking lot full?
    if total_available <= 0:
        print("Result: DENIED - Parking Lot Full")
        return False, "DENIED_FULL"

    # RULE 3: Is the user currently outside?
    if card_data.get('is_Entry') == True:
        print("Result: DENIED - Repeated Entry")
        return False, "DENIED_REPEATED"

    # RULE 4: Is the balance >= RM 20?
    balance = card_data.get('Balance', 0.0)
    if balance < 20.0:
        print(f"Result: DENIED - Insufficient Funds (RM {balance})")
        return False, "DENIED_FUNDS"
    
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