#include "wifi_module.h"
#include "rfid_module.h"
#include <string>

// #define GATE_MODE_ENTRY 1
#define GATE_MODE_EXIT 1

const String BASE_URL = "http://192.168.0.106:5000/";
const String ENTRY = "rfid_entry";
const String EXIT = "rfid_exit";

void setup() {
  Serial.begin(115200);
  setupWiFi();
  setupRFID();
}

void loop() {
#ifdef GATE_MODE_ENTRY
  String URL = BASE_URL + ENTRY;
  handleRFIDEntry(URL);
#endif

#ifdef GATE_MODE_EXIT
  String URL = BASE_URL + EXIT;
  handleRFIDExit(URL);
#endif
}