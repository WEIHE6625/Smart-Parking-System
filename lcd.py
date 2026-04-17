import time
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
        self._worker = threading.Thread(target=self._worker_loop, daemon=True)
        self._worker.start()
        self._is_showing_temp = False

    def connect_cloud_mqtt(self, broker, port, user, pw):
        self.mqtt_client.username_pw_set(user, pw)
        self.mqtt_client.tls_set()
        self.mqtt_client.on_connect = lambda c, u, f, rc, p=None: \
            print(f"LCD MQTT connected: rc={rc}", flush=True)
        self.mqtt_client.on_disconnect = lambda c, u, rc, p=None: \
            print(f"LCD MQTT disconnected: rc={rc}", flush=True)
        self.mqtt_client.connect(broker, port)
        self.mqtt_client.loop_start()
        
    def set_available_provider(self, provider_func):
        self.available_provider = provider_func

    def _get_available_slot(self):
        if self.available_provider is None:
            return 0
        try:
            return int(self.available_provider())
        except Exception:
            return 0

    def _safe_text(self, text):
        return str(text)[:16].ljust(16)

    def _write_now(self, line1, line2=""):
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
        self.mqtt_client.publish("parking/lcd/display", payload)

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
            print(f"DEBUG: Worker picked up job type: {job['type']}", flush=True)
            print(f"DEBUG: Worker picked up job: {job}", flush=True)

            if job["type"] == "temp":
                print(f"DEBUG: Attempting to publish TEMP: {job['line1']}", flush=True)
                self._is_showing_temp = True # Mark as busy
                self._write_now(job["line1"], job["line2"])
                time.sleep(job["duration"])
                self._is_showing_temp = False # Mark as free
                self._show_default_now()

            elif job["type"] == "default":
                print("DEBUG: Attempting to publish DEFAULT", flush=True)
                if not self._is_showing_temp:
                    self._show_default_now()

            self._queue.task_done()

    def show_default(self):
        if self._queue.empty():
            self._queue.put({"type": "default"})

    def show_success(self, balance, is_entry, user_name, duration=2):
        with self._queue.mutex: # Clear pending default updates
            self._queue.queue.clear()
        if is_entry:
            greeting = f"Welcome {user_name.capitalize()}!" if user_name else "Welcome!"
        else:
            greeting = f"Bye {user_name.capitalize()}!" if user_name else "Come Again!"

        line2 = f"RM{float(balance):.2f}"
        
        try:
            self._queue.put_nowait({
                "type": "temp",
                "line1": greeting,
                "line2": line2,
                "duration": duration
            })
            return 1
        except queue.Full:
            return 0

    def show_denied(self, message, duration=2):
        with self._queue.mutex: # Clear pending default updates
            self._queue.queue.clear()
            
        short_msg = message.replace("DENIED_", "").replace("_", " ").title()
        
        try:
            self._queue.put_nowait({
                "type": "temp",
                "line1": "Access Denied!",
                "line2": short_msg,
                "duration": duration
            })
            return 1
        except queue.Full:
            return 0

lcd_display = LCD()
