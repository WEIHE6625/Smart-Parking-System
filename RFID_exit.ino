#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>        
#include <HTTPClient.h> 

const char* wifi_ssid = "B100M";
const char* wifi_password = "12345678";

// Flask Server IP
const char* serverName = "http://192.168.X.X:5000/rfid_exit"; 

#define SS_PIN 5
#define RST_PIN 21
#define GREEN_LED 27
#define RED_LED 25
#define BUZZER 34
MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
byte nuidPICC[4];


void setup_wifi() {
  delay(10);
  WiFi.begin(wifi_ssid, wifi_password);
  while(WiFi.status() != WL_CONNECTED){
    Serial.println("WiFi Connecting...");
    delay(500);
  }
  Serial.println("WiFi Connected.")
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

  // Connect to Wi-Fi
  setup_wifi();

  Serial.println(F("This code scan the MIFARE Classsic NUID."));
  Serial.println(F("Please put your RFID card / tag:"));
}


void approved() {
  Serial.println("Action: OPENING GATE");
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  
  digitalWrite(BUZZER, HIGH); 
  delay(100);
  digitalWrite(BUZZER, LOW);
  delay(100);
  digitalWrite(BUZZER, HIGH); 
  delay(100);
  digitalWrite(BUZZER, LOW);
  
  delay(1500);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
}

void denied() {
  Serial.println("Action: ACCESS DENIED");
  digitalWrite(BUZZER, HIGH);
  
  digitalWrite(RED_LED, LOW);
  delay(100);
  digitalWrite(RED_LED, HIGH);
  delay(100);
  digitalWrite(RED_LED, LOW); 
  delay(100);
  digitalWrite(RED_LED, HIGH);
  
  delay(700);
  digitalWrite(BUZZER, LOW);
}


void loop() {
  // Reset the loop if no new card present on the sensor/reader.
  if (!rfid.PICC_IsNewCardPresent()){
    return;
  }
  // Verify if the NUID has been readed
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


  if (rfid.uid.uidByte[0] != nuidPICC [0] ||
  rfid.uid.uidByte[1] != nuidPICC[1] ||
  rfid.uid.uidByte [2] != nuidPICC [2] ||
  rfid.uid.uidByte[3] != nuidPICC [3] ) {
    Serial.println (F("A new card has been detected."));

    // Format the UID into a clean String
    String cardID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      cardID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
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
          approved()

        } else {
          // DENIED: Red Blinks + Short Beep
          denied()
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
}