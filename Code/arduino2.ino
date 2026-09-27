#include <WiFi.h>
#include <WebServer.h>
#include <NimBLEDevice.h>
#include <ESPmDNS.h>

const char* ssid = "hello";         // Your Mobile Hotspot SSID
const char* password = "12345678";  // Your Mobile Hotspot Password

WebServer server(80);

String wifiJson = "[]";
String bleJson = "[]";

SemaphoreHandle_t jsonMutex;

// Background task to scan Wi-Fi and BLE continuously
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

    // Update shared strings safely using mutex
    if (xSemaphoreTake(jsonMutex, portMAX_DELAY) == pdTRUE) {
      wifiJson = tempWifi;
      bleJson = tempBle;
      xSemaphoreGive(jsonMutex);
    }

    // Delay before next scan cycle to prevent CPU hogging
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Handler for /wifi endpoint
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

// Handler for /ble endpoint
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

void setup() {
  Serial.begin(115200);
  
  jsonMutex = xSemaphoreCreateMutex();
  
  // Set ESP32 to Station Mode (Connects to your phone's hotspot)
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

  // Initialize mDNS
  if (!MDNS.begin("shesafe")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started. You can connect to: http://shesafe.local");
    MDNS.addService("http", "tcp", 80);
  }

  // Initialize Bluetooth (NimBLE)
  NimBLEDevice::init("");

  // Setup Web Server Endpoints
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/ble", HTTP_GET, handleBLE);
  
  // Respond to preflight requests globally
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.println("Web Server running.");

  // Start the Wi-Fi/BLE scanning background task on Core 0
  xTaskCreatePinnedToCore(
    scanTask,      // Task function
    "ScanTask",    // Task name
    8192,          // Stack size (bytes)
    NULL,          // Parameter
    1,             // Priority
    NULL,          // Task handle
    0              // Core to run on (0 or 1)
  );
}

void loop() {
  // Keep the web server responsive
  server.handleClient();
}

