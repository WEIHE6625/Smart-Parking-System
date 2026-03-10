import time
from grove.display.jhd1802 import JHD1802

class RFID_LCD:
    def __init__(self):
        self.lcd = JHD1802()
        
    def _update_screen(self, line1, line2=""):
        self.lcd.setCursor(0, 0)
        self.lcd.write(line1.ljust(16))
        
        self.lcd.setCursor(1, 0)
        self.lcd.write(line2.ljust(16))

    def show_default(self, available_slot):
        self._update_screen("Parking System", f"A. Slot: {available_slot}")

    def show_success(self, balance, is_entry):
        if is_entry:
            self._update_screen("Welcome!", f"Balance: RM{float(balance):.2f}")
        else:
            self._update_screen("Come Again!", f"Balance: RM{float(balance):.2f}")

    def show_denied(self, message):
        # Clean up backend messages for the 16x2 character limit if needed
        short_msg = message.replace("DENIED_", "").replace("_", " ").title()
        self._update_screen("Access Denied!", short_msg[:16])