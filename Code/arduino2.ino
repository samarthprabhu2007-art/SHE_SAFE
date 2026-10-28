#include <WiFi.h>
#include <WebServer.h>
#include <NimBLEDevice.h>
#include <ESPmDNS.h>

// ─── Network Configuration ──────────────────────────────────────────────────
// Enter your phone's Mobile Hotspot or local Wi-Fi credentials:
const char* ssid = "hello";         // Your Mobile Hotspot SSID
const char* password = "12345678";  // Your Mobile Hotspot Password

WebServer server(80);

String wifiJson = "[]";
String bleJson = "[]";

SemaphoreHandle_t jsonMutex;

// ─── IR LED Blink Configuration (500ms ON / 500ms OFF) ──────────────────────
const int IR_LED_PIN = 14;          // GPIO pin wired to the IR LED
unsigned long lastBlinkTime = 0;
bool ledState = false;
const unsigned long blinkInterval = 500; // 500ms ON / 500ms OFF (1.0 Hz)

// ─── Background Wi-Fi & BLE Scanner Task (Runs on Core 0) ───────────────────
void scanTask(void* parameter) {
  for (;;) {
    // 1. Wi-Fi scan
    int n = WiFi.scanNetworks(false, true); 
    String tempWifi = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) tempWifi += ",";
      int ch = WiFi.channel(i);
      int freq = 2407 + ch * 5;
      tempWifi += "{";
      tempWifi += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
      tempWifi += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      tempWifi += "\"channel\":" + String(ch) + ",";
      tempWifi += "\"freq\":" + String(freq) + ",";
      tempWifi += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
      tempWifi += "}";
    }
    tempWifi += "]";
    WiFi.scanDelete();

    // 2. BLE scan
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    
    // Scan for 2 seconds
    NimBLEScanResults results = scan->getResults(2000); 
    String tempBle = "[";
    for (int i = 0; i < results.getCount(); i++) {
      auto dev = results.getDevice(i);
      if (i > 0) tempBle += ",";
      String name = dev->getName().c_str();
      if (name == "") name = dev->getAddress().toString().c_str();
      tempBle += "{";
      tempBle += "\"name\":\"" + name + "\",";
      tempBle += "\"rssi\":" + String(dev->getRSSI());
      tempBle += "}";
    }
    tempBle += "]";
    scan->clearResults(); 

    // Update shared JSON strings safely using mutex
    if (xSemaphoreTake(jsonMutex, portMAX_DELAY) == pdTRUE) {
      wifiJson = tempWifi;
      bleJson = tempBle;
      xSemaphoreGive(jsonMutex);
    }

    // Delay before next scan cycle to prevent CPU hogging
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ─── HTTP API Handlers ──────────────────────────────────────────────────────
void handleWifi() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String response;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = wifiJson;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "[]";
  }
  server.send(200, "application/json", response);
}

void handleBLE() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String response;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = bleJson;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "[]";
  }
  server.send(200, "application/json", response);
}

// Handle preflight CORS requests
void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

// ─── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  // Setup IR LED pin
  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(IR_LED_PIN, LOW);

  jsonMutex = xSemaphoreCreateMutex();
  
  // Connect ESP32 to phone hotspot / Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.println();
  Serial.print("Connecting to Mobile Hotspot: ");
  Serial.println(ssid);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("✅ Connected to Hotspot!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP()); 

  // Initialize mDNS (http://shesafe.local)
  if (!MDNS.begin("shesafe")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started: http://shesafe.local");
    MDNS.addService("http", "tcp", 80);
  }

  // Initialize Bluetooth (NimBLE)
  NimBLEDevice::init("");

  // Setup Web Server Endpoints
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/ble", HTTP_GET, handleBLE);
  
  // Respond to preflight CORS requests globally
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.println("Web Server running on port 80.");

  // Start the background Wi-Fi/BLE scanning task on Core 0
  xTaskCreatePinnedToCore(
    scanTask,      // Task function
    "ScanTask",    // Task name
    8192,          // Stack size (bytes)
    NULL,          // Parameter
    1,             // Priority
    NULL,          // Task handle
    0              // Core to run on (0)
  );
}

// ─── Main Loop ──────────────────────────────────────────────────────────────
void loop() {
  // Keep the HTTP API server responsive
  server.handleClient();

  // Non-blocking 500ms ON / 500ms OFF IR LED blink for camera optical detection
  if (millis() - lastBlinkTime >= blinkInterval) {
    lastBlinkTime = millis();
    ledState = !ledState;
    digitalWrite(IR_LED_PIN, ledState ? HIGH : LOW);
  }
}
