#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "rfid_module.h"

#define SS_PIN 5
#define RST_PIN 21
#define GREEN_LED 27
#define RED_LED 25
#define BUZZER 32

MFRC522 rfid(SS_PIN, RST_PIN);

const char* ENTRY_URL = "http://192.168.200.104:5000/rfid_entry";
const char* EXIT_URL  = "http://192.168.200.104:5000/rfid_exit";

void approved() {
  Serial.println("Action: OPENING GATE");
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);

  digitalWrite(BUZZER, HIGH);
  delay(150);
  digitalWrite(BUZZER, LOW);
  delay(150);
  digitalWrite(BUZZER, HIGH);
  delay(150);
  digitalWrite(BUZZER, LOW);

  delay(1050);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
}

void denied() {
  Serial.println("Action: ACCESS DENIED");
  digitalWrite(BUZZER, HIGH);

  digitalWrite(RED_LED, LOW);
  delay(300);
  digitalWrite(RED_LED, HIGH);
  delay(300);
  digitalWrite(RED_LED, LOW);
  delay(300);
  digitalWrite(RED_LED, HIGH);

  delay(100);
  digitalWrite(BUZZER, LOW);
}

String readCardUID() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return "";
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return "";
  }

  MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);

  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println("Your tag is NOT of type MIFARE Classic.");
    return "";
  }

  String cardID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      cardID += "0";
    }
    cardID += String(rfid.uid.uidByte[i], HEX);
  }

  cardID.toUpperCase();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  return cardID;
}

void sendRFIDToServer(const char* url) {
  String cardID = readCardUID();

  if (cardID == "") {
    return;
  }

  Serial.print("Scanned Card ID: ");
  Serial.println(cardID);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Disconnected");
    return;
  }

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String httpRequestData = "{\"rfid_uid\":\"" + cardID + "\"}";

  Serial.println("Sending to Flask...");
  int httpResponseCode = http.POST(httpRequestData);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("Server replied: ");
    Serial.println(response);

    if (response.indexOf("\"APPROVED\"") >= 0) {
      approved();
    } else {
      denied();
    }
  } else {
    Serial.print("Error connecting to server. Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

void setupRFID() {
  SPI.begin();
  rfid.PCD_Init();

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  digitalWrite(BUZZER, LOW);

  Serial.println("Please put your RFID card / tag:");
}

void handleRFIDEntry() {
  sendRFIDToServer(ENTRY_URL);
}

void handleRFIDExit() {
  sendRFIDToServer(EXIT_URL);
}