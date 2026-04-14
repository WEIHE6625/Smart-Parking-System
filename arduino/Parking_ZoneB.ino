#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>   // ← MOVED FROM ZONE A

// ─── Zone B ESP32 ─────────────────────────────────────────────────────────────
// Responsibilities:
//   - Monitor Zone B parking slots (Slot_B1, Slot_B2)
//   - Monitor Zone B illegal parking sensor
//   - Publish all status changes to MQTT
//   - Control the barrier gate servo (moved here from Zone A)
//   - Monitor gate safety sensor before closing
//   - Subscribe to Zone A slot status to drive gate logic
// ─────────────────────────────────────────────────────────────────────────────

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

const uint8_t BUZZER_PIN      = 4;
const uint8_t WARNING_LED_PIN = 15;

const unsigned long BUZZER_ON_MS  = 1000;
const unsigned long BUZZER_OFF_MS = 800;
const int BUZZER_FREQUENCY  = 2000;
const int BUZZER_CHANNEL    = 0;
const int BUZZER_RESOLUTION = 8;

// ── MOVED FROM ZONE A: Servo configuration ────────────────────────────────────
const uint8_t SERVO_PIN        = 18;
const int     SERVO_OPEN_DEG   = 90;
const int     SERVO_CLOSED_DEG = 0;

Servo barrierServo;
bool  servoOpen  = false;
int   currentPos = SERVO_CLOSED_DEG;
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Gate safety sensor ─────────────────────────────────────
const uint8_t GATE_SENSOR_TRIG_PIN    = 5;
const uint8_t GATE_SENSOR_ECHO_PIN    = 34;
const float   GATE_CLEAR_THRESHOLD_CM = 10.0;
const unsigned long GATE_RECHECK_DELAY_MS = 3000;

bool          gatePendingClose = false;
unsigned long gatePendingSince = 0;
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Zone A slot status — updated via MQTT subscription ─────
// Zone B now subscribes to Zone A slot topics so it can drive the gate.
const int ZONE_A_SLOT_COUNT = 2;   // ← match however many slots Zone A has
bool zoneAOccupied[ZONE_A_SLOT_COUNT] = { false, false };
// ─────────────────────────────────────────────────────────────────────────────

const char* WIFI_SSID     = "B100M-T3";
const char* WIFI_PASSWORD = "12345678";

const char* MQTT_BROKER = "192.168.0.101";
const int   MQTT_PORT   = 1883;
const char* MQTT_USER   = "";
const char* MQTT_PASS   = "";

const unsigned long STABLE_TIME_MS  = 1000;
const unsigned long ECHO_TIMEOUT_US = 25000;

WiFiClient   espClient;
PubSubClient mqttClient(espClient);

bool          buzzerEnabled    = false;
bool          buzzerState      = false;
unsigned long buzzerLastToggle = 0;

