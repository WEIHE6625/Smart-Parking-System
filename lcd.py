import time
import threading
import queue
from grove.display.jhd1802 import JHD1802


class LCD:
    def __init__(self):
        self.lcd = JHD1802()
        self.available_provider = None
        self._lock = threading.Lock()
        self._queue = queue.Queue()
        self._last_line1 = None
        self._last_line2 = None
        self._last_update_time = 0
        self._min_update_gap = 0.8
        self._worker = threading.Thread(target=self._worker_loop, daemon=True)
        self._worker.start()

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

        with self._lock:
            now = time.time()
            if line1 == self._last_line1 and line2 == self._last_line2:
                return
            elapsed = now - self._last_update_time
            if elapsed < self._min_update_gap:
                time.sleep(self._min_update_gap - elapsed)
            try:
                self.lcd.clear()
                time.sleep(0.05)
            except Exception:
                pass
            self.lcd.setCursor(0, 0)
            self.lcd.write(line1)
            time.sleep(0.08)

            self.lcd.setCursor(1, 0)
            self.lcd.write(line2)
            time.sleep(0.08)

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
                self._write_now(job["line1"], job["line2"])
                time.sleep(job["duration"])
                self._show_default_now()

            elif job["type"] == "default":
                self._show_default_now()

            self._queue.task_done()

    def show_default(self):
        self._queue.put({"type": "default"})

    def show_success(self, balance, is_entry, user_name, duration=2):
        if is_entry:
            greeting = f"Welcome {user_name.capitalize()}!" if user_name else "Welcome!"
        else:
            greeting = f"Bye {user_name.capitalize()}!" if user_name else "Come Again!"

        line2 = f"RM{float(balance):.2f}"
        self._queue.put({
            "type": "temp",
            "line1": greeting,
            "line2": line2,
            "duration": duration
        })

    def show_denied(self, message, duration=2):
        short_msg = message.replace("DENIED_", "").replace("_", " ").title()
        self._queue.put({
            "type": "temp",
            "line1": "Access Denied!",
            "line2": short_msg,
            "duration": duration
        })


lcd_display = LCD()
