#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "secrets.h"

#define DHTPIN 15
#define DHTTYPE DHT22
#define LED_PIN 2

// Update these two to match your VM domain and MQTT_NAME from .env
const char* SERVER_URL = "http://iot-nharris2005.northcentralus.cloudapp.azure.com/readings";
const char* MQTT_NAME  = "nia";
const char* MQTT_BROKER = "135.232.203.144";

String topicMax   = String("ecse3038/") + MQTT_NAME + "/alarm/max";
String topicState = String("ecse3038/") + MQTT_NAME + "/alarm/state";
String topicStatus = String("ecse3038/") + MQTT_NAME + "/status";

DHT dht(DHTPIN, DHTTYPE);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

float maxTempLimit = 8.0; // Default until retained message arrives
bool lastLedState = false;
unsigned long lastPostTime = 0;

// Helper: Validate if incoming MQTT string is a valid number (rejects "abc")
bool isValidFloat(String str, float &outVal) {
  str.trim();
  if (str.length() == 0) return false;
  char* endPtr = nullptr;
  float val = strtod(str.c_str(), &endPtr);
  if (endPtr == str.c_str() || *endPtr != '\0') return false;
  outVal = val;
  return true;
}

// B4 & B5: Evaluate LED immediately and publish alarm/state ONLY when it changes
void evaluateAlarm(float currentTemp) {
  bool shouldBeOn = (currentTemp > maxTempLimit);
  digitalWrite(LED_PIN, shouldBeOn ? HIGH : LOW);

  if (shouldBeOn != lastLedState) {
    lastLedState = shouldBeOn;
    const char* stateStr = shouldBeOn ? "on" : "off";
    mqtt.publish(topicState.c_str(), stateStr, true);
    Serial.printf("LED changed -> %s (Temp: %.1f, Limit: %.1f)\n", stateStr, currentTemp, maxTempLimit);
  }
}

// B3: Handle incoming alarm/max messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  float parsedLimit;
  if (isValidFloat(msg, parsedLimit)) {
    maxTempLimit = parsedLimit;
    Serial.printf("Updated max_temp limit: %.2f\n", maxTempLimit);
    float currentTemp = dht.readTemperature();
    if (!isnan(currentTemp)) {
      evaluateAlarm(currentTemp); // React immediately when limit arrives
    }
  } else {
    Serial.printf("Ignored invalid limit payload: %s\n", msg.c_str());
  }
}

// B2: Connect to Class Broker with Last Will ("offline", retained)
void reconnectMQTT() {
  while (!mqtt.connected()) {
    String clientId = "esp32-" + WiFi.macAddress();
    // connect(id, willTopic, willQoS, willRetain, willMessage)
    if (mqtt.connect(clientId.c_str(), topicStatus.c_str(), 1, true, "offline")) {
      mqtt.publish(topicStatus.c_str(), "online", true);
      mqtt.subscribe(topicMax.c_str());
      Serial.println("MQTT Connected & Subscribed to " + topicMax);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  dht.begin();

  WiFi.begin("Wokwi-GUEST", "");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.println("WiFi Connected! MAC: " + WiFi.macAddress());

  mqtt.setServer(MQTT_BROKER, 1883);
  mqtt.setCallback(mqttCallback);
  reconnectMQTT();

  // Publish initial state ("off") on boot
  mqtt.publish(topicState.c_str(), "off", true);
}

void loop() {
  if (!mqtt.connected()) {
    reconnectMQTT();
  }
  mqtt.loop();

  float temp = dht.readTemperature();
  if (!isnan(temp)) {
    evaluateAlarm(temp); // Continuously check sensor slider changes
  }

  // B1: Every 10 seconds, POST {"mac", "temp"} to API
  if (millis() - lastPostTime >= 10000) {
    lastPostTime = millis();
    if (WiFi.status() == WL_CONNECTED && !isnan(temp)) {
      HTTPClient http;
      http.begin(SERVER_URL);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-API-Key", DEVICE_KEY);

      JsonDocument doc;
      doc["mac"] = WiFi.macAddress();
      doc["temp"] = temp;

      String body;
      serializeJson(doc, body);
      int code = http.POST(body);
      Serial.printf("POST /readings -> %d\n", code);
      http.end();
    }
  }
}