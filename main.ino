#include "wifi/wifi_module.h"
#include "rfid/rfid_module.h"

#define GATE_MODE_ENTRY 1
// #define GATE_MODE_EXIT 1

const char* ENTRY_URL = "http://192.168.200.104:5000/rfid_entry";
const char* EXIT_URL = "http://192.168.200.104:5000/rfid_exit";

void setup() {
  Serial.begin(115200);
  setupWiFi();
  setupRFID();
}

void loop() {
#ifdef GATE_MODE_ENTRY
  handleRFID(ENTRY_URL);
#endif

#ifdef GATE_MODE_EXIT
  handleRFID(EXIT_URL);
#endif
}