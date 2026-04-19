# Smart Campus Parking System
## 1. Database Configuration
  - **Database Provider:** Firebase Realtime Database.
  - **Access Credentials:** The system uses a service account key located in serviceAccountKey.json for administrative access.
  - **Database URL:** *https://spsystem-44375-default-rtdb.asia-southeast1.firebasedatabase.app*
## 2. Account Information for Access
  - **Administrator Access:** Full administrative control is granted via the firebase-admin SDK using the **serviceAccountKey.json** file.
  - **User Access:** Authenticated users can access their specific profile and card information. Sessions are managed via Flask using card_id as the primary identifier.
      - Example account:
        - Admin: email: admin@gmail.com, password: admin123
        - User: email: lim@gmail.com, password: abcd1234 (all user password are abcd1234)
## 3. Data Structure & Schema
The database is structured as a JSON tree. Below are the primary nodes and their attributes:
- */User/{user_id}*
  - Name: String (User's full name).
  - Email: String (User's email address).
  - Role: String (e.g., "user" or "admin").

- */Card/{card_id}*
  - User_ID: String (Links the card to a specific User ID).
  - Balance: Float (Current monetary balance; minimum RM 20 required for entry).
  - is_Entry: Boolean (True if the user is currently inside the parking facility).
  - Expiry_Date: String (Format: "MM-YYYY"; checked during entry validation).
  - Entry_Time: String (Format: "YYYY-MM-DD:HH:M:S"; recorded upon successful entry).

- */Parking_History/Session_{number}*
  - Card_ID: String (The unique ID of the card used).
  - Entry_Time: String.
  - Exit_Time: String.
  - Fee_Paid: Float (Calculated based on duration).

## 4. System Implementation & Usage
- **Authentication & Validation:**
  - The rfid_validate_entry function checks if a card exists, if the user is already inside (to prevent tailgating), if the balance is $\ge$ RM 20, and if the card has expired.

- **Financial Transactions:**
  - **Top-Up:** The */topup* route allows users to increase their Balance attribute in the */Card* node.
  - **Fee Calculation:** Fees are calculated upon exit based on the duration between *Entry_Time* and *Exit_Time*.
  - **Real-Time Monitoring:** The system utilizes MQTT (HiveMQ) for sub-second updates, but uses Firebase as the permanent source of truth for user data and parking history.
  - **Parking History:** Every completed parking session is logged in the Parking_History node with a unique session ID (e.g., Session_000001).

## For developing only:
  - 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

  - Raspberry PI pip install problem:
      sudo pip3 install -r requirements.txt --break-system-packages


  - RFID Sensor:
      SDA     5
      SCK     18
      MOSI    23
      MISO    19      
      RST     21

  - For Servo Motor gate:
      Servo Motor voltage ==> 5V
      Ultrasonic sensor voltage ==> 3.3V
      if servo motor got random vibration, adjust the servo_motor.attach(MIN_pulse, MAX_pulse) values, then use appropriate openAngles and closeAngles