// ── MOVED FROM ZONE A: smooth_move ───────────────────────────────────────────
void smooth_move(int targetAngle) {
  barrierServo.attach(SERVO_PIN, 500, 2400);

  if (currentPos < targetAngle) {
    for (int pos = currentPos; pos <= targetAngle; pos++) {
      barrierServo.write(pos);
      delay(20);
    }
  } else {
    for (int pos = currentPos; pos >= targetAngle; pos--) {
      barrierServo.write(pos);
      delay(20);
    }
  }

  currentPos = targetAngle;
  delay(200);
  barrierServo.detach();
}
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Gate safety sensor reading ─────────────────────────────
float readGateSensorCm() {
  float sum   = 0;
  int   valid = 0;

  for (int i = 0; i < 3; i++) {
    digitalWrite(GATE_SENSOR_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(GATE_SENSOR_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(GATE_SENSOR_TRIG_PIN, LOW);

    unsigned long duration = pulseIn(GATE_SENSOR_ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
    if (duration > 0) { sum += duration * 0.017; valid++; }
    delay(10);
  }

  return (valid == 0) ? -1.0 : sum / valid;
}

bool isGateClear() {
  float d = readGateSensorCm();

  if (d < 0) {
    Serial.println("[GATE SENSOR] No valid reading - assuming clear");
    return true;
  }

  Serial.print("[GATE SENSOR] Distance: ");
  Serial.print(d);
  Serial.println(" cm");

  return d > GATE_CLEAR_THRESHOLD_CM;
}
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Gate logic helpers ────────────────────────────────────
bool isZoneAFull() {
  for (int i = 0; i < ZONE_A_SLOT_COUNT; i++) {
    if (!zoneAOccupied[i]) return false;
  }
  return true;
}

bool isAnyZoneBOccupied() {
  for (int i = 0; i < SLOT_COUNT; i++) {
    if (zoneB[i].occupied) return true;
  }
  return false;
}
// ─────────────────────────────────────────────────────────────────────────────

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

// ── MOVED FROM ZONE A: MQTT callback — now listens to Zone A slot topics ──────
//
// Previously Zone A listened to Zone B topics.
// Now Zone B listens to Zone A topics so it can drive the gate.
// Topic format: parking/Zone_A/Slot_A<N>/status
//
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[256];
  unsigned int copyLen = length < sizeof(msg) - 1 ? length : sizeof(msg) - 1;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  String topicStr = String(topic);

  // Only process Zone A slot status messages
  if (!topicStr.startsWith("parking/Zone_A/Slot_A")) return;

  // Determine which Zone A slot index (Slot_A1 → index 0, etc.)
  int slotIndex = -1;
  for (int i = 0; i < ZONE_A_SLOT_COUNT; i++) {
    String expectedSlot = "Slot_A" + String(i + 1);
    if (topicStr.indexOf(expectedSlot) != -1) {
      slotIndex = i;
      break;
    }
  }

  if (slotIndex < 0 || slotIndex >= ZONE_A_SLOT_COUNT) return;

  String msgStr = String(msg);
  if (msgStr.indexOf("\"occupied\"") != -1) {
    zoneAOccupied[slotIndex] = true;
  } else if (msgStr.indexOf("\"empty\"") != -1) {
    zoneAOccupied[slotIndex] = false;
  }

  Serial.print("[ZONE A] Slot_A");
  Serial.print(slotIndex + 1);
  Serial.print(" -> ");
  Serial.println(zoneAOccupied[slotIndex] ? "OCCUPIED" : "EMPTY");

  // Re-evaluate gate immediately since Zone A state just changed
  extern void updateServoForZoneA();
  updateServoForZoneA();
}
// ─────────────────────────────────────────────────────────────────────────────

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

      // ── MOVED FROM ZONE A: subscribe to Zone A slot status ────────────────
      mqttClient.subscribe("parking/Zone_A/+/status");
      Serial.println("Subscribed to parking/Zone_A/+/status");
      // ─────────────────────────────────────────────────────────────────────
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retry in 2 seconds");
      delay(2000);
    }
  }
}

void ensureConnections() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected())        connectMQTT();
  mqttClient.loop();
}

void setupPins(ParkingSlot &slot) {
  pinMode(slot.trigPin,     OUTPUT);
  pinMode(slot.echoPin,     INPUT);
  pinMode(slot.redLedPin,   OUTPUT);
  pinMode(slot.greenLedPin, OUTPUT);

  digitalWrite(slot.trigPin,     LOW);
  digitalWrite(slot.redLedPin,   LOW);
  digitalWrite(slot.greenLedPin, LOW);
}

void setupIllegalSensorPins(IllegalParkingSensor &sensor) {
  pinMode(sensor.trigPin, OUTPUT);
  pinMode(sensor.echoPin, INPUT);
  digitalWrite(sensor.trigPin, LOW);
}

// ── FIXED: buzzer now uses digitalWrite to avoid LEDC conflict with servo ─────
void setupBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}
// ─────────────────────────────────────────────────────────────────────────────

void setupWarningLed() {
  pinMode(WARNING_LED_PIN, OUTPUT);
  digitalWrite(WARNING_LED_PIN, LOW);
}

