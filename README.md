# Smart-Parking-System
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

Raspberry PI pip install problem:
sudo pip3 install -r requirements.txt --break-system-packages

network of esp32, flask and raspberry pi must always in the same LAN

RFID Sensor:
SDA     5
SCK     18
MOSI    23
MISO    19

RST     21

For Servo Motor gate (Isaac):
Servo Motor voltage ==> 5V
Ultrasonic sensor voltage ==> 3.3V
Move ultrasonic sensor back a bit, don't be too close
if servo motor got random vibration, adjust the servo_motor.attach(MIN_pulse, MAX_pulse) values, then use appropriate openAngles and closeAngles
