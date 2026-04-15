from flask import Flask, request, render_template, redirect, url_for, session
import threading
from firebase_admin import db
import paho.mqtt.client as mqtt
from lcd import lcd_display

from authentication import auth_bp
from rfid_module import rfid_bp
from parking_slot_module import *
from firebase_config import init_firebase
from parking_logic import *

# Initialize Flask and Firebase
app = Flask(__name__)
app.secret_key = "secretkey"
init_firebase()

app.register_blueprint(auth_bp)
app.register_blueprint(rfid_bp)
app.register_blueprint(slot_bp)


@app.route('/')
def home():
    lcd_display.show_default()
    return redirect(url_for('auth.login'))

@app.route('/dashboard')
def dashboard():
    return render_template(
        'dashboard.html',
        parking_state=parking_state
    )

@app.route('/topup', methods=['GET', 'POST'])
def topup():
    # If the user is not logged in, redirect them to the login page
    if 'card_id' not in session:
        return redirect(url_for('auth.login'))

    user_card_id = session['card_id']
    card_ref = db.reference(f'Card/{user_card_id}')
    
    # Submit
    if request.method == 'POST':
        topup_amount = float(request.form.get('amount', 0.0))
        
        if topup_amount > 0:
            # Fetch the current balance and add the new amount
            current_balance = float(card_ref.get().get('Balance', 0.0))
            new_balance = current_balance + topup_amount
            
            # Update the database
            card_ref.update({'Balance': new_balance})
            return redirect(url_for('topup', success=f"Successfully added RM {topup_amount:.2f}!"))

    # Fetch the balance to display on the screen
    current_balance = float(card_ref.get().get('Balance', 0.0))
    
    # Catch the success message from the URL (if it exists)
    success_msg = request.args.get('success')
    
    return render_template('topup.html', balance=current_balance, card_id=user_card_id, success=success_msg)

if __name__ == '__main__':
    mqtt_thread = threading.Thread(target=start_mqtt_listener, daemon=True)
    mqtt_thread.start()
    lcd_display.connect_cloud_mqtt(
        broker="dd35fd08fd3e4f6bbd826d8384d43855.s1.eu.hivemq.cloud",
        port=8883,
        user="zceping",
        pw="Tzp014588!"
    )
    app.run(host='0.0.0.0', port=5000, use_reloader=False)