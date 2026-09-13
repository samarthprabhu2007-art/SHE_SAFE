#include <WiFi.h>
#include <WebServer.h>
#include <NimBLEDevice.h>

const char* ssid = "ESP32-Dashboard";
const char* password = "12345678";

WebServer server(80);

String wifiJson = "[]";
String bleJson = "[]";

SemaphoreHandle_t jsonMutex;
TaskHandle_t scanTaskHandle = NULL;

const int IR_LED_PIN = 14;
unsigned long lastBlinkTime = 0;
bool ledState = false;
const int blinkInterval = 500; // 500ms ON / 500ms OFF (1 Hz)

void scanTask(void* parameter) {
  for (;;) {
    // 1. Wi-Fi scan (blocking in this task is fine)
    int n = WiFi.scanNetworks(false, true); 
    String tempWifi = "[";
    for (int i = 0; i < n; i++) {
      if (i) tempWifi += ",";
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

    // 2. BLE scan (blocking in this task is fine)
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->getResults(2000); 
    String tempBle = "[";
    for (int i = 0; i < results.getCount(); i++) {
      auto dev = results.getDevice(i);
      if (i) tempBle += ",";
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

    // Delay before next scan cycle
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void handleWifi() {
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
  String response;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = bleJson;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "[]";
  }
  server.send(200, "application/json", response);
}

void handlePIR() {
  // Mock PIR sensor: active for 4s every 15s
  unsigned long currentSec = (millis() / 1000) % 15;
  bool motion_detected = currentSec < 4;
  String json = "{";
  json += "\"motion\":" + String(motion_detected ? "true" : "false") + ",";
  json += "\"time\":\"" + String(millis() / 1000) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleWhatsApp() {
  // Simulates alert transmission on ESP32 serial logs and notifies dashboard
  Serial.println("[ESP32 ALERT] Spy Camera trigger POST received! WhatsApp alert simulated.");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Green API message simulated via ESP32 Serial Out.\"}");
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>SheSafe · Spy Camera Detector</title>
  <style>
@import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700;800&family=Poppins:wght@400;500;600;700;800&family=Plus+Jakarta+Sans:wght@400;500;600;700&display=swap');

:root {
  /* Premium cohesive soft warm light palette */
  --bg-main: #FAF8F9;
  --bg-gradient: linear-gradient(135deg, #FAF4F6 0%, #E8F0FE 50%, #E6F8F3 100%);
  --panel-bg: rgba(255, 255, 255, 0.85);
  --panel-border: rgba(246, 235, 237, 0.85);
  --panel-border-active: rgba(216, 17, 89, 0.25);
  
  /* Primary & Status Colors */
  --brand-primary: #D81159;
  --brand-primary-glow: rgba(216, 17, 89, 0.15);
  --brand-primary-light: #FFE5EC;
  
  --status-secure: #06D6A0;
  --status-secure-glow: rgba(6, 214, 160, 0.15);
  
  --status-warning: #FF9F1C;
  --status-warning-glow: rgba(255, 159, 28, 0.15);
  
  --status-danger: #FF3366;
  --status-danger-glow: rgba(255, 51, 102, 0.15);
  
  /* Soft Typography Colors */
  --text-primary: #1A1D2C;
  --text-secondary: #5C5E6E;
  --text-dim: #9FA1B0;
  
  /* Fonts */
  --font-title: 'Poppins', sans-serif;
  --font-sans: 'Outfit', sans-serif;
  --font-hud: 'Plus Jakarta Sans', sans-serif;
  --font-mono: 'Plus Jakarta Sans', monospace;
}

* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}

body {
  background: var(--bg-main);
  background-image: var(--bg-gradient);
  color: var(--text-primary);
  font-family: var(--font-sans);
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 24px;
  overflow-x: hidden;
}

/* Background elements (subtle soft glow blobs) */
body::before {
  content: "";
  position: absolute;
  top: -15%;
  right: -10%;
  width: 50vw;
  height: 50vw;
  background: radial-gradient(circle, rgba(216, 17, 89, 0.15) 0%, transparent 70%);
  z-index: -1;
  pointer-events: none;
  filter: blur(40px);
}

body::after {
  content: "";
  position: absolute;
  bottom: -10%;
  left: -15%;
  width: 60vw;
  height: 60vw;
  background: radial-gradient(circle, rgba(6, 214, 160, 0.12) 0%, transparent 70%);
  z-index: -1;
  pointer-events: none;
  filter: blur(50px);
}

.dashboard-container {
  width: 100%;
  max-width: 1100px;
  display: flex;
  flex-direction: column;
  gap: 24px;
  margin-top: 15px;
}

/* Header */
header {
  width: 100%;
  max-width: 1100px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  border-bottom: 1px solid var(--panel-border);
  padding-bottom: 20px;
}

.header-title-group h1 {
  font-family: var(--font-title);
  font-size: 26px;
  font-weight: 700;
  letter-spacing: 0.5px;
  color: var(--text-primary);
  background: linear-gradient(135deg, var(--text-primary) 30%, var(--brand-primary));
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}

.header-title-group p {
  font-family: var(--font-sans);
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
  letter-spacing: 0.5px;
  margin-top: 4px;
}

.system-status {
  display: flex;
  align-items: center;
  gap: 8px;
  background: var(--panel-bg);
  border: 1px solid var(--panel-border);
  padding: 8px 16px;
  border-radius: 30px;
  font-family: var(--font-sans);
  font-weight: 600;
  font-size: 12px;
  color: var(--text-secondary);
  box-shadow: 0 4px 12px var(--brand-primary-glow);
}

.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background-color: var(--status-secure);
  box-shadow: 0 0 10px var(--status-secure-glow);
  animation: pulse-glow 2s infinite;
}

@keyframes pulse-glow {
  0%, 100% { 
    transform: scale(1); 
    opacity: 1;
    box-shadow: 0 0 6px var(--status-secure), 0 0 12px var(--status-secure-glow);
  }
  50% { 
    transform: scale(1.15); 
    opacity: 0.85;
    box-shadow: 0 0 12px var(--status-secure), 0 0 24px var(--status-secure-glow);
  }
}

/* Grid Layout */
.layout-grid {
  display: grid;
  grid-template-columns: 1.25fr 1fr;
  gap: 24px;
}

@media (max-width: 900px) {
  .layout-grid {
    grid-template-columns: 1fr;
  }
}

/* Panel Design */
.panel {
  background: var(--panel-bg);
  border: 1px solid var(--panel-border);
  border-radius: 24px;
  padding: 24px;
  box-shadow: 0 8px 30px 0 rgba(26, 29, 44, 0.04);
  transition: transform 0.3s cubic-bezier(0.16, 1, 0.3, 1), border-color 0.3s ease, box-shadow 0.3s ease;
  position: relative;
  overflow: hidden;
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
}

.panel:hover {
  border-color: var(--panel-border-active);
  box-shadow: 0 16px 40px 0 rgba(26, 29, 44, 0.08);
  transform: translateY(-4px) scale(1.008);
}

.panel-title {
  font-family: var(--font-title);
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
  margin-bottom: 20px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  border-bottom: 1px solid var(--panel-border);
  padding-bottom: 12px;
}

/* Camera Feed Container */
.feed-wrapper {
  position: relative;
  width: 100%;
  aspect-ratio: 4/3;
  background: #1C1215;
  border-radius: 18px;
  overflow: hidden;
  border: 4px solid var(--panel-bg);
  box-shadow: 0 8px 25px rgba(220, 180, 190, 0.15);
}

#camVideo {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  z-index: 1;
}

#camCanvas {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  z-index: 2;
  pointer-events: none;
}

/* Overlay Elements - Soft Modern Focus Look */
.feed-overlay-hud {
  position: absolute;
  inset: 0;
  z-index: 3;
  pointer-events: none;
  border: 1px dashed rgba(255, 255, 255, 0.2);
  margin: 15px;
  border-radius: 12px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  padding: 12px;
}

.hud-corners {
  position: absolute;
  inset: 0;
}
.hud-corners::before, .hud-corners::after {
  content: "";
  position: absolute;
  width: 12px;
  height: 12px;
  border-color: var(--brand-primary);
  border-style: solid;
}
.hud-corners::before {
  top: -2px; left: -2px; border-width: 2px 0 0 2px; border-top-left-radius: 4px;
}
.hud-corners::after {
  top: -2px; right: -2px; border-width: 2px 2px 0 0; border-top-right-radius: 4px;
}

.hud-bottom-corners {
  position: absolute;
  inset: 0;
}
.hud-bottom-corners::before, .hud-bottom-corners::after {
  content: "";
  position: absolute;
  width: 12px;
  height: 12px;
  border-color: var(--brand-primary);
  border-style: solid;
}
.hud-bottom-corners::before {
  bottom: -2px; left: -2px; border-width: 0 0 2px 2px; border-bottom-left-radius: 4px;
}
.hud-bottom-corners::after {
  bottom: -2px; right: -2px; border-width: 0 2px 2px 0; border-bottom-right-radius: 4px;
}

.hud-top-info {
  display: flex;
  justify-content: space-between;
  font-family: var(--font-hud);
  font-size: 11px;
  font-weight: 500;
  color: #fff;
  text-shadow: 0 2px 4px rgba(0, 0, 0, 0.4);
}

.hud-bottom-info {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  font-family: var(--font-hud);
  font-size: 11px;
  font-weight: 500;
  color: rgba(255, 255, 255, 0.8);
  text-shadow: 0 2px 4px rgba(0, 0, 0, 0.4);
}

/* Threat Level Widget */
.threat-indicator-card {
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 20px;
  border-radius: 18px;
  background: var(--bg-main);
  border: 1px solid var(--panel-border);
  margin-top: 20px;
  transition: all 0.3s;
}

.threat-indicator-card.alert-active {
  border-color: var(--brand-primary);
  background: var(--brand-primary-light);
  box-shadow: 0 8px 25px var(--brand-primary-glow);
  animation: pulse-border 1.5s infinite alternate;
}

@keyframes pulse-border {
  0% { border-color: var(--panel-border-active); }
  100% { border-color: var(--brand-primary); }
}

.threat-gauge {
  position: relative;
  width: 70px;
  height: 70px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.threat-circle-svg {
  transform: rotate(-90deg);
  width: 70px;
  height: 70px;
}

.threat-circle-bg {
  fill: none;
  stroke: #EADCDD;
  stroke-width: 7;
}

.threat-circle-fill {
  fill: none;
  stroke: var(--brand-primary);
  stroke-width: 7;
  stroke-dasharray: 188.4;
  stroke-dashoffset: 188.4;
  stroke-linecap: round;
  transition: stroke-dashoffset 0.5s ease-out, stroke 0.5s;
}

.threat-indicator-card.alert-active .threat-circle-fill {
  stroke: var(--status-danger);
}

.threat-percent {
  position: absolute;
  font-family: var(--font-title);
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
}

.threat-meta {
  flex: 1;
}

.threat-status-label {
  font-family: var(--font-title);
  font-size: 15px;
  font-weight: 700;
  color: var(--text-primary);
}

.threat-indicator-card.alert-active .threat-status-label {
  color: var(--rose-pink-dark);
}

.threat-desc {
  font-size: 12px;
  color: var(--text-secondary);
  margin-top: 6px;
  line-height: 1.4;
}

/* Controls Grid */
.controls-grid {
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.control-item {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.control-header {
  display: flex;
  justify-content: space-between;
  font-family: var(--font-sans);
  font-size: 12px;
  font-weight: 600;
  color: var(--text-secondary);
}

.control-header span:last-child {
  color: var(--brand-primary);
  font-weight: 700;
}

/* Custom range sliders */
input[type="range"] {
  -webkit-appearance: none;
  width: 100%;
  height: 6px;
  background: var(--brand-primary-light);
  border-radius: 4px;
  outline: none;
  border: none;
}

input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: var(--brand-primary);
  cursor: pointer;
  border: 3px solid #fff;
  box-shadow: 0 2px 6px rgba(216, 17, 89, 0.3);
  transition: transform 0.2s cubic-bezier(0.175, 0.885, 0.32, 1.275), background-color 0.2s;
}

input[type="range"]::-webkit-slider-thumb:hover {
  transform: scale(1.2);
  background: var(--brand-primary);
}

/* Select element */
select {
  background: #FFF;
  border: 1px solid var(--panel-border);
  color: var(--text-primary);
  padding: 10px 14px;
  border-radius: 12px;
  outline: none;
  font-family: var(--font-sans);
  font-weight: 500;
  font-size: 13px;
  cursor: pointer;
  box-shadow: 0 2px 6px rgba(26, 29, 44, 0.03);
  transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
  appearance: none;
  background-image: url("data:image/svg+xml;charset=UTF-8,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%235C5E6E' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3e%3cpolyline points='6 9 12 15 18 9'%3e%3c/polyline%3e%3c/svg%3e");
  background-repeat: no-repeat;
  background-position: right 14px center;
  background-size: 16px;
  padding-right: 40px;
}

select:focus {
  border-color: var(--brand-primary);
  box-shadow: 0 0 0 3px rgba(216, 17, 89, 0.15);
  transform: translateY(-1px);
}

/* Plotting area */
.plot-container {
  position: relative;
  width: 100%;
  height: 90px;
  background: #FAF8F9;
  border: 1px solid var(--panel-border);
  border-radius: 12px;
  margin-top: 10px;
  overflow: hidden;
}

#plotCanvas {
  width: 100%;
  height: 100%;
  display: block;
}

/* Buttons */
.btn-container {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 14px;
  margin-top: 15px;
}

.btn {
  font-family: var(--font-title);
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.5px;
  padding: 12px 18px;
  border-radius: 12px;
  cursor: pointer;
  transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
  text-align: center;
  background: #fff;
  border: 1px solid var(--panel-border);
  color: var(--text-secondary);
  outline: none;
}

.btn:hover {
  border-color: var(--brand-primary);
  color: var(--brand-primary);
  background: var(--brand-primary-light);
  transform: translateY(-2px);
}

.btn.primary {
  border-color: var(--brand-primary);
  color: #fff;
  background: var(--brand-primary);
  box-shadow: 0 4px 15px rgba(216, 17, 89, 0.2);
}

.btn.primary:hover {
  background: #B50E43;
  border-color: #B50E43;
  box-shadow: 0 6px 18px rgba(216, 17, 89, 0.35);
  transform: translateY(-2px);
}

.btn.danger {
  border-color: rgba(255, 51, 102, 0.2);
  color: var(--status-danger);
  background: rgba(255, 51, 102, 0.08);
}

.btn.danger:hover {
  background: var(--status-danger);
  border-color: var(--status-danger);
  color: #fff;
  box-shadow: 0 4px 15px rgba(255, 51, 102, 0.2);
  transform: translateY(-2px);
}

.btn.warning {
  border-color: rgba(255, 159, 28, 0.2);
  color: #E07A00;
  background: rgba(255, 159, 28, 0.08);
}

.btn.warning:hover {
  background: var(--status-warning);
  border-color: var(--status-warning);
  color: #fff;
  box-shadow: 0 4px 15px rgba(255, 159, 28, 0.2);
  transform: translateY(-2px);
}

/* Logging console */
.console-box {
  background: #FDF9FA;
  border: 1px solid var(--panel-border);
  border-radius: 14px;
  padding: 16px;
  font-family: var(--font-hud);
  font-size: 11px;
  font-weight: 500;
  height: 180px;
  overflow-y: auto;
  line-height: 1.6;
}

.console-line {
  margin-bottom: 6px;
  border-left: 4px solid transparent;
  padding-left: 10px;
  transition: all 0.2s ease;
}

.console-line.info { border-color: var(--brand-primary); color: var(--text-primary); }
.console-line.alert { border-color: var(--status-danger); color: var(--status-danger); font-weight: bold; }
.console-line.success { border-color: var(--status-secure); color: var(--status-secure); }
.console-line.warning { border-color: var(--status-warning); color: #D76F30; }
.console-line.system { border-color: var(--text-dim); color: var(--text-secondary); }

/* Color presets display preview dot */
.preset-visuals {
  display: flex;
  gap: 12px;
  margin-top: 6px;
}

.color-preview {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  border: 2px solid #fff;
  background: magenta;
  box-shadow: 0 2px 6px rgba(255, 0, 255, 0.4);
}

/* PIR Sensor pill */
.pir-container {
  display: flex;
  align-items: center;
  gap: 16px;
  background: #FAF8F9;
  border: 1px solid var(--panel-border);
  border-radius: 16px;
  padding: 14px 18px;
  margin-top: 18px;
}

.pir-dot {
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: var(--text-dim);
  flex-shrink: 0;
  transition: all 0.3s;
}

.pir-dot.active {
  background: var(--status-danger);
  box-shadow: 0 0 10px var(--status-danger-glow);
}

.pir-dot.clear {
  background: var(--status-secure);
  box-shadow: 0 0 8px var(--status-secure-glow);
}

.pir-meta {
  display: flex;
  flex-direction: column;
}

.pir-label {
  font-family: var(--font-title);
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.2px;
}

.pir-sub {
  font-family: var(--font-sans);
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 3px;
}

/* Footer / Status bar */
footer {
  width: 100%;
  max-width: 1100px;
  margin-top: auto;
  padding-top: 40px;
  padding-bottom: 20px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-family: var(--font-sans);
  font-size: 11px;
  font-weight: 500;
  color: var(--text-dim);
  border-top: 1px solid var(--panel-border);
}

footer a {
  color: var(--brand-primary);
  text-decoration: none;
}
footer a:hover {
  text-decoration: underline;
}

/* Mode Selector Toggle */
.mode-selector {
  display: flex;
  justify-content: center;
  gap: 16px;
  width: 100%;
  max-width: 1100px;
  margin-top: 10px;
  margin-bottom: 5px;
}

.mode-selector .btn {
  flex: 1;
  padding: 14px 20px;
  font-size: 13px;
  border-radius: 16px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
}

.mode-selector .btn.active {
  background: var(--brand-primary);
  color: #fff;
  border-color: var(--brand-primary);
  box-shadow: 0 4px 15px rgba(216, 17, 89, 0.2);
}

.mode-selector .btn.active:hover {
  background: #B50E43;
}

/* Hidden container helper */
.hidden {
  display: none !important;
}

/* Radar Canvas Viewport Overlay HUD */
.viewport-wrapper {
  position: relative;
  width: 100%;
  aspect-ratio: 1/1;
  background: #FFF5F7; /* Soft light pink background */
  border-radius: 18px;
  overflow: hidden;
  border: 4px solid var(--panel-bg);
  box-shadow: 0 8px 25px rgba(216, 17, 89, 0.05);
  display: flex;
  align-items: center;
  justify-content: center;
}

#radarCanvas {
  background: #FFFFFF; /* White background */
  border: 1px solid rgba(216, 17, 89, 0.1);
  border-radius: 50%;
  display: block;
  max-width: 95%;
  max-height: 95%;
}

.viewport-overlay-hud {
  position: absolute;
  inset: 0;
  z-index: 3;
  pointer-events: none;
  border: 1px dashed rgba(216, 17, 89, 0.25); /* Pink dashed border */
  margin: 15px;
  border-radius: 12px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  padding: 12px;
}

.viewport-overlay-hud .hud-top-info {
  color: var(--brand-primary);
  text-shadow: none;
}

.viewport-overlay-hud .hud-bottom-info {
  color: var(--text-secondary);
  text-shadow: none;
}

/* Tactical Vector Compass Navigator Container */
.navigator-container {
  padding: 24px;
  background: var(--panel-bg);
  border: 1px solid var(--panel-border);
  border-radius: 24px;
  margin-bottom: 24px;
  text-align: center;
  box-shadow: 0 8px 30px 0 rgba(26, 29, 44, 0.04);
  transition: all 0.3s ease;
}

.navigator-container.wall-pulsing {
  border-color: var(--status-danger);
  animation: wall-pulse 1.4s ease-in-out infinite;
}

.navigator-container.found-pulsing {
  border-color: var(--status-secure);
  animation: found-pulse 1.2s ease-in-out infinite;
}

@keyframes wall-pulse {
  0%, 100% { box-shadow: 0 0 12px rgba(255, 51, 102, 0.1); }
  50% { box-shadow: 0 0 28px rgba(255, 51, 102, 0.4); }
}

@keyframes found-pulse {
  0%, 100% { box-shadow: 0 0 12px rgba(6, 214, 160, 0.1); }
  50% { box-shadow: 0 0 28px rgba(6, 214, 160, 0.4); }
}

.compass-wrapper {
  width: 120px;
  height: 120px;
  margin: 15px auto;
  position: relative;
  border: 2px solid rgba(216, 17, 89, 0.2);
  border-radius: 50%;
  background: radial-gradient(circle, #FAF8F9 0%, #FFF0F2 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: inset 0 2px 8px rgba(26, 29, 44, 0.03);
}

.precision-arrow {
  font-size: 50px;
  color: var(--brand-primary);
  text-shadow: 0 0 10px var(--brand-primary-glow);
  transition: transform 0.3s cubic-bezier(0.25, 0.8, 0.25, 1);
  display: inline-block;
  transform-origin: center center;
  line-height: 1;
}

.guidance-text {
  font-family: var(--font-title);
  font-size: 15px;
  font-weight: 700;
  margin-top: 15px;
  text-transform: uppercase;
  color: var(--text-primary);
}

.sig-log {
  font-family: var(--font-hud);
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 8px;
  letter-spacing: 0.3px;
}

/* Lock-on target layouts */
.lockon-layout {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  width: 100%;
}

.lockon-meta {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 6px;
  text-align: left;
}

.lockon-type {
  font-family: var(--font-hud);
  font-size: 11px;
  font-weight: 700;
  color: var(--brand-primary);
  text-transform: uppercase;
}

.lockon-name {
  font-family: var(--font-title);
  font-size: 16px;
  font-weight: 700;
  color: var(--text-primary);
}

.lockon-detail {
  font-family: var(--font-hud);
  font-size: 13px;
  color: var(--text-secondary);
}

.lockon-range {
  font-family: var(--font-hud);
  font-size: 12px;
  color: var(--text-dim);
}

.lockon-gauge-container {
  width: 75px;
  height: 75px;
  display: flex;
  align-items: center;
  justify-content: center;
}

/* Delta status pills */
.delta-pill {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 20px;
  font-size: 11px;
  font-weight: 700;
  margin-left: 6px;
}

.delta-up {
  background: var(--status-secure-glow);
  color: var(--status-secure);
  border: 1px solid var(--status-secure);
}

.delta-down {
  background: var(--status-danger-glow);
  color: var(--status-danger);
  border: 1px solid var(--status-danger);
}

.delta-flat {
  background: var(--status-warning-glow);
  color: var(--status-warning);
  border: 1px solid var(--status-warning);
}

/* Tables for Network Nodes and BLE Beacons */
.table-wrapper {
  overflow-x: auto;
  margin-top: 10px;
}

table {
  width: 100%;
  border-collapse: collapse;
  text-align: left;
}

th, td {
  padding: 12px;
  font-size: 13px;
  border-bottom: 1px solid var(--panel-border);
}

th {
  font-family: var(--font-title);
  color: var(--text-secondary);
  font-weight: 600;
  text-transform: uppercase;
  font-size: 11px;
  letter-spacing: 0.5px;
}

td {
  color: var(--text-primary);
  word-break: break-all;
}

tr:hover td {
  background: var(--bg-main);
}

/* Compact single-line action buttons in tables */
td button.btn {
  padding: 6px 12px;
  border-radius: 10px;
  font-size: 11px;
  white-space: nowrap;
  min-width: unset;
  width: auto;
  display: inline-block;
}

td:last-child {
  white-space: nowrap;
  width: 1%;
}

/* HIT WALL Button style */
.btn-wall {
  border-color: var(--status-danger) !important;
  color: var(--status-danger) !important;
  background: rgba(255, 51, 102, 0.08);
}

.btn-wall:hover {
  background: var(--status-danger) !important;
  color: #fff !important;
}

/* SOS Button pulsing animation */
#sosBtn {
  animation: sos-pulse 1.6s infinite alternate;
  box-shadow: 0 4px 12px var(--status-danger-glow);
  transition: transform 0.2s, background-color 0.2s;
}

#sosBtn:hover {
  background: #B50E43 !important;
  border-color: #B50E43 !important;
  transform: scale(1.05);
}

@keyframes sos-pulse {
  0% { 
    box-shadow: 0 0 4px var(--status-danger-glow); 
  }
  100% { 
    box-shadow: 0 0 16px rgba(255, 51, 102, 0.75), 0 0 24px rgba(255, 51, 102, 0.35); 
  }
}


</style>
</head>
<body>

  <header>
    <div class="header-title-group">
      <h1>sheSafe // Tactical Camera Radar</h1>
      <p>TACTICAL WIRELESS SIGNAL RADAR & WIRED LENS DEFENSE</p>
    </div>
    <div style="display: flex; align-items: center; gap: 14px;">
      <button class="btn danger" id="sosBtn" onclick="triggerSOS()" style="background: var(--status-danger); color: white; border-color: var(--status-danger); padding: 8px 18px; border-radius: 20px; font-weight: 700; display: flex; align-items: center; gap: 6px;">🚨 SOS EMERGENCY</button>
      <div class="system-status">
        <div class="status-dot"></div>
        <span id="systemStatusText">SMARTGUARD ACTIVE</span>
      </div>
    </div>
  </header>

  <div class="mode-selector">
    <button class="btn mode-btn active" id="wirelessModeBtn" onclick="setMode('wireless')">🛰️ Wireless Camera Radar</button>
    <button class="btn mode-btn" id="wiredModeBtn" onclick="setMode('wired')">📹 Wired Camera Detector</button>
  </div>

  <div class="dashboard-container">
    
    <!-- WIRELESS MODE GRID -->
    <div class="layout-grid" id="wirelessGrid">
      <div style="display: flex; flex-direction: column; gap: 24px;">
        
        <div class="panel" id="sensorCard">
          <div class="panel-title">
            <span>◉ GYROSCOPE COMPASS CALIBRATION</span>
          </div>
          <p style="color: var(--text-secondary); font-size: 13px; margin-bottom: 16px; text-align: left;">
            Tap to link internal motion sensors for high-precision vector steering updates.
          </p>
          <button class="btn primary" onclick="initSensors()">Link Gyro Compass</button>
        </div>

        <div class="navigator-container" id="navPanel">
          <div class="panel-title" style="margin-bottom: 5px; border-bottom: none; padding-bottom: 0;">
            <span>◉ TACTICAL VECTOR COMPASS</span>
          </div>
          <div class="compass-wrapper">
            <div class="precision-arrow" id="navArrow">&#x2191;</div>
          </div>
          <div class="guidance-text" id="navText">Lock a Target Below to Start Scanning</div>
          <div class="sig-log" id="sigLog">Memory Grid Empty</div>
          <button onclick="reportWall()" id="wallBtn" class="btn btn-wall" style="margin-top: 15px; display: none;">&#x1F9F1; HIT WALL</button>
        </div>

        <div class="panel">
          <div class="panel-title">
            <span>◉ LIVE SIGNAL RADAR SCREEN</span>
            <span id="radarHeading" style="font-family: var(--font-hud); font-size: 11px; color: var(--text-secondary);">Heading Offset: 0&#176;</span>
          </div>
          <div class="viewport-wrapper">
            <canvas id="radarCanvas" width="320" height="320"></canvas>
            <div class="viewport-overlay-hud">
              <div class="hud-corners"></div>
              <div class="hud-bottom-corners"></div>
              <div class="hud-top-info">
                <span>SYS RADAR: ACTIVE</span>
                <span>GRID: 2.4GHz / BLE</span>
              </div>
              <div class="hud-bottom-info">
                <span>SECTORS: 8 CHANNELS</span>
                <span>SWEEP RATE: 3000ms</span>
              </div>
            </div>
          </div>
        </div>

      </div>

      <div style="display: flex; flex-direction: column; gap: 24px;">
        
        <div class="panel">
          <div class="panel-title">
            <span>◉ LOCK-ON TARGET METRICS</span>
          </div>
          <div id="trackerStatus">
            <div class="lockon-layout" style="opacity: 0.6;">
              <div class="lockon-meta">
                <div class="lockon-type">IDLE SCANNING MODE</div>
                <p style="font-size:12px; color:var(--text-secondary); margin-top:4px;">No device locked. Click "LOCK" on any node below to start target acquisition loops.</p>
              </div>
            </div>
          </div>
        </div>

        <div class="panel">
          <div class="panel-title">
            <span>◉ SIGNAL WAVE ANALYSIS</span>
            <span id="wirelessActiveTargetLabel" style="font-family: var(--font-hud); font-size: 11px; color: var(--text-secondary);">NO ACTIVE TARGET</span>
          </div>
          <div class="plot-container">
            <canvas id="wirelessPlotCanvas"></canvas>
          </div>
        </div>

        <div class="panel">
          <div class="panel-title">
            <span>◉ SIGNAL THRESHOLD FILTER</span>
          </div>
          <div style="text-align: left; padding: 5px 0;">
            <input type="range" id="rssiSlider" min="-100" max="-30" value="-50">
            <div id="rssiValue" style="margin-top: 10px; font-weight: bold; font-family: var(--font-hud); font-size: 12px; color: var(--brand-primary);">FILTER: -50 dBm</div>
          </div>
        </div>

        <div class="panel">
          <div class="panel-title">
            <span>◉ WIFI NETWORK NODES</span>
          </div>
          <div class="table-wrapper">
            <table>
              <thead>
                <tr>
                  <th>SSID / BSSID</th>
                  <th>RSSI</th>
                  <th>Track</th>
                </tr>
              </thead>
              <tbody id="wifiTable"></tbody>
            </table>
          </div>
        </div>

        <div class="panel">
          <div class="panel-title">
            <span>◉ BLE TARGET BEACONS</span>
          </div>
          <div class="table-wrapper">
            <table>
              <thead>
                <tr>
                  <th>Device Target Name</th>
                  <th>RSSI</th>
                  <th>Track</th>
                </tr>
              </thead>
              <tbody id="bleTable"></tbody>
            </table>
          </div>
        </div>

        <div class="panel">
          <div class="panel-title">
            <span>◉ SYSTEM DIAGNOSTIC LOGS</span>
            <button class="btn" style="padding: 2px 8px; font-size: 9px; margin: 0;" onclick="clearLogs()">CLEAR</button>
          </div>
          <div class="console-box" id="wirelessConsoleLogs"></div>
        </div>

      </div>
    </div>
    
    <!-- WIRED MODE GRID (HIDDEN BY DEFAULT) -->
    <div class="layout-grid hidden" id="wiredGrid">
      
      <!-- LEFT COLUMN: FEED & ANALYTICS -->
      <div style="display: flex; flex-direction: column; gap: 20px;">
        
        <!-- Live Feed Panel -->
        <div class="panel">
          <div class="panel-title">
            <span>◉ LIVE CAMERA TARGETING FEED</span>
            <span id="fpsDisplay" style="font-family: var(--font-hud); font-size: 11px; color: var(--text-secondary);">30 FPS</span>
          </div>
          
          <div class="feed-wrapper" id="feedWrapper">
            <video id="camVideo" autoplay muted playsinline></video>
            <canvas id="camCanvas"></canvas>
            
            <div class="feed-overlay-hud">
              <div class="hud-corners"></div>
              <div class="hud-bottom-corners"></div>
              <div class="hud-top-info">
                <span>SCANNER: ACTIVE</span>
                <span id="resolutionText">RESOLUTION: ---x---</span>
              </div>
              <div class="hud-bottom-info">
                <span>CV GRID: 32x24</span>
                <span id="targetCountText">CANDIDATES: 0</span>
              </div>
            </div>
          </div>
          
          <!-- Threat Level Indicator -->
          <div class="threat-indicator-card" id="threatCard">
            <div class="threat-gauge">
              <svg class="threat-circle-svg">
                <circle class="threat-circle-bg" cx="32" cy="32" r="30"></circle>
                <circle class="threat-circle-fill" id="threatCircle" cx="32" cy="32" r="30"></circle>
              </svg>
              <div class="threat-percent" id="threatPercent">0%</div>
            </div>
            <div class="threat-meta">
              <div class="threat-status-label" id="threatStatusLabel">SCANNING AREA...</div>
              <div class="threat-desc" id="threatDesc">Point your camera towards suspicious spots. We are searching for rhythmic blinking infrared light reflections.</div>
            </div>
          </div>
        </div>

        <!-- Signal Analysis Panel -->
        <div class="panel">
          <div class="panel-title">
            <span>◉ SIGNAL WAVE ANALYSIS</span>
            <span style="font-family: var(--font-hud); font-size: 11px; color: var(--text-secondary);" id="wiredActiveTargetLabel">NO ACTIVE TARGET</span>
          </div>
          <div style="font-size: 12px; color: var(--text-secondary); margin-bottom: 8px; line-height: 1.4;">
            This graph tracks target brightness in real time. A genuine blinking hidden camera will produce a regular, repeating wave pattern.
          </div>
          <div class="plot-container">
            <canvas id="wiredPlotCanvas"></canvas>
          </div>
        </div>
      </div>

      <!-- RIGHT COLUMN: CONTROLS & LOGS -->
      <div style="display: flex; flex-direction: column; gap: 20px;">
        
        <!-- Detection Parameters -->
        <div class="panel">
          <div class="panel-title">
            <span>◉ CAMERA DETECTION SETTINGS</span>
          </div>
          
          <div class="controls-grid">
            
            <div class="control-item">
              <div class="control-header">
                <span>COLOR SIGNATURE PRESET</span>
              </div>
              <select id="presetSelector" onchange="applyPreset(this.value)">
                <option value="default">Pink/Purple IR Glow (Standard)</option>
                <option value="whitePink">Bright White-Pink (Intense reflection)</option>
                <option value="deepViolet">Deep Violet/Blue IR Glow</option>
                <option value="custom">Custom Configuration (Sliders below)</option>
              </select>
            </div>

            <div class="control-item">
              <div class="control-header">
                <span>TARGET HUE CENTER</span>
                <span id="hueVal">315° (Magenta/Pink)</span>
              </div>
              <input type="range" id="hueCenterSlider" min="0" max="360" value="315" oninput="updateControls()">
            </div>

            <div class="control-item">
              <div class="control-header">
                <span>HUE TOLERANCE RANGE</span>
                <span id="hueToleranceVal">±30°</span>
              </div>
              <input type="range" id="hueToleranceSlider" min="5" max="80" value="30" oninput="updateControls()">
            </div>

            <div class="control-item">
              <div class="control-header">
                <span>MINIMUM SATURATION</span>
                <span id="satVal">20%</span>
              </div>
              <input type="range" id="satSlider" min="0" max="100" value="20" oninput="updateControls()">
            </div>

            <div class="control-item">
              <div class="control-header">
                <span>MINIMUM LIGHTNESS</span>
                <span id="lightVal">35%</span>
              </div>
              <input type="range" id="lightSlider" min="0" max="100" value="35" oninput="updateControls()">
            </div>

            <div class="control-item">
              <div class="control-header">
                <span>BLINK RATE TARGET</span>
                <span id="blinkVal">500 ms (1.0 Hz)</span>
              </div>
              <input type="range" id="blinkSlider" min="200" max="1200" step="50" value="500" oninput="updateControls()">
            </div>

            <div class="control-item">
              <div class="control-header">
                <span>DETECTION THRESHOLD / SENSITIVITY</span>
                <span id="sensVal">Medium (5/10)</span>
              </div>
              <input type="range" id="sensSlider" min="1" max="10" value="5" oninput="updateControls()">
            </div>
          </div>

          <div class="btn-container">
            <button class="btn primary" id="simBtn" onclick="toggleSimulation()">⚡ SIMULATE SPY CAM</button>
            <button class="btn danger" onclick="resetSystem()">↺ RESET SYSTEM</button>
          </div>
          
          <div class="pir-container">
            <div class="pir-dot" id="pirDot"></div>
            <div class="pir-meta">
              <span class="pir-label" id="pirLabel">PIR SENSOR: CONNECTING...</span>
              <span class="pir-sub" id="pirSub">Polling server /pir-status...</span>
            </div>
          </div>
        </div>

        <!-- System Console Logs -->
        <div class="panel">
          <div class="panel-title">
            <span>◉ SYSTEM DIAGNOSTIC LOGS</span>
            <button class="btn" style="padding: 2px 8px; font-size: 9px; margin: 0;" onclick="clearLogs()">CLEAR</button>
          </div>
          <div class="console-box" id="wiredConsoleLogs">
            <!-- Dynamic logs will appear here -->
          </div>
        </div>
      </div>
      
    </div>

  </div>
  </div>

  <footer>
    <span>SHESAFE PROTECT v3.0 · ADVANCED IR ANALYSIS</span>
    <span>YOUR SAFETY IS SECURITY</span>
  </footer>

  <script>
    // === SHARED / MODE STATE ===
    let currentMode = 'wireless';

    // === WIRED DETECTOR CONFIG & STATE ===
    let config = {
      hueCenter: 300,
      hueTolerance: 45,
      minSaturation: 0.15,
      minLightness: 0.20,
      blinkRateTarget: 500, // in ms
      sensitivity: 6
    };

    const presets = {
      default: { hueCenter: 300, hueTolerance: 45, minSaturation: 0.15, minLightness: 0.20, blinkRateTarget: 500 },
      whitePink: { hueCenter: 335, hueTolerance: 40, minSaturation: 0.10, minLightness: 0.55, blinkRateTarget: 500 },
      deepViolet: { hueCenter: 280, hueTolerance: 30, minSaturation: 0.25, minLightness: 0.15, blinkRateTarget: 500 }
    };

    const video = document.getElementById('camVideo');
    const camCanvas = document.getElementById('camCanvas');
    const camCtx = camCanvas.getContext('2d');
    const fpsDisplay = document.getElementById('fpsDisplay');
    const resolutionText = document.getElementById('resolutionText');
    const targetCountText = document.getElementById('targetCountText');
    const threatCard = document.getElementById('threatCard');
    const threatCircle = document.getElementById('threatCircle');
    const threatPercent = document.getElementById('threatPercent');
    const threatStatusLabel = document.getElementById('threatStatusLabel');
    const threatDesc = document.getElementById('threatDesc');
    const wiredActiveTargetLabel = document.getElementById('wiredActiveTargetLabel');
    
    const wiredPlotCanvas = document.getElementById('wiredPlotCanvas');
    const wiredPlotCtx = wiredPlotCanvas.getContext('2d');
    
    let videoWidth = 640;
    let videoHeight = 480;
    let streamActive = false;
    let frameCount = 0;
    let lastFpsTime = Date.now();
    let processingCanvas = document.createElement('canvas');
    let procCtx = processingCanvas.getContext('2d', { willReadFrequently: true });
    
    const GRID_W = 320;
    const GRID_H = 240;
    processingCanvas.width = GRID_W;
    processingCanvas.height = GRID_H;

    let isSimulating = false;
    let simAngle = 0;
    let simBlinkState = true;
    let lastSimBlinkTime = Date.now();

    let trackedTargets = [];
    let nextTargetId = 1;
    let primaryTargetId = null;

    // === WIRELESS RADAR CONFIG & STATE ===
    let rssiThreshold = -50;
    let currentWifiData = [];
    let currentBLEData = [];
    let lastRawWifiData = "";
    let lastRawBLEData = "";

    let baselineAlpha = null;
    let currentAlpha  = 0;
    let relativeHeading = 0;
    let targetVectorAngle = 0;
    let gyroLinked = false;

    const NUM_SECTORS = 8;
    let sectorData = Array.from({length: NUM_SECTORS}, () => ({rssi:-Infinity,avg:-100,count:0,momentum:0}));
    let wallZones   = new Array(NUM_SECTORS).fill(false);
    let wallActive  = false;
    let trackingState = 'IDLE';

    let trackedDevice    = null;
    let trackedBLEDevice = null;
    let previousRSSI     = null;
    let previousBLERSSI  = null;
    let lastSeen   = 0;
    let lastBLESeen = 0;

    let prevScanRSSI    = null;
    let prevScanBLERSSI = null;
    let rssiHistory     = [];

    const radarCanvas = document.getElementById("radarCanvas");
    const radarCtx    = radarCanvas.getContext("2d");
    let rcx, rcy, maxRadarRadius;

    const wirelessPlotCanvas = document.getElementById("wirelessPlotCanvas");
    const wirelessPlotCtx = wirelessPlotCanvas.getContext("2d");

    // === UNIFIED LOGGER ===
    function log(msg, type = 'info') {
      const timeStr = new Date().toLocaleTimeString('en-US', { hour12: false });
      const html = `[${timeStr}] ${msg}`;
      
      ['wirelessConsoleLogs', 'wiredConsoleLogs'].forEach(id => {
        const el = document.getElementById(id);
        if (!el) return;
        const line = document.createElement('div');
        line.className = `console-line ${type}`;
        line.innerHTML = html;
        el.appendChild(line);
        el.scrollTop = el.scrollHeight;
        while (el.children.length > 50) {
          el.removeChild(el.firstChild);
        }
      });
    }

    function clearLogs() {
      const ids = ['wirelessConsoleLogs', 'wiredConsoleLogs'];
      ids.forEach(id => {
        const el = document.getElementById(id);
        if (el) el.innerHTML = '';
      });
      log('System log cleared.', 'system');
    }

    // === MODE SELECTION SWITCHER ===
    function setMode(mode) {
      if (mode === currentMode) return;
      currentMode = mode;
      
      const wirelessBtn = document.getElementById('wirelessModeBtn');
      const wiredBtn = document.getElementById('wiredModeBtn');
      const wirelessGrid = document.getElementById('wirelessGrid');
      const wiredGrid = document.getElementById('wiredGrid');
      
      if (mode === 'wireless') {
        wirelessBtn.classList.add('active');
        wiredBtn.classList.remove('active');
        wirelessGrid.classList.remove('hidden');
        wiredGrid.classList.add('hidden');
        
        // Disable wired webcam to save processing and privacy
        if (streamActive) {
          stopWiredCamera();
        }
        
        log('Switched dashboard to Wireless Signal Radar.', 'system');
      } else {
        wirelessBtn.classList.remove('active');
        wiredBtn.classList.add('active');
        wirelessGrid.classList.add('hidden');
        wiredGrid.classList.remove('hidden');
        
        // Start wired camera CV stream
        if (!streamActive) {
          initCamera();
        }
        
        log('Switched dashboard to Wired Camera Detector.', 'system');
      }
    }

    // === WIRED CAMERA LENS CV LOGIC ===
    function applyPreset(presetName) {
      if (presetName === 'custom') return;
      const preset = presets[presetName];
      if (preset) {
        document.getElementById('hueCenterSlider').value = preset.hueCenter;
        document.getElementById('hueToleranceSlider').value = preset.hueTolerance;
        document.getElementById('satSlider').value = preset.minSaturation * 100;
        document.getElementById('lightSlider').value = preset.minLightness * 100;
        document.getElementById('blinkSlider').value = preset.blinkRateTarget;
        
        config.hueCenter = preset.hueCenter;
        config.hueTolerance = preset.hueTolerance;
        config.minSaturation = preset.minSaturation;
        config.minLightness = preset.minLightness;
        config.blinkRateTarget = preset.blinkRateTarget;
        
        updateSliderLabels();
        log(`Applied Preset: ${document.getElementById('presetSelector').options[document.getElementById('presetSelector').selectedIndex].text}`, 'success');
      }
    }

    function updateControls() {
      document.getElementById('presetSelector').value = 'custom';
      
      config.hueCenter = parseInt(document.getElementById('hueCenterSlider').value);
      config.hueTolerance = parseInt(document.getElementById('hueToleranceSlider').value);
      config.minSaturation = parseInt(document.getElementById('satSlider').value) / 100;
      config.minLightness = parseInt(document.getElementById('lightSlider').value) / 100;
      config.blinkRateTarget = parseInt(document.getElementById('blinkSlider').value);
      config.sensitivity = parseInt(document.getElementById('sensSlider').value);
      
      updateSliderLabels();
    }

    function updateSliderLabels() {
      document.getElementById('hueVal').innerText = `${config.hueCenter}°`;
      document.getElementById('hueToleranceVal').innerText = `±${config.hueTolerance}°`;
      document.getElementById('satVal').innerText = `${Math.round(config.minSaturation * 100)}%`;
      document.getElementById('lightVal').innerText = `${Math.round(config.minLightness * 100)}%`;
      document.getElementById('blinkVal').innerText = `${config.blinkRateTarget} ms (${(1000 / (config.blinkRateTarget * 2)).toFixed(1)} Hz)`;
      
      const sensWords = ['V. Low', 'Low', 'Low-Med', 'Medium', 'Medium', 'Med-High', 'High', 'V. High', 'Max Alert', 'Unstable'];
      document.getElementById('sensVal').innerText = `${sensWords[config.sensitivity - 1]} (${config.sensitivity}/10)`;
    }

    function toggleSimulation() {
      isSimulating = !isSimulating;
      const btn = document.getElementById('simBtn');
      if (isSimulating) {
        btn.innerText = '■ STOP SIMULATION';
        btn.className = 'btn warning';
        log('SIMULATION STARTED: Injecting artificial 500ms pink blinking signal.', 'warning');
      } else {
        btn.innerText = '⚡ SIMULATE SPY CAM';
        btn.className = 'btn primary';
        log('Simulation stopped.', 'system');
      }
    }

    function resetSystem() {
      trackedTargets = [];
      primaryTargetId = null;
      isSimulating = false;
      
      const btn = document.getElementById('simBtn');
      btn.innerText = '⚡ SIMULATE SPY CAM';
      btn.className = 'btn primary';
      
      updateThreatDisplay(0, 'SCANNING AREA...', 'System reset successfully.');
      log('System reset. Bounding boxes and target buffers cleared.', 'success');
    }

    function rgbToHsl(r, g, b) {
      r /= 255; g /= 255; b /= 255;
      const max = Math.max(r, g, b), min = Math.min(r, g, b);
      let h, s, l = (max + min) / 2;
      
      if (max === min) {
        h = s = 0;
      } else {
        const d = max - min;
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        switch (max) {
          case r: h = (g - b) / d + (g < b ? 6 : 0); break;
          case g: h = (b - r) / d + 2; break;
          case b: h = (r - g) / d + 4; break;
        }
        h /= 6;
      }
      return [h * 360, s, l];
    }

    function initCamera() {
      log('Initializing camera feed...', 'system');
      navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: 640 },
          height: { ideal: 480 },
          facingMode: 'environment'
        },
        audio: false
      })
      .then(stream => {
        video.srcObject = stream;
        video.play();
        streamActive = true;
        
        video.onloadedmetadata = () => {
          videoWidth = video.videoWidth;
          videoHeight = video.videoHeight;
          camCanvas.width = videoWidth;
          camCanvas.height = videoHeight;
          
          resolutionText.innerText = `RES: ${videoWidth}x${videoHeight}`;
          log(`Camera feed active. Resolution: ${videoWidth}x${videoHeight}`, 'success');
          
          requestAnimationFrame(processFrame);
        };
      })
      .catch(err => {
        log(`Camera access denied or failed: ${err.message}`, 'alert');
        document.getElementById('systemStatusText').innerText = 'ERROR: CAM_FAIL';
        document.getElementById('systemStatusText').style.color = '#FF3366';
      });
    }

    function stopWiredCamera() {
      if (video.srcObject) {
        const tracks = video.srcObject.getTracks();
        tracks.forEach(track => track.stop());
        video.srcObject = null;
      }
      streamActive = false;
      log('Wired camera stream stopped.', 'system');
    }

    function processFrame() {
      if (!streamActive || currentMode !== 'wired') return;

      frameCount++;
      const now = Date.now();
      
      if (now - lastFpsTime >= 1000) {
        fpsDisplay.innerText = `${frameCount} FPS`;
        frameCount = 0;
        lastFpsTime = now;
      }

      procCtx.drawImage(video, 0, 0, GRID_W, GRID_H);
      
      if (isSimulating) {
        if (now - lastSimBlinkTime >= 500) {
          simBlinkState = !simBlinkState;
          lastSimBlinkTime = now;
        }
        
        simAngle += 0.015;
        const orbitRadiusX = GRID_W * 0.25;
        const orbitRadiusY = GRID_H * 0.25;
        const simX = Math.round(GRID_W / 2 + Math.cos(simAngle) * orbitRadiusX);
        const simY = Math.round(GRID_H / 2 + Math.sin(simAngle) * orbitRadiusY);
        
        if (simBlinkState) {
          procCtx.fillStyle = 'rgba(255, 30, 220, 1.0)';
          procCtx.beginPath();
          procCtx.arc(simX, simY, 2.5, 0, Math.PI * 2);
          procCtx.fill();
        }
      }

      const imgData = procCtx.getImageData(0, 0, GRID_W, GRID_H);
      const data = imgData.data;
      const matchingPixels = [];
      const stride = 2;

      for (let y = 0; y < GRID_H; y += stride) {
        for (let x = 0; x < GRID_W; x += stride) {
          const idx = (y * GRID_W + x) * 4;
          const r = data[idx];
          const g = data[idx+1];
          const b = data[idx+2];
          
          if (r > 50 && b > 50 && r > g + 2 && b > g + 2) {
            const [h, s, l] = rgbToHsl(r, g, b);
            
            let hueDiff = Math.abs(h - config.hueCenter);
            if (hueDiff > 180) hueDiff = 360 - hueDiff;
            
            if (hueDiff <= config.hueTolerance && s >= config.minSaturation && l >= config.minLightness) {
              matchingPixels.push({ x, y, intensity: l });
            }
          }
        }
      }

      const blobs = findBlobs(matchingPixels);
      trackBlobs(blobs, now);
      drawHud(now);
      updateDashboardData(now);

      requestAnimationFrame(processFrame);
    }

    function findBlobs(pixels) {
      if (pixels.length === 0) return [];
      
      const visited = new Set();
      const blobs = [];
      const pixelGrid = {};
      pixels.forEach(p => {
        pixelGrid[`${p.x},${p.y}`] = p;
      });

      const maxDistance = 6.0;

      pixels.forEach(p => {
        const key = `${p.x},${p.y}`;
        if (visited.has(key)) return;

        const queue = [p];
        visited.add(key);
        
        let minX = p.x, maxX = p.x;
        let minY = p.y, maxY = p.y;
        let sumX = 0, sumY = 0;
        let sumIntensity = 0;
        let pCount = 0;

        while (queue.length > 0) {
          const curr = queue.shift();
          sumX += curr.x;
          sumY += curr.y;
          sumIntensity += curr.intensity;
          pCount++;

          if (curr.x < minX) minX = curr.x;
          if (curr.x > maxX) maxX = curr.x;
          if (curr.y < minY) minY = curr.y;
          if (curr.y > maxY) maxY = curr.y;

          for (let dx = -4; dx <= 4; dx += 2) {
            for (let dy = -4; dy <= 4; dy += 2) {
              if (dx === 0 && dy === 0) continue;
              const nx = curr.x + dx;
              const ny = curr.y + dy;
              const nKey = `${nx},${ny}`;
              
              if (pixelGrid[nKey] && !visited.has(nKey)) {
                visited.add(nKey);
                queue.push(pixelGrid[nKey]);
              }
            }
          }
        }

        const minSize = Math.max(1, 11 - config.sensitivity); 
        const maxSize = 250;
        const maxDim = 50;
        const blobW = (maxX - minX) + 1;
        const blobH = (maxY - minY) + 1;
        if (pCount >= minSize && pCount <= maxSize && blobW <= maxDim && blobH <= maxDim) {
          blobs.push({
            x: minX,
            y: minY,
            w: blobW,
            h: blobH,
            cx: sumX / pCount,
            cy: sumY / pCount,
            intensity: sumIntensity / pCount,
            size: pCount
          });
        }
      });

      return blobs;
    }

    function trackBlobs(blobs, now) {
      const scaleX = videoWidth / GRID_W;
      const scaleY = videoHeight / GRID_H;

      const currentBlobs = blobs.map(b => ({
        x: b.x * scaleX,
        y: b.y * scaleY,
        w: b.w * scaleX,
        h: b.h * scaleY,
        cx: b.cx * scaleX,
        cy: b.cy * scaleY,
        intensity: b.intensity
      }));

      const matchThreshold = 200;
      const targetUsed = new Set();

      trackedTargets.forEach(target => {
        let bestBlob = null;
        let minDist = Infinity;
        let bestIdx = -1;

        currentBlobs.forEach((blob, idx) => {
          if (targetUsed.has(idx)) return;
          const dist = Math.hypot(target.cx - blob.cx, target.cy - blob.cy);
          if (dist < matchThreshold && dist < minDist) {
            minDist = dist;
            bestBlob = blob;
            bestIdx = idx;
          }
        });

        if (bestBlob) {
          target.x = target.x * 0.6 + bestBlob.x * 0.4;
          target.y = target.y * 0.6 + bestBlob.y * 0.4;
          target.w = target.w * 0.7 + bestBlob.w * 0.3;
          target.h = target.h * 0.7 + bestBlob.h * 0.3;
          target.cx = target.cx * 0.6 + bestBlob.cx * 0.4;
          target.cy = target.cy * 0.6 + bestBlob.cy * 0.4;
          target.lastSeen = now;
          target.active = true;
          
          target.signalHistory.push({ time: now, val: bestBlob.intensity });
          targetUsed.add(bestIdx);
        } else {
          target.active = false;
          target.signalHistory.push({ time: now, val: 0 });
        }

        while (target.signalHistory.length > 0 && now - target.signalHistory[0].time > 5500) {
          target.signalHistory.shift();
        }

        analyzeBlinkFrequency(target, now);
      });

      currentBlobs.forEach((blob, idx) => {
        if (targetUsed.has(idx)) return;
        
        const newTarget = {
          id: nextTargetId++,
          x: blob.x,
          y: blob.y,
          w: blob.w,
          h: blob.h,
          cx: blob.cx,
          cy: blob.cy,
          firstSeen: now,
          lastSeen: now,
          active: true,
          signalHistory: [{ time: now, val: blob.intensity }],
          threatScore: 0,
          blinkState: true,
          lastTransitionTime: now,
          transitions: [],
          isCamera: false,
          loggedAlert: false
        };
        trackedTargets.push(newTarget);
      });

      const initialCount = trackedTargets.length;
      trackedTargets = trackedTargets.filter(t => now - t.lastSeen < 1500);
      if (trackedTargets.length < initialCount) {
        if (primaryTargetId && !trackedTargets.find(t => t.id === primaryTargetId)) {
          primaryTargetId = null;
        }
      }

      if (trackedTargets.length > 0) {
        if (!primaryTargetId || !trackedTargets.find(t => t.id === primaryTargetId)) {
          const bestTarget = trackedTargets.reduce((prev, curr) => {
            return (curr.threatScore > prev.threatScore) ? curr : prev;
          }, trackedTargets[0]);
          primaryTargetId = bestTarget.id;
        }
      } else {
        primaryTargetId = null;
      }
      
      targetCountText.innerText = `TARGETS: ${trackedTargets.length}`;
    }

    function analyzeBlinkFrequency(target, now) {
      const history = target.signalHistory;
      if (history.length < 10) return;

      let minVal = Infinity;
      let maxVal = -Infinity;
      history.forEach(pt => {
        if (pt.val < minVal) minVal = pt.val;
        if (pt.val > maxVal) maxVal = pt.val;
      });

      const range = maxVal - minVal;
      const targetDur = config.blinkRateTarget;

      if (range < 0.12) {
        target.threatScore = Math.max(0, target.threatScore - 0.5);
        if (target.threatScore < 40 && target.isCamera) {
          target.isCamera = false;
        }
        return;
      }

      const timeSinceLastTransition = now - target.lastTransitionTime;
      const timeoutThreshold = Math.max(1500, targetDur * 2.2);
      if (timeSinceLastTransition > timeoutThreshold) {
        target.threatScore = Math.max(0, target.threatScore - 0.5);
        if (target.threatScore < 40 && target.isCamera) {
          target.isCamera = false;
        }
        if (target.transitions.length > 0 && timeSinceLastTransition > timeoutThreshold + 1000) {
          target.transitions = [];
        }
      }

      const thresholdVal = minVal + (range * 0.5);
      const currentVal = history[history.length - 1].val;
      const isCurrentlyOn = currentVal > thresholdVal;

      if (isCurrentlyOn !== target.blinkState) {
        const duration = now - target.lastTransitionTime;
        target.blinkState = isCurrentlyOn;
        target.lastTransitionTime = now;

        if (duration > 100 && duration < 2500) {
          target.transitions.push({ time: now, dur: duration });
        }

        if (target.transitions.length > 8) {
          target.transitions.shift();
        }

        verifyTransitionsConsistency(target);
      }
    }

    function verifyTransitionsConsistency(target) {
      if (target.transitions.length < 4) {
        target.threatScore = Math.max(0, target.threatScore - 5);
        return;
      }

      let matchCount = 0;
      const recentTrans = target.transitions.slice(-6);
      const targetRate = config.blinkRateTarget;
      const tolerance = Math.max(80, targetRate * 0.20);

      recentTrans.forEach(tr => {
        if (Math.abs(tr.dur - targetRate) <= tolerance) {
          matchCount++;
        }
      });

      const matchRatio = matchCount / recentTrans.length;

      let totalJitter = 0;
      for (let i = 1; i < recentTrans.length; i++) {
        totalJitter += Math.abs(recentTrans[i].dur - recentTrans[i-1].dur);
      }
      const avgJitter = recentTrans.length > 1 ? totalJitter / (recentTrans.length - 1) : 0;
      const maxAllowedJitter = Math.max(80, targetRate * 0.25);

      if (matchRatio >= 0.75 && avgJitter <= maxAllowedJitter) {
        target.threatScore = Math.min(100, target.threatScore + 15);
        if (target.threatScore >= 75 && !target.isCamera) {
          target.isCamera = true;
        }
      } else {
        target.threatScore = Math.max(0, target.threatScore - 15);
        if (target.threatScore < 40 && target.isCamera) {
          target.isCamera = false;
        }
      }
    }

    function drawHud(now) {
      camCtx.clearRect(0, 0, videoWidth, videoHeight);

      trackedTargets.forEach(target => {
        if (target.threatScore < 45) return;

        const x = Math.max(0, target.x);
        const y = Math.max(0, target.y);
        const w = Math.min(videoWidth - x, target.w);
        const h = Math.min(videoHeight - y, target.h);

        const isPrimary = (target.id === primaryTargetId);
        const hudColor = '#D81159'; 
        const labelText = `⚠️ CAMERA DETECTED (${Math.round(target.threatScore)}%)`;

        camCtx.strokeStyle = hudColor;
        camCtx.lineWidth = 3;
        camCtx.setLineDash([]);
        camCtx.strokeRect(x, y, w, h);
        
        if (isPrimary) {
          const bracketSize = Math.max(10, Math.min(w * 0.25, 20));
          camCtx.lineWidth = 4;
          
          camCtx.beginPath();
          camCtx.moveTo(x + bracketSize, y); camCtx.lineTo(x, y); camCtx.lineTo(x, y + bracketSize);
          camCtx.stroke();

          camCtx.beginPath();
          camCtx.moveTo(x + w - bracketSize, y); camCtx.lineTo(x + w, y); camCtx.lineTo(x + w, y + bracketSize);
          camCtx.stroke();

          camCtx.beginPath();
          camCtx.moveTo(x + bracketSize, y + h); camCtx.lineTo(x, y + h); camCtx.lineTo(x, y + bracketSize);
          camCtx.stroke();

          camCtx.beginPath();
          camCtx.moveTo(x + w - bracketSize, y + h); camCtx.lineTo(x + w, y + h); camCtx.lineTo(x + w, y + h - bracketSize);
          camCtx.stroke();
        }

        camCtx.fillStyle = hudColor;
        camCtx.font = 'bold 11px system-ui, -apple-system, sans-serif';
        
        const labelWidth = camCtx.measureText(labelText).width;
        camCtx.fillRect(x, Math.max(12, y - 16), labelWidth + 8, 14);
        
        camCtx.fillStyle = '#ffffff';
        camCtx.fillText(labelText, x + 4, Math.max(22, y - 6));

        if (isPrimary) {
          camCtx.strokeStyle = 'rgba(216, 17, 89, 0.15)';
          camCtx.lineWidth = 1;
          
          camCtx.beginPath();
          camCtx.moveTo(0, target.cy); camCtx.lineTo(videoWidth, target.cy);
          camCtx.stroke();
          
          camCtx.beginPath();
          camCtx.moveTo(target.cx, 0); camCtx.lineTo(target.cx, videoHeight);
          camCtx.stroke();
          
          camCtx.strokeStyle = hudColor;
          camCtx.beginPath();
          const ringRad = Math.max(w, h) * (0.8 + Math.sin(now / 100) * 0.1);
          camCtx.arc(target.cx, target.cy, ringRad, 0, Math.PI * 2);
          camCtx.stroke();
        }
      });
    }

    function updateDashboardData(now) {
      if (trackedTargets.length === 0) {
        updateThreatDisplay(0, 'SCANNING AREA...', 'Searching for blinking IR lens signatures. Point camera towards hidden spots.');
        wiredActiveTargetLabel.innerText = 'NO ACTIVE TARGET';
        drawEmptyPlot();
        return;
      }

      const primaryTarget = trackedTargets.find(t => t.id === primaryTargetId);
      if (!primaryTarget) return;

      wiredActiveTargetLabel.innerText = `ACTIVE TARGET: ID_${primaryTarget.id}`;

      const threat = Math.round(primaryTarget.threatScore);
      let statusStr = 'LOCKING TARGET...';
      let descStr = `Target ID_${primaryTarget.id} under evaluation. Color matches IR spectrum. Monitoring blink rate stability...`;

      if (threat >= 40 && threat < 75) {
        statusStr = 'WARNING: STROBING SIGNAL';
        descStr = `Strobing light detected from Target ID_${primaryTarget.id}. Frequency is close to the covert IR profile (${config.blinkRateTarget}ms).`;
      } else if (threat >= 75) {
        statusStr = 'CRITICAL ALARM: COVERT CAMERA';
        descStr = `🚨 POSITIVE IDENTIFICATION! Target ID_${primaryTarget.id} is emitting a rhythmic near-infrared ${config.blinkRateTarget}ms blink signature. Bounding box locked.`;
      }

      if (threat >= 45 && !primaryTarget.loggedAlert) {
        primaryTarget.loggedAlert = true;
        log("🚨 CAMERA DETECTED: Blinking 500ms Spy Camera Detected!", "alert");
        triggerWhatsAppAlert(primaryTarget.id);
      }
      
      updateThreatDisplay(threat, statusStr, descStr);
      drawSignalPlot(primaryTarget);
    }

    function triggerWhatsAppAlert(targetId) {
      log(`Sending WhatsApp alert message for Target ID_${targetId} directly via mobile data...`, 'system');
      const greenApiUrl = "https://7107.api.greenapi.com/waInstance7107650314/sendMessage/48984660ecf34156bed84404ba9a9ebeeed2dec466144fd4bd";
      fetch(greenApiUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          chatId: "918317470775@c.us",
          message: `⚠️ SMARTGUARD ALERT: Spy camera detected! (Target ID_${targetId}, Blinking: ${config.blinkRateTarget}ms)`
        })
      })
      .then(response => {
        if (!response.ok) {
          throw new Error(`HTTP error! status: ${response.status}`);
        }
        return response.json();
      })
      .then(data => {
        if (data.idMessage) {
          log(`✉️ WhatsApp alert successfully sent to +91 83174 70775 via Mobile Data!`, 'success');
        } else {
          log(`❌ Failed to send WhatsApp: ${JSON.stringify(data)}`, 'alert');
        }
      })
      .catch(error => {
        log(`❌ Error sending WhatsApp: ${error.message} (Ensure mobile data is enabled on your phone)`, 'alert');
      });
    }

    function updateThreatDisplay(score, status, description) {
      threatPercent.innerText = `${score}%`;
      
      const circumference = 188.4;
      const offset = circumference - (score / 100) * circumference;
      threatCircle.style.strokeDashoffset = offset;

      threatStatusLabel.innerText = status;
      threatDesc.innerText = description;

      if (score >= 75) {
        threatCard.className = 'threat-indicator-card alert-active';
      } else {
        threatCard.className = 'threat-indicator-card';
      }
    }

    function drawSignalPlot(target) {
      const W = wiredPlotCanvas.offsetWidth;
      const H = wiredPlotCanvas.offsetHeight;
      wiredPlotCanvas.width = W;
      wiredPlotCanvas.height = H;

      wiredPlotCtx.clearRect(0, 0, W, H);

      wiredPlotCtx.strokeStyle = 'rgba(246, 235, 237, 0.6)';
      wiredPlotCtx.lineWidth = 1;
      for (let i = 1; i < 4; i++) {
        const y = (H / 4) * i;
        wiredPlotCtx.beginPath();
        wiredPlotCtx.moveTo(0, y);
        wiredPlotCtx.lineTo(W, y);
        wiredPlotCtx.stroke();
      }

      const history = target.signalHistory;
      if (history.length === 0) return;

      const durationWindow = 5000;
      const now = Date.now();

      let minVal = 0;
      let maxVal = 1;
      history.forEach(pt => {
        if (pt.val > maxVal) maxVal = pt.val;
      });

      let valSum = 0;
      history.forEach(pt => valSum += pt.val);
      const avg = valSum / history.length;

      wiredPlotCtx.lineWidth = 2.5;

      for (let i = 1; i < history.length; i++) {
        const ptPrev = history[i - 1];
        const ptCurr = history[i];

        const timeAgoPrev = now - ptPrev.time;
        const timeAgoCurr = now - ptCurr.time;
        if (timeAgoPrev > durationWindow) continue;

        const xPrev = W - (timeAgoPrev / durationWindow) * W;
        const yPrev = H - 5 - ((ptPrev.val - minVal) / (maxVal - minVal)) * (H - 10);

        const xCurr = W - (timeAgoCurr / durationWindow) * W;
        const yCurr = H - 5 - ((ptCurr.val - minVal) / (maxVal - minVal)) * (H - 10);

        if (ptCurr.val > avg) {
          wiredPlotCtx.strokeStyle = target.threatScore >= 45 ? '#FF3366' : '#FF9F1C';
        } else {
          wiredPlotCtx.strokeStyle = '#06D6A0';
        }

        wiredPlotCtx.beginPath();
        wiredPlotCtx.moveTo(xPrev, yPrev);
        wiredPlotCtx.lineTo(xCurr, yCurr);
        wiredPlotCtx.stroke();
      }

      wiredPlotCtx.strokeStyle = 'rgba(255, 159, 28, 0.7)';
      wiredPlotCtx.setLineDash([4, 4]);
      const threshY = H - 5 - ((avg - minVal) / (maxVal - minVal)) * (H - 10);
      wiredPlotCtx.beginPath();
      wiredPlotCtx.moveTo(0, threshY);
      wiredPlotCtx.lineTo(W, threshY);
      wiredPlotCtx.stroke();
      wiredPlotCtx.setLineDash([]);
      
      wiredPlotCtx.fillStyle = '#FF9F1C';
      wiredPlotCtx.font = '9px system-ui, sans-serif';
      wiredPlotCtx.fillText('THRESHOLD', 5, threshY - 3);
    }

    function drawEmptyPlot() {
      const W = wiredPlotCanvas.offsetWidth;
      const H = wiredPlotCanvas.offsetHeight;
      wiredPlotCanvas.width = W;
      wiredPlotCanvas.height = H;
      wiredPlotCtx.clearRect(0, 0, W, H);
      
      wiredPlotCtx.fillStyle = '#A89397';
      wiredPlotCtx.font = '11px system-ui, sans-serif';
      wiredPlotCtx.textAlign = 'center';
      wiredPlotCtx.fillText('WAITING FOR TARGET SIGNAL INPUT...', W / 2, H / 2 + 3);
    }

    async function pollPIR() {
      try {
        const response = await fetch('/pir-status');
        const data = await response.json();
        const dot = document.getElementById('pirDot');
        const lbl = document.getElementById('pirLabel');
        const sub = document.getElementById('pirSub');
        
        if (data.motion) {
          dot.className = 'pir-dot active';
          lbl.innerText = 'PIR SENSOR: MOTION DETECTED';
          lbl.style.color = '#FF3366';
          sub.innerText = `Last Activity: ${data.time || new Date().toLocaleTimeString()}`;
        } else {
          dot.className = 'pir-dot clear';
          lbl.innerText = 'PIR SENSOR: SECURE / NO MOTION';
          lbl.style.color = '#06D6A0';
          sub.innerText = 'Monitoring area for movement...';
        }
      } catch (err) {
        document.getElementById('pirDot').className = 'pir-dot';
        document.getElementById('pirLabel').innerText = 'PIR SENSOR: OFFLINE';
        document.getElementById('pirLabel').style.color = 'var(--text-dim)';
        document.getElementById('pirSub').innerText = 'Server offline (Start server.py)';
      }
    }

    // === WIRELESS RADAR LOGIC ===
    function signalPercent(rssi) {
      if (rssi >= -50) return 100;
      if (rssi <= -100) return 0;
      return Math.round(2 * (rssi + 100));
    }

    function estimateDistance(rssi) {
      return Math.pow(10, (-40 - rssi) / 20);
    }

    function getCurrentSector(heading) {
      let h = ((heading % 360) + 360) % 360;
      return Math.floor(((h + 22.5) % 360) / 45);
    }

    function deltaLabel(delta) {
      if (delta === null || delta === undefined) return '';
      if (delta > 2)  return '<span class="delta-pill delta-up">&#x25B2; +' + delta.toFixed(0) + ' dBm</span>';
      if (delta < -2) return '<span class="delta-pill delta-down">&#x25BC; ' + delta.toFixed(0) + ' dBm</span>';
      return '<span class="delta-pill delta-flat">~ ' + delta.toFixed(0) + ' dBm</span>';
    }

    function initSensors() {
      function attachListener() {
        gyroLinked = true;
        window.addEventListener('deviceorientation', function(event) {
          let raw = (event.webkitCompassHeading != null)
            ? (360 - event.webkitCompassHeading)
            : event.alpha;
          if (raw === null || raw === undefined) return;
          currentAlpha = raw;
          if (baselineAlpha === null) baselineAlpha = currentAlpha;
          let delta = currentAlpha - baselineAlpha;
          if (delta < 0)   delta += 360;
          if (delta > 360) delta -= 360;
          relativeHeading = delta;
          document.getElementById("radarHeading").innerText =
            "Heading Offset: " + Math.round(relativeHeading) + "deg";
          if (trackedDevice || trackedBLEDevice) updateCompassDisplay();
        }, true);
        document.getElementById("sensorCard").innerHTML =
          "<div class='panel-title'><span>◉ GYROSCOPE COMPASS CALIBRATION</span></div><p style='color:#06D6A0;margin:0;font-size:13px;font-weight:600;'>GYRO COMPASS LINKED &#x2714; &nbsp;<small style='color:var(--text-secondary);font-weight:normal;'>Rotate device to steer radar</small></p>";
        log("Gyro Compass linked successfully.", "success");
      }
      if (typeof DeviceOrientationEvent !== 'undefined' &&
          typeof DeviceOrientationEvent.requestPermission === 'function') {
        DeviceOrientationEvent.requestPermission()
          .then(state => { if (state === 'granted') attachListener(); })
          .catch(err => {
            console.error(err);
            log("Gyro Permission Denied.", "alert");
          });
      } else {
        attachListener();
      }
    }

    function resetTrackingState() {
      baselineAlpha = currentAlpha;
      targetVectorAngle = 0;
      trackingState = 'NAVIGATING';
      sectorData = Array.from({length: NUM_SECTORS}, () => ({rssi:-Infinity,avg:-100,count:0,momentum:0}));
      wallZones.fill(false);
      wallActive = false;
      prevScanRSSI    = null;
      prevScanBLERSSI = null;
      rssiHistory     = [];
      document.getElementById("navPanel").classList.remove("wall-pulsing","found-pulsing");
      document.getElementById("navArrow").innerHTML = "&#x2191;";
      document.getElementById("wallBtn").style.display = "inline-block";
    }

    function trackDevice(bssid) {
      trackedDevice = bssid; previousRSSI = null; lastSeen = Date.now();
      trackedBLEDevice = null; previousBLERSSI = null;
      resetTrackingState(); 
      log("Tracking WiFi Device: " + bssid, "info");
      renderWifiTable();
    }

    function trackBLEDevice(name) {
      trackedBLEDevice = name; previousBLERSSI = null; lastBLESeen = Date.now();
      trackedDevice = null; previousRSSI = null;
      resetTrackingState(); 
      log("Tracking BLE Beacon: " + name, "info");
      renderBleTable();
    }

    function stopTracking() {
      log("Target tracking stopped.", "system");
      trackedDevice = null; trackedBLEDevice = null;
      previousRSSI = null; previousBLERSSI = null;
      trackingState = 'IDLE'; rssiHistory = [];
      document.getElementById("trackerStatus").innerHTML =
        `<div class="lockon-layout" style="opacity: 0.6;">
          <div class="lockon-meta">
            <div class="lockon-type">IDLE SCANNING MODE</div>
            <p style="font-size:12px; color:var(--text-secondary); margin-top:4px;">No device locked. Click "LOCK" on any node below to start target acquisition loops.</p>
          </div>
        </div>`;
      document.getElementById("wirelessActiveTargetLabel").innerText = "NO ACTIVE TARGET";
      document.getElementById("navArrow").style.transform = "rotate(0deg)";
      document.getElementById("navArrow").innerHTML = "&#x2191;";
      document.getElementById("navText").innerText = "Select a Target Below to Initialize Navigation";
      document.getElementById("sigLog").innerText = "Memory Grid Empty";
      document.getElementById("wallBtn").style.display = "none";
      document.getElementById("navPanel").classList.remove("wall-pulsing","found-pulsing");
      drawWirelessEmptyPlot();
      renderWifiTable(); renderBleTable();
    }

    function executeGradientAscent(currentRSSI, prevRSSI) {
      rssiHistory.push({ time: Date.now(), val: currentRSSI });
      if (rssiHistory.length > 30) rssiHistory.shift();

      if (currentRSSI >= -50) {
        if (trackingState !== 'FOUND') {
          trackingState = 'FOUND';
          log("Target Proximity Reached! Target acquired near (>= -50 dBm).", "success");
        }
        updateCompassDisplay();
        return;
      }
      if (trackingState !== 'NAVIGATING') return;

      let delta = (prevRSSI !== null) ? (currentRSSI - prevRSSI) : 0;

      let sec = getCurrentSector(relativeHeading);
      let sd  = sectorData[sec];

      if (sd.count === 0) {
        sd.avg  = currentRSSI;
        sd.rssi = currentRSSI;
      } else {
        sd.avg  = currentRSSI * 0.65 + sd.avg * 0.35;
        if (currentRSSI > sd.rssi) sd.rssi = currentRSSI;
      }
      sd.count++;

      if (Math.abs(delta) > 2) {
        sd.momentum = (delta > 0) ? 1 : -1;
      }
      if (sd.momentum < 0 && sd.count > 1) {
        sd.avg = Math.max(sd.avg - 4, -100);
      }

      if (delta > 3) {
        sd.momentum = 1;
        sd.avg = Math.min(sd.avg + 3, -50);
        targetVectorAngle = sec * 45;
        let scoreStr = sectorData.map((s,i) =>
          wallZones[i] ? 'W' : (s.count > 0 ? (s.avg + s.momentum*5).toFixed(0) : '-')
        ).join('|');
        document.getElementById("sigLog").innerText =
          "UP +" + delta.toFixed(1) + " dBm | Keep going! Sec " + sec + " | [" + scoreStr + "]";
        updateCompassDisplay();
        return;
      }

      let bestSector = sec;
      let bestScore  = -Infinity;
      let hasVisited = false;

      for (let i = 0; i < NUM_SECTORS; i++) {
        if (wallZones[i]) continue;
        if (sectorData[i].count === 0) continue;
        hasVisited = true;
        let score = sectorData[i].avg + sectorData[i].momentum * 5;
        if (score > bestScore) { bestScore = score; bestSector = i; }
      }

      if (!hasVisited) bestSector = sec;

      targetVectorAngle = bestSector * 45;

      let scoreStr = sectorData.map((s,i) =>
        wallZones[i] ? 'W' : (s.count > 0 ? (s.avg + s.momentum*5).toFixed(0) : '-')
      ).join('|');
      document.getElementById("sigLog").innerText =
        "Best:Sec " + bestSector + " | D:" + (delta>=0?"+":"") + delta.toFixed(1) + " | [" + scoreStr + "]";
    }

    function reportWall() {
      if (!trackedDevice && !trackedBLEDevice) return;
      if (trackingState === 'FOUND') return;
      let sec = getCurrentSector(relativeHeading);
      wallZones[sec]        = true;
      sectorData[sec].momentum = -99;
      wallActive = true;
      log("Obstacle wall reported in Sector " + sec, "warning");
      updateCompassDisplay();
    }

    function updateCompassDisplay() {
      if (!trackedDevice && !trackedBLEDevice) return;
      const arrowEl = document.getElementById("navArrow");
      const textEl  = document.getElementById("navText");
      const panelEl = document.getElementById("navPanel");

      if (trackingState === 'FOUND') {
        arrowEl.style.transform = "rotate(0deg)";
        arrowEl.innerHTML = "&#x1F3AF;";
        textEl.innerHTML  = "<span style='color:var(--status-secure);font-size:1.2em;'>DEVICE FOUND NEARBY!</span><br>Stop walking. Look within 1-2 metres.";
        panelEl.classList.remove("wall-pulsing");
        panelEl.classList.add("found-pulsing");
        document.getElementById("wallBtn").style.display = "none";
        document.getElementById("sigLog").innerText = "PROXIMITY THRESHOLD EXCEEDED (>= -50 dBm)";
        return;
      }

      let displayAngle = (targetVectorAngle - relativeHeading + 360) % 360;
      arrowEl.style.transform = "rotate(" + displayAngle + "deg)";

      if (wallActive) {
        panelEl.classList.remove("found-pulsing");
        panelEl.classList.add("wall-pulsing");
        let sec   = getCurrentSector(relativeHeading);
        let leftS = (sec - 1 + NUM_SECTORS) % NUM_SECTORS;
        let rightS= (sec + 1) % NUM_SECTORS;
        let leftScore  = !wallZones[leftS]  ? (sectorData[leftS].avg  + sectorData[leftS].momentum  * 5) : -Infinity;
        let rightScore = !wallZones[rightS] ? (sectorData[rightS].avg + sectorData[rightS].momentum * 5) : -Infinity;
        let escapeText = "";
        if (leftScore > rightScore && leftScore > -Infinity)       escapeText = "MEMORY SUGGESTS: <b>TURN LEFT</b>";
        else if (rightScore > leftScore && rightScore > -Infinity) escapeText = "MEMORY SUGGESTS: <b>TURN RIGHT</b>";
        else                                                        escapeText = "BACKTRACK AND TRY A NEW DIRECTION.";
        textEl.innerHTML = "<span style='color:var(--status-danger);font-size:1.1em;'>WALL BLOCKED PATH</span><br>" + escapeText;
        if (displayAngle > 90 && displayAngle < 270) wallActive = false;
        return;
      }

      panelEl.classList.remove("wall-pulsing");
      if      (displayAngle > 330 || displayAngle < 30)  { textEl.innerHTML = "<span style='color:var(--status-secure);'>ALIGNED - PROCEED FORWARD</span>"; }
      else if (displayAngle >= 30  && displayAngle <= 150){ textEl.innerHTML = "TURN LEFT"; }
      else if (displayAngle > 150  && displayAngle < 210) { textEl.innerHTML = "<span style='color:var(--status-danger);'>WRONG WAY - TURN AROUND</span>"; }
      else                                                 { textEl.innerHTML = "TURN RIGHT"; }
    }

    async function loadWifi() {
      if (currentMode !== 'wireless') return;
      try {
        const response = await fetch('/wifi');
        let rawText  = await response.text();
        currentWifiData = JSON.parse(rawText);

        // DIRECTIONAL ORIENTATION STEERING SIMULATION
        // Scale RSSI based on user facing direction relative to mock location (90 deg - East)
        if (gyroLinked) {
          currentWifiData.forEach(net => {
            if (net.ssid === "SPY_CAM_WIFI") {
              let targetAngle = 90; // mock target direction
              let angleDiff = Math.abs(relativeHeading - targetAngle);
              if (angleDiff > 180) angleDiff = 360 - angleDiff;
              
              // Map 0-180 angle to -45 to -85 RSSI
              let strength = -45 - (angleDiff / 180) * 40;
              strength += (Math.random() * 4 - 2); // +/-2 dBm noise
              net.rssi = Math.round(strength);
            }
          });
          rawText = JSON.stringify(currentWifiData);
        }

        let isNewScan = (rawText !== lastRawWifiData);
        if (isNewScan) lastRawWifiData = rawText;
        renderWifiTable(isNewScan);
      } catch(e) {
        console.error("WiFi Poll Fail", e);
      }
    }

    function updateThreatDisplayWireless(score) {
      const circle = document.getElementById("rssiCircle");
      const percentText = document.getElementById("rssiPercent");
      if (!circle || !percentText) return;
      percentText.innerText = `${score}%`;
      const circumference = 188.4;
      const offset = circumference - (score / 100) * circumference;
      circle.style.strokeDashoffset = offset;
    }

    function renderWifiTable(isNewScan = false) {
      let rows = ''; let found = false;
      currentWifiData.sort((a,b) => b.rssi - a.rssi);
      currentWifiData.forEach(net => {
        if (trackedDevice === net.bssid) {
          found = true; lastSeen = Date.now();
          let delta = (prevScanRSSI !== null) ? (net.rssi - prevScanRSSI) : null;
          if (isNewScan) { 
            executeGradientAscent(net.rssi, prevScanRSSI); 
            prevScanRSSI = net.rssi; 
            log(`WiFi locked scan: RSSI ${net.rssi} dBm ${delta !== null ? '('+(delta >= 0 ? '+':'')+delta+' dBm)' : ''}`, "info");
          }
          previousRSSI = net.rssi;
          document.getElementById("wirelessActiveTargetLabel").innerText = `ACTIVE TARGET: ${net.ssid}`;
          
          const percent = signalPercent(net.rssi);
          let foundAlert = trackingState === 'FOUND'
            ? "<br><span style='color:var(--status-secure);font-weight:bold;'>PROXIMITY THRESHOLD REACHED</span>" : "";
          
          document.getElementById("trackerStatus").innerHTML =
            `<div class="lockon-layout">
              <div class="lockon-meta">
                <div class="lockon-type">LOCK-ON ACTIVE WIFI TARGET</div>
                <div class="lockon-name">SSID: ${net.ssid}</div>
                <div class="lockon-detail">Signal: ${net.rssi} dBm ${deltaLabel(delta)}</div>
                <div class="lockon-range">Est. Distance: ${estimateDistance(net.rssi).toFixed(1)} m ${foundAlert}</div>
                <button class="btn danger" style="margin-top: 10px;" onclick="stopTracking()">DROP TARGET</button>
              </div>
              <div class="lockon-gauge-container">
                <div class="threat-gauge">
                  <svg class="threat-circle-svg">
                    <circle class="threat-circle-bg" cx="32" cy="32" r="30"></circle>
                    <circle class="threat-circle-fill" id="rssiCircle" cx="32" cy="32" r="30"></circle>
                  </svg>
                  <div class="threat-percent" id="rssiPercent">0%</div>
                </div>
              </div>
            </div>`;
          
          updateThreatDisplayWireless(percent);
          drawWirelessSignalPlot(rssiHistory);
        }
        if (net.rssi < rssiThreshold) return;
        rows += "<tr><td><b>" + net.ssid + "</b><br><code style='color:var(--text-dim);font-size:0.8em;'>" + net.bssid + "</code></td>" +
                "<td>" + net.rssi + " dBm</td>" +
                "<td><button class='btn' onclick=\"trackDevice('" + net.bssid + "')\">LOCK</button></td></tr>";
      });
      if (trackedDevice && !found) {
        document.getElementById("trackerStatus").innerHTML =
          `<div class="lockon-layout">
            <div class="lockon-meta">
              <div class="lockon-type" style="color:var(--status-danger);">TARGET LOST</div>
              <button class="btn danger" style="margin-top: 10px;" onclick="stopTracking()">CLEAR TARGET</button>
            </div>
          </div>`;
        prevScanRSSI = null;
      }
      document.getElementById('wifiTable').innerHTML = rows;
      if (trackedDevice) updateCompassDisplay();
    }

    async function loadBLE() {
      if (currentMode !== 'wireless') return;
      try {
        const response = await fetch('/ble');
        let rawText  = await response.text();
        currentBLEData = JSON.parse(rawText);

        // DIRECTIONAL ORIENTATION STEERING SIMULATION
        if (gyroLinked) {
          currentBLEData.forEach(dev => {
            if (dev.name === "SPY_BEACON_BLE") {
              let targetAngle = 90;
              let angleDiff = Math.abs(relativeHeading - targetAngle);
              if (angleDiff > 180) angleDiff = 360 - angleDiff;
              
              let strength = -48 - (angleDiff / 180) * 40;
              strength += (Math.random() * 4 - 2);
              dev.rssi = Math.round(strength);
            }
          });
          rawText = JSON.stringify(currentBLEData);
        }

        let isNewScan = (rawText !== lastRawBLEData);
        if (isNewScan) lastRawBLEData = rawText;
        renderBleTable(isNewScan);
      } catch(e) {
        console.error("BLE Poll Fail", e);
      }
    }

    function renderBleTable(isNewScan = false) {
      let rows = ''; let found = false;
      currentBLEData.sort((a,b) => b.rssi - a.rssi);
      currentBLEData.forEach(dev => {
        if (trackedBLEDevice === dev.name) {
          found = true; lastBLESeen = Date.now();
          let delta = (prevScanBLERSSI !== null) ? (dev.rssi - prevScanBLERSSI) : null;
          if (isNewScan) { 
            executeGradientAscent(dev.rssi, prevScanBLERSSI); 
            prevScanBLERSSI = dev.rssi; 
            log(`BLE locked scan: RSSI ${dev.rssi} dBm ${delta !== null ? '('+(delta >= 0 ? '+':'')+delta+' dBm)' : ''}`, "info");
          }
          previousBLERSSI = dev.rssi;
          document.getElementById("wirelessActiveTargetLabel").innerText = `ACTIVE TARGET: ${dev.name}`;
          
          const percent = signalPercent(dev.rssi);
          let foundAlert = trackingState === 'FOUND'
            ? "<br><span style='color:var(--status-secure);font-weight:bold;'>PROXIMITY THRESHOLD REACHED</span>" : "";
          
          document.getElementById("trackerStatus").innerHTML =
            `<div class="lockon-layout">
              <div class="lockon-meta">
                <div class="lockon-type">LOCK-ON ACTIVE BLE BEACON</div>
                <div class="lockon-name">Beacon: ${dev.name}</div>
                <div class="lockon-detail">Signal: ${dev.rssi} dBm ${deltaLabel(delta)}</div>
                <div class="lockon-range">Est. Distance: ${estimateDistance(dev.rssi).toFixed(1)} m ${foundAlert}</div>
                <button class="btn danger" style="margin-top: 10px;" onclick="stopTracking()">DROP TARGET</button>
              </div>
              <div class="lockon-gauge-container">
                <div class="threat-gauge">
                  <svg class="threat-circle-svg">
                    <circle class="threat-circle-bg" cx="32" cy="32" r="30"></circle>
                    <circle class="threat-circle-fill" id="rssiCircle" cx="32" cy="32" r="30"></circle>
                  </svg>
                  <div class="threat-percent" id="rssiPercent">0%</div>
                </div>
              </div>
            </div>`;
          
          updateThreatDisplayWireless(percent);
          drawWirelessSignalPlot(rssiHistory);
        }
        if (dev.rssi < rssiThreshold) return;
        rows += "<tr><td><b>" + dev.name + "</b></td>" +
                "<td>" + dev.rssi + " dBm</td>" +
                "<td><button class='btn' onclick=\"trackBLEDevice('" + dev.name + "')\">LOCK</button></td></tr>";
      });
      if (trackedBLEDevice && !found) {
        document.getElementById("trackerStatus").innerHTML =
          `<div class="lockon-layout">
            <div class="lockon-meta">
              <div class="lockon-type" style="color:var(--status-danger);">TARGET LOST</div>
              <button class="btn danger" style="margin-top: 10px;" onclick="stopTracking()">CLEAR TARGET</button>
            </div>
          </div>`;
        prevScanBLERSSI = null;
      }
      document.getElementById('bleTable').innerHTML = rows;
      if (trackedBLEDevice) updateCompassDisplay();
    }

    function drawWirelessSignalPlot(history) {
      const W = wirelessPlotCanvas.offsetWidth;
      const H = wirelessPlotCanvas.offsetHeight;
      wirelessPlotCanvas.width = W;
      wirelessPlotCanvas.height = H;
      wirelessPlotCtx.clearRect(0, 0, W, H);

      wirelessPlotCtx.strokeStyle = 'rgba(246, 235, 237, 0.6)';
      wirelessPlotCtx.lineWidth = 1;
      for (let i = 1; i < 4; i++) {
        const y = (H / 4) * i;
        wirelessPlotCtx.beginPath();
        wirelessPlotCtx.moveTo(0, y);
        wirelessPlotCtx.lineTo(W, y);
        wirelessPlotCtx.stroke();
      }

      if (history.length === 0) return;

      const durationWindow = 60000; // 60 seconds rolling window for RSSI
      const now = Date.now();

      let minVal = -100;
      let maxVal = -30;

      let valSum = 0;
      history.forEach(pt => valSum += pt.val);
      const avg = valSum / history.length;

      wirelessPlotCtx.lineWidth = 2.5;

      for (let i = 1; i < history.length; i++) {
        const ptPrev = history[i - 1];
        const ptCurr = history[i];

        const timeAgoPrev = now - ptPrev.time;
        const timeAgoCurr = now - ptCurr.time;

        const xPrev = W - (timeAgoPrev / durationWindow) * W;
        const yPrev = H - 5 - ((ptPrev.val - minVal) / (maxVal - minVal)) * (H - 10);

        const xCurr = W - (timeAgoCurr / durationWindow) * W;
        const yCurr = H - 5 - ((ptCurr.val - minVal) / (maxVal - minVal)) * (H - 10);

        if (ptCurr.val > avg) {
          wirelessPlotCtx.strokeStyle = trackingState === 'FOUND' ? '#FF3366' : '#FF9F1C';
        } else {
          wirelessPlotCtx.strokeStyle = '#06D6A0';
        }

        wirelessPlotCtx.beginPath();
        wirelessPlotCtx.moveTo(xPrev, yPrev);
        wirelessPlotCtx.lineTo(xCurr, yCurr);
        wirelessPlotCtx.stroke();
      }

      wirelessPlotCtx.strokeStyle = 'rgba(255, 159, 28, 0.7)';
      wirelessPlotCtx.setLineDash([4, 4]);
      const threshY = H - 5 - ((avg - minVal) / (maxVal - minVal)) * (H - 10);
      wirelessPlotCtx.beginPath();
      wirelessPlotCtx.moveTo(0, threshY);
      wirelessPlotCtx.lineTo(W, threshY);
      wirelessPlotCtx.stroke();
      wirelessPlotCtx.setLineDash([]);
      
      wirelessPlotCtx.fillStyle = '#FF9F1C';
      wirelessPlotCtx.font = '9px system-ui, sans-serif';
      wirelessPlotCtx.fillText('THRESHOLD', 5, threshY - 3);
    }

    function drawWirelessEmptyPlot() {
      const W = wirelessPlotCanvas.offsetWidth;
      const H = wirelessPlotCanvas.offsetHeight;
      wirelessPlotCanvas.width = W;
      wirelessPlotCanvas.height = H;
      wirelessPlotCtx.clearRect(0, 0, W, H);
      
      wirelessPlotCtx.fillStyle = 'var(--text-dim)';
      wirelessPlotCtx.font = '11px system-ui, sans-serif';
      wirelessPlotCtx.textAlign = 'center';
      wirelessPlotCtx.fillText('WAITING FOR TARGET SIGNAL TRENDS...', W / 2, H / 2 + 3);
    }

    function drawChevronArrow(ctx, size) {
      let aLen = size, aWing = size / 2;
      ctx.shadowBlur = 10; ctx.shadowColor = "rgba(216,17,89,0.3)";
      ctx.strokeStyle = "rgba(216,17,89,0.95)";
      ctx.lineWidth = 4; ctx.lineCap = "round"; ctx.lineJoin = "round";
      ctx.beginPath();
      ctx.moveTo(-aWing, aLen / 2);
      ctx.lineTo(0, -aLen / 2);
      ctx.lineTo(aWing, aLen / 2);
      ctx.stroke();
      ctx.shadowBlur = 0;
    }

    function loopRadarAnimation() {
      if (currentMode !== 'wireless') {
        requestAnimationFrame(loopRadarAnimation);
        return;
      }
      
      radarCtx.clearRect(0, 0, radarCanvas.width, radarCanvas.height);

      if (!trackedDevice && !trackedBLEDevice) {
        radarCtx.strokeStyle = "rgba(216, 17, 89, 0.2)"; radarCtx.lineWidth = 1;
        for (let r = maxRadarRadius/3; r <= maxRadarRadius; r += maxRadarRadius/3) {
          radarCtx.beginPath(); radarCtx.arc(rcx,rcy,r,0,2*Math.PI); radarCtx.stroke();
        }
        let angle = (Date.now() / 600) % (2*Math.PI);
        radarCtx.strokeStyle = "rgba(216, 17, 89, 0.5)"; radarCtx.lineWidth = 2;
        radarCtx.beginPath(); radarCtx.moveTo(rcx,rcy);
        radarCtx.lineTo(rcx + maxRadarRadius*Math.cos(angle), rcy + maxRadarRadius*Math.sin(angle));
        radarCtx.stroke();
      }
      else if (trackingState === 'FOUND') {
        radarCtx.strokeStyle = "rgba(6, 214, 160, 0.8)"; radarCtx.lineWidth = 3;
        for (let r = maxRadarRadius/3; r <= maxRadarRadius; r += maxRadarRadius/3) {
          radarCtx.beginPath(); radarCtx.arc(rcx,rcy,r,0,2*Math.PI); radarCtx.stroke();
        }
        radarCtx.beginPath();
        radarCtx.moveTo(rcx,10); radarCtx.lineTo(rcx,radarCanvas.height-10);
        radarCtx.moveTo(10,rcy); radarCtx.lineTo(radarCanvas.width-10,rcy);
        radarCtx.stroke();
        radarCtx.shadowBlur = 25; radarCtx.shadowColor = "#06D6A0";
        radarCtx.fillStyle = "#06D6A0";
        radarCtx.beginPath(); radarCtx.arc(rcx,rcy,18,0,2*Math.PI); radarCtx.fill();
        radarCtx.shadowBlur = 0;
      }
      else {
        radarCtx.save();
        radarCtx.translate(rcx,rcy); radarCtx.rotate(-relativeHeading * Math.PI/180); radarCtx.translate(-rcx,-rcy);
        radarCtx.strokeStyle = "rgba(216, 17, 89, 0.15)"; radarCtx.lineWidth = 1;
        for (let r = maxRadarRadius/3; r <= maxRadarRadius; r += maxRadarRadius/3) {
          radarCtx.beginPath(); radarCtx.arc(rcx,rcy,r,0,2*Math.PI); radarCtx.stroke();
        }
        radarCtx.beginPath();
        radarCtx.moveTo(rcx,10); radarCtx.lineTo(rcx,radarCanvas.height-10);
        radarCtx.moveTo(10,rcy); radarCtx.lineTo(radarCanvas.width-10,rcy);
        radarCtx.stroke();

        for (let i = 0; i < NUM_SECTORS; i++) {
          let startAngle = ((i * 45 - 22.5 - 90) * Math.PI / 180);
          let endAngle   = ((i * 45 + 22.5 - 90) * Math.PI / 180);

          if (sectorData[i].count === 0) {
            radarCtx.fillStyle   = "rgba(216, 17, 89, 0.03)";
            radarCtx.strokeStyle = "rgba(216, 17, 89, 0.08)";
            radarCtx.lineWidth   = 0.5;
            radarCtx.beginPath(); radarCtx.moveTo(rcx,rcy);
            radarCtx.arc(rcx, rcy, maxRadarRadius * 0.95, startAngle, endAngle);
            radarCtx.closePath(); radarCtx.fill(); radarCtx.stroke();
            continue;
          }

          let score = sectorData[i].avg + sectorData[i].momentum * 5;
          let strength = Math.max(0, Math.min(1, (score + 100) / 60));
          let r = wallZones[i] ? 255 : Math.max(0, 216 * strength);
          let g = wallZones[i] ? 51 : Math.max(0, 17 * strength);
          let b = wallZones[i] ? 102 : Math.max(0, 89 * strength);
          let a = wallZones[i] ? 0.25 : (0.1 + 0.35 * strength);
          radarCtx.fillStyle   = "rgba(" + Math.round(r) + "," + Math.round(g) + "," + Math.round(b) + "," + a + ")";
          radarCtx.strokeStyle = "rgba(" + Math.round(r) + "," + Math.round(g) + "," + Math.round(b) + "," + Math.min(a*1.5,0.6) + ")";
          radarCtx.lineWidth   = 1;
          radarCtx.beginPath(); radarCtx.moveTo(rcx,rcy);
          radarCtx.arc(rcx, rcy, maxRadarRadius * 0.95, startAngle, endAngle);
          radarCtx.closePath(); radarCtx.fill(); radarCtx.stroke();
        }

        let rad    = (targetVectorAngle - 90) * (Math.PI/180);
        let dotX   = rcx + maxRadarRadius * 0.8 * Math.cos(rad);
        let dotY   = rcy + maxRadarRadius * 0.8 * Math.sin(rad);
        radarCtx.shadowBlur = 15; radarCtx.shadowColor = "#FF3366";
        radarCtx.fillStyle  = "#FF3366";
        radarCtx.beginPath(); radarCtx.arc(dotX,dotY,9,0,2*Math.PI); radarCtx.fill();
        radarCtx.shadowBlur = 0;

        radarCtx.fillStyle = "rgba(216, 17, 89, 0.6)";
        radarCtx.font = "bold 12px 'Plus Jakarta Sans'";
        radarCtx.textAlign = "center"; radarCtx.textBaseline = "middle";
        radarCtx.fillText("N", rcx,                       rcy - maxRadarRadius + 15);
        radarCtx.fillText("S", rcx,                       rcy + maxRadarRadius - 15);
        radarCtx.fillText("E", rcx + maxRadarRadius - 15, rcy);
        radarCtx.fillText("W", rcx - maxRadarRadius + 15, rcy);
        radarCtx.restore();

        let activeRSSI = trackedDevice ? previousRSSI : previousBLERSSI;
        if (activeRSSI !== null) {
          let displayAngle = (targetVectorAngle - relativeHeading + 360) % 360;
          let arrowRad  = (displayAngle - 90) * (Math.PI/180);
          let arrowDist = maxRadarRadius * 0.45;
          radarCtx.save();
          radarCtx.translate(rcx + arrowDist*Math.cos(arrowRad), rcy + arrowDist*Math.sin(arrowRad));
          radarCtx.rotate(arrowRad + Math.PI/2);
          drawChevronArrow(radarCtx, 24);
          radarCtx.restore();
        }
      }

      if (trackingState !== 'FOUND') {
        radarCtx.fillStyle = "var(--brand-primary)";
        radarCtx.beginPath(); radarCtx.arc(rcx,rcy,6,0,2*Math.PI); radarCtx.fill();
      }
      requestAnimationFrame(loopRadarAnimation);
    }

    // === SOS EMERGENCY BUTTON LOGIC ===
    function triggerSOS() {
      if (!confirm("Are you sure you want to trigger the Emergency SOS? This will send your location to your emergency contact.")) {
        return;
      }
      
      log("🚨 SOS TRIGGERED! Retrieving GPS location...", "alert");
      
      const timeStr = new Date().toLocaleTimeString('en-US', { hour12: false });
      const dateStr = new Date().toLocaleDateString('en-US');
      const timestamp = `${dateStr} ${timeStr}`;
      
      if (navigator.geolocation) {
        navigator.geolocation.getCurrentPosition(
          (position) => {
            const lat = position.coords.latitude;
            const lon = position.coords.longitude;
            const mapUrl = `https://maps.google.com/?q=${lat},${lon}`;
            sendSOSAlert(mapUrl, timestamp);
          },
          (error) => {
            log(`⚠️ Geolocation error: ${error.message}. Sending SOS without GPS.`, "warning");
            sendSOSAlert("Location Unavailable (GPS Permission Denied)", timestamp);
          },
          { enableHighAccuracy: true, timeout: 8000 }
        );
      } else {
        log("⚠️ Geolocation not supported. Sending SOS without GPS.", "warning");
        sendSOSAlert("Location Unavailable (Not Supported)", timestamp);
      }
    }

    function sendSOSAlert(locationStr, timestamp) {
      log("Sending SOS alert to +91 83174 70775...", "system");
      const greenApiUrl = "https://7107.api.greenapi.com/waInstance7107650314/sendMessage/48984660ecf34156bed84404ba9a9ebeeed2dec466144fd4bd";
      
      const messageText = `🚨 EMERGENCY SOS ALERT 🚨\n\n` +
                          `I need assistance immediately!\n` +
                          `Time: ${timestamp}\n` +
                          `Location: ${locationStr}`;
                          
      fetch(greenApiUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          chatId: "918317470775@c.us",
          message: messageText
        })
      })
      .then(response => {
        if (!response.ok) {
          throw new Error(`HTTP error! status: ${response.status}`);
        }
        return response.json();
      })
      .then(data => {
        if (data.idMessage) {
          log("✉️ SOS WhatsApp alert successfully sent to emergency contact!", "success");
          alert("SOS Message Sent Successfully!");
        } else {
          log(`❌ SOS send failed: ${JSON.stringify(data)}`, "alert");
        }
      })
      .catch(error => {
        log(`❌ Error sending SOS: ${error.message}`, "alert");
        alert("Failed to send SOS: " + error.message);
      });
    }

    // === INITIATOR / EVENT LISTENERS ===
    window.addEventListener('DOMContentLoaded', () => {
      // Initialize Canvas Dimensions for Radar Screen
      rcx = radarCanvas.width / 2;
      rcy = radarCanvas.height / 2;
      maxRadarRadius = rcx - 15;

      // Wireless filter slider
      const slider = document.getElementById("rssiSlider");
      slider.addEventListener("input", () => {
        rssiThreshold = parseInt(slider.value);
        document.getElementById("rssiValue").innerText =
          (rssiThreshold === -100) ? "ALL SIGNALS PASSED" : "FILTER: " + rssiThreshold + " dBm";
        renderWifiTable();
        renderBleTable();
      });

      // Load sliders and defaults for Wired CV Controls
      updateSliderLabels();

      // Clear/Initialize Plot Screens
      drawWirelessEmptyPlot();
      drawEmptyPlot();

      // Log startup
      log('sheSafe Surveillance Defense Command active.', 'system');
      log('Wireless Signal Radar loaded. Link Gyro Compass on mobile to begin directional scan sweeps.', 'info');
      log('Switch to Wired Camera Detector mode to verify lens CV scanning.', 'info');

      // Setup Pollers
      loadWifi(); loadBLE();
      setInterval(loadWifi, 3000);
      setInterval(loadBLE, 5000);

      pollPIR();
      setInterval(pollPIR, 2000);

      // Start radar canvas animation loop
      loopRadarAnimation();
    });
  </script>
