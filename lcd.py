import time
import threading
from grove.display.jhd1802 import JHD1802


class LCD:
    def __init__(self):
        self.lcd = JHD1802()
        self.available_provider = None
        self.roaming_provider = None
        self._lock = threading.Lock()
        self._last_line1 = None
        self._last_line2 = None
        self._last_update_time = 0
        self._min_update_gap = 0.2  # minimum gap between LCD writes
        self.is_busy = False                 # <--- Added to protect welcome messages
        self.roaming_thread_active = False


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

    def set_roaming_provider(self, provider_func):
        self.roaming_provider = provider_func

    def show_default(self):
        if self.is_busy: return
        available_slot = self._get_available_slot()
        self._update_screen("Parking System", f"Slot: {available_slot}")

    def clear_and_default(self):
        time.sleep(0.1)
        self.show_default()

    def show_success(self, balance, is_entry, user_name):
        self.is_busy = True  # Set busy flag to prevent default screen from interrupting welcome message
        if is_entry:
            greeting = f"Welcome {user_name.capitalize()}!" if user_name else "Welcome!"
        else:
            greeting = f"Bye {user_name.capitalize()}!" if user_name else "Come Again!"

        line2 = f"RM{float(balance):.2f}"
        self._update_screen(greeting, line2)

        time.sleep(2)
        self.is_busy = False
        if not self.roaming_thread_active:
            self.show_default()

    def show_denied(self, message):
        self.is_busy = True # Pause the roaming loop
        short_msg = message.replace("DENIED_", "").replace("_", " ").title()
        self._update_screen("Access Denied!", short_msg)

        time.sleep(2)
        self.is_busy = False
            
        if not self.roaming_thread_active:
            self.show_default()

    def start_roaming_loop(self):
        if self.roaming_thread_active:
            return # Loop is already running, no need to start another

        self.roaming_thread_active = True
        
        def loop():
            while self.roaming_provider and self.roaming_provider() > 0:
                if self.is_busy:
                    time.sleep(0.1)
                    continue # Wait if LCD is showing "Welcome"
                    
                # 1. Show Roaming for 3 seconds
                count = self.roaming_provider()
                if count > 0 and not self.is_busy:
                    self._update_screen("Parking Area", f"{count} Car(s) Roaming")
                    for _ in range(30): # 30 * 0.1s = 3 seconds
                        if self.roaming_provider() == 0 or self.is_busy: break
                        time.sleep(0.1)

                if self.roaming_provider() == 0: break

                # 2. Show Default for 5 seconds
                if not self.is_busy:
                    self.show_default()
                    for _ in range(50): # 50 * 0.1s = 5 seconds
                        if self.roaming_provider() == 0 or self.is_busy: break
                        time.sleep(0.1)

            # Once CARS_ROAMING hits 0, the loop naturally ends and shows default
            self.roaming_thread_active = False
            if not self.is_busy:
                self.show_default()
        # Run the alternating loop in the background
        threading.Thread(target=loop, daemon=True).start()
        
    def interrupt_roaming(self):
        self._roaming_interrupt = True

    def show_slot_update(self, zone, slot_id, status):
        total_available = self._get_available_slot()

        short_slot = slot_id.replace("_", "")
        short_status = "Occupied" if status.lower() == "occupied" else "Empty"

        self._update_screen(f"{short_slot} {short_status}", f"Slot: {total_available}")

        time.sleep(2)
        self.show_default()


lcd_display = LCD()