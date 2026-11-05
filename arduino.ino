// AEGIS - Hidden Camera Detector (ESP32)
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
    let trackedTargets = [];
    let nextTargetId = 1;
    let simulationEnabled = false;

    // Expected 0.5s rate: 500ms ON / 500ms OFF (1.0 Hz)
    const EXPECTED_LED_HALF_CYCLE_MS = 500;
    const MIN_VALID_HALF_CYCLE_MS = 280;
    const MAX_VALID_HALF_CYCLE_MS = 750;
    const STATIC_THRESHOLD_MS = 800; // Continuous ON > 800ms means static light/object (NOT blinking!)

    const setCameraStatus = (isOn) => {
      cameraStatus.innerHTML = isOn
        ? '<span class="status-dot"></span> Camera On'
        : '<span class="status-dot" style="background-color: var(--accent-blue);"></span> Camera Off';
      cameraStatus.style.backgroundColor = isOn
        ? 'rgba(16, 185, 129, 0.15)'
        : 'rgba(214, 51, 108, 0.12)';
      cameraStatus.style.color = isOn ? 'var(--accent-green)' : 'var(--accent-blue)';
    };

    const rgbToHsl = (r, g, b) => {
      r /= 255; g /= 255; b /= 255;
      const max = Math.max(r, g, b);
      const min = Math.min(r, g, b);
      const lightness = (max + min) / 2;
      const delta = max - min;
      if (delta === 0) return { hue: 0, saturation: 0, lightness };
      const saturation = delta / (1 - Math.abs(2 * lightness - 1));
      let hue;
      if (max === r) hue = 60 * (((g - b) / delta) % 6);
      else if (max === g) hue = 60 * (((b - r) / delta) + 2);
      else hue = 60 * (((r - g) / delta) + 4);
      return { hue: hue < 0 ? hue + 360 : hue, saturation, lightness };
    };

    // Detects optical light candidates across all colors (IR, Red, White, Blue, Green, Amber, etc.)
    const getOpticalCandidate = (r, g, b) => {
      // 1. Near-Infrared (IR): Appears as pink/magenta/purple on digital camera sensors
      if (r >= 60 && b >= 60 && (r > g + 10 || b > g + 10)) {
        const hsl = rgbToHsl(r, g, b);
        if (hsl.hue >= 260 && hsl.hue <= 355 && hsl.saturation >= 0.15 && hsl.lightness >= 0.20) {
          return { type: 'IR', name: 'IR Lens' };
        }
      }
      // 2. High-intensity White core (often center of intense IR or white LEDs)
      if (r >= 200 && g >= 200 && b >= 200) {
        return { type: 'WHITE', name: 'White LED' };
      }
      // 3. Red LED
      if (r >= 110 && r > g + 35 && r > b + 35) {
        return { type: 'RED', name: 'Red LED' };
      }
      // 4. Green LED
      if (g >= 110 && g > r + 35 && g > b + 35) {
        return { type: 'GREEN', name: 'Green LED' };
      }
      // 5. Blue LED
      if (b >= 110 && b > r + 35 && b > g + 35) {
        return { type: 'BLUE', name: 'Blue LED' };
      }
      // 6. Amber / Yellow LED
      if (r >= 140 && g >= 95 && b < 90 && (r > b + 45)) {
        return { type: 'AMBER', name: 'Amber LED' };
      }
      // 7. Any bright, saturated colour (including colours not covered above).
      // The brightness floor is deliberately lower than the old white-only floor,
      // so dimmer LEDs still register after camera exposure adjustments.
      const max = Math.max(r, g, b);
      const min = Math.min(r, g, b);
      const lum = (0.299 * r + 0.587 * g + 0.114 * b);
      if (max >= 145 && (max - min) >= 35 && lum >= 95) {
        return { type: 'COLOR', name: 'Bright Color' };
      }
      // 8. General concentrated bright optical spot, including glare/white LEDs.
      if (lum >= 155) {
        return { type: 'OPTICAL', name: 'Optical Light' };
      }
      return null;
    };

    function findOpticalBlobs(imageData) {
      const { data, width, height } = imageData;
      const size = width * height;
      const matches = new Uint8Array(size);
      const types = new Array(size);

      for (let index = 0; index < size; index++) {
        const offset = index * 4;
        const candidate = getOpticalCandidate(data[offset], data[offset + 1], data[offset + 2]);
        if (candidate) {
          matches[index] = 1;
          types[index] = candidate.type;
        }
      }

      const visited = new Uint8Array(size);
      const blobs = [];
      for (let start = 0; start < size; start++) {
        if (!matches[start] || visited[start]) continue;
        const queue = [start];
        visited[start] = 1;
        let pixels = 0, totalX = 0, totalY = 0, totalBrightness = 0;
        let minX = width, minY = height, maxX = 0, maxY = 0;
        const typeCounts = {};

        for (let head = 0; head < queue.length; head++) {
          const current = queue[head];
          const x = current % width;
          const y = Math.floor(current / width);
          const offset = current * 4;
          pixels++;
          totalX += x; totalY += y;
          totalBrightness += (data[offset] + data[offset + 1] + data[offset + 2]) / 3;
          minX = Math.min(minX, x); maxX = Math.max(maxX, x);
          minY = Math.min(minY, y); maxY = Math.max(maxY, y);

          const t = types[current];
          if (t) typeCounts[t] = (typeCounts[t] || 0) + 1;

          const neighbours = [current - 1, current + 1, current - width, current + width];
          for (const next of neighbours) {
            if (next < 0 || next >= size || visited[next] || !matches[next]) continue;
            const nextX = next % width;
            if (Math.abs(nextX - x) > 1) continue;
            visited[next] = 1;
            queue.push(next);
          }
        }

        const blobWidth = maxX - minX + 1;
        const blobHeight = maxY - minY + 1;

        // Discard large non-LED surfaces (walls, shirts)
        if (pixels >= 2 && pixels <= 1800 && blobWidth <= 100 && blobHeight <= 100) {
          let dominantType = 'OPTICAL';
          let maxCount = 0;
          for (const [k, v] of Object.entries(typeCounts)) {
            if (v > maxCount) { maxCount = v; dominantType = k; }
          }
          blobs.push({
            x: totalX / pixels,
            y: totalY / pixels,
            pixels,
            brightness: totalBrightness / pixels,
            type: dominantType,
            minX, minY, width: blobWidth, height: blobHeight
          });
        }
      }
      return blobs;
    }

    // Multi-target tracking: tracks all lights in the scene simultaneously
    function updateMultiTargetTracking(blobs, imageData, now) {
      const { data, width, height } = imageData;
      const matchedBlobIndices = new Set();
      const maxDistance = 65;

      // 1. Update existing targets
      trackedTargets.forEach(target => {
        let bestBlob = null;
        let bestDist = Infinity;
        let bestIdx = -1;

        blobs.forEach((blob, idx) => {
          if (matchedBlobIndices.has(idx)) return;
          const dist = Math.hypot(blob.x - target.x, blob.y - target.y);
          if (dist <= maxDistance && dist < bestDist) {
            bestDist = dist;
            bestBlob = blob;
            bestIdx = idx;
          }
        });

        if (bestBlob) {
          // Matched blob: target is ON
          matchedBlobIndices.add(bestIdx);
          target.x = target.x * 0.6 + bestBlob.x * 0.4;
          target.y = target.y * 0.6 + bestBlob.y * 0.4;
          target.minX = bestBlob.minX;
          target.minY = bestBlob.minY;
          target.width = bestBlob.width;
          target.height = bestBlob.height;
          target.type = bestBlob.type;
          target.lastSeen = now;
          target.isOn = true;
        } else {
          // No blob matched: sample around the previous footprint to detect OFF.
          const cx = Math.round(target.x);
          const cy = Math.round(target.y);
          const radius = Math.max(8, Math.min(18, Math.ceil(Math.max(target.width, target.height) * 0.75)));
          let opticalPixels = 0;
          for (let y = Math.max(0, cy - radius); y <= Math.min(height - 1, cy + radius); y++) {
            for (let x = Math.max(0, cx - radius); x <= Math.min(width - 1, cx + radius); x++) {
              const offset = (y * width + x) * 4;
              if (getOpticalCandidate(data[offset], data[offset + 1], data[offset + 2])) opticalPixels++;
            }
          }
          target.isOn = (opticalPixels >= Math.max(2, Math.floor(target.width * target.height * 0.08)));
        }

        // Analyze blink rate for this target
        analyzeTargetBlink(target, now);
      });

      // 2. Remove targets not seen for > 2000 ms
      trackedTargets = trackedTargets.filter(t => (now - t.lastSeen) <= 2000);

      // 3. Create new targets for unmatched blobs (up to max 8 concurrent targets)
      blobs.forEach((blob, idx) => {
        if (matchedBlobIndices.has(idx)) return;
        if (trackedTargets.length >= 8) return;

        trackedTargets.push({
          id: nextTargetId++,
          x: blob.x,
          y: blob.y,
          minX: blob.minX,
          minY: blob.minY,
          width: blob.width,
          height: blob.height,
          type: blob.type,
          isOn: true,
          lastSeen: now,
          lastBlinkState: true,
          lastTransitionAt: now,
          transitions: [],
          score: 0,
          status: 'ANALYZING',
          interval: 0
        });
      });
    }

    function analyzeTargetBlink(target, now) {
      const isOn = target.isOn;

      // Detect state transition with debounce (ignore < 130ms frame noise)
      if (isOn !== target.lastBlinkState && (now - target.lastTransitionAt >= 130)) {
        const interval = now - target.lastTransitionAt;
        target.lastBlinkState = isOn;
        target.lastTransitionAt = now;

        if (interval >= MIN_VALID_HALF_CYCLE_MS && interval <= MAX_VALID_HALF_CYCLE_MS) {
          target.transitions.push(interval);
          if (target.transitions.length > 8) target.transitions.shift();
        } else if (interval > MAX_VALID_HALF_CYCLE_MS) {
          target.transitions = [];
        }
      }

      const timeInCurrentState = now - target.lastTransitionAt;

      // STATIC LIGHT REJECTION:
      // If this specific light stays ON continuously for > 800ms without turning off,
      // it is a static lamp/LED reflection. Score = 0% and threat rejected.
      if (isOn && timeInCurrentState > STATIC_THRESHOLD_MS) {
        target.transitions = [];
        target.status = 'STATIC';
        target.score = 0;
        target.interval = 0;
        return;
      }

      // If too few transitions to confirm rhythm
      if (target.transitions.length < 3) {
        target.status = 'ANALYZING';
        target.score = Math.round((target.transitions.length / 3) * 35);
        target.interval = 0;
        return;
      }

      // Evaluate 0.5s (500ms) pattern consistency
      const recent = target.transitions.slice(-6);
      const avg = recent.reduce((sum, d) => sum + d, 0) / recent.length;
      const matching = recent.filter(d => Math.abs(d - EXPECTED_LED_HALF_CYCLE_MS) <= 140).length;
      const matchRatio = matching / recent.length;
      const avgDev = recent.reduce((sum, d) => sum + Math.abs(d - EXPECTED_LED_HALF_CYCLE_MS), 0) / recent.length;

      if (matchRatio >= 0.65 && avg >= 350 && avg <= 650) {
        const accuracy = Math.max(0, 1 - (avgDev / 250));
        let score = Math.round(accuracy * 100);
        if (recent.length === 3) score = Math.min(score, 78);
        else score = Math.min(100, Math.max(75, score));

        target.score = score;
        target.status = score >= 70 ? 'CONFIRMED' : 'ANALYZING';
        target.interval = Math.round(avg);
      } else {
        target.score = 0;
        target.status = 'WRONG_RATE';
        target.interval = Math.round(avg);
      }
    }

    // Draws overlay boxes for all simultaneously tracked targets
    function drawDetections(targets) {
      overlayContext.clearRect(0, 0, detectionOverlay.width, detectionOverlay.height);
      if (!targets || targets.length === 0) return;

      const scaleX = detectionOverlay.width / processingCanvas.width;
      const scaleY = detectionOverlay.height / processingCanvas.height;

      targets.forEach(target => {
        const screenX = target.minX * scaleX;
        const screenY = target.minY * scaleY;
        const screenW = target.width * scaleX;
        const screenH = target.height * scaleY;

        const typeLabel = target.type ? target.type : 'OPTICAL';
        let strokeColor = '#f59e0b';
        let label = `VERIFYING ${typeLabel}...`;
        let lineDash = [];

        if (simulationEnabled) {
          strokeColor = '#ef4444';
          label = `DEMO IR SIGNAL (${target.score}%)`;
        } else if (target.status === 'CONFIRMED') {
          strokeColor = '#ef4444'; // Bright Red Alert
          const rateText = target.interval > 0 ? ` (~${(target.interval / 1000).toFixed(2)}s)` : ' (~0.5s)';
          label = `⚠️ CAMERA DETECTED [${typeLabel}]${rateText}`;
        } else if (target.status === 'STATIC') {
          strokeColor = '#94a3b8'; // Muted grey
          label = `STATIC ${typeLabel} (IGNORED)`;
          lineDash = [4, 4];
        } else if (target.status === 'WRONG_RATE') {
          strokeColor = '#eab308';
          label = `${typeLabel}: ${(target.interval / 1000).toFixed(2)}s (NOT 0.5s)`;
          lineDash = [3, 3];
        } else {
          strokeColor = '#3b82f6';
          label = `ANALYZING ${typeLabel} (${target.score}%)`;
          lineDash = [2, 2];
        }

        overlayContext.save();
        overlayContext.strokeStyle = strokeColor;
        overlayContext.lineWidth = (target.status === 'CONFIRMED') ? 3 : 2;
        overlayContext.setLineDash(lineDash);

        const pad = 4;
        overlayContext.strokeRect(screenX - pad, screenY - pad, screenW + pad * 2, screenH + pad * 2);
        overlayContext.setLineDash([]);

        overlayContext.font = 'bold 11px system-ui, -apple-system, sans-serif';
        const textWidth = overlayContext.measureText(label).width;
        const labelY = Math.max(14, screenY - 8);

        overlayContext.fillStyle = strokeColor;
        overlayContext.fillRect(screenX - pad, labelY - 12, textWidth + 8, 15);

        overlayContext.fillStyle = '#ffffff';
        overlayContext.fillText(label, screenX - pad + 4, labelY);
        overlayContext.restore();
      });
    }

    function drawSimulationDot(now) {
      if (!simulationEnabled) return;
      const isSimOn = Math.floor(now / 500) % 2 === 1;
      if (!isSimOn) return;
      const x = 160;
      const y = 120;
      processingContext.fillStyle = '#ff00ff';
      processingContext.beginPath();
      processingContext.arc(x, y, 6, 0, Math.PI * 2);
      processingContext.fill();
    }

    // Draws uncropped, unzoomed full field-of-view camera frame
    function drawCameraFrame() {
      const sourceWidth = cameraPreview.videoWidth;
      const sourceHeight = cameraPreview.videoHeight;
      if (!sourceWidth || !sourceHeight) return;

      // Automatically size the container to match the camera's native aspect ratio (NO AUTO-ZOOM!)
      const expectedAspect = sourceWidth + ' / ' + sourceHeight;
      if (cameraCanvasContainer.style.aspectRatio !== expectedAspect) {
        cameraCanvasContainer.style.aspectRatio = expectedAspect;
      }

      // Synchronize overlay canvas pixel dimensions to the visible container
      const containerRect = cameraCanvasContainer.getBoundingClientRect();
      const containerW = Math.round(containerRect.width);
      const containerH = Math.round(containerRect.height);
      if (containerW > 0 && containerH > 0) {
        if (detectionOverlay.width !== containerW || detectionOverlay.height !== containerH) {
          detectionOverlay.width = containerW;
          detectionOverlay.height = containerH;
        }
      }

      // Draw full uncropped camera feed directly onto processing canvas
      processingContext.drawImage(
        cameraPreview,
        0, 0, sourceWidth, sourceHeight,
        0, 0, processingCanvas.width, processingCanvas.height
      );
    }

    function processCameraFrame() {
      if (!cameraStream || cameraPreview.readyState < HTMLMediaElement.HAVE_CURRENT_DATA) {
        processingFrameId = requestAnimationFrame(processCameraFrame);
        return;
      }
      drawCameraFrame();
      drawSimulationDot(performance.now());
      const now = performance.now();
      const imgData = processingContext.getImageData(0, 0, processingCanvas.width, processingCanvas.height);
      const blobs = findOpticalBlobs(imgData);

      updateMultiTargetTracking(blobs, imgData, now);

      // Determine highest threat among all simultaneously tracked targets
      const confirmedTarget = trackedTargets.find(t => t.status === 'CONFIRMED');
      const analyzingTarget = trackedTargets.find(t => t.status === 'ANALYZING');
      const maxScore = trackedTargets.reduce((max, t) => Math.max(max, t.score), 0);

      blinkScore.textContent = maxScore + '%';

      if (simulationEnabled) {
        blinkStatus.textContent = '(Demo 0.5s IR blink signal simulated)';
        blinkStatus.style.color = 'var(--accent-blue)';
      } else if (trackedTargets.length === 0) {
        blinkStatus.textContent = '(No optical target detected)';
        blinkStatus.style.color = 'var(--text-muted)';
      } else if (confirmedTarget) {
        blinkStatus.textContent = '🚨 0.5s Blink verified (~' + confirmedTarget.interval + 'ms) — ' + confirmedTarget.type + ' Camera Detected!';
        blinkStatus.style.color = '#ef4444';
      } else if (analyzingTarget) {
        blinkStatus.textContent = '⏳ ' + analyzingTarget.type + ' pulse detected — verifying 0.5s rate (' + analyzingTarget.score + '% match)...';
        blinkStatus.style.color = 'var(--accent-blue)';
      } else {
        // Only static or wrong rate targets present
        blinkStatus.textContent = '❌ Static lights detected (not blinking at 0.5s) — Threat rejected';
        blinkStatus.style.color = '#94a3b8';
      }

      drawDetections(trackedTargets);
      processingFrameId = requestAnimationFrame(processCameraFrame);
    }

    async function startCamera() {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        logMessage('Camera access is unavailable in this browser.');
        return;
      }

      cameraToggleBtn.disabled = true;
      try {
        cameraStream = await navigator.mediaDevices.getUserMedia({
          video: {
            facingMode: { ideal: 'environment' },
            width: { ideal: 1280 },
            height: { ideal: 720 }
          },
          audio: false
        });
        // Prefer 1× zoom and continuous focus/exposure where the browser exposes
        // those controls. Unsupported controls are simply left at device defaults.
        const videoTrack = cameraStream.getVideoTracks()[0];
        if (videoTrack && videoTrack.getCapabilities && videoTrack.applyConstraints) {
          const capabilities = videoTrack.getCapabilities();
          const deviceControls = {};
          if (capabilities.zoom && capabilities.zoom.min <= 1 && capabilities.zoom.max >= 1) {
            deviceControls.zoom = 1;
          }
          if (capabilities.focusMode && capabilities.focusMode.includes('continuous')) {
            deviceControls.focusMode = 'continuous';
          }
          if (capabilities.exposureMode && capabilities.exposureMode.includes('continuous')) {
            deviceControls.exposureMode = 'continuous';
          }
          if (Object.keys(deviceControls).length) {
            try { await videoTrack.applyConstraints({ advanced: [deviceControls] }); } catch (_) { /* Keep browser defaults. */ }
          }
        }
        cameraPreview.srcObject = cameraStream;
        await cameraPreview.play();
        cameraPreview.style.display = 'block';
        cameraPlaceholder.style.display = 'none';
        cameraCanvasContainer.classList.add('camera-active');
        detectionOverlay.style.display = 'block';
        simulationToggleBtn.disabled = false;
        processCameraFrame();
        cameraToggleBtn.textContent = 'Turn Camera Off';
        setCameraStatus(true);
        logMessage('Camera started. Rear-facing camera selected when available.');
      } catch (error) {
        const message = error.name === 'NotAllowedError'
          ? 'Camera permission denied. Allow camera access and try again.'
          : 'Camera could not start. Check that no other app is using it.';
        logMessage(message);
      } finally {
        cameraToggleBtn.disabled = false;
      }
    }

    function stopCamera() {
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
      simulationEnabled = false;
      simulationToggleBtn.disabled = true;
      simulationToggleBtn.textContent = 'Run Demo Blink';
      detectionOverlay.style.display = 'none';
      overlayContext.clearRect(0, 0, detectionOverlay.width, detectionOverlay.height);
      trackedTargets = [];
      blinkScore.textContent = '0%';
      blinkStatus.textContent = '(No optical target detected)';
      cameraToggleBtn.textContent = 'Turn Camera On';
      setCameraStatus(false);
    }

    cameraToggleBtn.addEventListener('click', () => {
      if (cameraStream) {
        stopCamera();
        logMessage('Camera stopped.');
      } else {
        startCamera();
      }
    });

    simulationToggleBtn.addEventListener('click', () => {
      simulationEnabled = !simulationEnabled;
      trackedTargets = [];
      blinkScore.textContent = '0%';
      simulationToggleBtn.textContent = simulationEnabled ? 'Stop Demo Blink' : 'Run Demo Blink';
      logMessage(simulationEnabled ? 'Demo IR blink simulation started.' : 'Demo IR blink simulation stopped.');
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

    function renderWifiList(signals) {
      wifiList.innerHTML = '';
      if (!signals.length) { wifiList.innerHTML = '<div class="empty-state"><span>📡</span>No Wi-Fi networks found</div>'; return; }
      signals.slice().sort((a, b) => b.rssi - a.rssi).forEach(sig => {
        const item = document.createElement('div');
        item.className = 'signal-item';
        item.innerHTML = `
          <span class="signal-icon">📶</span>
          <div class="signal-info">
            <div class="signal-name">${esc(sig.ssid) || '(Hidden)'}</div>
            <div class="signal-meta">Ch ${esc(sig.channel)} · ${esc(sig.bssid)}</div>
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${esc(sig.rssi)} dBm</span>
            <div class="rssi-bar-bg"><div class="rssi-bar-fill" style="width:${rssiToPercent(sig.rssi)}%;background:${rssiColor(sig.rssi)};"></div></div>
          </div>`;
        item.appendChild(makeTrackBtn('wifi', sig.bssid, sig.ssid || sig.bssid, sig.channel));
        wifiList.appendChild(item);
      });
    }

    function renderBleList(signals) {
      bleList.innerHTML = '';
      if (!signals.length) { bleList.innerHTML = '<div class="empty-state"><span>🔵</span>No Bluetooth devices found</div>'; return; }
      signals.slice().sort((a, b) => b.rssi - a.rssi).forEach(sig => {
        const item = document.createElement('div');
        item.className = 'signal-item';
        item.innerHTML = `
          <span class="signal-icon">🔹</span>
          <div class="signal-info">
            <div class="signal-name">${esc(sig.name)}</div>
            <div class="signal-meta">Bluetooth LE</div>
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${esc(sig.rssi)} dBm</span>
            <div class="rssi-bar-bg"><div class="rssi-bar-fill" style="width:${rssiToPercent(sig.rssi)}%;background:${rssiColor(sig.rssi)};"></div></div>
          </div>`;
        item.querySelector('.signal-meta').textContent = `Bluetooth LE - ${sig.id || 'identifier unavailable'}`;
        item.appendChild(makeTrackBtn('ble', sig.id, sig.name || sig.id));
        bleList.appendChild(item);
      });
    }

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