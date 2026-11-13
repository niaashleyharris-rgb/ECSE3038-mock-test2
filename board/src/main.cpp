#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"
#include <DHT.h>
#include <ArduinoJson.h>


#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  dht.begin();

  // 1. Join Wi-Fi & Print MAC Address
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void sendSensorData() {
  if (WiFi.status() != WL_CONNECTED) return;

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);

  // 2. Set Headers (Content-Type & Custom API Key)
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);

  // 3. Build JSON Payload
JsonDocument doc;
  doc["mac"] = WiFi.macAddress();
  doc["temp"] = temp;
  doc["humidity"] = hum;
  doc["timestamp"] = millis() / 1000.0;

  String requestBody;
  serializeJson(doc, requestBody);

  // 4. Send POST Request & Inspect Response
  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response Code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println("Response: " + response);

    // Stop sending if unauthorized or forbidden
    if (httpResponseCode == 401 || httpResponseCode == 403) {
      Serial.println("Authentication failure (401/403). Halting retries.");
      while (true) delay(1000);
    }
  } else {
    Serial.printf("HTTP POST Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}

void loop() {
  sendSensorData();
  delay(10000); // Send every 10 seconds
}