// ── MOVED FROM ZONE A: Servo setup ───────────────────────────────────────────
void setupServo() {
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  barrierServo.setPeriodHertz(50);
  barrierServo.attach(SERVO_PIN, 500, 2400);
  barrierServo.write(SERVO_CLOSED_DEG);
  delay(500);
  barrierServo.detach();
  currentPos = SERVO_CLOSED_DEG;
  servoOpen  = false;
  Serial.println("Servo initialised - barrier CLOSED");
}
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Gate safety sensor setup ───────────────────────────────
void setupGateSensor() {
  pinMode(GATE_SENSOR_TRIG_PIN, OUTPUT);
  pinMode(GATE_SENSOR_ECHO_PIN, INPUT);
  digitalWrite(GATE_SENSOR_TRIG_PIN, LOW);
  Serial.println("Gate safety sensor initialised (Trig:5, Echo:34)");
}
// ─────────────────────────────────────────────────────────────────────────────

void applyLedState(const ParkingSlot &slot) {
  digitalWrite(slot.redLedPin,   slot.occupied ? HIGH : LOW);
  digitalWrite(slot.greenLedPin, slot.occupied ? LOW  : HIGH);
}

// ── FIXED: ledcWriteTone replaced with digitalWrite ───────────────────────────
void startBuzzerAlert() {
  buzzerEnabled    = true;
  buzzerState      = true;
  buzzerLastToggle = millis();
  digitalWrite(BUZZER_PIN,      HIGH);
  digitalWrite(WARNING_LED_PIN, HIGH);
}

void stopBuzzerAlert() {
  buzzerEnabled = false;
  buzzerState   = false;
  digitalWrite(BUZZER_PIN,      LOW);
  digitalWrite(WARNING_LED_PIN, LOW);
}

void updateBuzzer(unsigned long now) {
  if (!buzzerEnabled) {
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  if (buzzerState) {
    if (now - buzzerLastToggle >= BUZZER_ON_MS) {
      buzzerState      = false;
      buzzerLastToggle = now;
      digitalWrite(BUZZER_PIN, LOW);
    }
  } else {
    if (now - buzzerLastToggle >= BUZZER_OFF_MS) {
      buzzerState      = true;
      buzzerLastToggle = now;
      digitalWrite(BUZZER_PIN, HIGH);
    }
  }
}
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Safe close with car detection ─────────────────────────
void safeCloseGate() {
  Serial.println("[GATE] Checking for cars under gate before closing...");

  if (!isGateClear()) {
    Serial.println("[GATE] Car detected under gate! Close CANCELLED - will retry in 3s");
    gatePendingClose = true;
    gatePendingSince = millis();
    mqttClient.publish("parking/Zone_A/barrier/status",
      "{\"barrier\":\"open\",\"reason\":\"close_blocked_car_detected\"}", true);
  } else {
    Serial.println("[GATE] Gate clear - closing barrier");
    gatePendingClose = false;
    smooth_move(SERVO_CLOSED_DEG);
    servoOpen = false;
    mqttClient.publish("parking/Zone_A/barrier/status",
      "{\"barrier\":\"closed\",\"reason\":\"space_available_zone_B_empty\"}", true);
  }
}
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Gate logic ─────────────────────────────────────────────
void updateServoForZoneA() {
  bool zoneAFull      = isZoneAFull();
  bool anyZoneBActive = isAnyZoneBOccupied();

  if (zoneAFull && !servoOpen) {
    Serial.println("[SERVO] Zone A FULL - opening barrier");
    gatePendingClose = false;
    smooth_move(SERVO_OPEN_DEG);
    servoOpen = true;
    mqttClient.publish("parking/Zone_A/barrier/status",
      "{\"barrier\":\"open\",\"reason\":\"zone_A_full\"}", true);

  } else if (!zoneAFull && !anyZoneBActive && servoOpen) {
    Serial.println("[SERVO] Zone A has space & Zone B empty - attempting close");
    safeCloseGate();

  } else if (!zoneAFull && anyZoneBActive && !servoOpen) {
    Serial.println("[SERVO] Zone A has space but Zone B occupied - barrier stays OPEN");
    gatePendingClose = false;
    smooth_move(SERVO_OPEN_DEG);
    servoOpen = true;
    mqttClient.publish("parking/Zone_A/barrier/status",
      "{\"barrier\":\"open\",\"reason\":\"zone_B_still_occupied\"}", true);
  }
}
// ─────────────────────────────────────────────────────────────────────────────

// ── MOVED FROM ZONE A: Pending close retry ───────────────────────────────────
void checkPendingGateClose(unsigned long now) {
  if (!gatePendingClose) return;
  if (now - gatePendingSince < GATE_RECHECK_DELAY_MS) return;

  bool zoneAFull      = isZoneAFull();
  bool anyZoneBActive = isAnyZoneBOccupied();
  bool shouldClose    = !zoneAFull && !anyZoneBActive && servoOpen;

  if (!shouldClose) {
    Serial.println("[GATE] Pending close cancelled - conditions changed");
    gatePendingClose = false;
    return;
  }

  Serial.println("[GATE] Retrying close after delay...");
  safeCloseGate();
}
// ─────────────────────────────────────────────────────────────────────────────

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
  float sum      = 0;
  int   validCount = 0;

  for (int i = 0; i < 3; i++) {
    float d = readDistanceCmOnce(trigPin, echoPin);
    if (d > 0) { sum += d; validCount++; }
    delay(10);
  }

  return (validCount == 0) ? -2.0 : sum / validCount;
}

