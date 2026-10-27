/*
 * AEGIS radar algorithm test firmware
 *
 * Flash this file only when testing radar-algorithm-test.html.
 * It does not replace Code/arduino.ino.
 *
 * Put the laptop, phone and ESP32 on the same Wi-Fi network. The ESP32 can
 * join a phone hotspot by filling TEST_WIFI_SSID and TEST_WIFI_PASSWORD.
 */
#include <WiFi.h>
#include <WebServer.h>
#include <NimBLEDevice.h>

const char* TEST_WIFI_SSID = "";       // e.g. "MyPhoneHotspot"
const char* TEST_WIFI_PASSWORD = "";   // e.g. "password123"

WebServer server(80);

String jsonEscape(const String& input) {
  String output;
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"' || c == '\\') output += '\\';
    if (c == '\n') output += "\\n";
    else if (c != '\r') output += c;
  }
  return output;
}

void sendJson(const String& payload) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", payload);
}

void handleWifi() {
  int count = WiFi.scanNetworks(false, true);
  String payload = "[";
  for (int i = 0; i < count; i++) {
    if (i) payload += ',';
    int channel = WiFi.channel(i);
    payload += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    payload += "\"rssi\":" + String(WiFi.RSSI(i)) + ',';
    payload += "\"channel\":" + String(channel) + ',';
    payload += "\"freq\":" + String(2407 + channel * 5) + ',';
    payload += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"}";
  }
  WiFi.scanDelete();
  sendJson(payload + "]");
}

void handleBle() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->getResults(2000);
  String payload = "[";
  for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice* device = results.getDevice(i);
    if (i) payload += ',';
    String address = device->getAddress().toString().c_str();
    String name = device->getName().c_str();
    if (!name.length()) name = address;
    payload += "{\"name\":\"" + jsonEscape(name) + "\",";
    payload += "\"address\":\"" + address + "\",";
    payload += "\"rssi\":" + String(device->getRSSI()) + "}";
  }
  scan->clearResults();
  sendJson(payload + "]");
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  if (strlen(TEST_WIFI_SSID)) {
    WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASSWORD);
    Serial.print("Joining test network");
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print('.');
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nRadar test ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi not connected. Fill TEST_WIFI_SSID/PASSWORD and reflash.");
  }

  NimBLEDevice::init("");
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/ble", HTTP_GET, handleBle);
  server.on("/", HTTP_GET, []() { server.send(200, "text/plain", "AEGIS radar test API: /wifi, /ble"); });
  server.begin();
}

void loop() {
  server.handleClient();
}
