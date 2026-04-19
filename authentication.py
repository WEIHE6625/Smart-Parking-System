import requests
from flask import Blueprint, request, render_template, redirect, url_for, session
from firebase_admin import db, auth
import firebase_admin.exceptions

auth_bp = Blueprint('auth', __name__)
FIREBASE_WEB_API_KEY = "AIzaSyC9J8YRhLSmNVa8Uo1NQ4FTOqKcmiyWxRE"

# The Register Route
@auth_bp.route('/register', methods=['GET', 'POST'])
def register():
    if request.method == 'POST':
        name = request.form.get('name')
        email = request.form.get('email')
        password = request.form.get('password')
        card_id = request.form.get('card_id')

        # Check if email is already registered
        try:
            user_by_email = auth.get_user_by_email(email)
            return render_template('register.html', error="Email already registered. Please login.")
        except firebase_admin.exceptions.FirebaseError:
            pass  # Email not found, proceed with registration

        card_ref = db.reference(f'Card/{card_id}')
        card_data = card_ref.get()

        if card_data is None:
            return render_template('register.html', error="Invalid Card ID.")

        if card_data.get('User_ID'): 
            return render_template('register.html', error="Card ID already registered.")

        try:
            user_record = auth.create_user(email=email, password=password, display_name=name)
            firebase_uid = user_record.uid 

            db.reference(f'User/{firebase_uid}').set({"Name": name, "Email": email, "Role": "user"})
            card_ref.update({"User_ID": firebase_uid})

            return redirect(url_for('auth.login', success="Account created successfully! Please login."))

        except firebase_admin.exceptions.FirebaseError as e:
            return render_template('register.html', error=str(e))

    return render_template('register.html')

# The Login Route
@auth_bp.route('/login', methods=['GET', 'POST'])
def login():
    success_msg = request.args.get('success')


    if request.method == 'POST':
        email = request.form.get('email')
        password = request.form.get('password')

        verify_url = f"https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key={FIREBASE_WEB_API_KEY}"
        payload = {"email": email, "password": password, "returnSecureToken": True}
        
        response = requests.post(verify_url, json=payload)
        
        if response.status_code == 200:
            user_data = response.json()
            firebase_uid = user_data['localId']

            user_ref = db.reference(f'User/{firebase_uid}').get()

            if not user_ref:
                return render_template('login.html', error="User profile not found.")

            user_role = user_ref.get('Role', '').lower()
            session['user_id'] = firebase_uid
            session['role'] = user_role
            
            if user_role == 'admin':
                session['card_id'] = None
                return redirect(url_for('dashboard')) 
            
            cards_ref = db.reference('Card').get()
            user_card_id = None
            
            if cards_ref:
                for card_id, card_info in cards_ref.items():
                    if card_info.get('User_ID') == firebase_uid:
                        user_card_id = card_id
                        break
            
            if not user_card_id:
                return render_template('login.html', error="No RFID card linked.")

            session['user_id'] = firebase_uid
            session['card_id'] = user_card_id
            
            return redirect(url_for('topup'))
            
        else:
            return render_template('login.html', error="Invalid email or password.")

    return render_template('login.html', success=success_msg)

@auth_bp.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('auth.login'))