bool isOccupied(float distanceCm, float thresholdCm) {
  return distanceCm <= thresholdCm;
}

bool isIllegalParking(float distanceCm, float thresholdCm) {
  return distanceCm > 0 && distanceCm <= thresholdCm;
}

void publishSlotStatus(const ParkingSlot &slot) {
  char topic[80];
  snprintf(topic, sizeof(topic), "parking/%s/%s/status", slot.zone, slot.slotId);

  char payload[180];
  snprintf(payload, sizeof(payload),
    "{\"zone\":\"%s\", \"slot_id\":\"%s\",\"status\":\"%s\",\"distance\":%.2f}",
    slot.zone, slot.slotId,
    slot.occupied ? "occupied" : "empty",
    slot.lastDistanceCm);

  bool ok = mqttClient.publish(topic, payload, true);
  Serial.print("MQTT publish to "); Serial.print(topic);
  Serial.println(ok ? " -> OK" : " -> FAILED");
}

void publishIllegalParkingStatus(const IllegalParkingSensor &sensor) {
  char topic[100];
  snprintf(topic, sizeof(topic), "parking/%s/illegal_parking/status", sensor.zone);

  char payload[220];
  snprintf(payload, sizeof(payload),
    "{\"zone\":\"%s\",\"sensor_id\":\"%s\",\"illegal_parking\":%s,\"distance\":%.2f,\"message\":\"%s\"}",
    sensor.zone, sensor.sensorId,
    sensor.illegalDetected ? "true" : "false",
    sensor.lastDistanceCm,
    sensor.illegalDetected ? "Illegal parking detected" : "Illegal parking cleared");

  bool ok = mqttClient.publish(topic, payload, true);
  Serial.print("MQTT publish to "); Serial.print(topic);
  Serial.println(ok ? " -> OK" : " -> FAILED");
}

void reportSlotChange(const ParkingSlot &slot) {
  Serial.print("[STATUS] "); Serial.print(slot.slotId);
  Serial.print(" -> "); Serial.print(slot.occupied ? "OCCUPIED" : "EMPTY");
  Serial.print(" | Distance: "); Serial.print(slot.lastDistanceCm);
  Serial.println(" cm");
}

void reportIllegalParkingChange(const IllegalParkingSensor &sensor) {
  Serial.print("[ILLEGAL] "); Serial.print(sensor.sensorId);
  Serial.print(" -> "); Serial.print(sensor.illegalDetected ? "DETECTED" : "CLEARED");
  Serial.print(" | Distance: "); Serial.print(sensor.lastDistanceCm);
  Serial.println(" cm");
}

