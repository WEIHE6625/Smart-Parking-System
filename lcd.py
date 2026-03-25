import time
import threading
from grove.display.jhd1802 import JHD1802


class LCD:
    def __init__(self):
        self.lcd = JHD1802()
        self.available_provider = None
        self._lock = threading.Lock()
        self._last_line1 = None
        self._last_line2 = None
        self._last_update_time = 0
        self._min_update_gap = 0.5  # minimum gap between LCD writes


    def _get_available_slot(self):
        if self.available_provider is None:
            return 0
        try:
            return int(self.available_provider())
        except Exception:
            return 0

    def _safe_text(self, text):
        return str(text)[:16].ljust(16)

    def _update_screen(self, line1, line2=""):
        line1 = self._safe_text(line1)
        line2 = self._safe_text(line2)

        with self._lock:
            now = time.time()
            if line1 == self._last_line1 and line2 == self._last_line2:
                return
            elapsed = now - self._last_update_time
            if elapsed < self._min_update_gap:
                time.sleep(self._min_update_gap - elapsed)
            self.lcd.setCursor(0, 0)
            self.lcd.write(line1)

            time.sleep(0.05)

            self.lcd.setCursor(1, 0)
            self.lcd.write(line2)

            self._last_line1 = line1
            self._last_line2 = line2
            self._last_update_time = time.time()

    def set_available_provider(self, provider_func):
        self.available_provider = provider_func

    def show_default(self):
        available_slot = self._get_available_slot()
        self._update_screen("Parking System", f"Slot: {available_slot}")

    def clear_and_default(self):
        time.sleep(0.1)
        self.show_default()

    def show_success(self, balance, is_entry, user_name):
        if is_entry:
            greeting = f"Welcome {user_name.capitalize()}!" if user_name else "Welcome!"
        else:
            greeting = f"Bye {user_name.capitalize()}!" if user_name else "Come Again!"

        line2 = f"RM{float(balance):.2f}"
        self._update_screen(greeting, line2)

        time.sleep(2)
        self.show_default()

    def show_denied(self, message):
        short_msg = message.replace("DENIED_", "").replace("_", " ").title()
        self._update_screen("Access Denied!", short_msg)

        time.sleep(2)
        self.show_default()

    def show_slot_update(self, zone, slot_id, status):
        total_available = self._get_available_slot()

        short_slot = slot_id.replace("Slot_", "")
        short_status = "Occupied" if status.lower() == "occupied" else "Empty"

        self._update_screen(f"{short_slot} {short_status}", f"Slot: {total_available}")

        time.sleep(2)
        self.show_default()


lcd_display = LCD()