</body>
</html>

)rawliteral";

void handleRoot() {
  size_t totalLen = strlen(index_html);
  server.setContentLength(totalLen);
  server.send(200, "text/html", "");
  
  size_t index = 0;
  size_t chunkSize = 2048;
  while (index < totalLen) {
    size_t left = totalLen - index;
    size_t willWrite = (left > chunkSize) ? chunkSize : left;
    server.sendContent(index_html + index, willWrite);
    index += willWrite;
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, password);
  
  // Connect to phone hotspot in the background
  WiFi.begin("saamarth", "12345678");
  Serial.println("\nConnecting to hotspot 'saamarth' in background...");
  
  NimBLEDevice::init("");
  
  pinMode(IR_LED_PIN, OUTPUT);
  jsonMutex = xSemaphoreCreateMutex();
  
  // Register handlers
  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/ble", handleBLE);
  server.on("/pir-status", handlePIR);
  server.on("/trigger-whatsapp", handleWhatsApp);
  
  server.begin();
  
  // Start background scanning task on Core 0
  xTaskCreatePinnedToCore(
    scanTask,
    "ScanTask",
    10000,
    NULL,
    1,
    &scanTaskHandle,
    0
  );
}

void loop() {
  server.handleClient();
  
  // Print Hotspot IP address on connection
  static bool wasConnected = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected) {
      Serial.println("\n[SYSTEM] ESP32 connected to phone hotspot!");
      Serial.print("[SYSTEM] Hotspot IP Address: ");
      Serial.println(WiFi.localIP());
      wasConnected = true;
    }
  } else {
    wasConnected = false;
  }
  
  // Non-blocking IR LED blink
  if (millis() - lastBlinkTime >= blinkInterval) {
    ledState = !ledState;
    digitalWrite(IR_LED_PIN, ledState ? HIGH : LOW);
    lastBlinkTime = millis();
  }
  
  delay(1); // yield to other tasks
}
