// AEGIS - Hidden Camera Detector (ESP32)
// Submitted for Phase 2
// Wireless Radar now works step by step: walk N steps, tap Read, app averages 2 fresh
// readings, compares with the previous point and updates sector scores.
// SOS, IR LED, code word, wired camera detector, routes, and unrelated firmware are preserved.

#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_idf_version.h>

const char* phoneHotspotSsid = "hello";
const char* phoneHotspotPassword = "12345678";

WebServer server(80);

String wifiJson = "[]";
String bleJson = "[]";
uint32_t completedWifiScanId = 0;
uint32_t completedBleScanId = 0;

SemaphoreHandle_t jsonMutex;
TaskHandle_t scanTaskHandle = NULL;

// Target-focused scanning so a reading takes well under a second:
// 0 = Wi-Fi + BLE (full list), 1 = Wi-Fi only, 2 = BLE only. trackChannel 0 = all channels.
volatile uint8_t trackMode = 0;
volatile uint8_t trackChannel = 0;

const uint8_t IR_LED_PIN = 23; // GPIO23 (D6 on this board)

void irLedBlinkTask(void* parameter) {
  bool ledState = false;
  for (;;) {
    ledState = !ledState;
    digitalWrite(IR_LED_PIN, ledState ? HIGH : LOW);
    Serial.printf("IR LED GPIO%d -> %s\n", (int)IR_LED_PIN, ledState ? "HIGH" : "LOW");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void scanTask(void* parameter) {
  uint32_t localScanId = 0;
  TickType_t lastCycleStart = xTaskGetTickCount();
  for (;;) {
    const uint32_t currentScanId = ++localScanId;
    const uint8_t mode = trackMode;
    const uint8_t ch = trackChannel;

    // 1. Wi-Fi scan (skipped while tracking a Bluetooth target)
    bool wifiScanOk = false;
    String tempWifi = "[";
    if (mode != 2) {
#if CONFIG_SOC_WIFI_SUPPORT_5G && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
      WiFi.setBandMode(WIFI_BAND_MODE_AUTO);
#endif
      int n = WiFi.scanNetworks(false, true, false, ch ? 120 : 80, ch);
      wifiScanOk = n >= 0;
      for (int i = 0; i < n; i++) {
        if (i) tempWifi += ",";
        int wch = WiFi.channel(i);
        int freq = (wch <= 13) ? (2407 + wch * 5) : (wch == 14 ? 2484 : 5000 + wch * 5);
        String band = (wch <= 14) ? "2.4GHz" : "5GHz";
        tempWifi += "{";
        tempWifi += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        tempWifi += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        tempWifi += "\"channel\":" + String(wch) + ",";
        tempWifi += "\"freq\":" + String(freq) + ",";
        tempWifi += "\"band\":\"" + band + "\",";
        tempWifi += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\",";
        tempWifi += "\"scanId\":" + String(currentScanId);
        tempWifi += "}";
      }
      WiFi.scanDelete();
    }
    tempWifi += "]";

    // 2. BLE scan (skipped while tracking a Wi-Fi target)
    BLEScanResults* results = nullptr;
    String tempBle = "[";
    if (mode != 1) {
      BLEScan* scan = BLEDevice::getScan();
      scan->setActiveScan(true);
      results = scan->start(1, false);
      if (results != nullptr) {
        for (int i = 0; i < results->getCount(); i++) {
          BLEAdvertisedDevice dev = results->getDevice(i);
          if (i) tempBle += ",";
          String name = dev.getName().c_str();
          if (name == "") name = dev.getAddress().toString().c_str();
          tempBle += "{";
          String deviceId = dev.getAddress().toString().c_str();
          tempBle += "\"id\":\"" + deviceId + "\",";
          tempBle += "\"name\":\"" + name + "\",";
          tempBle += "\"rssi\":" + String(dev.getRSSI()) + ",";
          tempBle += "\"scanId\":" + String(currentScanId);
          tempBle += "}";
        }
      }
      scan->clearResults();
    }
    tempBle += "]";

    // Update shared strings safely using mutex
    if (xSemaphoreTake(jsonMutex, portMAX_DELAY) == pdTRUE) {
      if (wifiScanOk) {
        wifiJson = tempWifi;
        completedWifiScanId = currentScanId;
      }
      if (results != nullptr) {
        bleJson = tempBle;
        completedBleScanId = currentScanId;
      }
      xSemaphoreGive(jsonMutex);
    }

    // Full scans every second; target-focused scans run back to back.
    vTaskDelayUntil(&lastCycleStart, pdMS_TO_TICKS(mode == 0 ? 1000 : 250));
  }
}

void handleWifi() {
  String response;
  uint32_t responseScanId = 0;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = wifiJson;
    responseScanId = completedWifiScanId;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "[]";
  }
  server.sendHeader("X-Scan-Id", String(responseScanId));
  server.send(200, "application/json", response);
}

void handleBLE() {
  String response;
  uint32_t responseScanId = 0;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = bleJson;
    responseScanId = completedBleScanId;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "[]";
  }
  server.sendHeader("X-Scan-Id", String(responseScanId));
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
  Serial.println("[ESP32 ALERT] Spy Camera trigger POST received! WhatsApp alert simulated.");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Green API message simulated via ESP32 Serial Out.\"}");
}