void initializeSlotState(ParkingSlot &slot) {
  float d = readDistanceCmFiltered(slot.trigPin, slot.echoPin);
  slot.lastDistanceCm = d;

  bool initialState = (d > 0) ? isOccupied(d, slot.occupiedThresholdCm) : false;
  slot.occupied          = initialState;
  slot.candidateOccupied = initialState;
  slot.candidateSince    = millis();

  applyLedState(slot);
}

void initializeIllegalParkingState(IllegalParkingSensor &sensor) {
  float d = readDistanceCmFiltered(sensor.trigPin, sensor.echoPin);
  sensor.lastDistanceCm   = d;
  Serial.print(d);

  bool initialState = (d > 0) ? isIllegalParking(d, sensor.illegalThresholdCm) : false;
  sensor.illegalDetected  = initialState;
  sensor.candidateIllegal = initialState;
  sensor.candidateSince   = millis();

  if (sensor.illegalDetected) startBuzzerAlert();
  else                         stopBuzzerAlert();
}

void updateSlot(ParkingSlot &slot, unsigned long now) {
  float distance = readDistanceCmFiltered(slot.trigPin, slot.echoPin);
  if (distance < 0) return;

  slot.lastDistanceCm = distance;
  bool rawOccupied    = isOccupied(distance, slot.occupiedThresholdCm);

  if (rawOccupied != slot.candidateOccupied) {
    slot.candidateOccupied = rawOccupied;
    slot.candidateSince    = now;
  }

  if (slot.occupied != slot.candidateOccupied && now - slot.candidateSince >= STABLE_TIME_MS) {
    slot.occupied = slot.candidateOccupied;
    applyLedState(slot);
    reportSlotChange(slot);
    publishSlotStatus(slot);
    updateServoForZoneA(); // re-evaluate gate when Zone B occupancy changes
  }
}

void updateIllegalParking(IllegalParkingSensor &sensor, unsigned long now) {
  float distance = readDistanceCmFiltered(sensor.trigPin, sensor.echoPin);
  if (distance < 0) return;

  sensor.lastDistanceCm = distance;
  bool rawIllegal       = isIllegalParking(distance, sensor.illegalThresholdCm);

  if (rawIllegal != sensor.candidateIllegal) {
    sensor.candidateIllegal = rawIllegal;
    sensor.candidateSince   = now;
  }

  if (sensor.illegalDetected != sensor.candidateIllegal &&
      now - sensor.candidateSince >= STABLE_TIME_MS) {

    sensor.illegalDetected = sensor.candidateIllegal;
    reportIllegalParkingChange(sensor);
    publishIllegalParkingStatus(sensor);

    if (sensor.illegalDetected) startBuzzerAlert();
    else                         stopBuzzerAlert();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < SLOT_COUNT; i++) setupPins(zoneB[i]);

  setupIllegalSensorPins(illegalSensor);
  setupGateSensor();   // ← MOVED FROM ZONE A
  setupBuzzer();
  setupWarningLed();
  setupServo();        // ← MOVED FROM ZONE A

  connectWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);  // ← MOVED FROM ZONE A
  connectMQTT();

  delay(500);

  for (int i = 0; i < SLOT_COUNT; i++) {
    initializeSlotState(zoneB[i]);
    reportSlotChange(zoneB[i]);
    publishSlotStatus(zoneB[i]);
  }

  // Evaluate gate based on initial sensor readings
  // Note: Zone A slot states will arrive via MQTT shortly after connection
  updateServoForZoneA();

  initializeIllegalParkingState(illegalSensor);
  reportIllegalParkingChange(illegalSensor);
  publishIllegalParkingStatus(illegalSensor);
}

void loop() {
  ensureConnections();

  unsigned long now = millis();

  for (int i = 0; i < SLOT_COUNT; i++) updateSlot(zoneB[i], now);

  updateIllegalParking(illegalSensor, now);
  updateBuzzer(now);
  checkPendingGateClose(now);  // ← MOVED FROM ZONE A
}
