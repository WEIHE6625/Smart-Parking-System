#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

struct ParkingSlot {
  const char* zone;
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

struct IllegalParkingSensor {
  const char* zone;
  const char* sensorId;
  uint8_t trigPin;
  uint8_t echoPin;
  float illegalThresholdCm;

  bool illegalDetected;
  bool candidateIllegal;
  unsigned long candidateSince;
  float lastDistanceCm;
};

ParkingSlot zoneB[] = {
  { "Zone_B", "Slot_B1", 26, 27, 21, 19, 8.0, false, false, 0, -1.0 },
  { "Zone_B", "Slot_B2", 17, 16, 23, 22, 8.0, false, false, 0, -1.0 }
};

const int SLOT_COUNT = sizeof(zoneB) / sizeof(zoneB[0]);

IllegalParkingSensor illegalSensor = { "Zone_B", "Illegal_Line_B", 32, 33, 12.0, false, false, 0, -1.0 };

const uint8_t BUZZER_PIN = 4;
const uint8_t WARNING_LED_PIN = 15;

const unsigned long BUZZER_ON_MS = 1000;
const unsigned long BUZZER_OFF_MS = 800;
const int BUZZER_FREQUENCY = 2000;
const int BUZZER_CHANNEL = 0;
const int BUZZER_RESOLUTION = 8;

const char* WIFI_SSID = "B100M-T8";
const char* WIFI_PASSWORD = "12345678";


// Use your broker IP here
// Example: if Mosquitto runs on same PC as Flask
const char* MQTT_BROKER = "192.168.0.102";
const int   MQTT_PORT   = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASS = "";

const unsigned long STABLE_TIME_MS = 1000;
const unsigned long ECHO_TIMEOUT_US = 25000;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool buzzerEnabled = false;
bool buzzerState = false;
unsigned long buzzerLastToggle = 0;

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

void connectMQTT() {
  while (!mqttClient.connected()) {
    String clientId = "ESP32-ZoneB-";
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
      mqttClient.publish("parking/Zone_B/system", "Zone B ESP32 online", true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retry in 2 seconds");
      delay(2000);
    }
  }
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

void setupIllegalSensorPins(IllegalParkingSensor &sensor) {
  pinMode(sensor.trigPin, OUTPUT);
  pinMode(sensor.echoPin, INPUT);
  digitalWrite(sensor.trigPin, LOW);
}

void setupBuzzer() {
  ledcAttach(BUZZER_PIN, BUZZER_FREQUENCY, BUZZER_RESOLUTION);
  ledcWriteTone(BUZZER_PIN, 0);
}

void setupWarningLed() {
  pinMode(WARNING_LED_PIN, OUTPUT);
  digitalWrite(WARNING_LED_PIN, LOW);
}

void applyLedState(const ParkingSlot &slot) {
  digitalWrite(slot.redLedPin, slot.occupied ? HIGH : LOW);
  digitalWrite(slot.greenLedPin, slot.occupied ? LOW : HIGH);
}

void startBuzzerAlert() {
  buzzerEnabled = true;
  buzzerState = true;
  buzzerLastToggle = millis();
  ledcWriteTone(BUZZER_PIN, BUZZER_FREQUENCY);
  digitalWrite(WARNING_LED_PIN, HIGH);
}

void stopBuzzerAlert() {
  buzzerEnabled = false;
  buzzerState = false;
  ledcWriteTone(BUZZER_PIN, 0);
  digitalWrite(WARNING_LED_PIN, LOW);
}

void updateBuzzer(unsigned long now) {
  if (!buzzerEnabled) {
    ledcWriteTone(BUZZER_PIN, 0);
    return;
  }

  if (buzzerState) {
    // currently ON
    if (now - buzzerLastToggle >= BUZZER_ON_MS) {
      buzzerState = false;
      buzzerLastToggle = now;
      ledcWriteTone(BUZZER_PIN, 0);
    }
  } else {
    // currently OFF
    if (now - buzzerLastToggle >= BUZZER_OFF_MS) {
      buzzerState = true;
      buzzerLastToggle = now;
      ledcWriteTone(BUZZER_PIN, BUZZER_FREQUENCY);
    }
  }
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

bool isIllegalParking(float distanceCm, float thresholdCm) {
  return distanceCm > 0 && distanceCm <= thresholdCm;
}

void publishSlotStatus(const ParkingSlot &slot) {
  char topic[80];
  // Updated topic to be dynamic: parking/zone<A>/Slot_A1/status
  snprintf(topic, sizeof(topic), "parking/%s/%s/status", slot.zone, slot.slotId);

  char payload[180];
  snprintf(
    payload,
    sizeof(payload),
    "{\"zone\":\"%s\", \"slot_id\":\"%s\",\"status\":\"%s\",\"distance\":%.2f}",
    slot.zone,
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

void publishIllegalParkingStatus(const IllegalParkingSensor &sensor) {
  char topic[100];
  snprintf(topic, sizeof(topic), "parking/%s/illegal_parking/status", sensor.zone);

  char payload[220];
  snprintf(
    payload,
    sizeof(payload),
    "{\"zone\":\"%s\",\"sensor_id\":\"%s\",\"illegal_parking\":%s,\"distance\":%.2f,\"message\":\"%s\"}",
    sensor.zone,
    sensor.sensorId,
    sensor.illegalDetected ? "true" : "false",
    sensor.lastDistanceCm,
    sensor.illegalDetected ? "Illegal parking detected" : "Illegal parking cleared"
  );

  bool ok = mqttClient.publish(topic, payload, true);

  Serial.print("MQTT publish to ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(ok ? "OK" : "FAILED");
}

void reportSlotChange(const ParkingSlot &slot) {
  Serial.print("[STATUS] ");
  Serial.print(slot.slotId);
  Serial.print(" -> ");
  Serial.print(slot.occupied ? "OCCUPIED" : "EMPTY");
  Serial.print(" | Distance: ");
  Serial.print(slot.lastDistanceCm);
  Serial.println(" cm");
}

void reportIllegalParkingChange(const IllegalParkingSensor &sensor) {
  Serial.print("[ILLEGAL] ");
  Serial.print(sensor.sensorId);
  Serial.print(" -> ");
  Serial.print(sensor.illegalDetected ? "DETECTED" : "CLEARED");
  Serial.print(" | Distance: ");
  Serial.print(sensor.lastDistanceCm);
  Serial.println(" cm");
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

void initializeIllegalParkingState(IllegalParkingSensor &sensor) {
  float d = readDistanceCmFiltered(sensor.trigPin, sensor.echoPin);
  sensor.lastDistanceCm = d;
  Serial.print(d);
  bool initialState = (d > 0) ? isIllegalParking(d, sensor.illegalThresholdCm) : false;
  sensor.illegalDetected = initialState;
  sensor.candidateIllegal = initialState;
  sensor.candidateSince = millis();

  if (sensor.illegalDetected) {
    startBuzzerAlert();
  } else {
    stopBuzzerAlert();
  }
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

void updateIllegalParking(IllegalParkingSensor &sensor, unsigned long now) {
  float distance = readDistanceCmFiltered(sensor.trigPin, sensor.echoPin);
  if (distance < 0) return;

  sensor.lastDistanceCm = distance;
  bool rawIllegal = isIllegalParking(distance, sensor.illegalThresholdCm);

  if (rawIllegal != sensor.candidateIllegal) {
    sensor.candidateIllegal = rawIllegal;
    sensor.candidateSince = now;
  }

  if (sensor.illegalDetected != sensor.candidateIllegal &&
      now - sensor.candidateSince >= STABLE_TIME_MS) {

    sensor.illegalDetected = sensor.candidateIllegal;

    reportIllegalParkingChange(sensor);
    publishIllegalParkingStatus(sensor);

    if (sensor.illegalDetected) {
      startBuzzerAlert();
    } else {
      stopBuzzerAlert();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < SLOT_COUNT; i++) {
    setupPins(zoneB[i]);
  }

  setupIllegalSensorPins(illegalSensor);
  setupBuzzer();
  setupWarningLed();

  connectWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  connectMQTT();

  delay(500);

  for (int i = 0; i < SLOT_COUNT; i++) {
    initializeSlotState(zoneB[i]);
    reportSlotChange(zoneB[i]);
    publishSlotStatus(zoneB[i]);
  }

  initializeIllegalParkingState(illegalSensor);
  reportIllegalParkingChange(illegalSensor);
  publishIllegalParkingStatus(illegalSensor);
}

void loop() {
  ensureConnections();

  unsigned long now = millis();

  for (int i = 0; i < SLOT_COUNT; i++) {
    updateSlot(zoneB[i], now);
  }

  updateIllegalParking(illegalSensor, now);
  updateBuzzer(now);
}