// GET /track?type=wifi&ch=6  -> scan only that Wi-Fi channel
// GET /track?type=ble        -> scan only Bluetooth
// GET /track?off=1           -> back to full Wi-Fi + BLE scanning
void handleTrack() {
  if (server.hasArg("off")) {
    trackMode = 0;
    trackChannel = 0;
  } else {
    String t = server.arg("type");
    trackMode = (t == "wifi") ? 1 : (t == "ble") ? 2 : 0;
    trackChannel = server.hasArg("ch") ? (uint8_t)server.arg("ch").toInt() : 0;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>AEGIS — Hidden Camera Detector</title>
  <style>
    :root {
      --bg-color: #fff5f8; --card-bg: #ffffff; --border-color: #f2c9d8;
      --text-main: #35212b; --text-muted: #80566a;
      --accent-blue: #d6336c; --accent-green: #098968;
      --sos-red: #e11d48; --sos-hover: #be123c;
    }
    * { box-sizing: border-box; margin: 0; padding: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      -webkit-tap-highlight-color: transparent; }
    body { background-color: var(--bg-color); color: var(--text-main); display: flex;
      flex-direction: column; height: 100dvh; overflow: hidden; }
    .hidden { display: none !important; }

    header { background-color: var(--card-bg); border-bottom: 1px solid var(--border-color);
      padding: 14px 16px; box-shadow: 0 2px 12px rgba(166,27,77,.08); display: flex;
      align-items: center; justify-content: space-between; z-index: 100; flex-shrink: 0; }
    .brand { display: flex; flex-direction: column; }
    .brand h1 { font-size: 1.1rem; font-weight: 700; letter-spacing: .5px; color: #a61b4d; }
    .brand span { font-size: .72rem; color: var(--text-muted); font-weight: 500; }

    .sos-btn { background-color: var(--sos-red); color: #fff; border: none; padding: 8px 16px;
      border-radius: 20px; font-weight: 700; font-size: .85rem; display: flex; align-items: center;
      gap: 6px; cursor: pointer; box-shadow: 0 0 12px rgba(239,68,68,.4);
      transition: transform .1s ease, background-color .2s ease; animation: pulse-red 2s infinite; }
    .sos-btn:active { transform: scale(.95); background-color: var(--sos-hover); }
    @keyframes pulse-red {
      0% { box-shadow: 0 0 0 0 rgba(239,68,68,.5); }
      70% { box-shadow: 0 0 0 8px rgba(239,68,68,0); }
      100% { box-shadow: 0 0 0 0 rgba(239,68,68,0); } }

    .mode-switch-container { padding: 12px 16px; background-color: var(--bg-color); flex-shrink: 0; }
    .mode-switcher { background-color: var(--card-bg); border: 1px solid var(--border-color);
      border-radius: 16px; padding: 4px; display: flex; gap: 4px; }
    .mode-tab { flex: 1; min-height: 46px; padding: 10px 8px; border: none; background: transparent;
      color: var(--text-muted); font-weight: 600; font-size: .85rem; border-radius: 8px; cursor: pointer;
      display: flex; align-items: center; justify-content: center; gap: 6px; transition: all .25s ease; }
    .mode-tab.active { background-color: var(--accent-blue); color: #fff; box-shadow: 0 3px 10px rgba(214,51,108,.25); }

    main { flex: 1; padding: 0 12px 12px; overflow-y: auto; display: flex; flex-direction: column; }
    .view-panel { display: none; flex-direction: column; gap: 12px; }
    .view-panel.active { display: flex; }
    .card { background-color: var(--card-bg); border: 1px solid var(--border-color); border-radius: 18px;
      padding: 14px; box-shadow: 0 4px 14px rgba(166,27,77,.06); }
    .card-title { font-size: .9rem; font-weight: 600; color: var(--text-muted); text-transform: uppercase;
      letter-spacing: .5px; margin-bottom: 12px; }

    .canvas-placeholder { width: 100%; aspect-ratio: 1; max-height: 280px; background-color: #fff0f5;
      border: 1px dashed var(--border-color); border-radius: 12px; display: flex; flex-direction: column;
      align-items: center; justify-content: center; color: var(--text-muted); gap: 8px; margin: 0 auto; }
    .canvas-placeholder.camera-active { max-width: 640px; max-height: none; position: relative; overflow: hidden; }
    #detectionOverlay { display: none; position: absolute; inset: 0; width: 100%; height: 100%; pointer-events: none; }
    .status-badge { display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px;
      background-color: rgba(16,185,129,.15); color: var(--accent-green); border-radius: 20px;
      font-size: .75rem; font-weight: 600; }
    .status-dot { width: 6px; height: 6px; background-color: var(--accent-green); border-radius: 50%; }

    footer { background-color: var(--card-bg); border-top: 1px solid var(--border-color); padding: 10px 16px;
      font-size: .75rem; color: var(--text-muted); display: flex; justify-content: space-between;
      align-items: center; flex-shrink: 0; }
    .log-line { font-family: monospace; color: #80566a; white-space: nowrap; overflow: hidden;
      text-overflow: ellipsis; max-width: 80%; }

    .modal-overlay { position: fixed; inset: 0; background-color: rgba(88,23,50,.35); display: none;
      align-items: center; justify-content: center; z-index: 1000; padding: 20px; }
    .modal-overlay.active { display: flex; }
    .modal-box { background-color: var(--card-bg); border: 1px solid var(--border-color); border-radius: 16px;
      padding: 24px; width: 100%; max-width: 340px; text-align: center; }
    .modal-box h2 { color: var(--sos-red); margin-bottom: 8px; }
    .modal-box p { font-size: .85rem; color: var(--text-muted); margin-bottom: 20px; }
    .modal-actions { display: flex; gap: 10px; }
    .btn { flex: 1; min-height: 46px; padding: 10px 14px; border-radius: 12px; border: none; font-weight: 600; cursor: pointer; }
    .btn-danger { background-color: var(--sos-red); color: #fff; }
    .btn-secondary { background-color: #ffe2eb; color: var(--text-main); }

    .settings-btn { background: none; border: none; font-size: 1.1rem; cursor: pointer; color: var(--text-muted);
      padding: 4px 6px; border-radius: 8px; line-height: 1; }
    .settings-btn:hover { color: var(--text-main); background-color: #ffe2eb; }
    .sheet-overlay { position: fixed; inset: 0; background-color: rgba(88,23,50,.25); z-index: 200; display: none; }
    .sheet-overlay.active { display: block; }
    .bottom-sheet { position: fixed; bottom: 0; left: 0; right: 0; background-color: var(--card-bg);
      border-top: 1px solid var(--border-color); border-radius: 20px 20px 0 0;
      padding: 16px 20px calc(32px + env(safe-area-inset-bottom)); z-index: 300;
      transform: translateY(100%); transition: transform .3s ease; }
    .bottom-sheet.active { transform: translateY(0); }
    .sheet-handle { width: 36px; height: 4px; background-color: #e9afc3; border-radius: 2px; margin: 0 auto 16px; }
    .sheet-title { font-size: .9rem; font-weight: 600; color: var(--text-muted); text-transform: uppercase;
      letter-spacing: .5px; margin-bottom: 16px; }
    .settings-field { margin-bottom: 14px; }
    .settings-field label { display: block; font-size: .78rem; color: var(--text-muted); margin-bottom: 6px; font-weight: 500; }
    .settings-field input { width: 100%; background-color: #fff7fa; border: 1px solid var(--border-color);
      border-radius: 8px; padding: 10px 12px; color: var(--text-main); font-size: .9rem; outline: none; }
    .settings-field input:focus { border-color: var(--accent-blue); }

    .cw-hint { font-size: .82rem; color: var(--text-muted); margin-bottom: 12px; }
    .cw-examples { display: flex; flex-direction: row; flex-wrap: wrap; align-items: baseline; margin-bottom: 12px; }
    .cw-chip { font-size: .78rem; color: var(--text-muted); font-style: italic; }
    .cw-chip:not(:last-child)::after { content: ",\00a0"; }
    .cw-status { display: none; flex-direction: column; gap: 4px; margin-bottom: 14px; }
    .round-status { display: none; font-size: .82rem; color: var(--accent-blue); margin-bottom: 10px;
      padding: 8px 10px; background-color: #fff7fa; border-radius: 8px; border: 1px solid var(--border-color); }
    .setup-row { display: flex; gap: 10px; }
    .mic-btn { background-color: var(--border-color); color: var(--text-muted); border: 1px solid transparent;
      transition: background-color .2s ease, color .2s ease, border-color .2s ease; }
    .mic-btn:disabled { opacity: .4; cursor: not-allowed; }
    .mic-btn.mic-active { background-color: rgba(16,185,129,.15); color: var(--accent-green); border-color: var(--accent-green); }
    #codeWordCard { margin-top: 24px; }

    /* ─── Wireless radar ─── */
    .esp-connect-bar { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
    .esp-ip-input { flex: 1; min-width: 130px; background-color: #fff7fa; border: 1px solid var(--border-color);
      border-radius: 10px; padding: 9px 12px; color: var(--text-main); font-size: .88rem; outline: none; }
    .esp-ip-input:focus { border-color: var(--accent-blue); }
    .btn-connect { background-color: var(--accent-blue); color: #fff; border: none; border-radius: 10px;
      padding: 9px 18px; font-weight: 700; font-size: .85rem; cursor: pointer; white-space: nowrap; }
    .btn-connect:active { transform: scale(.96); }
    .btn-connect.connected { background-color: var(--accent-green); }
    .esp-connected-ip { font-size: .75rem; color: var(--accent-green); font-weight: 600; display: none; margin-top: 6px; }
    .esp-connected-ip.visible { display: flex; gap: 5px; }
    .refresh-indicator { display: flex; align-items: center; gap: 6px; font-size: .72rem; color: var(--text-muted); }
    .refresh-dot { width: 7px; height: 7px; border-radius: 50%; background-color: #ccc; }
    .refresh-dot.pulsing { background-color: var(--accent-green); animation: pulse-green .6s ease; }
    @keyframes pulse-green {
      0% { box-shadow: 0 0 0 0 rgba(9,137,104,.5); }
      70% { box-shadow: 0 0 0 6px rgba(9,137,104,0); }
      100% { box-shadow: 0 0 0 0 rgba(9,137,104,0); } }

    
    /* Signal Strength RSSI Threshold Slider */
    .rssi-filter-wrap { margin-bottom: 10px; padding: 10px 12px; background: #fff7fa;
      border: 1px solid var(--border-color); border-radius: 12px; }
    .rssi-filter-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; }
    .rssi-filter-label { font-size: .78rem; font-weight: 700; color: var(--text-main); }
    .rssi-filter-val { font-size: .75rem; font-weight: 800; color: var(--accent-blue); }
    .rssi-slider { width: 100%; -webkit-appearance: none; height: 6px; border-radius: 3px;
      background: #f2c9d8; outline: none; }
    .rssi-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px;
      border-radius: 50%; background: var(--accent-blue); cursor: pointer;
      box-shadow: 0 1px 4px rgba(214,51,108,.4); }
    .rssi-slider::-moz-range-thumb { width: 18px; height: 18px; border-radius: 50%;
      background: var(--accent-blue); cursor: pointer; border: none; }

    /* Audit Filter Chips */
    .audit-filter-row { display: flex; gap: 6px; align-items: center; margin-bottom: 10px; overflow-x: auto; }
    .audit-chip { padding: 5px 10px; border-radius: 16px; border: 1px solid var(--border-color);
      background: #fff; font-size: .74rem; font-weight: 600; color: var(--text-muted); cursor: pointer; white-space: nowrap; }
    .audit-chip.active { background: var(--text-main); color: #fff; border-color: var(--text-main); }
    .audit-chip-clear { margin-left: auto; background: none; border: none; font-size: .72rem;
      color: var(--text-muted); cursor: pointer; padding: 4px 6px; border-radius: 6px; }
    .audit-chip-clear:hover { color: var(--sos-red); }

    /* Device Status Badges */
    .device-badge { display: inline-block; font-size: .65rem; font-weight: 800; padding: 2px 6px;
      border-radius: 6px; text-transform: uppercase; margin-top: 3px; letter-spacing: .3px; }
    .badge-camera { background: rgba(225,29,72,.15); color: var(--sos-red); border: 1px solid rgba(225,29,72,.3); }
    .badge-safe { background: rgba(9,137,104,.15); color: var(--accent-green); border: 1px solid rgba(9,137,104,.3); }
    .badge-unscanned { background: rgba(0,0,0,.05); color: var(--text-muted); }

    /* Camera Found / Not Found Resolution Buttons */
    .rbtn.cam-found { background-color: var(--sos-red); border-color: var(--sos-red); color: #fff; font-size: .88rem; flex-basis: 48%; }
    .rbtn.cam-safe { background-color: var(--accent-green); border-color: var(--accent-green); color: #fff; font-size: .88rem; flex-basis: 48%; }

    .signal-tabs { display: flex; gap: 6px; margin-bottom: 12px; }
    .signal-tab { flex: 1; padding: 7px 10px; border: 1px solid var(--border-color); border-radius: 10px;
      background: #fff7fa; color: var(--text-muted); font-size: .78rem; font-weight: 600; cursor: pointer; text-align: center; }
    .signal-tab.active { background-color: var(--accent-blue); color: #fff; border-color: var(--accent-blue); }
    .signal-tab-pane { display: none; }
    .signal-tab-pane.active { display: block; }
    .signal-list { display: flex; flex-direction: column; gap: 6px; max-height: 240px; overflow-y: auto; }
    .signal-item { display: flex; align-items: center; gap: 10px; background-color: #fff7fa;
      border: 1px solid var(--border-color); border-radius: 12px; padding: 9px 12px; }
    .signal-icon { font-size: 1.1rem; flex-shrink: 0; }
    .signal-info { flex: 1; min-width: 0; }
    .signal-name { font-size: .85rem; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    .signal-meta { font-size: .72rem; color: var(--text-muted); margin-top: 1px; }
    .rssi-bar-wrap { display: flex; flex-direction: column; align-items: flex-end; gap: 3px; flex-shrink: 0; }
    .rssi-value { font-size: .72rem; font-weight: 700; color: var(--text-muted); }
    .rssi-bar-bg { width: 56px; height: 6px; background-color: #f2c9d8; border-radius: 3px; overflow: hidden; }
    .rssi-bar-fill { height: 100%; border-radius: 3px; }
    .btn-track { background-color: #fff0f5; color: var(--accent-blue); border: 1px solid var(--border-color);
      border-radius: 8px; padding: 6px 12px; font-size: .74rem; font-weight: 700; cursor: pointer; flex-shrink: 0; }
    .btn-track:hover { background-color: var(--accent-blue); color: #fff; }
    .empty-state { text-align: center; padding: 20px 10px; color: var(--text-muted); font-size: .82rem; }
    .empty-state span { display: block; font-size: 1.5rem; margin-bottom: 6px; }

    /* Single big instruction */
    .instr { text-align: center; padding: 6px 4px 12px; }
    .instr-main { font-size: 1.45rem; font-weight: 800; line-height: 1.15; color: var(--text-main); }
    .instr-main.go { color: var(--accent-green); }
    .instr-main.turn { color: var(--accent-blue); }
    .instr-main.warn { color: var(--sos-red); }
    .instr-sub { font-size: .8rem; color: var(--text-muted); margin-top: 5px; min-height: 1.1em; }
    .spinner { width: 26px; height: 26px; margin: 0 auto 8px; border-radius: 50%;
      border: 3px solid var(--border-color); border-top-color: var(--accent-blue);
      animation: spin .8s linear infinite; }
    @keyframes spin { to { transform: rotate(360deg); } }

    .radar-wrap { position: relative; width: 100%; max-width: 320px; margin: 0 auto; }
    #radarCanvas { width: 100%; aspect-ratio: 1; display: block; }
    .radar-actions { display: flex; gap: 8px; margin-top: 12px; flex-wrap: wrap; }
    .rbtn { flex: 1; min-width: 110px; padding: 11px 10px; font-size: .82rem; font-weight: 700; border-radius: 10px;
      border: 1px solid var(--border-color); background: #fff; color: var(--text-main); cursor: pointer; }
    .rbtn:disabled { opacity: .45; cursor: not-allowed; }
    .rbtn.linked { background-color: rgba(16,185,129,.12); border-color: var(--accent-green); color: var(--accent-green); }
    .rbtn.read { background-color: var(--accent-blue); border-color: var(--accent-blue); color: #fff; flex-basis: 100%; font-size: .95rem; }
    .rbtn.wall { background-color: rgba(225,29,72,.1); border-color: var(--sos-red); color: var(--sos-red); }
    .rbtn.drop { color: var(--text-muted); }
    .rbtn.demo { border-style: dashed; color: var(--text-muted); }
    #demoHeadingRow { margin-top: 10px; font-size: .78rem; color: var(--text-muted); }
    #demoHeading { width: 100%; }
    #gyroStatus { margin-top: 8px; font-size: .75rem; color: var(--text-muted); text-align: center; }
    .wall-picker { margin-top: 12px; padding: 12px; background: #fff7fa; border: 1px solid var(--border-color); border-radius: 14px; }
    .wall-title { font-size: .85rem; font-weight: 700; text-align: center; }
    .wall-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin: 12px auto; max-width: 260px; }
    .wallbtn { padding: 13px 0; border-radius: 10px; border: 1px solid var(--border-color); background: #fff;
      font-weight: 700; font-size: .82rem; color: var(--text-main); cursor: pointer; }
    .wallbtn.on { background: rgba(225,29,72,.14); border-color: var(--sos-red); color: var(--sos-red); }
    .wallbtn.facing { box-shadow: inset 0 0 0 2px var(--accent-blue); }
    .wall-center { display: flex; align-items: center; justify-content: center; font-size: .7rem; font-weight: 800; color: var(--text-muted); }

    @media (max-width: 480px) {
      header { padding: 12px 14px; } .brand h1 { font-size: 1rem; }
      .mode-switch-container { padding: 10px 12px; } .mode-tab { font-size: .78rem; }
      .card { padding: 12px; } .canvas-placeholder.camera-active { max-width: 100%; }
      footer { padding: 9px 12px calc(9px + env(safe-area-inset-bottom)); }
    }
  </style>
</head>
<body>

  <header>
    <div class="brand">
      <h1>AEGIS</h1>
      <span>Covert Detector System</span>
    </div>
    <div style="display:flex; align-items:center; gap:10px;">
      <button class="settings-btn" id="settingsBtn" title="Settings">⚙️</button>
      <button class="sos-btn" id="sosBtn">🚨 <span>SOS</span></button>
    </div>
  </header>

  <div class="mode-switch-container">
    <div class="mode-switcher">
      <button class="mode-tab active" id="tabWireless">📡 Wireless Radar</button>
      <button class="mode-tab" id="tabWired">📷 Wired Detector</button>
    </div>
  </div>

  <main>
    <!-- ═════════ Wireless radar ═════════ -->
    <div class="view-panel active" id="panelWireless">

      <div class="card" id="connectCard">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
          <span class="card-title" style="margin-bottom:0;">ESP32 Connection</span>
          <div class="refresh-indicator">
            <div class="refresh-dot" id="refreshDot"></div>
            <span id="refreshLabel">Not connected</span>
          </div>
        </div>
        <div class="esp-connect-bar">
          <input class="esp-ip-input" type="text" id="espIpInput" value="192.168.4.1" placeholder="ESP32 IP" autocomplete="off" spellcheck="false">
          <button class="btn-connect" id="btnConnect">Connect</button>
        </div>
        <div class="esp-connected-ip" id="connectedIpDisplay"><span>✅</span><span id="connectedIpText"></span></div>
      </div>

      <!-- Signal list (hidden while a target is being tracked) -->
      <div class="card" id="signalsCard">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
          <span class="card-title" style="margin-bottom:0;">Pick a device to find</span>
          <span id="totalSignalCount" style="font-size:.75rem;color:var(--text-muted);">0 signals</span>
        </div>

        <!-- 1) Signal Strength RSSI Threshold Horizontal Scroller -->
        <div class="rssi-filter-wrap">
          <div class="rssi-filter-header">
            <span class="rssi-filter-label">⚡ Signal Strength Threshold</span>
            <span class="rssi-filter-val" id="rssiThresholdVal">ALL SIGNALS (-100 dBm)</span>
          </div>
          <input type="range" class="rssi-slider" id="rssiThresholdSlider" min="-100" max="-30" value="-100" step="1">
        </div>

        <!-- 3) Filter By Scanned Status -->
        <div class="audit-filter-row">
          <button class="audit-chip active" id="auditFilterAll">All Signals</button>
          <button class="audit-chip" id="auditFilterScanned">Scanned (<span id="scannedCount">0</span>)</button>
          <button class="audit-chip" id="auditFilterUnscanned">Not Scanned (<span id="unscannedCount">0</span>)</button>
          <button class="audit-chip-clear" id="btnClearHistory" title="Clear saved scan history">🗑️ Clear</button>
        </div>
        <div class="signal-tabs">
          <button class="signal-tab active" id="stWifi">📶 Wi-Fi (<span id="wifiCount">0</span>)</button>
          <button class="signal-tab" id="stBle">🔵 Bluetooth (<span id="bleCount">0</span>)</button>
        </div>
        <div class="signal-tab-pane active" id="paneWifi">
          <div class="signal-list" id="wifiList"><div class="empty-state"><span>📡</span>Connect to ESP32 to scan</div></div>
        </div>
        <div class="signal-tab-pane" id="paneBle">
          <div class="signal-list" id="bleList"><div class="empty-state"><span>🔵</span>Connect to ESP32 to scan</div></div>
        </div>
      </div>

      <!-- Radar + ONE instruction -->
      <div class="card" id="radarCard">
        <div class="instr">
          <div class="spinner hidden" id="spinner"></div>
          <div class="instr-main" id="instrMain">Tap “Track” on a device</div>
          <div class="instr-sub" id="instrSub"></div>
        </div>
        <div class="radar-wrap"><canvas id="radarCanvas" width="320" height="320"></canvas></div>
        <div class="wall-picker hidden" id="wallPicker">
          <div class="wall-title">Which directions have a wall?</div>
          <div class="wall-grid">
            <button class="wallbtn" data-s="7">NW</button>
            <button class="wallbtn" data-s="0">N</button>
            <button class="wallbtn" data-s="1">NE</button>
            <button class="wallbtn" data-s="6">W</button>
            <div class="wall-center">YOU</div>
            <button class="wallbtn" data-s="2">E</button>
            <button class="wallbtn" data-s="5">SW</button>
            <button class="wallbtn" data-s="4">S</button>
            <button class="wallbtn" data-s="3">SE</button>
          </div>
          <button class="rbtn read" id="btnWallsDone" style="width:100%;">✅ Done</button>
        </div>
        <div class="radar-actions">
          <button class="rbtn read hidden" id="btnRead">📍 Read here</button>
          <button class="rbtn" id="btnLinkGyro">🧭 <span id="gyroBtnText">Link Compass</span></button>
          <button class="rbtn wall hidden" id="btnHitWall">🧱 Edit walls</button>
          <!-- 2) Resolved options: Camera Found / No Camera -->
          <button class="rbtn cam-found hidden" id="btnMarkCamera">🚨 Camera Found</button>
          <button class="rbtn cam-safe hidden" id="btnMarkNoCamera">🛡️ No Camera</button>
          <button class="rbtn drop hidden" id="btnDropTarget">✕ Drop</button>
          <button class="rbtn demo" id="btnSimTarget">⚡ Demo</button>
        </div>
        <div id="demoHeadingRow" class="hidden">Demo: drag to turn the phone
          <input type="range" id="demoHeading" min="0" max="359" value="0"></div>
        <div id="gyroStatus" role="status" aria-live="polite">Compass: not linked</div>
      </div>
    </div>

    <!-- ═════════ Wired detector ═════════ -->
    <div class="view-panel" id="panelWired">
      <div class="card">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
          <span class="card-title" style="margin-bottom:0;">Optical IR Scanner</span>
          <span class="status-badge" id="cameraStatus" style="background-color:rgba(214,51,108,.12); color:var(--accent-blue);">
            <span class="status-dot" style="background-color:var(--accent-blue);"></span> Camera Off
          </span>
        </div>
        <div class="canvas-placeholder" id="cameraCanvasContainer">
          <video id="cameraPreview" autoplay muted playsinline
            style="display:none; width:100%; height:100%; object-fit:contain; background-color:#000; border-radius:12px;"></video>
          <canvas id="processingCanvas" width="480" height="360" hidden></canvas>
          <canvas id="detectionOverlay" width="480" height="360"></canvas>
          <div id="cameraPlaceholder" style="display:flex; flex-direction:column; align-items:center; gap:8px;">
            <span style="font-size:2rem;">📷</span>
            <span>IR Lens Blink Detector</span>
            <span style="font-size:.75rem; color:var(--text-muted);">(480×360 Client-Side Computer Vision)</span>
          </div>
        </div>
        <div style="display:flex; gap:10px; margin-top:12px;">
          <button class="btn btn-secondary" id="cameraToggleBtn" style="flex:1;">Turn Camera On</button>
          <button class="btn btn-secondary" id="simulationToggleBtn" style="flex:1;" disabled>Run Demo Blink</button>
        </div>
      </div>
      <div class="card">
        <div class="card-title">IR LED Blink Verification</div>
        <div style="font-size:.85rem; color:var(--text-muted);">
          Blink Match: <strong id="blinkScore" style="color:var(--text-main);">0%</strong>
          <span id="blinkStatus">(No optical target detected)</span>
          <div style="margin-top:6px; font-size:.75rem;">Expected pattern: 500 ms on / 500 ms off (1 Hz)</div>
        </div>
      </div>
    </div>

    <!-- Smart Code Word -->
    <div class="card" id="codeWordCard">
      <div class="card-title">Smart Code Word</div>
      <p class="cw-hint">Set a natural-sounding phrase to silently trigger SOS.</p>
      <div class="cw-examples" id="cwExamples">
        <span class="cw-chip">"Where did I put my blue bag?"</span>
        <span class="cw-chip">"I need to call mom"</span>
        <span class="cw-chip">"What time is our flight?"</span>
        <span class="cw-chip">"I think I left my keys in the car."</span>
      </div>
      <div class="cw-status" id="cwStatus">
        <span id="cwStatusText" style="font-size:.85rem; color:var(--accent-green); font-weight:600;">✅ Code Word Set</span>
        <span id="cwWordDisplay" style="font-size:.75rem; color:var(--text-muted);"></span>
      </div>
      <div class="round-status" id="roundStatus"></div>
      <button class="btn btn-secondary" id="roundRecordBtn" style="display:none; width:100%; margin-bottom:10px;"></button>
      <div class="setup-row">
        <button class="btn btn-secondary" id="cwSetBtn">Set Code Word</button>
        <button class="btn mic-btn" id="micBtn" disabled>🎙 Mic OFF</button>
      </div>
    </div>
  </main>

  <footer>
    <div class="log-line" id="systemLog">SYS: AEGIS App initialized. Mode: Wireless Radar.</div>
    <span style="font-size:.7rem; color:var(--text-muted);">v1.3</span>
  </footer>

  <div class="modal-overlay" id="sosModal">
    <div class="modal-box">
      <h2>🚨 Trigger Emergency SOS?</h2>
      <p>This will fetch your live GPS location and send an immediate WhatsApp alert to emergency contacts.</p>
      <div class="modal-actions">
        <button class="btn btn-secondary" id="sosCancel">Cancel</button>
        <button class="btn btn-danger" id="sosConfirm">Send Alert</button>
      </div>
    </div>
  </div>

  <div class="sheet-overlay" id="sheetOverlay"></div>
  <div class="bottom-sheet" id="settingsSheet">
    <div class="sheet-handle"></div>
    <div class="sheet-title">Emergency Profile</div>
    <div class="settings-field">
      <label for="inputName">Your Name</label>
      <input type="text" id="inputName" placeholder="e.g. Priya Sharma" autocomplete="off">
    </div>
    <div class="settings-field">
      <label for="inputContact">Emergency Contact</label>
      <input type="tel" id="inputContact" value="+917406531222" readonly style="opacity:.6; cursor:not-allowed; background:#fff7fa;">
      <small style="color:var(--text-muted); font-size:.72rem;">Pre-configured. Cannot be changed.</small>
    </div>
    <button class="btn btn-danger" id="saveSettings" style="width:100%; margin-top:8px;">Save</button>
  </div>

  <script>
    // ─── Green API Credentials ────────────────────────────────────────────────
    const GREEN_API_INSTANCE = '710722748361';
    const GREEN_API_TOKEN    = '303c9adf18e54cb3bce3236cec65758a6c457dacf91246a89e';

    const KEY_NAME     = 'aegis_user_name';
    const KEY_CONTACT  = 'aegis_emergency_contact';
    const KEY_CODEWORD = 'aegis_code_word';

    const systemLog = document.getElementById('systemLog');
    function logMessage(msg) {
      const time = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
      systemLog.textContent = `[${time}] ${msg}`;
    }
    // Escape anything that came from the air (SSIDs / BLE names) before using innerHTML.
    const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
      c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

    // ─── Tab Switching + Wired detector ───────────────────────────────────────
    const tabWireless   = document.getElementById('tabWireless');
    const tabWired      = document.getElementById('tabWired');
    const panelWireless = document.getElementById('panelWireless');
    const panelWired    = document.getElementById('panelWired');
    const cameraCanvasContainer = document.getElementById('cameraCanvasContainer');
    const cameraPreview = document.getElementById('cameraPreview');
    const cameraPlaceholder = document.getElementById('cameraPlaceholder');
    const cameraToggleBtn = document.getElementById('cameraToggleBtn');
    const simulationToggleBtn = document.getElementById('simulationToggleBtn');
    const cameraStatus = document.getElementById('cameraStatus');
    const processingCanvas = document.getElementById('processingCanvas');
    const processingContext = processingCanvas.getContext('2d', { willReadFrequently: true });
    const detectionOverlay = document.getElementById('detectionOverlay');
    const overlayContext = detectionOverlay.getContext('2d');
    const blinkScore = document.getElementById('blinkScore');
    const blinkStatus = document.getElementById('blinkStatus');
    let cameraStream = null;
    let processingFrameId = null;
    let simulationEnabled = false;

    const setCameraStatus = (isOn) => {
      cameraStatus.innerHTML = isOn
        ? '<span class="status-dot"></span> Camera On'
        : '<span class="status-dot" style="background-color: var(--accent-blue);"></span> Camera Off';
      cameraStatus.style.backgroundColor = isOn
        ? 'rgba(16, 185, 129, 0.15)'
        : 'rgba(214, 51, 108, 0.12)';
      cameraStatus.style.color = isOn ? 'var(--accent-green)' : 'var(--accent-blue)';
    };

    // ═══ Optical IR & Blinking Spy Camera Detector (From Hackathon Materials) ═══
    const config = {
      hueCenter: 300,
      hueTolerance: 45,
      minSaturation: 0.15,
      minLightness: 0.20,
      blinkRateTarget: 500, // 500 ms target
      sensitivity: 6
    };

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

    const rgbToHsl = (r, g, b) => {
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
    };

    const findBlobs = (pixels) => {
      if (pixels.length === 0) return [];
      
      const visited = new Set();
      const blobs = [];
      const pixelGrid = {};
      pixels.forEach(p => {
        pixelGrid[`${p.x},${p.y}`] = p;
      });

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
    };

    const trackBlobs = (blobs, now, viewW, viewH) => {
      const scaleX = viewW / GRID_W;
      const scaleY = viewH / GRID_H;

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
          isCamera: false
        };
        trackedTargets.push(newTarget);
      });

      trackedTargets = trackedTargets.filter(t => now - t.lastSeen < 1500);

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
    };

    const analyzeBlinkFrequency = (target, now) => {
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
    };

    const verifyTransitionsConsistency = (target) => {
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
    };

    const drawHud = (now, viewW, viewH) => {
      overlayContext.clearRect(0, 0, viewW, viewH);

      trackedTargets.forEach(target => {
        if (target.threatScore < 30) return;

        const x = Math.max(0, target.x);
        const y = Math.max(0, target.y);
        const w = Math.min(viewW - x, target.w);
        const h = Math.min(viewH - y, target.h);

        const isPrimary = (target.id === primaryTargetId);
        const isConfirmed = target.threatScore >= 75;
        const hudColor = isConfirmed ? '#ef4444' : '#f59e0b';
        const labelText = isConfirmed 
          ? `⚠️ CAMERA DETECTED (${Math.round(target.threatScore)}%)`
          : `ANALYZING BLINK (${Math.round(target.threatScore)}%)`;

        overlayContext.strokeStyle = hudColor;
        overlayContext.lineWidth = isConfirmed ? 3 : 2;
        overlayContext.setLineDash(isConfirmed ? [] : [4, 4]);
        overlayContext.strokeRect(x, y, w, h);
        overlayContext.setLineDash([]);
        
        if (isPrimary && isConfirmed) {
          const bracketSize = Math.max(10, Math.min(w * 0.25, 20));
          overlayContext.lineWidth = 4;
          
          overlayContext.beginPath();
          overlayContext.moveTo(x + bracketSize, y); overlayContext.lineTo(x, y); overlayContext.lineTo(x, y + bracketSize);
          overlayContext.stroke();

          overlayContext.beginPath();
          overlayContext.moveTo(x + w - bracketSize, y); overlayContext.lineTo(x + w, y); overlayContext.lineTo(x + w, y + bracketSize);
          overlayContext.stroke();

          overlayContext.beginPath();
          overlayContext.moveTo(x + bracketSize, y + h); overlayContext.lineTo(x, y + h); overlayContext.lineTo(x, y + bracketSize);
          overlayContext.stroke();

          overlayContext.beginPath();
          overlayContext.moveTo(x + w - bracketSize, y + h); overlayContext.lineTo(x + w, y + h); overlayContext.lineTo(x + w, y + h - bracketSize);
          overlayContext.stroke();

          overlayContext.strokeStyle = 'rgba(239, 68, 68, 0.25)';
          overlayContext.lineWidth = 1;
          overlayContext.beginPath();
          overlayContext.moveTo(0, target.cy); overlayContext.lineTo(viewW, target.cy);
          overlayContext.stroke();

          overlayContext.beginPath();
          overlayContext.moveTo(target.cx, 0); overlayContext.lineTo(target.cx, viewH);
          overlayContext.stroke();
          
          overlayContext.strokeStyle = hudColor;
          overlayContext.beginPath();
          const ringRad = Math.max(w, h) * (0.8 + Math.sin(now / 100) * 0.1);
          overlayContext.arc(target.cx, target.cy, ringRad, 0, Math.PI * 2);
          overlayContext.stroke();
        }

        overlayContext.fillStyle = hudColor;
        overlayContext.font = 'bold 11px system-ui, -apple-system, sans-serif';
        
        const labelWidth = overlayContext.measureText(labelText).width;
        overlayContext.fillRect(x, Math.max(12, y - 16), labelWidth + 8, 14);
        
        overlayContext.fillStyle = '#ffffff';
        overlayContext.fillText(labelText, x + 4, Math.max(22, y - 6));
      });
    };

    const updateDashboardData = (now) => {
      if (trackedTargets.length === 0) {
        blinkScore.textContent = '0%';
        blinkStatus.textContent = isSimulating ? '(Simulating 500ms spy cam...)' : '(No optical target detected)';
        blinkStatus.style.color = 'var(--text-muted)';
        return;
      }

      const primaryTarget = trackedTargets.find(t => t.id === primaryTargetId) || trackedTargets[0];
      const threat = Math.round(primaryTarget.threatScore);
      blinkScore.textContent = `${threat}%`;

      if (threat >= 75) {
        blinkStatus.textContent = `🚨 COVERT CAMERA CONFIRMED! 500ms rhythmic blink locked`;
        blinkStatus.style.color = '#ef4444';
      } else if (threat >= 40) {
        blinkStatus.textContent = `⚠️ Strobing optical signature detected — analyzing 500ms rhythm (${threat}%)...`;
        blinkStatus.style.color = 'var(--accent-blue)';
      } else {
        blinkStatus.textContent = `Target under observation (${threat}%) — monitoring stability...`;
        blinkStatus.style.color = 'var(--text-muted)';
      }
    };

    const processFrame = () => {
      if (!cameraStream) return;

      const now = Date.now();

      // Synchronize overlay canvas pixel dimensions with display container
      const containerRect = cameraCanvasContainer.getBoundingClientRect();
      const containerW = Math.round(containerRect.width);
      const containerH = Math.round(containerRect.height);
      if (containerW > 0 && containerH > 0) {
        if (detectionOverlay.width !== containerW || detectionOverlay.height !== containerH) {
          detectionOverlay.width = containerW;
          detectionOverlay.height = containerH;
        }
      }

      // Draw camera preview onto 320x240 processing canvas
      processingContext.drawImage(cameraPreview, 0, 0, GRID_W, GRID_H);

      // Inject simulated 500ms blinking IR target when simulation is enabled
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
          processingContext.fillStyle = 'rgba(255, 30, 220, 1.0)';
          processingContext.beginPath();
          processingContext.arc(simX, simY, 3, 0, Math.PI * 2);
          processingContext.fill();
        }
      }

      const imgData = processingContext.getImageData(0, 0, GRID_W, GRID_H);
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
      trackBlobs(blobs, now, detectionOverlay.width, detectionOverlay.height);
      drawHud(now, detectionOverlay.width, detectionOverlay.height);
      updateDashboardData(now);

      processingFrameId = requestAnimationFrame(processFrame);
    };

    const startCamera = async () => {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        logMessage('Camera access is unavailable in this browser.');
        return;
      }

      cameraToggleBtn.disabled = true;
      try {
        cameraStream = await navigator.mediaDevices.getUserMedia({
          video: {
            width: { ideal: 640 },
            height: { ideal: 480 },
            facingMode: { ideal: 'environment' }
          },
          audio: false
        });
        cameraPreview.srcObject = cameraStream;
        await cameraPreview.play();
        cameraPreview.style.display = 'block';
        cameraPlaceholder.style.display = 'none';
        cameraCanvasContainer.classList.add('camera-active');
        detectionOverlay.style.display = 'block';
        simulationToggleBtn.disabled = false;
        cameraToggleBtn.textContent = 'Turn Camera Off';
        setCameraStatus(true);
        logMessage('Camera feed active. Point camera at hidden locations.');
        processingFrameId = requestAnimationFrame(processFrame);
      } catch (error) {
        const message = error.name === 'NotAllowedError'
          ? 'Camera permission denied. Allow camera access and try again.'
          : 'Camera could not start. Check that no other app is using it.';
        logMessage(message);
      } finally {
        cameraToggleBtn.disabled = false;
      }
    };

    const stopCamera = () => {
      if (cameraStream) {
        cameraStream.getTracks().forEach(track => track.stop());
        cameraStream = null;
      }
      if (processingFrameId) {
        cancelAnimationFrame(processingFrameId);
        processingFrameId = null;
      }
      cameraPreview.srcObject = null;
      cameraPreview.style.display = 'none';
      cameraPlaceholder.style.display = 'flex';
      cameraCanvasContainer.classList.remove('camera-active');
      isSimulating = false;
      simulationToggleBtn.disabled = true;
      simulationToggleBtn.textContent = 'Run Demo Blink';
      detectionOverlay.style.display = 'none';
      overlayContext.clearRect(0, 0, detectionOverlay.width, detectionOverlay.height);
      trackedTargets = [];
      primaryTargetId = null;
      blinkScore.textContent = '0%';
      blinkStatus.textContent = '(No optical target detected)';
      blinkStatus.style.color = 'var(--text-muted)';
      cameraToggleBtn.textContent = 'Turn Camera On';
      setCameraStatus(false);
    };

    cameraToggleBtn.addEventListener('click', () => {
      if (cameraStream) {
        stopCamera();
        logMessage('Camera stopped.');
      } else {
        startCamera();
      }
    });

    simulationToggleBtn.addEventListener('click', () => {
      isSimulating = !isSimulating;
      trackedTargets = [];
      primaryTargetId = null;
      blinkScore.textContent = '0%';
      simulationToggleBtn.textContent = isSimulating ? 'Stop Demo Blink' : 'Run Demo Blink';
      logMessage(isSimulating ? 'Demo IR blink simulation started.' : 'Demo IR blink simulation stopped.');
    });

        tabWireless.addEventListener('click', () => {
      stopCamera();
      tabWireless.classList.add('active');
      tabWired.classList.remove('active');
      panelWireless.classList.add('active');
      panelWired.classList.remove('active');
      logMessage('Switched to Mode A: Wireless Camera Radar.');
    });

    tabWired.addEventListener('click', () => {
      tabWired.classList.add('active');
      tabWireless.classList.remove('active');
      panelWired.classList.add('active');
      panelWireless.classList.remove('active');
      logMessage('Switched to Mode B: Wired IR Camera Detector.');
    });

    window.addEventListener('beforeunload', stopCamera);

    // ─── Settings / Emergency Profile ─────────────────────────────────────────
    const settingsBtn   = document.getElementById('settingsBtn');
    const sheetOverlay  = document.getElementById('sheetOverlay');
    const settingsSheet = document.getElementById('settingsSheet');
    const inputName     = document.getElementById('inputName');
    const inputContact  = document.getElementById('inputContact');
    const saveSettings  = document.getElementById('saveSettings');
    const FIXED_CONTACT = '+917406531222';
    localStorage.setItem(KEY_CONTACT, FIXED_CONTACT);

    function openSettings() {
      inputName.value = localStorage.getItem(KEY_NAME) || '';
      inputContact.value = FIXED_CONTACT;
      settingsSheet.classList.add('active');
      sheetOverlay.classList.add('active');
    }
    function closeSettings() {
      settingsSheet.classList.remove('active');
      sheetOverlay.classList.remove('active');
    }
    settingsBtn.addEventListener('click', openSettings);
    sheetOverlay.addEventListener('click', closeSettings);
    saveSettings.addEventListener('click', () => {
      const name = inputName.value.trim();
      if (!name) { logMessage('⚠️ Please enter your name.'); return; }
      localStorage.setItem(KEY_NAME, name);
      localStorage.setItem(KEY_CONTACT, FIXED_CONTACT);
      closeSettings();
      logMessage('✅ Emergency profile saved. Requesting location permission...');
      if ('geolocation' in navigator) {
        navigator.geolocation.getCurrentPosition(
          () => logMessage('✅ Location permission granted. SOS ready.'),
          () => logMessage('⚠️ Location permission denied. SOS will send without GPS.'),
          { timeout: 10000, maximumAge: 60000 });
      }
    });

    // ─── SOS Alert ────────────────────────────────────────────────────────────
    const sosBtn     = document.getElementById('sosBtn');
    const sosModal   = document.getElementById('sosModal');
    const sosCancel  = document.getElementById('sosCancel');
    const sosConfirm = document.getElementById('sosConfirm');
    let lastSosSentAt = 0;

    function sendSOSAlert(lat, lon) {
      const now = Date.now();
      if (now - lastSosSentAt < 30000) return;
      lastSosSentAt = now;
      const name    = localStorage.getItem(KEY_NAME)    || 'Unknown User';
      const contact = localStorage.getItem(KEY_CONTACT) || '';
      if (!contact) { logMessage('⚠️ SOS: No emergency contact set. Tap ⚙️ to add one.'); return; }
      const locationStr = (lat !== null && lon !== null) ? `https://maps.google.com/?q=${lat},${lon}` : 'Unavailable';
      const message = `🚨 EMERGENCY: ${name} has triggered an SOS alert. Location: ${locationStr}`;
      const chatId = contact.replace(/\D/g, '') + '@c.us';
      fetch(`https://api.green-api.com/waInstance${GREEN_API_INSTANCE}/sendMessage/${GREEN_API_TOKEN}`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ chatId, message })
      })
        .then(res => res.json())
        .then(() => logMessage('🚨 SOS alert sent via WhatsApp.'))
        .catch(() => logMessage('⚠️ SOS: WhatsApp delivery failed. Check connectivity.'));
    }

    function triggerSOSWithLocation() {
      logMessage('🚨 SOS Triggered! Fetching GPS coordinates...');
      if ('geolocation' in navigator) {
        navigator.geolocation.getCurrentPosition(
          (pos) => { logMessage('🚨 GPS acquired. Sending WhatsApp alert...'); sendSOSAlert(pos.coords.latitude, pos.coords.longitude); },
          () => { logMessage('🚨 GPS denied. Sending alert without location...'); sendSOSAlert(null, null); },
          { timeout: 5000 });
      } else sendSOSAlert(null, null);
    }

    sosBtn.addEventListener('click', () => sosModal.classList.add('active'));
    sosCancel.addEventListener('click', () => sosModal.classList.remove('active'));
    sosConfirm.addEventListener('click', () => {
      sosModal.classList.remove('active');
      lastSosSentAt = 0;
      triggerSOSWithLocation();
    });

    // ─── Smart Code Word ──────────────────────────────────────────────────────
    const cwSetBtn       = document.getElementById('cwSetBtn');
    const micBtn         = document.getElementById('micBtn');
    const cwExamples     = document.getElementById('cwExamples');
    const cwStatus       = document.getElementById('cwStatus');
    const cwStatusText   = document.getElementById('cwStatusText');
    const cwWordDisplay  = document.getElementById('cwWordDisplay');
    const roundStatus    = document.getElementById('roundStatus');
    const roundRecordBtn = document.getElementById('roundRecordBtn');
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    let isCapturing = false, captureRound = 0, capturedPhrases = [];

    function refreshCodeWordUI() {
      const stored = localStorage.getItem(KEY_CODEWORD);
      if (stored) {
        cwExamples.style.display = 'none';
        cwStatus.style.display = 'flex';
        cwStatusText.textContent = '✅ Code Word Set';
        cwWordDisplay.textContent = `"${stored}"`;
        cwSetBtn.textContent = 'Change Code Word';
        micBtn.disabled = false;
      } else {
        cwExamples.style.display = 'flex';
        cwStatus.style.display = 'none';
        cwSetBtn.textContent = 'Set Code Word';
        micBtn.disabled = true;
      }
    }

    function getBestPhrase(phrases) {
      const freq = {};
      phrases.forEach(p => { freq[p] = (freq[p] || 0) + 1; });
      return Object.entries(freq).sort((a, b) => b[1] - a[1] || b[0].length - a[0].length)[0][0];
    }

    function startCaptureRound() {
      if (!SpeechRecognition) {
        logMessage('⚠️ Voice features require Chrome or Edge. Firefox does not support Web Speech API.');
        cwSetBtn.disabled = false; isCapturing = false;
        return;
      }
      if (captureRound >= 3) {
        const best = getBestPhrase(capturedPhrases);
        roundStatus.style.display = 'block';
        roundStatus.textContent = `Best match: "${best}"`;
        roundRecordBtn.textContent = '✅ Use this phrase';
        roundRecordBtn.style.display = 'block';
        roundRecordBtn.disabled = false;
        roundRecordBtn.onclick = () => {
          localStorage.setItem(KEY_CODEWORD, best);
          isCapturing = false; captureRound = 0; capturedPhrases = [];
          roundStatus.style.display = 'none';
          roundRecordBtn.style.display = 'none';
          roundRecordBtn.onclick = null;
          cwSetBtn.disabled = false;
          refreshCodeWordUI();
          logMessage('✅ Code word saved.');
        };
        return;
      }
      captureRound++;
      roundRecordBtn.textContent = `🎙 Record Round ${captureRound} of 3`;
      roundRecordBtn.style.display = 'block';
      roundRecordBtn.disabled = false;
      roundRecordBtn.onclick = () => {
        roundRecordBtn.disabled = true;
        roundRecordBtn.textContent = '⏺ Listening...';
        roundStatus.style.display = 'block';
        roundStatus.textContent = `Round ${captureRound} of 3 — say your phrase now...`;
        const rec = new SpeechRecognition();
        rec.lang = 'en-US'; rec.interimResults = false; rec.maxAlternatives = 1;
        rec.onresult = (e) => {
          const phrase = e.results[0][0].transcript.trim();
          capturedPhrases.push(phrase);
          roundStatus.textContent = `✓ Round ${captureRound} of 3: "${phrase}"`;
          roundRecordBtn.style.display = 'none';
          startCaptureRound();
        };
        rec.onerror = () => {
          roundStatus.textContent = `⚠️ Could not hear — tap to try again.`;
          captureRound--;
          startCaptureRound();
        };
        rec.start();
      };
    }

    cwSetBtn.addEventListener('click', () => {
      if (isCapturing) return;
      if (isMicOn) stopContinuousListening();
      isCapturing = true; captureRound = 0; capturedPhrases = [];
      cwSetBtn.disabled = true;
      roundStatus.style.display = 'none';
      roundRecordBtn.style.display = 'none';
      roundRecordBtn.onclick = null;
      startCaptureRound();
    });

    // ─── Continuous Voice Trigger ─────────────────────────────────────────────
    let isMicOn = false;
    let continuousRec = null;
    const IS_FILE_PROTOCOL = location.protocol === 'file:';

    function startContinuousListening() {
      if (!SpeechRecognition) {
        logMessage('⚠️ Voice features require Chrome or Edge. Firefox does not support Web Speech API.');
        isMicOn = false; micBtn.textContent = '🎙 Mic OFF'; micBtn.classList.remove('mic-active');
        return;
      }
      continuousRec = new SpeechRecognition();
      continuousRec.lang = 'en-US';
      continuousRec.continuous = true;
      continuousRec.interimResults = true;
      continuousRec.onresult = (e) => {
        const codeWord = localStorage.getItem(KEY_CODEWORD);
        if (!codeWord) return;
        const codeWordLower = codeWord.toLowerCase();
        for (let i = e.resultIndex; i < e.results.length; i++) {
          const transcript = e.results[i][0].transcript.toLowerCase();
          if (transcript.includes(codeWordLower)) {
            logMessage('🔑 Code word detected — silent SOS triggered.');
            if ('geolocation' in navigator) {
              navigator.geolocation.getCurrentPosition(
                (pos) => sendSOSAlert(pos.coords.latitude, pos.coords.longitude),
                () => sendSOSAlert(null, null), { timeout: 5000 });
            } else sendSOSAlert(null, null);
            break;
          }
        }
      };
      continuousRec.onerror = (e) => {
        if (e.error === 'not-allowed') {
          logMessage('MIC: Permission denied. Enable microphone access and try again.');
          isMicOn = false; micBtn.textContent = '🎙 Mic OFF'; micBtn.classList.remove('mic-active');
        }
      };
      if (!IS_FILE_PROTOCOL) {
        continuousRec.onend = () => { if (isMicOn) continuousRec.start(); };
      } else {
        continuousRec.onend = () => {
          if (isMicOn) {
            isMicOn = false; micBtn.textContent = '🎙 Mic OFF'; micBtn.classList.remove('mic-active');
            logMessage('MIC: Paused after silence. Serve over HTTP for continuous listening.');
          }
        };
      }
      continuousRec.start();
      logMessage('MIC: Listening for code word...');
    }

    function stopContinuousListening() {
      if (continuousRec) { continuousRec.onend = null; continuousRec.stop(); continuousRec = null; }
      logMessage('MIC: Stopped.');
    }

    micBtn.addEventListener('click', () => {
      if (isMicOn) {
        isMicOn = false; micBtn.textContent = '🎙 Mic OFF'; micBtn.classList.remove('mic-active');
        stopContinuousListening();
      } else {
        isMicOn = true; micBtn.textContent = '🎙 Mic ON'; micBtn.classList.add('mic-active');
        startContinuousListening();
      }
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // ─── WIRELESS RADAR (step-by-step) ────────────────────────────────────────
    //
    // Workflow:
    //   1. Tap Track  -> app reads the target 2 times (baseline) and averages.
    //   2. App says "Walk N steps <direction>". No readings are taken while you walk.
    //   3. You arrive and tap "Read here" -> 2 fresh readings, averaged (spinner shown).
    //   4. delta = newAvg - previousAvg (dBm, closer to 0 = stronger).
    //      The sign decides whether the walked sector (and neighbors) gain or lose score,
    //      the size of delta decides how much. The new average becomes the next baseline.
    //   5. At each spot, first mark which directions have walls. They are ignored for that move only
    //      (no score penalty); the best remaining sector is recommended. Then repeat from 2.
    // The compass only orients the sectors; it cannot detect walking or distance.

    const $ = id => document.getElementById(id);
    const btnConnect = $('btnConnect'), espIpInput = $('espIpInput');
    const connectedIpDisplay = $('connectedIpDisplay'), connectedIpText = $('connectedIpText');
    const refreshDot = $('refreshDot'), refreshLabel = $('refreshLabel');
    const wifiList = $('wifiList'), bleList = $('bleList');
    const wifiCount = $('wifiCount'), bleCount = $('bleCount'), totalSignalCount = $('totalSignalCount');
    const radarCanvas = $('radarCanvas'), rctx = radarCanvas.getContext('2d');
    const btnLinkGyro = $('btnLinkGyro'), gyroBtnText = $('gyroBtnText'), gyroStatusText = $('gyroStatus');
    const btnHitWall = $('btnHitWall'), btnRead = $('btnRead'), btnDropTarget = $('btnDropTarget');
    const btnMarkCamera = $('btnMarkCamera'), btnMarkNoCamera = $('btnMarkNoCamera');

    /*
     * ══════════════════════════════════════════════════════════════════════════
     * [FUTURE MOBILE APP PERSISTENCE & CLOUD AUDIT ROADMAP]
     * In this web/ESP32 interface, inspected networks are saved in memory and
     * local browser storage (localStorage).
     *
     * WHEN WE DEVELOP THE PRODUCTION MOBILE APP (Flutter / React Native):
     * 1. On-Device SQLite Database: Store persistent logs of every inspected
     *    BSSID, Bluetooth MAC address, signal attenuation, and physical room tag.
     * 2. Audit Trail & Certification: Allow users to generate a "SheSafe Clearance Report"
     *    PDF certifying the hotel room / changing room was swept and cleared.
     * 3. Threat Cloud Sync: Confirmed spy camera MACs/BSSIDs will sync to the SheSafe
     *    threat cloud database to instantly flag known hidden cameras for other users.
     * 4. Multi-Room Management: Tag devices by room (e.g., "Bedroom", "Bathroom",
     *    "Wardrobe") to maintain separate device profiles per location.
     * ══════════════════════════════════════════════════════════════════════════
     */
    const SCANNED_STORAGE_KEY = 'shesafe_scanned_networks';
    let scannedHistory = {};
    try {
      const stored = localStorage.getItem(SCANNED_STORAGE_KEY);
      if (stored) scannedHistory = JSON.parse(stored);
    } catch (_) { scannedHistory = {}; }

    const saveScannedHistory = () => {
      try { localStorage.setItem(SCANNED_STORAGE_KEY, JSON.stringify(scannedHistory)); } catch (_) {}
      updateAuditFilterCounts();
    };

    let rssiThreshold = -100;
    let auditFilter = 'all'; // 'all', 'scanned', 'unscanned'

    const updateAuditFilterCounts = () => {
      const allSignals = [...(lastWifiData || []), ...(lastBleData || [])];
      let scCount = 0;
      let unCount = 0;
      const seenKeys = new Set();
      allSignals.forEach(s => {
        const key = String(s.bssid || s.id || '').toLowerCase();
        if (!key || seenKeys.has(key)) return;
        seenKeys.add(key);
        if (scannedHistory[key]) scCount++;
        else unCount++;
      });
      if ($('scannedCount')) $('scannedCount').textContent = scCount;
      if ($('unscannedCount')) $('unscannedCount').textContent = unCount;
    };
    const btnSimTarget = $('btnSimTarget');
    const wallPicker = $('wallPicker'), btnWallsDone = $('btnWallsDone');
    const wallBtns = Array.from(document.querySelectorAll('.wallbtn'));
    const instrMain = $('instrMain'), instrSub = $('instrSub'), spinnerEl = $('spinner');
    const connectCard = $('connectCard'), signalsCard = $('signalsCard');
    const demoHeadingRow = $('demoHeadingRow'), demoHeading = $('demoHeading');

    $('stWifi').addEventListener('click', () => {
      $('stWifi').classList.add('active'); $('stBle').classList.remove('active');
      $('paneWifi').classList.add('active'); $('paneBle').classList.remove('active');
    });
    $('stBle').addEventListener('click', () => {
      $('stBle').classList.add('active'); $('stWifi').classList.remove('active');
      $('paneBle').classList.add('active'); $('paneWifi').classList.remove('active');
    });

    // ─── Tunables ─────────────────────────────────────────────────────────────
    const NUM = 8;
    const NAMES = ['North', 'North-East', 'East', 'South-East', 'South', 'South-West', 'West', 'North-West'];
    const STEPS_PER_MOVE = 5;        // how many steps the user is asked to walk each round
    const READ_COUNT = 2;            // fresh readings averaged per point
    const RSSI_DEADBAND_DB = 2;      // changes smaller than this are treated as noise (scaled down)
    const SCORE_PER_DB = 3;          // score points per dB of change
    const MAX_SCORE_CHANGE = 24;     // cap on a single update
    const NEIGHBOR_SHARE = 0.4;      // share of a score change also given to each adjacent sector
    const RSSI_SAMPLE_MIN = -110;
    const RSSI_SAMPLE_MAX = 0;
    const POLL_LIST_MS = 1000;       // refresh rate of the device list
    const POLL_TRACK_MS = 350;       // refresh rate while tracking one device

    let espBaseUrl = '', isConnected = false, scanInterval = null, scanInFlight = false;
    let lastWifiData = [], lastBleData = [];
    let lastListFetch = 0;
    let demoSimActive = false, demoTimer = null;

    let heading = 0;
    let gyroOk = false, absSeen = false, gyroListenersAttached = false, gyroCheckTimer = null;

    let target = null;            // { type, id, name }; id is Wi-Fi BSSID or Bluetooth MAC.
    let phase = 'IDLE';           // IDLE | READING | WALLS | MOVE
    let readingPurpose = 'baseline'; // 'baseline' | 'measure'
    let sectorScores = Array(NUM).fill(50);
    let testedSectors = Array(NUM).fill(false);
    let walls = new Array(NUM).fill(false);
    let recommendedSector = null;
    let bestBearing = null;
    let missCount = 0, wallMsgUntil = 0;
    let lastProcessedScanId = null;
    let samples = [];
    let prevAvg = null;           // average RSSI at the previous point
    let movedSector = null;       // sector the user was told to walk toward for this measurement
    let lastNote = '';            // result of the last comparison, shown under the instruction

    // Demo world: fake camera 20 units east of the start; each move = 5 units.
    const DEMO_CAM = { x: 20, y: 0 };
    let demoPos = { x: 0, y: 0 };

    const secOf = h => Math.floor((((h % 360) + 360) % 360 + 22.5) / 45) % NUM;
    const clampScore = score => Math.max(0, Math.min(100, score));
    function resetSectors() {
      sectorScores = Array(NUM).fill(50);
      testedSectors = Array(NUM).fill(false);
      walls = new Array(NUM).fill(false);
      recommendedSector = 0; // Deterministic first guess for a fresh target: North.
      bestBearing = 0;
    }
    resetSectors();

    function rssiToPercent(r) { return Math.max(0, Math.min(100, ((r + 100) / 70) * 100)); }
    function rssiColor(r) { return r >= -55 ? '#098968' : r >= -70 ? '#f59e0b' : '#e11d48'; }

    function setGyroStatus(msg, ok) {
      gyroStatusText.textContent = 'Compass: ' + msg;
      gyroStatusText.style.color = ok ? 'var(--accent-green)' : '#b45309';
    }

    function onOrient(e, isAbs) {
      let h = null;
      if (e.webkitCompassHeading != null) h = e.webkitCompassHeading;       // iOS (clockwise)
      else if (e.alpha != null) h = (360 - e.alpha + 360) % 360;            // Android: alpha is counter-clockwise → flip
      if (h === null) return;
      if (isAbs) absSeen = true; else if (absSeen && e.webkitCompassHeading == null) return;
      if (demoSimActive) return;
      heading = h;
      if (!gyroOk) {
        gyroOk = true;
        clearTimeout(gyroCheckTimer);
        btnLinkGyro.classList.add('linked');
        gyroBtnText.textContent = 'Compass OK';
        logMessage('Compass linked.');
      }
      setGyroStatus(`OK (${Math.round(heading)}°)`, true);
      if (target) render();
    }

    function initSensors() {
      if (!window.isSecureContext) {
        setGyroStatus('blocked — page is not HTTPS. Enable the Chrome flag "Insecure origins treated as secure" for this ESP32 address.');
        gyroBtnText.textContent = 'Compass blocked';
        return;
      }
      if (typeof DeviceOrientationEvent === 'undefined') {
        setGyroStatus('this browser has no orientation sensor.');
        gyroBtnText.textContent = 'Unsupported';
        return;
      }
      setGyroStatus('waiting for sensor…');
      if (!gyroListenersAttached) {
        window.addEventListener('deviceorientationabsolute', e => onOrient(e, true), true);
        window.addEventListener('deviceorientation', e => onOrient(e, false), true);
        gyroListenersAttached = true;
      }
      clearTimeout(gyroCheckTimer);
      gyroCheckTimer = setTimeout(() => {
        if (!gyroOk) { setGyroStatus('no data. Check sensor permission / HTTPS flag.'); gyroBtnText.textContent = 'Not detected'; }
      }, 5000);
      if (typeof DeviceOrientationEvent.requestPermission === 'function') {
        DeviceOrientationEvent.requestPermission().then(p => {
          if (p !== 'granted') { clearTimeout(gyroCheckTimer); setGyroStatus('permission denied.'); }
        }).catch(() => setGyroStatus('permission error.'));
      }
    }
    btnLinkGyro.addEventListener('click', initSensors);

    // ─── Target control ───────────────────────────────────────────────────────
    function setTrackingUI(on) {
      signalsCard.classList.toggle('hidden', on);
      connectCard.classList.toggle('hidden', on);
      btnHitWall.classList.toggle('hidden', !on);
      btnRead.classList.toggle('hidden', !on);
      btnMarkCamera.classList.toggle('hidden', !on);
      btnMarkNoCamera.classList.toggle('hidden', !on);
      btnDropTarget.classList.toggle('hidden', !on);
    }

    function chooseRecommendedSector() {
      let candidates = [];
      for (let i = 0; i < NUM; i++) if (!walls[i]) candidates.push(i);
      if (!candidates.length) {
        // Every direction is marked as a wall: ignore the marks so there is still a direction to try.
        walls = new Array(NUM).fill(false);
        candidates = [0, 1, 2, 3, 4, 5, 6, 7];
        logMessage('All directions were marked as walls; ignoring the marks.');
      }
      let best = candidates[0];
      for (const i of candidates.slice(1)) {
        if (sectorScores[i] > sectorScores[best] + 0.001) best = i;
      }
      // On an exact tie keep the previous recommendation to avoid flip-flopping.
      if (recommendedSector !== null && candidates.includes(recommendedSector) &&
          Math.abs(sectorScores[recommendedSector] - sectorScores[best]) <= 0.001) best = recommendedSector;
      recommendedSector = best;
      bestBearing = best * 45;
    }

    function resetMeasurementHistory() {
      samples = [];
      prevAvg = null;
      movedSector = null;
      lastNote = '';
      missCount = 0;
      lastProcessedScanId = null;
    }

    function startReading(purpose) {
      if (!target || phase === 'READING') return;
      readingPurpose = purpose;
      samples = [];
      missCount = 0;
      phase = 'READING';
      // Only accept scans completed after this moment: ignore the scan already cached.
      const current = target.type === 'wifi' ? lastWifiData : lastBleData;
      const cur = Number(current && current.scanId);
      lastProcessedScanId = Number.isFinite(cur) && cur > 0 ? cur : null;
      if (purpose === 'measure') {
        movedSector = recommendedSector;
        if (demoSimActive && movedSector !== null) moveDemoUser(movedSector);
      }
      logMessage(`Reading ${READ_COUNT} fresh values — hold still.`);
      render();
    }

    // Tell the ESP32 to scan only the selected device (much faster readings).
    function setEspTrack(type, channel) {
      if (!isConnected || !espBaseUrl || demoSimActive) return;
      const q = type ? `type=${type}${channel ? '&ch=' + channel : ''}` : 'off=1';
      fetch(`${espBaseUrl}/track?${q}`, { cache: 'no-store' }).catch(() => {});
    }

    function lockTarget(type, id, name, channel) {
      if (!id) {
        logMessage('Cannot track this device because its stable Wi-Fi/Bluetooth identifier is unavailable.');
        return;
      }
      target = { type, id: String(id), name };
      setEspTrack(type, type === 'wifi' ? (Number(channel) || 0) : 0);
      resetSectors();
      resetMeasurementHistory();
      phase = 'IDLE';
      setTrackingUI(true);
      logMessage(`Tracking only ${name} (${target.id}). Taking the starting reading.`);
      window.scrollTo(0, 0);
      document.querySelector('main').scrollTop = 0;
      startReading('baseline');
    }

    function dropTarget() {
      if (!target) return;
      logMessage(`Stopped tracking ${target.name}. Measurements and wall marks cleared.`);
      setEspTrack(null);
      target = null; phase = 'IDLE'; bestBearing = null;
      resetSectors();
      resetMeasurementHistory();
      setTrackingUI(false);
      renderWifiList(lastWifiData); renderBleList(lastBleData);
      render();
    }
    btnDropTarget.addEventListener('click', dropTarget);

    btnMarkCamera.addEventListener('click', () => {
      if (!target) return;
      const key = String(target.id).toLowerCase();
      scannedHistory[key] = {
        id: target.id,
        name: target.name,
        type: target.type,
        status: 'CAMERA',
        timestamp: Date.now()
      };
      saveScannedHistory();
      logMessage(`🚨 Target confirmed as SPY CAMERA: ${target.name} (${target.id})`);
      alert(`🚨 Camera Confirmed!\n\nDevice: ${target.name} (${target.id})\nSaved in audit history as SPY CAMERA.`);
      dropTarget();
    });

    btnMarkNoCamera.addEventListener('click', () => {
      if (!target) return;
      const key = String(target.id).toLowerCase();
      scannedHistory[key] = {
        id: target.id,
        name: target.name,
        type: target.type,
        status: 'SAFE',
        timestamp: Date.now()
      };
      saveScannedHistory();
      logMessage(`🛡️ Target cleared: ${target.name} (${target.id}) marked as NO CAMERA (Safe).`);
      dropTarget();
    });

    // User arrived at the new spot and pressed Read.
    btnRead.addEventListener('click', () => {
      if (!target || phase !== 'MOVE' || recommendedSector === null) return;
      startReading('measure');
    });

    function addScore(i, v) {
      sectorScores[i] = clampScore(sectorScores[i] + v);
    }

    // delta = newAvg - prevAvg (dBm). Positive = stronger = moved the right way.
    function applyDelta(delta) {
      const s = movedSector;
      if (s === null || s === undefined) return;
      testedSectors[s] = true;
      const abs = Math.abs(delta);
      let mag = Math.min(MAX_SCORE_CHANGE, abs * SCORE_PER_DB);
      if (abs < RSSI_DEADBAND_DB) mag *= 0.5; // inside the noise margin: only a small nudge
      const left = (s + NUM - 1) % NUM, right = (s + 1) % NUM;
      const opp = (s + NUM / 2) % NUM;
      const oppL = (opp + NUM - 1) % NUM, oppR = (opp + 1) % NUM;
      if (delta >= 0) {
        addScore(s, mag);
        addScore(left, mag * NEIGHBOR_SHARE); addScore(right, mag * NEIGHBOR_SHARE);
        addScore(opp, -mag * 0.3);
      } else {
        addScore(s, -mag);
        addScore(left, -mag * NEIGHBOR_SHARE); addScore(right, -mag * NEIGHBOR_SHARE);
        addScore(opp, mag * 0.5);
        addScore(oppL, mag * 0.2); addScore(oppR, mag * 0.2);
      }
    }

    function finishReading() {
      const avg = samples.reduce((a, b) => a + b, 0) / samples.length;
      samples = [];
      if (readingPurpose === 'baseline') {
        prevAvg = avg;
        lastNote = `Starting signal: ${avg.toFixed(1)} dBm.`;
        logMessage(`Baseline recorded: ${avg.toFixed(1)} dBm.`);
      } else {
        const delta = avg - prevAvg;
        applyDelta(delta);
        const word = Math.abs(delta) < RSSI_DEADBAND_DB ? 'barely changed' : (delta > 0 ? 'got stronger' : 'got weaker');
        lastNote = `Signal ${word} (${delta > 0 ? '+' : ''}${delta.toFixed(1)} dB, now ${avg.toFixed(1)} dBm).`;
        logMessage(lastNote);
        prevAvg = avg;
        movedSector = null;
      }
      walls = new Array(NUM).fill(false); // walls apply to one move only; ask again at this new spot
      phase = 'WALLS';
    }

    function onSample(rssi) {
      if (phase !== 'READING') return;
      const value = Number(rssi);
      if (!Number.isFinite(value) || value < RSSI_SAMPLE_MIN || value > RSSI_SAMPLE_MAX) return;
      missCount = 0;
      samples.push(value);
      if (samples.length >= READ_COUNT) finishReading();
      render();
    }

    // Wall selection: asked at every new spot, before the next direction is chosen.
    btnHitWall.addEventListener('click', () => {
      if (!target || phase !== 'MOVE') return;
      phase = 'WALLS';
      render();
    });
    wallBtns.forEach(b => b.addEventListener('click', () => {
      if (!target || phase !== 'WALLS') return;
      const i = Number(b.dataset.s);
      walls[i] = !walls[i];
      render();
    }));
    btnWallsDone.addEventListener('click', () => {
      if (!target || phase !== 'WALLS') return;
      chooseRecommendedSector();
      phase = 'MOVE';
      const blocked = NAMES.filter((n, i) => walls[i]);
      logMessage(blocked.length ? `Ignoring walls in: ${blocked.join(', ')} (this move only).` : 'No walls marked.');
      render();
    });

    function relWord(d) {
      if (d <= 30 || d >= 330) return 'straight ahead';
      if (d < 150) return 'to your right';
      if (d <= 210) return 'behind you';
      return 'to your left';
    }

    function setInstr(main, sub, cls, busy) {
      instrMain.textContent = main;
      instrMain.className = 'instr-main ' + (cls || '');
      instrSub.textContent = sub || '';
      spinnerEl.classList.toggle('hidden', !busy);
    }

    function render() {
      if (!target) {
        setInstr('Tap Track on a device', 'Pick a specific Wi-Fi access point or Bluetooth device from the list above');
        btnHitWall.classList.add('hidden');
        btnRead.classList.add('hidden');
        wallPicker.classList.add('hidden');
        return;
      }
      wallPicker.classList.toggle('hidden', phase !== 'WALLS');
      btnHitWall.classList.remove('hidden');
      btnRead.classList.remove('hidden');
      btnRead.disabled = !(phase === 'MOVE' && gyroOk && recommendedSector !== null);
      btnHitWall.disabled = phase !== 'MOVE';

      if (phase === 'READING') {
        btnRead.textContent = '⏳ Reading…';
        if (missCount >= 3) {
          setInstr('Selected device not seen', `Still waiting for ${target.name} (${target.id}). Other signals are ignored.`, 'warn', true);
        } else {
          const what = readingPurpose === 'baseline' ? 'READING START POINT' : 'READING SIGNAL';
          setInstr(what, `Hold still. Reading ${Math.min(samples.length + 1, READ_COUNT)} of ${READ_COUNT}…`, 'turn', true);
        }
        return;
      }
      btnRead.textContent = '📍 Read here';
      if (phase === 'WALLS') {
        wallBtns.forEach(b => {
          const i = Number(b.dataset.s);
          b.classList.toggle('on', !!walls[i]);
          b.classList.toggle('facing', gyroOk && secOf(heading) === i);
        });
        setInstr('MARK THE WALLS',
          `${lastNote} Tap every direction blocked by a wall (none is fine), then tap Done. They are ignored for this move only.`, 'turn');
        return;
      }
      if (!gyroOk) {
        setInstr('Tap Link Compass', 'Needed to orient the direction sectors. Compass heading does not detect walking.', 'warn');
        return;
      }
      if (recommendedSector === null) {
        setInstr('NO DIRECTION AVAILABLE', 'Tap Edit walls and adjust the marks.', 'warn');
        return;
      }
      const direction = NAMES[recommendedSector];
      const score = Math.round(sectorScores[recommendedSector]);
      const rel = ((recommendedSector * 45 - heading) % 360 + 360) % 360;
      const blocked = NAMES.filter((n, i) => walls[i]);
      const wallNote = blocked.length ? `Ignoring walls: ${blocked.join(', ')}. ` : '';
      const close = prevAvg !== null && prevAvg >= -45 ? ' Very strong signal: the device is very close, search by sight.' : '';
      setInstr(`WALK ${STEPS_PER_MOVE} STEPS ${direction.toUpperCase()}`,
        `${lastNote} ${wallNote}That is ${relWord(rel)}. When you arrive, tap Read here. Direction score ${score}/100.${close}`, 'go');
    }

    // Radar drawing: front of the phone is always UP.
    function drawRadar() {
      const W = radarCanvas.width, H = radarCanvas.height;
      const cx = W / 2, cy = H / 2, maxR = Math.min(cx, cy) - 8;
      const rad = deg => (deg - heading - 90) * Math.PI / 180;     // bearing → canvas angle
      rctx.clearRect(0, 0, W, H);

      rctx.beginPath(); rctx.arc(cx, cy, maxR, 0, Math.PI * 2);
      rctx.fillStyle = '#fff0f5'; rctx.fill();

      if (target) {
        for (let i = 0; i < NUM; i++) {
          const a0 = rad(i * 45 - 22.5), a1 = rad(i * 45 + 22.5);
          rctx.beginPath(); rctx.moveTo(cx, cy); rctx.arc(cx, cy, maxR, a0, a1); rctx.closePath();
          if (walls[i]) rctx.fillStyle = 'rgba(225,29,72,0.40)';
          else {
            const score = sectorScores[i];
            const alpha = 0.08 + (score / 100) * 0.24;
            rctx.fillStyle = score >= 60 ? `rgba(16,185,129,${alpha})` : score >= 45 ? `rgba(245,158,11,${alpha})` : `rgba(214,51,108,${alpha})`;
          }
          rctx.fill();
          if (walls[i]) {
            const am = rad(i * 45);
            rctx.font = '18px system-ui'; rctx.textAlign = 'center'; rctx.textBaseline = 'middle';
            rctx.fillStyle = '#000';
            rctx.fillText('WALL', cx + maxR * 0.78 * Math.cos(am), cy + maxR * 0.78 * Math.sin(am));
          }
        }
      }

      for (let i = 1; i <= 4; i++) {
        rctx.beginPath(); rctx.arc(cx, cy, (maxR / 4) * i, 0, Math.PI * 2);
        rctx.strokeStyle = 'rgba(242,201,216,0.9)'; rctx.lineWidth = 1; rctx.stroke();
      }
      for (let i = 0; i < NUM; i++) {
        const a = rad(i * 45 - 22.5);
        rctx.beginPath(); rctx.moveTo(cx, cy); rctx.lineTo(cx + maxR * Math.cos(a), cy + maxR * Math.sin(a));
        rctx.strokeStyle = 'rgba(242,201,216,0.7)'; rctx.stroke();
      }

      // "FRONT" marker at the top = where the phone points
      rctx.fillStyle = '#35212b';
      rctx.beginPath(); rctx.moveTo(cx, 3); rctx.lineTo(cx - 7, 15); rctx.lineTo(cx + 7, 15); rctx.closePath(); rctx.fill();

      // Arrow shows where to walk next.
      if (target && bestBearing !== null && phase === 'MOVE') {
        const a = rad(bestBearing);
        const color = '#d6336c';
        const len = maxR * 0.8;
        const tx = cx + len * Math.cos(a), ty = cy + len * Math.sin(a);
        rctx.save();
        rctx.strokeStyle = color; rctx.fillStyle = color; rctx.lineWidth = 7; rctx.lineCap = 'round';
        rctx.beginPath(); rctx.moveTo(cx, cy); rctx.lineTo(tx, ty); rctx.stroke();
        const hl = 20;
        rctx.beginPath();
        rctx.moveTo(tx + 6 * Math.cos(a), ty + 6 * Math.sin(a));
        rctx.lineTo(tx - hl * Math.cos(a - 0.5), ty - hl * Math.sin(a - 0.5));
        rctx.lineTo(tx - hl * Math.cos(a + 0.5), ty - hl * Math.sin(a + 0.5));
        rctx.closePath(); rctx.fill();
        rctx.restore();
      }

      // Reading animation: expanding pulse rings
      if (target && phase === 'READING') {
        const t = (performance.now() % 1200) / 1200;
        for (let k = 0; k < 2; k++) {
          const p = (t + k * 0.5) % 1;
          rctx.beginPath(); rctx.arc(cx, cy, 10 + p * (maxR - 10), 0, Math.PI * 2);
          rctx.strokeStyle = `rgba(214,51,108,${0.55 * (1 - p)})`;
          rctx.lineWidth = 3; rctx.stroke();
        }
      }

      // You
      rctx.beginPath(); rctx.arc(cx, cy, 9, 0, Math.PI * 2);
      rctx.fillStyle = '#35212b'; rctx.fill();
      rctx.fillStyle = '#fff'; rctx.font = 'bold 8px system-ui'; rctx.textAlign = 'center'; rctx.textBaseline = 'middle';
      rctx.fillText('YOU', cx, cy);
    }

    function animateRadar() { drawRadar(); requestAnimationFrame(animateRadar); }

    // ─── Demo (fake camera 20 units East; each Read moves you 5 units toward the arrow) ──
    function moveDemoUser(sector) {
      const b = sector * 45 * Math.PI / 180;
      demoPos.x += Math.sin(b) * 5;
      demoPos.y += Math.cos(b) * 5;
    }
    function demoRssi() {
      const d = Math.max(1, Math.hypot(DEMO_CAM.x - demoPos.x, DEMO_CAM.y - demoPos.y));
      const rssi = -30 - 25 * Math.log10(d);
      return Math.max(-100, Math.min(-20, rssi + (Math.random() * 2.4 - 1.2)));
    }

    btnSimTarget.addEventListener('click', () => {
      demoSimActive = !demoSimActive;
      btnSimTarget.textContent = demoSimActive ? '⚡ Stop Demo' : '⚡ Demo';
      demoHeadingRow.classList.toggle('hidden', !demoSimActive);
      if (demoSimActive) {
        gyroOk = true; heading = +demoHeading.value;
        demoPos = { x: 0, y: 0 };
        lastWifiData = [{ ssid: 'SPY_CAM_WIFI', bssid: 'AA:BB:CC:DD:EE:01', rssi: -62, channel: 6 }];
        lastBleData = [];
        demoTimer = setInterval(() => { if (target && phase === 'READING') onSample(demoRssi()); }, 700);
        lockTarget('wifi', 'AA:BB:CC:DD:EE:01', 'SPY_CAM_WIFI');
        logMessage('Demo: a fake camera is East. Follow the arrow, tap Read here, and the fake signal changes as you "walk".');
      } else {
        clearInterval(demoTimer); demoTimer = null;
        gyroOk = false; gyroBtnText.textContent = 'Link Compass'; btnLinkGyro.classList.remove('linked');
        setGyroStatus('not linked');
        dropTarget();
      }
    });
    demoHeading.addEventListener('input', () => { if (demoSimActive) { heading = +demoHeading.value; render(); } });

    // ─── Lists ────────────────────────────────────────────────────────────────
    function makeTrackBtn(type, id, name, channel) {
      const btn = document.createElement('button');
      btn.className = 'btn-track';
      btn.textContent = 'Track';
      btn.addEventListener('click', e => { e.stopPropagation(); lockTarget(type, id, name, channel); });
      return btn;
    }

    const filterSignals = (signals, isWifi) => {
      if (!Array.isArray(signals)) return [];
      return signals.filter(sig => {
        // 1. Signal strength threshold slider filter
        if (sig.rssi < rssiThreshold) return false;

        // 3. Already scanned vs Not scanned filter
        const key = String(isWifi ? sig.bssid : (sig.id || sig.name || '')).toLowerCase();
        const isScanned = Boolean(scannedHistory[key]);
        if (auditFilter === 'scanned') return isScanned;
        if (auditFilter === 'unscanned') return !isScanned;
        return true;
      });
    };

    function renderWifiList(signals) {
      wifiList.innerHTML = '';
      updateAuditFilterCounts();
      if (!signals || !signals.length) {
        wifiList.innerHTML = '<div class="empty-state"><span>📡</span>No Wi-Fi networks found</div>';
        return;
      }
      const filtered = filterSignals(signals, true);
      if (!filtered.length) {
        wifiList.innerHTML = `<div class="empty-state"><span>🔍</span>No signals match threshold (${rssiThreshold > -100 ? '≥ ' + rssiThreshold + ' dBm, ' : ''}${auditFilter === 'all' ? 'no devices' : auditFilter})</div>`;
        return;
      }
      filtered.slice().sort((a, b) => b.rssi - a.rssi).forEach(sig => {
        const key = String(sig.bssid || '').toLowerCase();
        const record = scannedHistory[key];
        let badgeHtml = '';
        let btnText = 'Track';
        if (record) {
          if (record.status === 'CAMERA') {
            badgeHtml = '<span class="device-badge badge-camera">🚨 SPY CAMERA</span>';
            btnText = 'Re-Track';
          } else {
            badgeHtml = '<span class="device-badge badge-safe">🛡️ SAFE (NO CAM)</span>';
            btnText = 'Re-Track';
          }
        } else {
          badgeHtml = '<span class="device-badge badge-unscanned">⏳ Not Scanned</span>';
        }

        const item = document.createElement('div');
        item.className = 'signal-item';
        item.innerHTML = `
          <span class="signal-icon">📶</span>
          <div class="signal-info">
            <div class="signal-name">${esc(sig.ssid) || '(Hidden)'}</div>
            <div class="signal-meta">Ch ${esc(sig.channel)} · ${esc(sig.bssid)}</div>
            <div>${badgeHtml}</div>
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${esc(sig.rssi)} dBm</span>
            <div class="rssi-bar-bg"><div class="rssi-bar-fill" style="width:${rssiToPercent(sig.rssi)}%;background:${rssiColor(sig.rssi)};"></div></div>
          </div>`;
        const btn = makeTrackBtn('wifi', sig.bssid, sig.ssid || sig.bssid, sig.channel);
        btn.textContent = btnText;
        item.appendChild(btn);
        wifiList.appendChild(item);
      });
    }

    function renderBleList(signals) {
      bleList.innerHTML = '';
      updateAuditFilterCounts();
      if (!signals || !signals.length) {
        bleList.innerHTML = '<div class="empty-state"><span>🔵</span>No Bluetooth devices found</div>';
        return;
      }
      const filtered = filterSignals(signals, false);
      if (!filtered.length) {
        bleList.innerHTML = `<div class="empty-state"><span>🔍</span>No Bluetooth signals match threshold (${rssiThreshold > -100 ? '≥ ' + rssiThreshold + ' dBm, ' : ''}${auditFilter === 'all' ? 'no devices' : auditFilter})</div>`;
        return;
      }
      filtered.slice().sort((a, b) => b.rssi - a.rssi).forEach(sig => {
        const key = String(sig.id || sig.name || '').toLowerCase();
        const record = scannedHistory[key];
        let badgeHtml = '';
        let btnText = 'Track';
        if (record) {
          if (record.status === 'CAMERA') {
            badgeHtml = '<span class="device-badge badge-camera">🚨 SPY CAMERA</span>';
            btnText = 'Re-Track';
          } else {
            badgeHtml = '<span class="device-badge badge-safe">🛡️ SAFE (NO CAM)</span>';
            btnText = 'Re-Track';
          }
        } else {
          badgeHtml = '<span class="device-badge badge-unscanned">⏳ Not Scanned</span>';
        }

        const item = document.createElement('div');
        item.className = 'signal-item';
        item.innerHTML = `
          <span class="signal-icon">🔹</span>
          <div class="signal-info">
            <div class="signal-name">${esc(sig.name)}</div>
            <div class="signal-meta">Bluetooth LE - ${sig.id || 'identifier unavailable'}</div>
            <div>${badgeHtml}</div>
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${esc(sig.rssi)} dBm</span>
            <div class="rssi-bar-bg"><div class="rssi-bar-fill" style="width:${rssiToPercent(sig.rssi)}%;background:${rssiColor(sig.rssi)};"></div></div>
          </div>`;
        const btn = makeTrackBtn('ble', sig.id, sig.name || sig.id);
        btn.textContent = btnText;
        item.appendChild(btn);
        bleList.appendChild(item);
      });
    }

    // Slider and audit filter control listeners
    const rssiSlider = $('rssiThresholdSlider');
    const rssiValDisplay = $('rssiThresholdVal');
    if (rssiSlider) {
      rssiSlider.addEventListener('input', () => {
        rssiThreshold = Number(rssiSlider.value);
        if (rssiThreshold <= -100) {
          rssiValDisplay.textContent = 'ALL SIGNALS (-100 dBm)';
        } else {
          rssiValDisplay.textContent = `FILTER: ≥ ${rssiThreshold} dBm`;
        }
        renderWifiList(lastWifiData);
        renderBleList(lastBleData);
      });
    }

    const setAuditFilter = (filter) => {
      auditFilter = filter;
      $('auditFilterAll').classList.toggle('active', filter === 'all');
      $('auditFilterScanned').classList.toggle('active', filter === 'scanned');
      $('auditFilterUnscanned').classList.toggle('active', filter === 'unscanned');
      renderWifiList(lastWifiData);
      renderBleList(lastBleData);
    };

    $('auditFilterAll').addEventListener('click', () => setAuditFilter('all'));
    $('auditFilterScanned').addEventListener('click', () => setAuditFilter('scanned'));
    $('auditFilterUnscanned').addEventListener('click', () => setAuditFilter('unscanned'));

    $('btnClearHistory').addEventListener('click', () => {
      if (confirm('Clear all saved network scan results?')) {
        scannedHistory = {};
        saveScannedHistory();
        logMessage('Cleared network inspection history.');
        renderWifiList(lastWifiData);
        renderBleList(lastBleData);
      }
    });

    function pulseRefresh(ok) {
      refreshDot.classList.remove('pulsing');
      void refreshDot.offsetWidth;
      if (ok) { refreshDot.style.backgroundColor = ''; refreshDot.classList.add('pulsing'); setTimeout(() => refreshDot.classList.remove('pulsing'), 700); }
      else refreshDot.style.backgroundColor = '#e11d48';
    }

    // ─── Fetch from ESP32 ─────────────────────────────────────────────────────
    async function fetchSignalArray(url) {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 8000);
      let response;
      try { response = await fetch(url, { signal: controller.signal, cache: 'no-store' }); }
      finally { clearTimeout(timeoutId); }
      if (!response.ok) throw new Error(`${url} returned HTTP ${response.status}`);
      const data = await response.json();
      if (!Array.isArray(data)) throw new Error(`${url} did not return a JSON array`);
      const headerValue = response.headers.get('X-Scan-Id');
      const headerId = headerValue === null ? NaN : Number(headerValue);
      data.scanId = Number.isFinite(headerId) && headerId > 0 ? headerId : Number(data[0] && data[0].scanId);
      return data;
    }

    function scanFailureHelp(error) {
      if (error && error.name === 'AbortError') return 'ESP32 timed out. Check that the phone and ESP32 are on the same network.';
      if (window.location.protocol === 'https:') return 'HTTPS pages cannot read the ESP32 HTTP API. Open the ESP32 HTTP address directly.';
      return 'Use the ESP32 IP shown in Serial Monitor. The phone and ESP32 must be on the same network.';
    }

    // While READING, take one sample per new scan for the selected BSSID / Bluetooth MAC.
    function feedTarget(wifi, ble) {
      if (!target || demoSimActive || phase !== 'READING') return;
      const signals = target.type === 'wifi' ? wifi : ble;
      const scanId = Number(signals.scanId);
      if (!Number.isFinite(scanId) || scanId <= 0 || scanId === lastProcessedScanId) return;
      lastProcessedScanId = scanId;
      const sig = target.type === 'wifi'
        ? wifi.find(item => String(item.bssid || '').toLowerCase() === target.id.toLowerCase())
        : ble.find(item => String(item.id || '').toLowerCase() === target.id.toLowerCase());
      if (sig) {
        onSample(sig.rssi);
      } else {
        missCount++;
        if (missCount === 3) setEspTrack(target.type, 0); // channel may have changed: scan all channels
        render();
      }
    }

    async function fetchSignals() {
      if (!isConnected || !espBaseUrl || scanInFlight) return;
      if (!target && Date.now() - lastListFetch < POLL_LIST_MS) return;
      scanInFlight = true;
      try {
        if (target) {
          // Tracking: poll only the selected device's endpoint, fast.
          if (target.type === 'wifi') lastWifiData = await fetchSignalArray(`${espBaseUrl}/wifi`);
          else lastBleData = await fetchSignalArray(`${espBaseUrl}/ble`);
          feedTarget(lastWifiData, lastBleData);
          refreshLabel.textContent = 'Tracking';
        } else {
          lastListFetch = Date.now();
          const [wifi, ble] = await Promise.all([fetchSignalArray(`${espBaseUrl}/wifi`), fetchSignalArray(`${espBaseUrl}/ble`)]);
          lastWifiData = wifi; lastBleData = ble;
          wifiCount.textContent = wifi.length; bleCount.textContent = ble.length;
          totalSignalCount.textContent = `${wifi.length + ble.length} signals`;
          renderWifiList(wifi); renderBleList(ble);
          refreshLabel.textContent = `Updated - ${wifi.length} Wi-Fi, ${ble.length} BLE`;
          pulseRefresh(true);
        }
      } catch (err) {
        pulseRefresh(false);
        refreshLabel.textContent = 'Scan failed';
        logMessage(`Scan error: ${err && err.message ? err.message : 'Request failed'}. ${scanFailureHelp(err)}`);
      } finally {
        scanInFlight = false;
      }
    }

    btnConnect.addEventListener('click', async () => {
      if (isConnected) {
        setEspTrack(null);
        isConnected = false; espBaseUrl = '';
        clearInterval(scanInterval); scanInterval = null;
        btnConnect.textContent = 'Connect'; btnConnect.classList.remove('connected');
        connectedIpDisplay.classList.remove('visible');
        refreshLabel.textContent = 'Not connected'; refreshDot.style.backgroundColor = '#ccc';
        lastWifiData = []; lastBleData = [];
        wifiList.innerHTML = '<div class="empty-state"><span>📡</span>Connect to ESP32 to scan</div>';
        bleList.innerHTML = '<div class="empty-state"><span>🔵</span>Connect to ESP32 to scan</div>';
        wifiCount.textContent = '0'; bleCount.textContent = '0'; totalSignalCount.textContent = '0 signals';
        logMessage('Disconnected from ESP32.');
        return;
      }
      const ip = espIpInput.value.trim();
      if (!ip) { logMessage('⚠️ Enter an IP address first.'); return; }
      const url = (ip.startsWith('http') ? ip : `http://${ip}`).replace(/\/+$/, '');
      btnConnect.textContent = '⏳ Connecting...'; btnConnect.disabled = true;
      logMessage(`Connecting to ESP32 at ${url}…`);
      try {
        await Promise.all([fetchSignalArray(`${url}/wifi`), fetchSignalArray(`${url}/ble`)]);
        isConnected = true; espBaseUrl = url;
        btnConnect.textContent = '⏏ Disconnect'; btnConnect.classList.add('connected');
        connectedIpText.textContent = `Connected to ${ip}`;
        connectedIpDisplay.classList.add('visible');
        refreshLabel.textContent = 'Connected';
        logMessage(`✅ Connected to ESP32 at ${ip}.`);
        await fetchSignals();
        scanInterval = setInterval(fetchSignals, POLL_TRACK_MS);
      } catch (err) {
        if (window.location.protocol === 'https:' && url.startsWith('http:')) {
          alert("HTTPS pages cannot connect to the ESP32 HTTP API. Open the ESP32 HTTP address directly.");
        }
        logMessage(`❌ Could not reach ESP32 at ${url}. Is the device on?`);
        btnConnect.textContent = 'Connect';
        refreshDot.style.backgroundColor = '#e11d48';
      } finally {
        btnConnect.disabled = false;
      }
    });

    // ─── Init ─────────────────────────────────────────────────────────────────
    window.addEventListener('load', () => {
      if (!localStorage.getItem(KEY_NAME) || !localStorage.getItem(KEY_CONTACT)) openSettings();
      refreshCodeWordUI();
      if (IS_FILE_PROTOCOL) logMessage('⚠️ file:// detected — serve over HTTP for mic & location to work fully.');
      else logMessage('SYS: App initialized. Mode: Wireless Radar.');

      const isDeviceHost = window.location.protocol === 'http:' &&
        (window.location.hostname.endsWith('.local') || /^\d{1,3}(\.\d{1,3}){3}$/.test(window.location.hostname));
      if (isDeviceHost) { espIpInput.value = window.location.origin; btnConnect.click(); }

      render();
      animateRadar();
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
  WiFi.mode(WIFI_STA);

  WiFi.begin(phoneHotspotSsid, phoneHotspotPassword);
  Serial.println("\nConnecting to hotspot 'hello' in background...");

  BLEDevice::init("");

  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(IR_LED_PIN, LOW);
  jsonMutex = xSemaphoreCreateMutex();

  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/ble", handleBLE);
  server.on("/pir-status", handlePIR);
  server.on("/trigger-whatsapp", handleWhatsApp);
  server.on("/track", handleTrack);

  server.begin();

  xTaskCreate(irLedBlinkTask, "IrLedBlink", 2048, NULL, 1, NULL);

  xTaskCreatePinnedToCore(scanTask, "ScanTask", 10000, NULL, 1, &scanTaskHandle, 0);
}

void loop() {
  server.handleClient();

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

  delay(1);
}