import time
import os
import threading
import queue
import json
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion

class LCD:
    def __init__(self):
        self.available_provider = None
        self._lock = threading.Lock()
        self._queue = queue.Queue()
        self._last_line1 = None
        self._last_line2 = None
        self._last_update_time = 0
        self._min_update_gap = 1.5
        self.mqtt_client = mqtt.Client(CallbackAPIVersion.VERSION2)
        self._is_showing_temp = False
        self._worker = None

        self._is_connected = False 
        self._mqtt_creds = None
        self._synced_slot_count = 0

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        print(f"[PID {os.getpid()}] LCD connected! Subscribing to sync topic...", flush=True)
        # Subscribe to the topic where you broadcast your parking state
        client.subscribe("parking/full_state")

    def _on_message(self, client, userdata, msg):
        try:
            if msg.topic == "parking/full_state":
                data = json.loads(msg.payload.decode('utf-8'))
                
                if "TOTAL_AVAILABLE" in data:
                    new_count = int(data["TOTAL_AVAILABLE"])
                    
                    # Only trigger an update if the count actually changed!
                    if new_count != self._synced_slot_count:
                        self._synced_slot_count = new_count
                        print(f"[PID {os.getpid()}] Worker synced slot count to: {self._synced_slot_count}", flush=True)
                        self.show_default()
        except Exception as e:
            print(f"LCD Sync Error: {e}", flush=True)

    def _ensure_worker_alive(self):
        """Ensures the background thread is running in the current process."""
        if self._worker is None or not self._worker.is_alive():
            self._worker = threading.Thread(target=self._worker_loop, daemon=True)
            self._worker.start()

    def setup_mqtt_credentials(self, broker, port, user, pw):
        self._mqtt_creds = (broker, port, user, pw)

    def _ensure_connected(self):
        broker, port, user, pw = self._mqtt_creds
        if not self._is_connected:
            print(f"[PID {os.getpid()}] Starting MQTT Connection for this worker...", flush=True)
            self.mqtt_client.username_pw_set(user, pw)
            self.mqtt_client.tls_set()
            
            self.mqtt_client.on_connect = self._on_connect
            self.mqtt_client.on_message = self._on_message

            self.mqtt_client.connect(broker, port)
            self.mqtt_client.loop_start()
            
            self._is_connected = True
        
    def set_available_provider(self, func):
        self.total_available = func

    def _get_available_slot(self):
        # We no longer need the provider function, we just return the synced value!
        return self._synced_slot_count

    def _safe_text(self, text):
        return str(text)[:16].ljust(16)

    def _write_now(self, line1, line2=""):
        print(f"Is connected: {self._is_connected}")
        self._ensure_connected()

        line1 = self._safe_text(line1)
        line2 = self._safe_text(line2)

        # Check timing and dedup OUTSIDE the lock
        now = time.time()
        with self._lock:
            if line1 == self._last_line1 and line2 == self._last_line2:
                return
            elapsed = now - self._last_update_time
        
        # Sleep OUTSIDE the lock so MQTT thread isn't blocked
        if elapsed < self._min_update_gap:
            time.sleep(self._min_update_gap - elapsed)

        payload = json.dumps({"line1": line1, "line2": line2}) 
        msg_info = self.mqtt_client.publish("parking/lcd/display", payload, qos=1)
        
        try:
            msg_info.wait_for_publish(timeout=2.0)
            if msg_info.is_published():
                print(f"[PID {os.getpid()}] MQTT Publish SUCCESS and confirmed by HiveMQ!", flush=True)
            else:
                print(f"[PID {os.getpid()}] MQTT Publish FAILED: Timeout! The network thread is dead.", flush=True)
        except Exception as e:
            print(f"[PID {os.getpid()}] MQTT Publish ERROR: {e}", flush=True)

        with self._lock:
            self._last_line1 = line1
            self._last_line2 = line2
            self._last_update_time = time.time()

    def _show_default_now(self):
        available_slot = self._get_available_slot()
        self._write_now("Parking System", f"Slot: {available_slot}")

    def _worker_loop(self):
        while True:
            job = self._queue.get()

            if job["type"] == "temp":
                self._is_showing_temp = True # Mark as busy
                self._write_now(job["line1"], job["line2"])
                time.sleep(job["duration"])
                self._is_showing_temp = False # Mark as free
                self._show_default_now()

            elif job["type"] == "default":
                if not self._is_showing_temp:
                    self._show_default_now()

            self._queue.task_done()

    def show_default(self):
        self._ensure_worker_alive()
        if self._queue.empty():
            self._queue.put({"type": "default"})

    def show_success(self, balance, is_entry, user_name, duration=3):
        self._ensure_worker_alive()
        with self._queue.mutex: # Clear pending default updates
            self._queue.queue.clear()
        if is_entry:
            greeting = f"Welcome {user_name.capitalize()}!" if user_name else "Welcome!"
        else:
            greeting = f"Bye {user_name.capitalize()}!" if user_name else "Come Again!"

        line2 = f"RM{float(balance):.2f}"
        
        self._queue.put_nowait({
                "type": "temp",
                "line1": greeting,
                "line2": line2,
                "duration": duration
            })

    def show_denied(self, message, duration=3):
        self._ensure_worker_alive()
        with self._queue.mutex: # Clear pending default updates
            self._queue.queue.clear()
            
        short_msg = message.replace("DENIED_", "").replace("_", " ").title()
        
        self._queue.put_nowait({
                "type": "temp",
                "line1": "Access Denied!",
                "line2": short_msg,
                "duration": duration
            })

lcd_display = LCD()
