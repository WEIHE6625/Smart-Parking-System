import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
import json
import time
from grove.display.jhd1802 import JHD1802

# Initialize the physical LCD
lcd = JHD1802()

def on_connect(client, userdata, flags, rc):
    print(f"Connected to HiveMQ with result code {rc}")
    client.subscribe("parking/lcd/display")

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        line1 = data.get("line1", "")[:16].ljust(16)
        line2 = data.get("line2", "")[:16].ljust(16)
        
        lcd.setCursor(0, 0)
        lcd.write(line1)
        lcd.setCursor(1, 0)
        lcd.write(line2)
        print(f"Displayed: {line1.strip()} | {line2.strip()}")
    except Exception as e:
        print(f"Error processing message: {e}")

client = mqtt.Client(CallbackAPIVersion.VERSION2)
# Use your HiveMQ Cloud Credentials
client.username_pw_set("zceping", "Tzp014588!")
client.tls_set()
client.on_connect = on_connect
client.on_message = on_message

client.connect("dd35fd08fd3e4f6bbd826d8384d43855.s1.eu.hivemq.cloud", 8883)
client.loop_forever()