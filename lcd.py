import time
from grove.display.jhd1802 import JHD1802

class LCD:
    def __init__(self):
            self.lcd = JHD1802()

    def _update_screen(self, line1, line2=""):
        self.lcd.setCursor(0, 0)
        self.lcd.write(" " * 16)
        self.lcd.setCursor(1, 0)
        self.lcd.write(" " * 16)

        self.lcd.setCursor(0, 0)
        self.lcd.write(str(line1)[:16].ljust(16))

        self.lcd.setCursor(1, 0)
        self.lcd.write(str(line2)[:16].ljust(16))

    def clear_and_default(self, available_slot):
        self._update_screen("", "")
        time.sleep(0.2)
        self.show_default(available_slot)

    def show_default(self, available_slot):
        self._update_screen("Parking System", f"Slot: {available_slot}")

    def show_success(self, balance, is_entry, user_name, available_slot):
        if is_entry:
            greeting = f"Welcome {user_name.capitalize()}!" if user_name else "Welcome!"
        else:
            greeting = f"Bye {user_name.capitalize()}!" if user_name else "Come Again!"

        greeting = greeting[:16]
        line2 = f"RM{float(balance):.2f} A:{available_slot}"
        self._update_screen(greeting, line2[:16])

        time.sleep(2)
        self.clear_and_default(available_slot)

    def show_denied(self, message, available_slot):
        short_msg = message.replace("DENIED_", "").replace("_", " ").title()
        self._update_screen("Access Denied!", short_msg[:16])

        time.sleep(2)
        self.clear_and_default(available_slot)

    def show_slot_update(self, zone, slot_id, status, total_available):
        short_slot = slot_id.replace("Slot_", "")
        short_status = "Occupied" if status.lower() == "occupied" else "Empty"
        self._update_screen(f"{short_slot} {short_status}"[:16], f"Slot: {total_available}"[:16])

        time.sleep(2)
        self.clear_and_default(total_available)


lcd_display = LCD()