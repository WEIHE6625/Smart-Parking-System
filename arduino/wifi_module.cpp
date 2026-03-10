#include <Arduino.h>
#include <WiFi.h>
#include "wifi_module.h"

const char* wifi_ssid = "B100M";
const char* wifi_password = "12345678";

void setupWiFi() {
  delay(10);
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Connecting...");
    delay(500);
  }

  Serial.println("WiFi Connected.");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}