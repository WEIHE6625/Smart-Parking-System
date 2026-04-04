#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>        
#include <HTTPClient.h> 
#include <ESP32Servo.h>

const char* wifi_ssid = "B100M-T3";
const char* wifi_password = "12345678";

// Flask Server IP
const char* serverName = "http://192.168.0.104:5000/rfid_entry"; 

#define SS_PIN 5
#define RST_PIN 21
#define GREEN_LED 27
#define RED_LED 25  
#define BUZZER 32
#define ULTRASONIC_TRIG 13
#define ULTRASONIC_ECHO 34
#define SERVO_PIN 15


MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
Servo entry_gate;

int thresholdCM = 5;

void setup_wifi() {
  delay(10);
  WiFi.begin(wifi_ssid, wifi_password);
  while(WiFi.status() != WL_CONNECTED){
    Serial.println("WiFi Connecting...");
    delay(500);
  }
  Serial.println("WiFi Connected.");
}

void setup() {
  Serial.begin(115200);
  SPI.begin(); // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522


  // Initialize LED pins
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
 
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  digitalWrite(BUZZER, LOW);

  // Initialize ultrasonic sensor
  pinMode(ULTRASONIC_TRIG, OUTPUT);
  pinMode(ULTRASONIC_ECHO, INPUT);

  //Initialize servo motor
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  entry_gate.setPeriodHertz(50);    // Standard 50hz servo frequency
  entry_gate.attach(SERVO_PIN, 500, 2400); // Attach with min/max pulse widths
  entry_gate.write(50);

  // Connect to Wi-Fi
  setup_wifi();

  Serial.println(F("Please put your RFID card / tag:"));
}



bool car_is_blocking_gate() {
  digitalWrite(ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG, LOW);

  long duration = pulseIn(ULTRASONIC_ECHO, HIGH, 25000);
  float distanceCM = duration * 0.0343 / 2;
  Serial.println(distanceCM);
  if(distanceCM > 0 && distanceCM < thresholdCM) {
    return true; 
  }
  
  return false;
  

}

void detect_car_blocking_gate() {
  while(car_is_blocking_gate()) {
    Serial.println("Car is currently blocking gate. Waiting for car to move before closing");
    delay(1000);
  }
}


void gate_action() {
  entry_gate.write(90);
  delay(3000);
  detect_car_blocking_gate();
  entry_gate.write(0);
}
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

  gate_action();
  
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


void loop() {
  // Reset the loop if no new card present on the sensor/reader.
  
  if (!rfid.PICC_IsNewCardPresent()){
    return;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }
  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
  Serial.println(rfid.PICC_GetTypeName(piccType));

  // Check is the PICC of Classic MIFARE type
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
  piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
  piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println (F("Your tag is NOT of type MIFARE Classic."));
    return;
  }


  // Format the UID into a clean String
  String cardID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    // If the hex number is only 1 digit, add a leading '0' (like '0A')
    if (rfid.uid.uidByte[i] < 0x10) {
      cardID += "0"; 
    }
    cardID += String(rfid.uid.uidByte[i], HEX);
  }

  cardID.toUpperCase();
  Serial.print("Scanned Card ID: ");
  Serial.println(cardID);

  // Send the HTTP POST request to Flask
  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;
    http.begin(serverName); 
    http.addHeader("Content-Type", "application/json");
    
    // Create a simple JSON payload
    String httpRequestData = "{\"rfid_uid\":\"" + cardID + "\"}";
    
    Serial.println("Sending to Flask...");
    int httpResponseCode = http.POST(httpRequestData);
    
    // Read the response from Flask
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Server replied: ");
      Serial.println(response);

      if (response.indexOf("\"APPROVED\"") > 0) {
        // SUCCESS: Green LED + Double Beep
        approved();

      } else {
        // DENIED: Red Blinks + Short Beep
        denied();
      }

    } else {
      Serial.print("Error connecting to server. Code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
