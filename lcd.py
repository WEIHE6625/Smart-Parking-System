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

    def _ensure_worker_alive(self):
        """Ensures the background thread is running in the current process."""
        if self._worker is None or not self._worker.is_alive():
            self._worker = threading.Thread(target=self._worker_loop, daemon=True)

            self._worker.start()
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

        print(f"[PID {os.getpid()}] Line 1: {line1} | Line 2: {line2}", flush=True)
        # Check timing and dedup OUTSIDE the lock
        now = time.time()
        with self._lock:
            if line1 == self._last_line1 and line2 == self._last_line2:
                print(f"Same || Line 1: {line1} | Line 2: {line2}")
                return
            elapsed = now - self._last_update_time
            print(f"Else || Line 1: {line1} | Line 2: {line2}")
        
        print("State 1")
        # Sleep OUTSIDE the lock so MQTT thread isn't blocked
        if elapsed < self._min_update_gap:
            time.sleep(self._min_update_gap - elapsed)

        print("State 2")
        payload = json.dumps({"line1": line1, "line2": line2}) 
        print(f"Publishing to MQTT: {payload}", flush=True) 
        self.mqtt_client.publish("parking/lcd/display", payload)
        print("State 3")

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
                # self._show_default_now()

            elif job["type"] == "default":
                print("DEBUG: Attempting to publish DEFAULT", flush=True)
                if not self._is_showing_temp:
                    self._show_default_now()

            self._queue.task_done()

    def _log_queue_state(self):
        """Helper to snapshot and print the current queue contents."""
        # We use the mutex to ensure we get a consistent snapshot
        with self._queue.mutex:
            # Convert deque to list to see items
            current_items = list(self._queue.queue)
            count = len(current_items)
            
        print(f"--- Queue Status (Size: {count}) ---")
        if count == 0:
            print("Queue is empty.")
        else:
            for i, item in enumerate(current_items):
                print(f" [{i}] Type: {item['type']} | Content: {item.get('line1', 'N/A')}")
        print("-----------------------------------")

    def show_default(self):
        self._ensure_worker_alive()
        print(f"DEBUG [show_default]: Called on instance pid={os.getpid()}", flush=True)
        if self._queue.empty():
            self._queue.put({"type": "default"})
            # self._log_queue_state()

    def show_success(self, balance, is_entry, user_name, duration=2):
        self._ensure_worker_alive()
        print(f"DEBUG [show_success]: Called on instance pid={os.getpid()}", flush=True)
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

    def show_denied(self, message, duration=2):
        self._ensure_worker_alive()
        print(f"DEBUG [show_denied]: Called on instance pid={os.getpid()}", flush=True)
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
