#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

struct ParkingSlot {
  const char* slotId;
  uint8_t trigPin;
  uint8_t echoPin;
  uint8_t redLedPin;
  uint8_t greenLedPin;
  float occupiedThresholdCm;

  bool occupied;
  bool candidateOccupied;
  unsigned long candidateSince;
  float lastDistanceCm;
};

ParkingSlot zoneA[] = {
  { "A1", 18, 5, 19, 21, 12.0, false, false, 0, -1.0 },
  { "A2", 17, 16, 23, 22, 12.0, false, false, 0, -1.0 }
};

const int SLOT_COUNT = sizeof(zoneA) / sizeof(zoneA[0]);

const char* WIFI_SSID = "B100M-T8";
const char* WIFI_PASSWORD = "12345678";

// Use your broker IP here
// Setup: sudo systemctl enable mosquitto.service, then using sudo systemctl status mosquitto.service
// mosquitto_sub -h localhost -t "parking/#" -v to connect mqtt with raspberry pi
// Example: if Mosquitto runs on same PC as Flask
const char* MQTT_BROKER = "192.168.0.101";
const int   MQTT_PORT   = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASS = "";

// Flask Server IP
const char* FLASK_SERVER = "http://192.168.0.201:5000/rfid_entry"; 

const unsigned long STABLE_TIME_MS = 1000;
const unsigned long ECHO_TIMEOUT_US = 25000;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void connectMQTT() {
  while (!mqttClient.connected()) {
    String clientId = "ESP32-ZoneA-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    Serial.print("Connecting to MQTT... ");

    bool connected;
    if (strlen(MQTT_USER) > 0) {
      connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    } else {
      connected = mqttClient.connect(clientId.c_str());
    }

    if (connected) {
      Serial.println("connected");

      // Optional: publish boot status
      mqttClient.publish("parking/zoneA/system", "Zone A ESP32 online", true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retry in 2 seconds");
      delay(2000);
    }
  }
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void ensureConnections() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    connectMQTT();
  }

  mqttClient.loop();
}

void setupPins(ParkingSlot &slot) {
  pinMode(slot.trigPin, OUTPUT);
  pinMode(slot.echoPin, INPUT);
  pinMode(slot.redLedPin, OUTPUT);
  pinMode(slot.greenLedPin, OUTPUT);

  digitalWrite(slot.trigPin, LOW);
  digitalWrite(slot.redLedPin, LOW);
  digitalWrite(slot.greenLedPin, LOW);
}

void applyLedState(const ParkingSlot &slot) {
  digitalWrite(slot.redLedPin, slot.occupied ? HIGH : LOW);
  digitalWrite(slot.greenLedPin, slot.occupied ? LOW : HIGH);
}

float readDistanceCmOnce(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) return -1.0;

  return duration * 0.017;
}

float readDistanceCmFiltered(uint8_t trigPin, uint8_t echoPin) {
  float sum = 0;
  int validCount = 0;

  for (int i = 0; i < 3; i++) {
    float d = readDistanceCmOnce(trigPin, echoPin);
    if (d > 0) {
      sum += d;
      validCount++;
    }
    delay(10);
  }

  if (validCount == 0) return -2.0;
  return sum / validCount;
}

bool isOccupied(float distanceCm, float thresholdCm) {
  return distanceCm <= thresholdCm;
}

void publishSlotStatus(const ParkingSlot &slot) {
  char topic[64];
  snprintf(topic, sizeof(topic), "parking/zoneA/%s/status", slot.slotId);

  char payload[160];
  snprintf(
    payload,
    sizeof(payload),
    "{\"slot_id\":\"%s\",\"zone\":\"A\",\"status\":\"%s\",\"distance\":%.2f}",
    slot.slotId,
    slot.occupied ? "occupied" : "empty",
    slot.lastDistanceCm
  );

  bool ok = mqttClient.publish(topic, payload, true);

  Serial.print("MQTT publish to ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(ok ? "OK" : "FAILED");
}

void sendSlotStatusToFlask(const ParkingSlot &slot) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, cannot send to Flask");
    return;
  }

  HTTPClient http;
  http.begin(FLASK_SERVER);
  http.addHeader("Content-Type", "application/json");

  char payload[200];
  snprintf(
    payload,
    sizeof(payload),
    "{\"slot_id\":\"%s\",\"zone\":\"A\",\"status\":\"%s\",\"distance\":%.2f}",
    slot.slotId,
    slot.occupied ? "occupied" : "empty",
    slot.lastDistanceCm
  );

  int httpResponseCode = http.POST(payload);

  Serial.print("Flask response code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("Flask response: ");
    Serial.println(response);
  } else {
    Serial.print("HTTP POST failed: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

void reportSlotChange(const ParkingSlot &slot) {
  Serial.print("[STATUS] ");
  Serial.print(slot.slotId);
  Serial.print(" -> ");
  Serial.print(slot.occupied ? "OCCUPIED" : "EMPTY");
  Serial.print(" | Distance: ");
  Serial.print(slot.lastDistanceCm);
  Serial.println(" cm");

  sendSlotStatusToFlask(slot);
}

void initializeSlotState(ParkingSlot &slot) {
  float d = readDistanceCmFiltered(slot.trigPin, slot.echoPin);
  slot.lastDistanceCm = d;

  bool initialState = (d > 0) ? isOccupied(d, slot.occupiedThresholdCm) : false;
  slot.occupied = initialState;
  slot.candidateOccupied = initialState;
  slot.candidateSince = millis();

  applyLedState(slot);
}

void updateSlot(ParkingSlot &slot, unsigned long now) {
  float distance = readDistanceCmFiltered(slot.trigPin, slot.echoPin);
  if (distance < 0) return;

  slot.lastDistanceCm = distance;
  bool rawOccupied = isOccupied(distance, slot.occupiedThresholdCm);

  if (rawOccupied != slot.candidateOccupied) {
    slot.candidateOccupied = rawOccupied;
    slot.candidateSince = now;
  }

  if (slot.occupied != slot.candidateOccupied && now - slot.candidateSince >= STABLE_TIME_MS) {
    slot.occupied = slot.candidateOccupied;
    applyLedState(slot);
    reportSlotChange(slot);
    publishSlotStatus(slot);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < SLOT_COUNT; i++) {
    setupPins(zoneA[i]);
  }

  connectWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  connectMQTT();

  delay(500);

  for (int i = 0; i < SLOT_COUNT; i++) {
    initializeSlotState(zoneA[i]);
    reportSlotChange(zoneA[i]);
  }
}

void loop() {
  ensureConnections();
  unsigned long now = millis();

  for (int i = 0; i < SLOT_COUNT; i++) {
    updateSlot(zoneA[i], now);
  }
}