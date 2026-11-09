// AEGIS - Hidden Camera Detector (ESP32 - 2.4GHz Promiscuous Sniffer)
// Wireless Radar: 3-tier spy camera architecture
//   Tier 1: Passive Sus Triage (Vendor OUI + SSID/BLE Name Keyword + Tuya/SmartLife Beacon Service UUIDs)
//   Tier 2: 2.4GHz Promiscuous Deep Scanning (To-DS=1 Uplink Asymmetry Sniffing across Channels 1-13)
//   Tier 3: Direct VBR Active Probing (Channel Lock, Still Baseline vs Motion Uplink Bitrate Delta, Data+Mgmt Frames)
// Preserved: Optical IR Scanner, 1Hz IR LED Task (GPIO 23), Emergency SOS, Green API WhatsApp,
//            Smart Code Word, 8-Sector Differential RSSI Walking Radar, Gyroscope/Compass, Wall Picker, Demo Mode.

#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_idf_version.h>
#include <esp_wifi.h>

const char* phoneHotspotSsid = "hello";
const char* phoneHotspotPassword = "12345678";

WebServer server(80);

String wifiJson = "[]";
String bleJson = "[]";
String clientsJson = "[]";
String probeResultJson = "{}";
uint32_t completedWifiScanId = 0;
uint32_t completedBleScanId = 0;

SemaphoreHandle_t jsonMutex;
TaskHandle_t scanTaskHandle = NULL;

// Target-focused scanning:
// 0 = Wi-Fi + BLE (full list), 1 = Wi-Fi AP only, 2 = BLE only, 3 = Client Promiscuous Tracking
volatile uint8_t trackMode = 0;
volatile uint8_t trackChannel = 0;
uint8_t trackClientMac[6] = {0};
uint8_t espOwnMac[6] = {0};

const uint8_t IR_LED_PIN = 23; // GPIO23 (D6 on this board)

// ─── Promiscuous Sniffer Structures & State ──────────────────────────────────
#define MAX_DISCOVERED_CLIENTS 32

struct DiscoveredClient {
  uint8_t mac[6];
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
  uint32_t uplinkPackets;
  uint32_t uplinkBytes;
};

DiscoveredClient discoveredClients[MAX_DISCOVERED_CLIENTS];
volatile uint8_t clientCount = 0;

enum SnifferMode {
  SNIFFER_OFF = 0,
  SNIFFER_DEEPSCAN,
  SNIFFER_PROBE,
  SNIFFER_TRACK_CLIENT
};
volatile SnifferMode currentSnifferMode = SNIFFER_OFF;

portMUX_TYPE sniffMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t targetProbeMac[6] = {0};
volatile uint32_t probePackets = 0;
volatile uint32_t probeBytes = 0;
volatile int8_t probeLastRssi = -110;
volatile uint32_t probeChannelHits[14] = {0};

volatile bool triggerDeepScan = false;
volatile bool triggerProbe = false;
String targetProbePhase = "baseline";
uint8_t targetProbeChannel = 0;

// ─── Camera / IoT Vendor OUI Database (First 3 Bytes) ────────────────────────
struct OuiEntry {
  uint8_t b[3];
  const char* vendor;
};

const OuiEntry KNOWN_CAMERA_OUIS[] = {
  {{0xE0, 0x50, 0x8B}, "Hikvision"},
  {{0xC0, 0x56, 0xE3}, "Hikvision"},
  {{0x4C, 0xBD, 0x8F}, "Hikvision"},
  {{0xD8, 0x1F, 0x12}, "Tuya"},
  {{0x10, 0xD5, 0x61}, "Tuya"},
  {{0x7C, 0xF6, 0x66}, "Tuya"},
  {{0x00, 0x12, 0x12}, "Xiongmai"},
  {{0x74, 0xB5, 0x87}, "Xiongmai"},
  {{0x24, 0x0A, 0xC4}, "Espressif"},
  {{0x30, 0xAE, 0xA4}, "Espressif"},
  {{0xA0, 0x20, 0xA6}, "Espressif"},
  {{0x34, 0x85, 0x18}, "Espressif"},
  {{0x48, 0x27, 0xE2}, "Espressif"},
  {{0xEC, 0xDA, 0x3B}, "Espressif"},
  {{0xD4, 0x8A, 0xFC}, "Espressif"},
  {{0x2C, 0xAA, 0x8E}, "Wyze"},
  {{0x00, 0xE0, 0x4C}, "ShenZhen Bilian"}
};
const size_t KNOWN_CAMERA_OUIS_COUNT = sizeof(KNOWN_CAMERA_OUIS) / sizeof(KNOWN_CAMERA_OUIS[0]);

const char* SUSPICIOUS_KEYWORDS[] = {
  "cam", "ipcam", "lookcam", "minicam", "v380",
  "tuya", "smartlife", "hdwifi", "hid", "blink", "yi"
};
const size_t SUSPICIOUS_KEYWORDS_COUNT = sizeof(SUSPICIOUS_KEYWORDS) / sizeof(SUSPICIOUS_KEYWORDS[0]);

void macToStr(const uint8_t mac[6], char* out) {
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool parseMacBytes(const String& macStr, uint8_t out[6]) {
  String s = macStr;
  s.trim();
  if (s.length() < 11) return false;
  unsigned int b[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6 ||
      sscanf(s.c_str(), "%x-%x-%x-%x-%x-%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6 ||
      sscanf(s.c_str(), "%2x%2x%2x%2x%2x%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
    return true;
  }
  return false;
}

const char* checkCameraOuiBytes(const uint8_t mac[3]) {
  for (size_t i = 0; i < KNOWN_CAMERA_OUIS_COUNT; i++) {
    if (KNOWN_CAMERA_OUIS[i].b[0] == mac[0] &&
        KNOWN_CAMERA_OUIS[i].b[1] == mac[1] &&
        KNOWN_CAMERA_OUIS[i].b[2] == mac[2]) {
      return KNOWN_CAMERA_OUIS[i].vendor;
    }
  }
  return nullptr;
}

const char* checkCameraOuiStr(const String& macStr) {
  uint8_t b[6];
  if (parseMacBytes(macStr, b)) {
    return checkCameraOuiBytes(b);
  }
  return nullptr;
}

bool checkSuspiciousName(String name) {
  name.toLowerCase();
  for (size_t i = 0; i < SUSPICIOUS_KEYWORDS_COUNT; i++) {
    if (name.indexOf(SUSPICIOUS_KEYWORDS[i]) >= 0) {
      return true;
    }
  }
  return false;
}

String escapeJson(const String& s) {
  String out = "";
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c >= 32) out += c;
  }
  return out;
}

// ─── Promiscuous RX Callback (Data, Mgmt & Control Frames) ──────────────────
void IRAM_ATTR wifiPromiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT && type != WIFI_PKT_CTRL && type != WIFI_PKT_MISC) return;
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 16) return;

  uint8_t fc0 = payload[0];
  uint8_t fc1 = payload[1];
  uint8_t frameType = (fc0 >> 2) & 0x03;
  // frameType: 0 = Management, 1 = Control, 2 = Data

  uint8_t toDs = fc1 & 0x01;
  uint8_t fromDs = (fc1 >> 1) & 0x01;

  const uint8_t* addr1 = &payload[4];
  const uint8_t* addr2 = &payload[10];
  const uint8_t* addr3 = (len >= 24) ? &payload[16] : nullptr;

  portENTER_CRITICAL_ISR(&sniffMux);

  // 1. DEEPSCAN mode: looking for client uplink (To-DS == 1, From-DS == 0)
  if (currentSnifferMode == SNIFFER_DEEPSCAN) {
    if (frameType == 2 && len >= 24) {
      const uint8_t* clientMac = nullptr;
      const uint8_t* apBssid = nullptr;
      if (toDs == 1 && fromDs == 0) {
        clientMac = addr2;
        apBssid = addr1;
      } else if (toDs == 0 && fromDs == 1) {
        clientMac = addr1;
        apBssid = addr2;
      }
      if (clientMac != nullptr && apBssid != nullptr) {
      // Addr1 is AP BSSID, Addr2 is Client Source MAC
      // Ignore broadcast & multicast source MACs
        bool isUnicast = (clientMac[0] & 0x01) == 0;
        bool isNonZero = (clientMac[0] | clientMac[1] | clientMac[2] | clientMac[3] | clientMac[4] | clientMac[5]) != 0;
        bool isNotBroadcast = (clientMac[0] != 0xFF || clientMac[1] != 0xFF || clientMac[2] != 0xFF ||
                               clientMac[3] != 0xFF || clientMac[4] != 0xFF || clientMac[5] != 0xFF);
        bool isNotOwn = (espOwnMac[0] | espOwnMac[1] | espOwnMac[2] | espOwnMac[3] | espOwnMac[4] | espOwnMac[5]) == 0 ||
                        (memcmp(clientMac, espOwnMac, 6) != 0);
        if (isUnicast && isNonZero && isNotBroadcast && isNotOwn) {
        int matchIdx = -1;
        for (int i = 0; i < clientCount; i++) {
          if (memcmp(discoveredClients[i].mac, clientMac, 6) == 0) {
            matchIdx = i;
            break;
          }
        }
        if (matchIdx >= 0) {
          discoveredClients[matchIdx].uplinkPackets++;
          discoveredClients[matchIdx].uplinkBytes += len;
          discoveredClients[matchIdx].rssi = pkt->rx_ctrl.rssi;
          discoveredClients[matchIdx].channel = pkt->rx_ctrl.channel;
        } else if (clientCount < MAX_DISCOVERED_CLIENTS) {
          memcpy(discoveredClients[clientCount].mac, clientMac, 6);
          memcpy(discoveredClients[clientCount].bssid, apBssid, 6);
          discoveredClients[clientCount].rssi = pkt->rx_ctrl.rssi;
          discoveredClients[clientCount].channel = pkt->rx_ctrl.channel;
          discoveredClients[clientCount].uplinkPackets = 1;
          discoveredClients[clientCount].uplinkBytes = len;
          clientCount++;
        }
      }
    }
  }

  }

  // 2. PROBE mode: match target MAC in Addr1, Addr2, or Addr3 (Data, Mgmt & Control frames allowed)
  if (currentSnifferMode == SNIFFER_PROBE) {
    if (frameType == 0 || frameType == 1 || frameType == 2) {
    bool match = (memcmp(addr2, targetProbeMac, 6) == 0) ||
                 (memcmp(addr1, targetProbeMac, 6) == 0) ||
                 (addr3 != nullptr && memcmp(addr3, targetProbeMac, 6) == 0);
    if (match) {
      probePackets++;
      probeBytes += len;
      probeLastRssi = pkt->rx_ctrl.rssi;
      uint8_t ch = pkt->rx_ctrl.channel;
      if (ch >= 1 && ch <= 13) {
        probeChannelHits[ch]++;
      }
    }
  }

  }

  // 3. TRACK CLIENT mode: track RSSI of specific client MAC across Addr1, Addr2, and Addr3
  if (currentSnifferMode == SNIFFER_TRACK_CLIENT) {
    if (frameType == 0 || frameType == 1 || frameType == 2) {
      if (memcmp(addr1, targetProbeMac, 6) == 0 || memcmp(addr2, targetProbeMac, 6) == 0 || (addr3 != nullptr && memcmp(addr3, targetProbeMac, 6) == 0)) {
      probeLastRssi = pkt->rx_ctrl.rssi;
      probePackets++;
    }
  }

  }

  portEXIT_CRITICAL_ISR(&sniffMux);
}

void startPromiscuous(uint8_t channel) {
  // Enable promiscuous mode FIRST (before setting channel).
  // In promiscuous mode the radio is NOT locked to the association channel,
  // so esp_wifi_set_channel() succeeds without disconnecting from the hotspot.
  // Disconnecting would make the ESP32 unreachable from the phone mid-scan.
  wifi_promiscuous_filter_t filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL
  };
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&wifiPromiscuousCallback);
  esp_wifi_set_promiscuous(true);
  if (channel > 0) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  }
}

void stopPromiscuousAndReconnect() {
  esp_wifi_set_promiscuous(false);
  portENTER_CRITICAL(&sniffMux);
  currentSnifferMode = SNIFFER_OFF;
  portEXIT_CRITICAL(&sniffMux);

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.begin(phoneHotspotSsid, phoneHotspotPassword);
    unsigned long startT = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startT < 15000)) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

String buildClientsJson() {
  String json = "[";
  for (int i = 0; i < clientCount; i++) {
    if (i) json += ",";
    char macStr[18];
    char bssidStr[18];
    macToStr(discoveredClients[i].mac, macStr);
    macToStr(discoveredClients[i].bssid, bssidStr);

    bool isSus = false;
    String reasons = "[";
    bool firstR = true;

    const char* vendor = checkCameraOuiBytes(discoveredClients[i].mac);
    if (vendor != nullptr) {
      isSus = true;
      if (!firstR) reasons += ",";
      reasons += "\"Hidden client OUI matches " + String(vendor) + " (IoT/Camera)\"";
      firstR = false;
    }

    if (discoveredClients[i].uplinkPackets >= 4 || discoveredClients[i].uplinkBytes >= 1500) {
      isSus = true;
      if (!firstR) reasons += ",";
      reasons += "\"Heavy uplink data stream (To-DS=1)\"";
      firstR = false;
    }
    reasons += "]";

    json += "{";
    json += "\"mac\":\"" + String(macStr) + "\",";
    json += "\"bssid\":\"" + String(bssidStr) + "\",";
    json += "\"ssid\":\"Client (" + String(macStr) + ")\",";
    json += "\"channel\":" + String(discoveredClients[i].channel) + ",";
    json += "\"rssi\":" + String(discoveredClients[i].rssi) + ",";
    json += "\"uplinkPackets\":" + String(discoveredClients[i].uplinkPackets) + ",";
    json += "\"uplinkBytes\":" + String(discoveredClients[i].uplinkBytes) + ",";
    json += "\"isClient\":true,";
    json += "\"sus\":" + String(isSus ? "true" : "false") + ",";
    json += "\"reasons\":" + reasons;
    json += "}";
  }
  json += "]";
  return json;
}

// ─── IR LED Blink Task (GPIO 23) ─────────────────────────────────────────────
void irLedBlinkTask(void* parameter) {
  bool ledState = false;
  for (;;) {
    ledState = !ledState;
    digitalWrite(IR_LED_PIN, ledState ? HIGH : LOW);
    Serial.printf("IR LED GPIO%d -> %s\n", (int)IR_LED_PIN, ledState ? "HIGH" : "LOW");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ─── Background Scanner / Sniffer Task (Core 0) ──────────────────────────────
void scanTask(void* parameter) {
  uint32_t localScanId = 0;
  TickType_t lastCycleStart = xTaskGetTickCount();
  for (;;) {
    const uint32_t currentScanId = ++localScanId;

    // Handle Promiscuous Deep Scan trigger
    if (triggerDeepScan) {
      triggerDeepScan = false;
      Serial.println("[DEEPSCAN] Starting 2.4GHz promiscuous sweep (Channels 1-13)...");

      portENTER_CRITICAL(&sniffMux);
      clientCount = 0;
      memset(discoveredClients, 0, sizeof(discoveredClients));
      currentSnifferMode = SNIFFER_DEEPSCAN;
      portEXIT_CRITICAL(&sniffMux);

      startPromiscuous(1);

      const uint8_t channelsToSweep[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
      const size_t totalChannels = sizeof(channelsToSweep) / sizeof(channelsToSweep[0]);
      for (size_t i = 0; i < totalChannels; i++) {
        uint8_t ch = channelsToSweep[i];
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        vTaskDelay(pdMS_TO_TICKS(400));
      }

      stopPromiscuousAndReconnect();

      String newClientsJson = buildClientsJson();
      if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        clientsJson = newClientsJson;
        xSemaphoreGive(jsonMutex);
      }
      Serial.printf("[DEEPSCAN] Finished. Discovered %d clients.\n", (int)clientCount);
      lastCycleStart = xTaskGetTickCount();
      continue;
    }

    // Handle Direct VBR Active Probe trigger
    if (triggerProbe) {
      triggerProbe = false;
      Serial.printf("[PROBE] Phase: %s, Ch: %d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    targetProbePhase.c_str(), (int)targetProbeChannel,
                    targetProbeMac[0], targetProbeMac[1], targetProbeMac[2],
                    targetProbeMac[3], targetProbeMac[4], targetProbeMac[5]);

      if (targetProbePhase == "lock" || targetProbeChannel == 0) {
        // Step 1: Channel Lock - sweep channels 1–13
        portENTER_CRITICAL(&sniffMux);
        currentSnifferMode = SNIFFER_PROBE;
        probePackets = 0;
        probeBytes = 0;
        probeLastRssi = -110;
        memset((void*)probeChannelHits, 0, sizeof(probeChannelHits));
        portEXIT_CRITICAL(&sniffMux);

        startPromiscuous(1);

        for (uint8_t ch = 1; ch <= 13; ch++) {
          esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
          vTaskDelay(pdMS_TO_TICKS(190));
        }

        stopPromiscuousAndReconnect();

        uint8_t bestCh = 0;
        uint32_t maxHits = 0;
        for (uint8_t ch = 1; ch <= 13; ch++) {
          if (probeChannelHits[ch] > maxHits) {
            maxHits = probeChannelHits[ch];
            bestCh = ch;
          }
        }

        char macBuf[18];
        macToStr(targetProbeMac, macBuf);
        String res = "{";
        res += "\"phase\":\"lock\",";
        res += "\"mac\":\"" + String(macBuf) + "\",";
        res += "\"channel\":" + String(bestCh) + ",";
        res += "\"found\":" + String(bestCh > 0 ? "true" : "false") + ",";
        res += "\"packets\":" + String(maxHits);
        res += "}";

        if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          probeResultJson = res;
          xSemaphoreGive(jsonMutex);
        }
      } else {
        // Step 2 or 3: 2.0s listening window on locked channel
        portENTER_CRITICAL(&sniffMux);
        currentSnifferMode = SNIFFER_PROBE;
        probePackets = 0;
        probeBytes = 0;
        probeLastRssi = -110;
        portEXIT_CRITICAL(&sniffMux);

        startPromiscuous(targetProbeChannel);

        vTaskDelay(pdMS_TO_TICKS(2000));

        stopPromiscuousAndReconnect();

        portENTER_CRITICAL(&sniffMux);
        uint32_t pkts = probePackets;
        uint32_t bytes = probeBytes;
        int8_t lastR = probeLastRssi;
        portEXIT_CRITICAL(&sniffMux);

        char macBuf[18];
        macToStr(targetProbeMac, macBuf);
        float pps = (float)pkts / 2.0f;
        float bps = (float)bytes / 2.0f;

        String res = "{";
        res += "\"phase\":\"" + targetProbePhase + "\",";
        res += "\"mac\":\"" + String(macBuf) + "\",";
        res += "\"channel\":" + String(targetProbeChannel) + ",";
        res += "\"packets\":" + String(pkts) + ",";
        res += "\"bytes\":" + String(bytes) + ",";
        res += "\"pps\":" + String(pps, 1) + ",";
        res += "\"bps\":" + String(bps, 1) + ",";
        res += "\"lastRssi\":" + String(lastR);
        res += "}";

        if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          probeResultJson = res;
          xSemaphoreGive(jsonMutex);
        }
      }
      lastCycleStart = xTaskGetTickCount();
      continue;
    }

    const uint8_t mode = trackMode;
    const uint8_t ch = trackChannel;

    // Client tracking mode: quick 200ms promiscuous sniff on target channel
    if (mode == 3) {
      portENTER_CRITICAL(&sniffMux);
      memcpy(targetProbeMac, trackClientMac, 6);
      probeLastRssi = -110;
      probePackets = 0;
      currentSnifferMode = SNIFFER_TRACK_CLIENT;
      portEXIT_CRITICAL(&sniffMux);

      startPromiscuous(ch ? ch : 1);
      vTaskDelay(pdMS_TO_TICKS(200));
      esp_wifi_set_promiscuous(false);

      portENTER_CRITICAL(&sniffMux);
      currentSnifferMode = SNIFFER_OFF;
      int8_t crssi = (probePackets > 0) ? probeLastRssi : -110;
      bool gotPacket = (probePackets > 0);
      portEXIT_CRITICAL(&sniffMux);

      char macBuf[18];
      macToStr(trackClientMac, macBuf);
      String trackMacStr = String(macBuf);

      if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < clientCount; i++) {
          char cMac[18];
          macToStr(discoveredClients[i].mac, cMac);
          if (trackMacStr.equalsIgnoreCase(cMac)) {
            if (gotPacket) discoveredClients[i].rssi = crssi;
            break;
          }
        }
        String tempTrackWifi = "[{";
        tempTrackWifi += "\"ssid\":\"Client " + trackMacStr + "\",";
        tempTrackWifi += "\"bssid\":\"" + trackMacStr + "\",";
        tempTrackWifi += "\"mac\":\"" + trackMacStr + "\",";
        tempTrackWifi += "\"rssi\":" + String(gotPacket ? crssi : -110) + ",";
        tempTrackWifi += "\"channel\":" + String(ch) + ",";
        tempTrackWifi += "\"isClient\":true,";
        tempTrackWifi += "\"scanId\":" + String(currentScanId);
        tempTrackWifi += "}]";
        wifiJson = tempTrackWifi;
        completedWifiScanId = currentScanId;
        xSemaphoreGive(jsonMutex);
      }
      vTaskDelayUntil(&lastCycleStart, pdMS_TO_TICKS(250));
      continue;
    }

    // 1. Wi-Fi AP scan with Passive Sus Triage
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
        String ssid = WiFi.SSID(i);
        String bssid = WiFi.BSSIDstr(i);

        bool isSus = false;
        String reasonsJson = "[";
        bool firstR = true;

        if (checkSuspiciousName(ssid)) {
          isSus = true;
          if (!firstR) reasonsJson += ",";
          reasonsJson += "\"SSID matches camera keyword\"";
          firstR = false;
        }

        const char* vendor = checkCameraOuiStr(bssid);
        if (vendor != nullptr) {
          isSus = true;
          if (!firstR) reasonsJson += ",";
          reasonsJson += "\"OUI matches " + String(vendor) + " (IoT/Camera)\"";
          firstR = false;
        }
        reasonsJson += "]";

        tempWifi += "{";
        tempWifi += "\"ssid\":\"" + escapeJson(ssid) + "\",";
        tempWifi += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        tempWifi += "\"channel\":" + String(wch) + ",";
        tempWifi += "\"freq\":" + String(freq) + ",";
        tempWifi += "\"band\":\"" + band + "\",";
        tempWifi += "\"bssid\":\"" + bssid + "\",";
        tempWifi += "\"sus\":" + String(isSus ? "true" : "false") + ",";
        tempWifi += "\"reasons\":" + reasonsJson + ",";
        tempWifi += "\"scanId\":" + String(currentScanId);
        tempWifi += "}";
      }
      WiFi.scanDelete();
    }
    tempWifi += "]";

    if (triggerDeepScan || triggerProbe) continue;

    // 2. BLE scan with Passive Sus Triage
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
          String deviceId = dev.getAddress().toString().c_str();
          if (name == "") name = deviceId;

          bool isSus = false;
          String reasonsJson = "[";
          bool firstR = true;

          if (checkSuspiciousName(name)) {
            isSus = true;
            if (!firstR) reasonsJson += ",";
            reasonsJson += "\"Device name matches camera keyword\"";
            firstR = false;
          }

          const char* vendor = checkCameraOuiStr(deviceId);
          if (vendor != nullptr) {
            isSus = true;
            if (!firstR) reasonsJson += ",";
            reasonsJson += "\"OUI matches " + String(vendor) + " (IoT/Camera)\"";
            firstR = false;
          }

          if (dev.haveServiceUUID()) {
            String suuid = dev.getServiceUUID().toString().c_str();
            suuid.toLowerCase();
            if (suuid.indexOf("fd50") >= 0) {
              isSus = true;
              if (!firstR) reasonsJson += ",";
              reasonsJson += "\"BLE Tuya pairing beacon (0xFD50)\"";
              firstR = false;
            } else if (suuid.indexOf("fe50") >= 0) {
              isSus = true;
              if (!firstR) reasonsJson += ",";
              reasonsJson += "\"BLE SmartLife/IoT beacon (0xFE50)\"";
              firstR = false;
            }
          }
          reasonsJson += "]";

          tempBle += "{";
          tempBle += "\"id\":\"" + deviceId + "\",";
          tempBle += "\"name\":\"" + escapeJson(name) + "\",";
          tempBle += "\"rssi\":" + String(dev.getRSSI()) + ",";
          tempBle += "\"sus\":" + String(isSus ? "true" : "false") + ",";
          tempBle += "\"reasons\":" + reasonsJson + ",";
          tempBle += "\"scanId\":" + String(currentScanId);
          tempBle += "}";
        }
      }
      scan->clearResults();
    }
    tempBle += "]";

    // Update shared JSON strings safely using mutex
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

    if (triggerDeepScan || triggerProbe) continue;

    // Full scans every second; target-focused scans run back to back
    vTaskDelayUntil(&lastCycleStart, pdMS_TO_TICKS(mode == 0 ? 1000 : 250));
  }
}

// ─── WebServer Route Handlers ────────────────────────────────────────────────
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

void handleClients() {
  String response;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = clientsJson;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "[]";
  }
  server.send(200, "application/json", response);
}

void handleDeepScan() {
  server.send(200, "application/json", "{\"ok\":true,\"waitMs\":5500}");
  server.client().flush();
  vTaskDelay(pdMS_TO_TICKS(250));
  triggerDeepScan = true;
}

void handleProbe() {
  String macStr = server.arg("mac");
  parseMacBytes(macStr, targetProbeMac);
  targetProbeChannel = server.hasArg("ch") ? (uint8_t)server.arg("ch").toInt() : 0;
  targetProbePhase = server.hasArg("phase") ? server.arg("phase") : "baseline";

  server.send(200, "application/json", "{\"ok\":true,\"waitMs\":2500}");
  server.client().flush();
  vTaskDelay(pdMS_TO_TICKS(250));
  triggerProbe = true;
}

void handleProbeResult() {
  String response;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = probeResultJson;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "{}";
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
  Serial.println("[ESP32 ALERT] Spy Camera trigger POST received! WhatsApp alert simulated.");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Green API message simulated via ESP32 Serial Out.\"}");
}

// GET /track?type=wifi&ch=6           -> scan only that Wi-Fi channel
// GET /track?type=client&mac=...&ch=N -> promiscuous track client MAC
// GET /track?type=ble                 -> scan only Bluetooth
// GET /track?off=1                    -> back to full Wi-Fi + BLE scanning
void handleTrack() {
  if (server.hasArg("off")) {
    trackMode = 0;
    trackChannel = 0;
    memset(trackClientMac, 0, 6);
  } else {
    String t = server.arg("type");
    if (t == "client") {
      trackMode = 3;
      parseMacBytes(server.arg("mac"), trackClientMac);
      trackChannel = server.hasArg("ch") ? (uint8_t)server.arg("ch").toInt() : 0;
    } else if (t == "wifi") {
      trackMode = 1;
      trackChannel = server.hasArg("ch") ? (uint8_t)server.arg("ch").toInt() : 0;
    } else if (t == "ble") {
      trackMode = 2;
      trackChannel = 0;
    } else {
      trackMode = 0;
      trackChannel = 0;
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// ─── Embedded Web Application (HTML / CSS / JS) ──────────────────────────────
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

    /* ─── Wireless radar & Deep Scan / Active Probe Styling ─── */
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

    .btn-deepscan { background: linear-gradient(135deg, #a61b4d, #d6336c); color: #fff; border: none;
      border-radius: 8px; padding: 5px 11px; font-size: .72rem; font-weight: 700; cursor: pointer;
      display: inline-flex; align-items: center; gap: 4px; box-shadow: 0 2px 6px rgba(214,51,108,.25); }
    .btn-deepscan:disabled { opacity: .55; cursor: not-allowed; }

    .btn-baseline { background: #fff0f5; color: #a61b4d; border: 1px solid var(--border-color);
      border-radius: 8px; padding: 5px 11px; font-size: .72rem; font-weight: 700; cursor: pointer;
      display: inline-flex; align-items: center; gap: 4px; transition: background-color .15s ease; }
    .btn-baseline:active { transform: scale(.96); background-color: #ffe2eb; }

    .baseline-filter-container { display: flex; align-items: center; justify-content: space-between;
      margin-bottom: 10px; padding: 6px 10px; background-color: #fff7fa; border: 1px dashed var(--border-color);
      border-radius: 10px; font-size: .75rem; }
    .baseline-toggle-label { display: flex; align-items: center; gap: 6px; cursor: pointer; font-weight: 600; color: var(--text-muted); user-select: none; }
    .baseline-toggle-label input[type="checkbox"] { accent-color: var(--accent-blue); cursor: pointer; width: 14px; height: 14px; }
    .baseline-count-pill { font-size: .68rem; color: var(--text-muted); background: #ffe2eb; border-radius: 10px; padding: 2px 7px; font-weight: 600; }

    .manual-probe-bar { display: flex; gap: 6px; margin-bottom: 10px; align-items: center; }
    .probe-mac-input { flex: 2; background-color: #fff; border: 1px solid var(--border-color);
      border-radius: 8px; padding: 7px 10px; font-size: .76rem; color: var(--text-main); outline: none; font-family: monospace; }
    .probe-ch-input { width: 56px; background-color: #fff; border: 1px solid var(--border-color);
      border-radius: 8px; padding: 7px 4px; font-size: .76rem; color: var(--text-main); outline: none; text-align: center; }
    .btn-probe-sm { background-color: #6366f1; color: #fff; border: none; border-radius: 8px;
      padding: 7px 10px; font-size: .74rem; font-weight: 700; cursor: pointer; white-space: nowrap; }
    .btn-probe-sm:active { transform: scale(.96); }

    .signal-tabs { display: flex; gap: 6px; margin-bottom: 12px; }
    .signal-tab { flex: 1; padding: 7px 10px; border: 1px solid var(--border-color); border-radius: 10px;
      background: #fff7fa; color: var(--text-muted); font-size: .78rem; font-weight: 600; cursor: pointer; text-align: center; }
    .signal-tab.active { background-color: var(--accent-blue); color: #fff; border-color: var(--accent-blue); }
    .signal-tab-pane { display: none; }
    .signal-tab-pane.active { display: block; }
    .signal-list { display: flex; flex-direction: column; gap: 6px; max-height: 250px; overflow-y: auto; }
    .signal-item { display: flex; align-items: center; gap: 10px; background-color: #fff7fa;
      border: 1px solid var(--border-color); border-radius: 12px; padding: 9px 12px; transition: border-color .2s; }
    .signal-item.sus { border: 1.5px solid #e11d48; background-color: #fff0f3; }
    .signal-icon { font-size: 1.1rem; flex-shrink: 0; }
    .signal-info { flex: 1; min-width: 0; }
    .signal-name { font-size: .85rem; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; display: flex; align-items: center; gap: 4px; }
    .signal-meta { font-size: .72rem; color: var(--text-muted); margin-top: 1px; }

    .sus-badge { display: inline-flex; align-items: center; gap: 2px; background-color: #ffe4e6;
      color: #be123c; border: 1px solid #fda4af; border-radius: 5px; padding: 0 5px; font-size: .66rem; font-weight: 700; }
    .client-badge { display: inline-flex; align-items: center; gap: 2px; background-color: #e0f2fe;
      color: #0369a1; border: 1px solid #7dd3fc; border-radius: 5px; padding: 0 5px; font-size: .66rem; font-weight: 700; }
    .sus-reasons { font-size: .7rem; color: #be123c; margin-top: 3px; line-height: 1.25; }

    .rssi-bar-wrap { display: flex; flex-direction: column; align-items: flex-end; gap: 3px; flex-shrink: 0; }
    .rssi-value { font-size: .72rem; font-weight: 700; color: var(--text-muted); }
    .rssi-bar-bg { width: 52px; height: 6px; background-color: #f2c9d8; border-radius: 3px; overflow: hidden; }
    .rssi-bar-fill { height: 100%; border-radius: 3px; }

    .item-actions { display: flex; gap: 5px; align-items: center; flex-shrink: 0; }
    .btn-track { background-color: #fff0f5; color: var(--accent-blue); border: 1px solid var(--border-color);
      border-radius: 8px; padding: 6px 10px; font-size: .74rem; font-weight: 700; cursor: pointer; }
    .btn-track:hover { background-color: var(--accent-blue); color: #fff; }
    .btn-probe { background-color: #ede9fe; color: #6366f1; border: 1px solid #c4b5fd;
      border-radius: 8px; padding: 6px 8px; font-size: .72rem; font-weight: 700; cursor: pointer; }
    .btn-probe:hover { background-color: #6366f1; color: #fff; }

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

    /* Probe Modal Styling */
    .probe-modal-box { max-width: 380px; text-align: left; }
    .modal-close-btn { background: none; border: none; font-size: 1.1rem; color: var(--text-muted); cursor: pointer; padding: 4px; }
    .probe-target-chip { background-color: #f1f5f9; border: 1px solid var(--border-color); border-radius: 8px;
      padding: 6px 10px; font-size: .78rem; color: var(--text-main); margin-bottom: 12px; }
    .probe-step { border: 1px solid var(--border-color); background: #fff; border-radius: 10px; padding: 10px; margin-bottom: 8px; }
    .probe-step.active { border-color: #6366f1; box-shadow: 0 0 0 1px #6366f1; }
    .probe-step.completed { border-color: var(--accent-green); background-color: #f0fdf4; }
    .probe-step-title { font-size: .8rem; font-weight: 700; color: var(--text-main); margin-bottom: 3px; }
    .probe-step-desc { font-size: .74rem; color: var(--text-muted); margin-bottom: 8px; line-height: 1.25; }
    .probe-val-row { display: flex; gap: 12px; font-size: .76rem; margin-bottom: 8px; background: #f8fafc; padding: 4px 8px; border-radius: 6px; }
    .probe-progress { height: 5px; background-color: #e2e8f0; border-radius: 3px; overflow: hidden; margin-bottom: 8px; }
    .probe-progress-bar { height: 100%; width: 0%; background-color: #6366f1; transition: width .1s linear; }
    .probe-verdict-box { border-radius: 10px; padding: 12px; margin-top: 10px; font-size: .82rem; line-height: 1.35; }
    .probe-verdict-box.alert { background-color: #fef2f2; border: 1.5px solid var(--sos-red); color: #991b1b; }
    .probe-verdict-box.clean { background-color: #f0fdf4; border: 1.5px solid var(--accent-green); color: #166534; }
    .verdict-title { font-weight: 800; font-size: .88rem; margin-bottom: 4px; }

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
          <div style="display:flex; align-items:center; gap:8px;">
            <button class="btn-baseline" id="btnBaselineRoom">💾 Baseline Room</button>
            <button class="btn-baseline" id="btnResetBaseline">↺ Reset Baseline</button>
            <button class="btn-deepscan" id="btnDeepScan">🔍 Deep Scan (5.5s)</button>
            <span id="totalSignalCount" style="font-size:.75rem;color:var(--text-muted);">0 signals</span>
          </div>
        </div>
        <div class="signal-tabs">
          <button class="signal-tab active" id="stWifi">📶 Wi-Fi (<span id="wifiCount">0</span>)</button>
          <button class="signal-tab" id="stBle">🔵 Bluetooth (<span id="bleCount">0</span>)</button>
        </div>
        <div class="baseline-filter-container">
          <label class="baseline-toggle-label">
            <input type="checkbox" id="toggleFilterNewOnly">
            <span>Filter Baseline (Show New Only)</span>
          </label>
          <span class="baseline-count-pill" id="baselineCountPill">Baseline: 0 devices</span>
        </div>
        <div class="signal-tab-pane active" id="paneWifi">
          <div class="manual-probe-bar">
            <input class="probe-mac-input" type="text" id="manualProbeMac" placeholder="Audit MAC (XX:XX:XX:XX:XX:XX)" maxlength="17" autocomplete="off" spellcheck="false">
            <input class="probe-ch-input" type="number" id="manualProbeCh" placeholder="Ch (opt)" min="1" max="13">
            <button class="btn-probe-sm" id="btnManualProbe">🔦 Probe</button>
          </div>
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
    <span style="font-size:.7rem; color:var(--text-muted);">v1.4</span>
  </footer>

  <!-- SOS Modal -->
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

  <!-- Direct VBR Active Probe Modal -->
  <div class="modal-overlay" id="probeModal">
    <div class="modal-box probe-modal-box">
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
        <h3 style="font-size:1rem; color:var(--text-main); display:flex; align-items:center; gap:6px;">
          <span>🔦</span> Direct VBR Active Probe
        </h3>
        <button class="modal-close-btn" id="probeModalClose">✕</button>
      </div>

      <div class="probe-target-chip" id="probeTargetInfo">
        Target: <strong>--</strong> · Ch: <span id="probeTargetCh">--</span>
      </div>

      <!-- Step 1: Channel Lock -->
      <div class="probe-step" id="probeStep1">
        <div class="probe-step-title">Step 1: Channel Lock</div>
        <div class="probe-step-desc" id="probeStep1Desc">Channel unknown. Sweep channels 1–13 to locate transmission.</div>
        <div class="probe-progress hidden" id="probeProg1"><div class="probe-progress-bar" id="probeProgBar1"></div></div>
        <button class="btn btn-secondary" id="btnProbeLock" style="width:100%;">Sweep & Lock Channel (2.5s)</button>
      </div>

      <!-- Step 2: Still Baseline -->
      <div class="probe-step" id="probeStep2">
        <div class="probe-step-title">Step 2: Still Baseline (2s)</div>
        <div class="probe-step-desc">Hold the room completely still and tap Read Baseline.</div>
        <div class="probe-val-row hidden" id="probeBaselineVals">
          <div>Packets: <strong id="basePps">0/s</strong></div>
          <div>Traffic: <strong id="baseBps">0 KB/s</strong></div>
        </div>
        <div class="probe-progress hidden" id="probeProg2"><div class="probe-progress-bar" id="probeProgBar2"></div></div>
        <button class="btn btn-secondary" id="btnProbeBaseline" style="width:100%;">⏸ Read Baseline</button>
      </div>

      <!-- Step 3: Motion Probe -->
      <div class="probe-step" id="probeStep3">
        <div class="probe-step-title">Step 3: Motion Probe (2s)</div>
        <div class="probe-step-desc">Wave your hand or shine a flashlight around the room and tap Read Motion.</div>
        <div class="probe-val-row hidden" id="probeMotionVals">
          <div>Packets: <strong id="motionPps">0/s</strong></div>
          <div>Traffic: <strong id="motionBps">0 KB/s</strong></div>
        </div>
        <div class="probe-progress hidden" id="probeProg3"><div class="probe-progress-bar" id="probeProgBar3"></div></div>
        <button class="btn btn-secondary" id="btnProbeMotion" style="width:100%;" disabled>🏃 Read Motion</button>
      </div>

      <!-- Step 4: Verdict -->
      <div class="probe-verdict-box hidden" id="probeVerdictBox">
        <div class="verdict-title" id="verdictTitle">Evaluating...</div>
        <div class="verdict-details" id="verdictDetails"></div>
      </div>

      <div style="display:flex; gap:8px; margin-top:14px;">
        <button class="btn btn-secondary" id="btnProbeReset" style="display:none;">↺ Test Again</button>
        <button class="btn btn-secondary" id="btnProbeDone">Close</button>
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
    const GREEN_API_INSTANCE = 'ENTER_INSTANCE_ID_HERE';
    const GREEN_API_TOKEN    = 'ENTER_API_KEY_HERE';

    const KEY_NAME     = 'aegis_user_name';
    const KEY_CONTACT  = 'aegis_emergency_contact';
    const KEY_CODEWORD = 'aegis_code_word';

    const systemLog = document.getElementById('systemLog');
    function logMessage(msg) {
      const time = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
      systemLog.textContent = `[${time}] ${msg}`;
    }
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
    const STATIC_THRESHOLD_MS = 800;

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

    const getOpticalCandidate = (r, g, b) => {
      if (r >= 60 && b >= 60 && (r > g + 10 || b > g + 10)) {
        const hsl = rgbToHsl(r, g, b);
        if (hsl.hue >= 260 && hsl.hue <= 355 && hsl.saturation >= 0.15 && hsl.lightness >= 0.20) {
          return { type: 'IR', name: 'IR Lens' };
        }
      }
      if (r >= 200 && g >= 200 && b >= 200) return { type: 'WHITE', name: 'White LED' };
      if (r >= 110 && r > g + 35 && r > b + 35) return { type: 'RED', name: 'Red LED' };
      if (g >= 110 && g > r + 35 && g > b + 35) return { type: 'GREEN', name: 'Green LED' };
      if (b >= 110 && b > r + 35 && b > g + 35) return { type: 'BLUE', name: 'Blue LED' };
      if (r >= 140 && g >= 95 && b < 90 && (r > b + 45)) return { type: 'AMBER', name: 'Amber LED' };
      const max = Math.max(r, g, b);
      const min = Math.min(r, g, b);
      const lum = (0.299 * r + 0.587 * g + 0.114 * b);
      if (max >= 145 && (max - min) >= 35 && lum >= 95) return { type: 'COLOR', name: 'Bright Color' };
      if (lum >= 155) return { type: 'OPTICAL', name: 'Optical Light' };
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

    function updateMultiTargetTracking(blobs, imageData, now) {
      const { data, width, height } = imageData;
      const matchedBlobIndices = new Set();
      const maxDistance = 65;

      trackedTargets.forEach(target => {
        let bestBlob = null, bestDist = Infinity, bestIdx = -1;
        blobs.forEach((blob, idx) => {
          if (matchedBlobIndices.has(idx)) return;
          const dist = Math.hypot(blob.x - target.x, blob.y - target.y);
          if (dist <= maxDistance && dist < bestDist) {
            bestDist = dist; bestBlob = blob; bestIdx = idx;
          }
        });

        if (bestBlob) {
          matchedBlobIndices.add(bestIdx);
          target.x = target.x * 0.6 + bestBlob.x * 0.4;
          target.y = target.y * 0.6 + bestBlob.y * 0.4;
          target.minX = bestBlob.minX; target.minY = bestBlob.minY;
          target.width = bestBlob.width; target.height = bestBlob.height;
          target.type = bestBlob.type;
          target.lastSeen = now;
          target.isOn = true;
        } else {
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

        analyzeTargetBlink(target, now);
      });

      trackedTargets = trackedTargets.filter(t => (now - t.lastSeen) <= 2000);

      blobs.forEach((blob, idx) => {
        if (matchedBlobIndices.has(idx) || trackedTargets.length >= 8) return;
        trackedTargets.push({
          id: nextTargetId++, x: blob.x, y: blob.y,
          minX: blob.minX, minY: blob.minY, width: blob.width, height: blob.height,
          type: blob.type, isOn: true, lastSeen: now, lastBlinkState: true,
          lastTransitionAt: now, transitions: [], score: 0, status: 'ANALYZING', interval: 0
        });
      });
    }

    function analyzeTargetBlink(target, now) {
      const isOn = target.isOn;
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
      if (isOn && timeInCurrentState > STATIC_THRESHOLD_MS) {
        target.transitions = []; target.status = 'STATIC'; target.score = 0; target.interval = 0;
        return;
      }

      if (target.transitions.length < 3) {
        target.status = 'ANALYZING';
        target.score = Math.round((target.transitions.length / 3) * 35);
        target.interval = 0;
        return;
      }

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
        target.score = 0; target.status = 'WRONG_RATE'; target.interval = Math.round(avg);
      }
    }

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
          strokeColor = '#ef4444'; label = `DEMO IR SIGNAL (${target.score}%)`;
        } else if (target.status === 'CONFIRMED') {
          strokeColor = '#ef4444';
          const rateText = target.interval > 0 ? ` (~${(target.interval / 1000).toFixed(2)}s)` : ' (~0.5s)';
          label = `⚠️ CAMERA DETECTED [${typeLabel}]${rateText}`;
        } else if (target.status === 'STATIC') {
          strokeColor = '#94a3b8'; label = `STATIC ${typeLabel} (IGNORED)`; lineDash = [4, 4];
        } else if (target.status === 'WRONG_RATE') {
          strokeColor = '#eab308'; label = `${typeLabel}: ${(target.interval / 1000).toFixed(2)}s (NOT 0.5s)`; lineDash = [3, 3];
        } else {
          strokeColor = '#3b82f6'; label = `ANALYZING ${typeLabel} (${target.score}%)`; lineDash = [2, 2];
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
      processingContext.fillStyle = '#ff00ff';
      processingContext.beginPath();
      processingContext.arc(160, 120, 6, 0, Math.PI * 2);
      processingContext.fill();
    }

    function drawCameraFrame() {
      const sourceWidth = cameraPreview.videoWidth;
      const sourceHeight = cameraPreview.videoHeight;
      if (!sourceWidth || !sourceHeight) return;

      const expectedAspect = sourceWidth + ' / ' + sourceHeight;
      if (cameraCanvasContainer.style.aspectRatio !== expectedAspect) {
        cameraCanvasContainer.style.aspectRatio = expectedAspect;
      }

      const containerRect = cameraCanvasContainer.getBoundingClientRect();
      const containerW = Math.round(containerRect.width);
      const containerH = Math.round(containerRect.height);
      if (containerW > 0 && containerH > 0) {
        if (detectionOverlay.width !== containerW || detectionOverlay.height !== containerH) {
          detectionOverlay.width = containerW;
          detectionOverlay.height = containerH;
        }
      }

      processingContext.drawImage(cameraPreview, 0, 0, sourceWidth, sourceHeight, 0, 0, processingCanvas.width, processingCanvas.height);
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
          video: { facingMode: { ideal: 'environment' }, width: { ideal: 1280 }, height: { ideal: 720 } },
          audio: false
        });
        const videoTrack = cameraStream.getVideoTracks()[0];
        if (videoTrack && videoTrack.getCapabilities && videoTrack.applyConstraints) {
          const capabilities = videoTrack.getCapabilities();
          const deviceControls = {};
          if (capabilities.zoom && capabilities.zoom.min <= 1 && capabilities.zoom.max >= 1) deviceControls.zoom = 1;
          if (capabilities.focusMode && capabilities.focusMode.includes('continuous')) deviceControls.focusMode = 'continuous';
          if (capabilities.exposureMode && capabilities.exposureMode.includes('continuous')) deviceControls.exposureMode = 'continuous';
          if (Object.keys(deviceControls).length) {
            try { await videoTrack.applyConstraints({ advanced: [deviceControls] }); } catch (_) {}
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
        logMessage('Camera started.');
      } catch (error) {
        const message = error.name === 'NotAllowedError'
          ? 'Camera permission denied.'
          : 'Camera could not start. Check permissions.';
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
      if (cameraStream) { stopCamera(); logMessage('Camera stopped.'); }
      else { startCamera(); }
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
      tabWireless.classList.add('active'); tabWired.classList.remove('active');
      panelWireless.classList.add('active'); panelWired.classList.remove('active');
      logMessage('Switched to Mode A: Wireless Camera Radar.');
    });

    tabWired.addEventListener('click', () => {
      tabWired.classList.add('active'); tabWireless.classList.remove('active');
      panelWired.classList.add('active'); panelWireless.classList.remove('active');
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
      if (!contact) { logMessage('⚠️ SOS: No emergency contact set.'); return; }
      const locationStr = (lat !== null && lon !== null) ? `https://maps.google.com/?q=${lat},${lon}` : 'Unavailable';
      const message = `🚨 EMERGENCY: ${name} has triggered an SOS alert. Location: ${locationStr}`;
      const chatId = contact.replace(/\D/g, '') + '@c.us';
      fetch(`https://api.green-api.com/waInstance${GREEN_API_INSTANCE}/sendMessage/${GREEN_API_TOKEN}`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ chatId, message })
      })
        .then(res => res.json())
        .then(() => logMessage('🚨 SOS alert sent via WhatsApp.'))
        .catch(() => logMessage('⚠️ SOS: WhatsApp delivery failed.'));
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
        logMessage('⚠️ Voice features require Chrome or Edge.');
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

    let isMicOn = false;
    let continuousRec = null;
    const IS_FILE_PROTOCOL = location.protocol === 'file:';

    function startContinuousListening() {
      if (!SpeechRecognition) {
        logMessage('⚠️ Voice features require Chrome or Edge.');
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
          logMessage('MIC: Permission denied.');
          isMicOn = false; micBtn.textContent = '🎙 Mic OFF'; micBtn.classList.remove('mic-active');
        }
      };
      if (!IS_FILE_PROTOCOL) {
        continuousRec.onend = () => { if (isMicOn) continuousRec.start(); };
      } else {
        continuousRec.onend = () => {
          if (isMicOn) {
            isMicOn = false; micBtn.textContent = '🎙 Mic OFF'; micBtn.classList.remove('mic-active');
            logMessage('MIC: Paused after silence.');
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
    // ─── WIRELESS RADAR + 3-TIER SPY CAMERA DETECTION ─────────────────────────
    const $ = id => document.getElementById(id);
    const btnConnect = $('btnConnect'), espIpInput = $('espIpInput');
    const connectedIpDisplay = $('connectedIpDisplay'), connectedIpText = $('connectedIpText');
    const refreshDot = $('refreshDot'), refreshLabel = $('refreshLabel');
    const wifiList = $('wifiList'), bleList = $('bleList');
    const wifiCount = $('wifiCount'), bleCount = $('bleCount'), totalSignalCount = $('totalSignalCount');
    const btnDeepScan = $('btnDeepScan'), btnBaselineRoom = $('btnBaselineRoom'), btnResetBaseline = $('btnResetBaseline');
    const toggleFilterNewOnly = $('toggleFilterNewOnly'), baselineCountPill = $('baselineCountPill');
    let filterNewOnly = false;
    const radarCanvas = $('radarCanvas'), rctx = radarCanvas.getContext('2d');
    const btnLinkGyro = $('btnLinkGyro'), gyroBtnText = $('gyroBtnText'), gyroStatusText = $('gyroStatus');
    const btnHitWall = $('btnHitWall'), btnRead = $('btnRead'), btnDropTarget = $('btnDropTarget');
    const btnSimTarget = $('btnSimTarget');
    const wallPicker = $('wallPicker'), btnWallsDone = $('btnWallsDone');
    const wallBtns = Array.from(document.querySelectorAll('.wallbtn'));
    const instrMain = $('instrMain'), instrSub = $('instrSub'), spinnerEl = $('spinner');
    const connectCard = $('connectCard'), signalsCard = $('signalsCard');
    const demoHeadingRow = $('demoHeadingRow'), demoHeading = $('demoHeading');

    // Probe Modal Elements
    const probeModal = $('probeModal'), probeModalClose = $('probeModalClose'), btnProbeDone = $('btnProbeDone');
    const btnProbeReset = $('btnProbeReset');
    const probeStep1 = $('probeStep1'), probeStep2 = $('probeStep2'), probeStep3 = $('probeStep3');
    const btnProbeLock = $('btnProbeLock'), btnProbeBaseline = $('btnProbeBaseline'), btnProbeMotion = $('btnProbeMotion');
    const probeProgBar1 = $('probeProgBar1'), probeProg1 = $('probeProg1');
    const probeProgBar2 = $('probeProgBar2'), probeProg2 = $('probeProg2');
    const probeProgBar3 = $('probeProgBar3'), probeProg3 = $('probeProg3');
    const probeVerdictBox = $('probeVerdictBox'), verdictTitle = $('verdictTitle'), verdictDetails = $('verdictDetails');

    $('stWifi').addEventListener('click', () => {
      $('stWifi').classList.add('active'); $('stBle').classList.remove('active');
      $('paneWifi').classList.add('active'); $('paneBle').classList.remove('active');
    });
    $('stBle').addEventListener('click', () => {
      $('stBle').classList.add('active'); $('stWifi').classList.remove('active');
      $('paneBle').classList.add('active'); $('paneWifi').classList.remove('active');
    });

    const NUM = 8;
    const NAMES = ['North', 'North-East', 'East', 'South-East', 'South', 'South-West', 'West', 'North-West'];
    const STEPS_PER_MOVE = 5;
    const READ_COUNT = 2;
    const RSSI_DEADBAND_DB = 2;
    const SCORE_PER_DB = 3;
    const MAX_SCORE_CHANGE = 24;
    const NEIGHBOR_SHARE = 0.4;
    const RSSI_SAMPLE_MIN = -110;
    const RSSI_SAMPLE_MAX = 0;
    const POLL_LIST_MS = 1000;
    const POLL_TRACK_MS = 350;

    let espBaseUrl = '', isConnected = false, scanInterval = null, scanInFlight = false;
    let lastWifiData = [], lastBleData = [], lastClientsData = [];
    let lastListFetch = 0;
    let demoSimActive = false, demoTimer = null;

    let heading = 0;
    let gyroOk = false, absSeen = false, gyroListenersAttached = false, gyroCheckTimer = null;

    let target = null;
    let phase = 'IDLE';
    let readingPurpose = 'baseline';
    let sectorScores = Array(NUM).fill(50);
    let testedSectors = Array(NUM).fill(false);
    let walls = new Array(NUM).fill(false);
    let recommendedSector = null;
    let bestBearing = null;
    let missCount = 0;
    let lastProcessedScanId = null;
    let samples = [];
    let prevAvg = null;
    let movedSector = null;
    let lastNote = '';

    const DEMO_CAM = { x: 20, y: 0 };
    let demoPos = { x: 0, y: 0 };

    const secOf = h => Math.floor((((h % 360) + 360) % 360 + 22.5) / 45) % NUM;
    const clampScore = score => Math.max(0, Math.min(100, score));
    function resetSectors() {
      sectorScores = Array(NUM).fill(50);
      testedSectors = Array(NUM).fill(false);
      walls = new Array(NUM).fill(false);
      recommendedSector = 0;
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
      if (e.webkitCompassHeading != null) h = e.webkitCompassHeading;
      else if (e.alpha != null) h = (360 - e.alpha + 360) % 360;
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
        setGyroStatus('blocked — HTTPS or secure origin required.');
        gyroBtnText.textContent = 'Compass blocked';
        return;
      }
      if (typeof DeviceOrientationEvent === 'undefined') {
        setGyroStatus('browser has no orientation sensor.');
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
        if (!gyroOk) { setGyroStatus('no data. Check sensor permission.'); gyroBtnText.textContent = 'Not detected'; }
      }, 5000);
      if (typeof DeviceOrientationEvent.requestPermission === 'function') {
        DeviceOrientationEvent.requestPermission().then(p => {
          if (p !== 'granted') { clearTimeout(gyroCheckTimer); setGyroStatus('permission denied.'); }
        }).catch(() => setGyroStatus('permission error.'));
      }
    }
    btnLinkGyro.addEventListener('click', initSensors);

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
        walls = new Array(NUM).fill(false);
        candidates = [0, 1, 2, 3, 4, 5, 6, 7];
        logMessage('All directions were marked as walls; marks reset.');
      }
      let best = candidates[0];
      for (const i of candidates.slice(1)) {
        if (sectorScores[i] > sectorScores[best] + 0.001) best = i;
      }
      if (recommendedSector !== null && candidates.includes(recommendedSector) &&
          Math.abs(sectorScores[recommendedSector] - sectorScores[best]) <= 0.001) best = recommendedSector;
      recommendedSector = best;
      bestBearing = best * 45;
    }

    function resetMeasurementHistory() {
      samples = []; prevAvg = null; movedSector = null; lastNote = ''; missCount = 0; lastProcessedScanId = null;
    }

    function startReading(purpose) {
      if (!target || phase === 'READING') return;
      readingPurpose = purpose;
      samples = []; missCount = 0; phase = 'READING';
      const current = (target.type === 'wifi' || target.type === 'client') ? lastWifiData : lastBleData;
      const cur = Number(current && current.scanId);
      lastProcessedScanId = Number.isFinite(cur) && cur > 0 ? cur : null;
      if (purpose === 'measure') {
        movedSector = recommendedSector;
        if (demoSimActive && movedSector !== null) moveDemoUser(movedSector);
      }
      logMessage(`Reading ${READ_COUNT} fresh values — hold still.`);
      render();
    }

    function setEspTrack(type, channel, mac) {
      if (!isConnected || !espBaseUrl || demoSimActive) return;
      let q = 'off=1';
      if (type) {
        q = `type=${type}${channel ? '&ch=' + channel : ''}${mac ? '&mac=' + encodeURIComponent(mac) : ''}`;
      }
      fetch(`${espBaseUrl}/track?${q}`, { cache: 'no-store' }).catch(() => {});
    }

    function lockTarget(type, id, name, channel) {
      if (!id) {
        logMessage('Cannot track this device because its identifier is unavailable.');
        return;
      }
      target = { type, id: String(id), name };
      setEspTrack(type, Number(channel) || 0, id);
      resetSectors();
      resetMeasurementHistory();
      phase = 'IDLE';
      setTrackingUI(true);
      logMessage(`Tracking only ${name} (${target.id}). Taking baseline reading.`);
      window.scrollTo(0, 0);
      document.querySelector('main').scrollTop = 0;
      startReading('baseline');
    }

    function dropTarget() {
      if (!target) return;
      logMessage(`Stopped tracking ${target.name}.`);
      setEspTrack(null);
      target = null; phase = 'IDLE'; bestBearing = null;
      resetSectors();
      resetMeasurementHistory();
      setTrackingUI(false);
      renderWifiList(lastWifiData); renderBleList(lastBleData);
      render();
    }
    btnDropTarget.addEventListener('click', dropTarget);

    btnRead.addEventListener('click', () => {
      if (!target || phase !== 'MOVE' || recommendedSector === null) return;
      startReading('measure');
    });

    function addScore(i, v) { sectorScores[i] = clampScore(sectorScores[i] + v); }

    function applyDelta(delta) {
      const s = movedSector;
      if (s === null || s === undefined) return;
      testedSectors[s] = true;
      const abs = Math.abs(delta);
      let mag = Math.min(MAX_SCORE_CHANGE, abs * SCORE_PER_DB);
      if (abs < RSSI_DEADBAND_DB) mag *= 0.5;
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
      walls = new Array(NUM).fill(false);
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

    btnHitWall.addEventListener('click', () => {
      if (!target || phase !== 'MOVE') return;
      phase = 'WALLS'; render();
    });
    wallBtns.forEach(b => b.addEventListener('click', () => {
      if (!target || phase !== 'WALLS') return;
      const i = Number(b.dataset.s);
      walls[i] = !walls[i]; render();
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
        setInstr('Tap Track on a device', 'Pick a Wi-Fi AP, discovered Client, or BLE beacon from the list');
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
          setInstr('Target signal quiet', `Waiting for packet from ${target.name} (${target.id}). Hold still...`, 'warn', true);
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
        setInstr('MARK THE WALLS', `${lastNote} Tap directions blocked by a wall, then tap Done.`, 'turn');
        return;
      }
      if (!gyroOk) {
        setInstr('Tap Link Compass', 'Needed to orient direction sectors.', 'warn');
        return;
      }
      if (recommendedSector === null) {
        setInstr('NO DIRECTION AVAILABLE', 'Tap Edit walls and adjust marks.', 'warn');
        return;
      }
      const direction = NAMES[recommendedSector];
      const score = Math.round(sectorScores[recommendedSector]);
      const rel = ((recommendedSector * 45 - heading) % 360 + 360) % 360;
      const blocked = NAMES.filter((n, i) => walls[i]);
      const wallNote = blocked.length ? `Ignoring walls: ${blocked.join(', ')}. ` : '';
      const close = prevAvg !== null && prevAvg >= -45 ? ' Very strong signal: target is close, search by sight.' : '';
      setInstr(`WALK ${STEPS_PER_MOVE} STEPS ${direction.toUpperCase()}`,
        `${lastNote} ${wallNote}That is ${relWord(rel)}. When you arrive, tap Read here. Sector score ${score}/100.${close}`, 'go');
    }

    function drawRadar() {
      const W = radarCanvas.width, H = radarCanvas.height;
      const cx = W / 2, cy = H / 2, maxR = Math.min(cx, cy) - 8;
      const rad = deg => (deg - heading - 90) * Math.PI / 180;
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

      rctx.fillStyle = '#35212b';
      rctx.beginPath(); rctx.moveTo(cx, 3); rctx.lineTo(cx - 7, 15); rctx.lineTo(cx + 7, 15); rctx.closePath(); rctx.fill();

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

      if (target && phase === 'READING') {
        const t = (performance.now() % 1200) / 1200;
        for (let k = 0; k < 2; k++) {
          const p = (t + k * 0.5) % 1;
          rctx.beginPath(); rctx.arc(cx, cy, 10 + p * (maxR - 10), 0, Math.PI * 2);
          rctx.strokeStyle = `rgba(214,51,108,${0.55 * (1 - p)})`;
          rctx.lineWidth = 3; rctx.stroke();
        }
      }

      rctx.beginPath(); rctx.arc(cx, cy, 9, 0, Math.PI * 2);
      rctx.fillStyle = '#35212b'; rctx.fill();
      rctx.fillStyle = '#fff'; rctx.font = 'bold 8px system-ui'; rctx.textAlign = 'center'; rctx.textBaseline = 'middle';
      rctx.fillText('YOU', cx, cy);
    }

    function animateRadar() { drawRadar(); requestAnimationFrame(animateRadar); }

    function moveDemoUser(sector) {
      const b = sector * 45 * Math.PI / 180;
      demoPos.x += Math.sin(b) * 5; demoPos.y += Math.cos(b) * 5;
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
        lastWifiData = [{ ssid: 'SPY_CAM_WIFI', bssid: 'AA:BB:CC:DD:EE:01', rssi: -62, channel: 6, sus: true, reasons: ['Simulated target'] }];
        lastBleData = [];
        demoTimer = setInterval(() => { if (target && phase === 'READING') onSample(demoRssi()); }, 700);
        lockTarget('wifi', 'AA:BB:CC:DD:EE:01', 'SPY_CAM_WIFI');
        logMessage('Demo started. Fake camera is East.');
      } else {
        clearInterval(demoTimer); demoTimer = null;
        gyroOk = false; gyroBtnText.textContent = 'Link Compass'; btnLinkGyro.classList.remove('linked');
        setGyroStatus('not linked');
        dropTarget();
      }
    });
    demoHeading.addEventListener('input', () => { if (demoSimActive) { heading = +demoHeading.value; render(); } });

    // ─── Direct VBR Active Probe Frontend Logic ──────────────────────────────
    let probeTarget = { mac: '', channel: 0, name: '' };
    let baselineData = null, motionData = null;

    function openProbeModal(mac, channel, name) {
      if (!isConnected || !espBaseUrl) {
        logMessage('⚠️ Connect to ESP32 before probing.');
        return;
      }
      if (!mac) {
        logMessage('⚠️ Invalid MAC address.');
        return;
      }
      probeTarget = { mac: mac.toUpperCase(), channel: Number(channel) || 0, name: name || mac };
      $('probeTargetInfo').innerHTML = `Target: <strong>${esc(probeTarget.mac)}</strong> (${esc(probeTarget.name)}) · Ch: <span id="probeTargetCh">${probeTarget.channel || 'Unknown'}</span>`;

      baselineData = null; motionData = null;
      probeVerdictBox.classList.add('hidden');
      btnProbeReset.style.display = 'none';
      $('probeBaselineVals').classList.add('hidden');
      $('probeMotionVals').classList.add('hidden');
      btnProbeBaseline.disabled = false;
      btnProbeMotion.disabled = true;

      if (!probeTarget.channel || probeTarget.channel === 0) {
        probeStep1.classList.remove('completed');
        probeStep1.classList.add('active');
        btnProbeLock.disabled = false;
        btnProbeLock.style.display = 'block';
        $('probeStep1Desc').textContent = 'Channel unknown. Sweep channels 1–13 to locate transmission.';
        probeStep2.classList.remove('active', 'completed');
      } else {
        probeStep1.classList.add('completed');
        probeStep1.classList.remove('active');
        btnProbeLock.style.display = 'none';
        $('probeStep1Desc').textContent = `Locked on Channel ${probeTarget.channel}.`;
        probeStep2.classList.add('active');
        probeStep2.classList.remove('completed');
      }
      probeStep3.classList.remove('active', 'completed');

      probeModal.classList.add('active');
    }

    function closeProbeModal() {
      probeModal.classList.remove('active');
    }
    probeModalClose.addEventListener('click', closeProbeModal);
    btnProbeDone.addEventListener('click', closeProbeModal);

    function runProgressBar(barEl, wrapEl, durationMs) {
      wrapEl.classList.remove('hidden');
      barEl.style.width = '0%';
      const start = Date.now();
      return new Promise(resolve => {
        const timer = setInterval(() => {
          const elapsed = Date.now() - start;
          const pct = Math.min(100, (elapsed / durationMs) * 100);
          barEl.style.width = pct + '%';
          if (elapsed >= durationMs) {
            clearInterval(timer);
            setTimeout(() => { wrapEl.classList.add('hidden'); resolve(); }, 150);
          }
        }, 50);
      });
    }

    // Retry fetch until a valid JSON object is returned (not {} or empty).
    // requiredKey: if set, also waits until the response has that key with a real value.
    async function fetchJsonWithRetry(url, retries = 15, delayMs = 1000, requiredKey) {
      for (let i = 0; i < retries; i++) {
        try {
          const res = await fetch(url, { cache: 'no-store' });
          if (res.ok) {
            const data = await res.json();
            const headerValue = res.headers ? res.headers.get('X-Scan-Id') : null;
            if (Array.isArray(data) && headerValue !== null) {
              const headerId = Number(headerValue);
              if (Number.isFinite(headerId) && headerId > 0) data.scanId = headerId;
            }
            // If a requiredKey was specified, retry if the key is missing/undefined
            if (requiredKey && (data[requiredKey] === undefined || data[requiredKey] === null)) {
              await new Promise(r => setTimeout(r, delayMs));
              continue;
            }
            return data;
          }
        } catch (e) {}
        await new Promise(r => setTimeout(r, delayMs));
      }
      throw new Error("Failed after retries: " + url);
    }

    // Safe number formatter — returns '—' instead of NaN for missing values
    function fmtNum(val, decimals) {
      const n = Number(val);
      return Number.isFinite(n) ? n.toFixed(decimals) : '—';
    }

    btnProbeLock.addEventListener('click', async () => {
      btnProbeLock.disabled = true;
      btnProbeLock.textContent = '⏳ Sweeping channels 1–13...';
      logMessage(`Sweeping channels 1–13 for target ${probeTarget.mac}...`);
      scanInFlight = true;

      try {
        await fetch(`${espBaseUrl}/probe?mac=${encodeURIComponent(probeTarget.mac)}&ch=0&phase=lock`, { cache: 'no-store' });
        await runProgressBar(probeProgBar1, probeProg1, 2600);

        const res = await fetchJsonWithRetry(`${espBaseUrl}/probe-result`, 15, 1000, 'phase');
        if (res.found && res.channel > 0) {
          probeTarget.channel = res.channel;
          $('probeTargetCh').textContent = res.channel;
          $('probeStep1Desc').textContent = `✅ Channel ${res.channel} locked (${res.packets} packets captured).`;
          probeStep1.classList.add('completed');
          probeStep1.classList.remove('active');
          btnProbeLock.style.display = 'none';
          probeStep2.classList.add('active');
          logMessage(`Channel ${res.channel} locked for ${probeTarget.mac}. Ready for baseline.`);
        } else {
          $('probeStep1Desc').textContent = `⚠️ No packets heard across channels 1–13. Target might be quiet or out of range.`;
          btnProbeLock.disabled = false;
          btnProbeLock.textContent = 'Retry Channel Sweep (2.5s)';
          logMessage(`No transmissions detected for ${probeTarget.mac}.`);
        }
      } catch (err) {
        logMessage(`Probe error: ${err.message}`);
        btnProbeLock.disabled = false;
        btnProbeLock.textContent = 'Sweep & Lock Channel (2.5s)';
      } finally {
        scanInFlight = false;
      }
    });

    btnProbeBaseline.addEventListener('click', async () => {
      btnProbeBaseline.disabled = true;
      btnProbeBaseline.textContent = '⏳ Sampling Baseline (2s)...';
      logMessage(`Reading still baseline traffic for ${probeTarget.mac} on Ch ${probeTarget.channel}...`);
      scanInFlight = true;

      try {
        await fetch(`${espBaseUrl}/probe?mac=${encodeURIComponent(probeTarget.mac)}&ch=${probeTarget.channel}&phase=baseline`, { cache: 'no-store' });
        await runProgressBar(probeProgBar2, probeProg2, 2300);

        const res = await fetchJsonWithRetry(`${espBaseUrl}/probe-result`, 15, 1000, 'phase');
        baselineData = res;
        $('basePps').textContent = `${fmtNum(res.pps,1)}/s (${res.packets||0} pkts)`;
        $('baseBps').textContent = `${fmtNum((res.bps||0)/1024,2)} KB/s`;
        $('probeBaselineVals').classList.remove('hidden');

        probeStep2.classList.add('completed');
        probeStep2.classList.remove('active');
        btnProbeBaseline.textContent = '✅ Baseline Recorded';
        probeStep3.classList.add('active');
        btnProbeMotion.disabled = false;
        logMessage(`Baseline: ${res.packets||0} pkts (${fmtNum((res.bytes||0)/1024,2)} KB). Now read motion.`);
      } catch (err) {
        logMessage(`Baseline probe error: ${err.message}`);
        btnProbeBaseline.disabled = false;
        btnProbeBaseline.textContent = '⏸ Read Baseline';
      } finally {
        scanInFlight = false;
      }
    });

    btnProbeMotion.addEventListener('click', async () => {
      btnProbeMotion.disabled = true;
      btnProbeMotion.textContent = '⏳ Sampling Motion Traffic (2s)...';
      logMessage(`Reading motion-induced traffic for ${probeTarget.mac}...`);
      scanInFlight = true;

      try {
        await fetch(`${espBaseUrl}/probe?mac=${encodeURIComponent(probeTarget.mac)}&ch=${probeTarget.channel}&phase=motion`, { cache: 'no-store' });
        await runProgressBar(probeProgBar3, probeProg3, 2300);

        const res = await fetchJsonWithRetry(`${espBaseUrl}/probe-result`, 15, 1000, 'phase');
        motionData = res;
        $('motionPps').textContent = `${fmtNum(res.pps,1)}/s (${res.packets||0} pkts)`;
        $('motionBps').textContent = `${fmtNum((res.bps||0)/1024,2)} KB/s`;
        $('probeMotionVals').classList.remove('hidden');

        probeStep3.classList.add('completed');
        probeStep3.classList.remove('active');
        btnProbeMotion.textContent = '✅ Motion Recorded';

        evaluateProbeVerdict();
      } catch (err) {
        logMessage(`Motion probe error: ${err.message}`);
        btnProbeMotion.disabled = false;
        btnProbeMotion.textContent = '🏃 Read Motion';
      } finally {
        scanInFlight = false;
      }
    });

    function evaluateProbeVerdict() {
      const bBytes = baselineData ? (baselineData.bytes || 0) : 0;
      const mBytes = motionData ? (motionData.bytes || 0) : 0;
      const bPkts  = baselineData ? (baselineData.packets || 0) : 0;
      const mPkts  = motionData ? (motionData.packets || 0) : 0;

      const deltaBytes = mBytes - bBytes;
      const deltaPct = bBytes > 0 ? Math.round((deltaBytes / bBytes) * 100) : (mBytes > 0 ? 100 : 0);
      const deltaPkts = mPkts - bPkts;

      probeVerdictBox.classList.remove('hidden', 'alert', 'clean');

      const isSpike = (deltaPct >= 35 && deltaPkts >= 8) || (bPkts < 5 && mPkts >= 15);

      if (isSpike) {
        probeVerdictBox.classList.add('alert');
        verdictTitle.innerHTML = `🚨 VBR Spike Detected (+${deltaPct}% traffic on motion) — Likely Live Camera`;
        verdictDetails.innerHTML = `
          <strong>Motion Traffic:</strong> ${(mBytes/1024).toFixed(1)} KB (${mPkts} pkts)<br>
          <strong>Still Baseline:</strong> ${(bBytes/1024).toFixed(1)} KB (${bPkts} pkts)<br>
          <em>Device bitrate spiked sharply when motion occurred in the room, typical of real-time video encoders (H.264/H.265 VBR).</em>
        `;
        markDeviceAsSus(probeTarget.mac, 'Verified VBR motion spike');
        logMessage(`🚨 PROBE ALERT: ${probeTarget.mac} exhibited +${deltaPct}% VBR motion spike! Flagged as suspicious camera.`);
      } else {
        probeVerdictBox.classList.add('clean');
        verdictTitle.innerHTML = `✅ Stable Bitrate — No Motion Compression Spike`;
        verdictDetails.innerHTML = `
          <strong>Motion Traffic:</strong> ${(mBytes/1024).toFixed(1)} KB (${mPkts} pkts)<br>
          <strong>Still Baseline:</strong> ${(bBytes/1024).toFixed(1)} KB (${bPkts} pkts)<br>
          <strong>Delta:</strong> ${deltaPct > 0 ? '+' : ''}${deltaPct}% traffic difference (within normal idle fluctuations).
        `;
        logMessage(`Probe verdict for ${probeTarget.mac}: Stable bitrate (Delta: ${deltaPct}%).`);
      }

      btnProbeReset.style.display = 'block';
    }

    btnProbeReset.addEventListener('click', () => {
      openProbeModal(probeTarget.mac, probeTarget.channel, probeTarget.name);
    });

    $('btnManualProbe').addEventListener('click', () => {
      const mac = $('manualProbeMac').value.trim();
      const ch = parseInt($('manualProbeCh').value.trim(), 10) || 0;
      if (!mac || mac.length < 11) {
        logMessage('⚠️ Please enter a valid MAC address (e.g. 24:0A:C4:12:34:56).');
        return;
      }
      openProbeModal(mac, ch, 'Manual Target');
    });

    function markDeviceAsSus(mac, reason) {
      let found = false;
      const updateList = (arr) => {
        arr.forEach(item => {
          const id = item.bssid || item.mac || item.id || '';
          if (id.toLowerCase() === mac.toLowerCase()) {
            item.sus = true;
            if (!item.reasons) item.reasons = [];
            if (!item.reasons.includes(reason)) item.reasons.push(reason);
            found = true;
          }
        });
      };
      updateList(lastWifiData);
      updateList(lastBleData);
      updateList(lastClientsData);
      if (!found) {
        lastWifiData.push({
          ssid: `Probed Target (${mac})`,
          bssid: mac,
          mac: mac,
          channel: probeTarget.channel || 1,
          rssi: -60,
          isClient: true,
          sus: true,
          reasons: [reason]
        });
      }
      renderWifiList(lastWifiData);
      renderBleList(lastBleData);
    }

    // ─── Deep Scan Button Handler ─────────────────────────────────────────────
    btnDeepScan.addEventListener('click', async () => {
      if (!isConnected || !espBaseUrl) {
        logMessage('⚠️ Connect to ESP32 before running Deep Scan.');
        return;
      }
      btnDeepScan.disabled = true;
      btnDeepScan.textContent = '⏳ Sniffing (5.5s)...';
      logMessage('🔍 Deep Scan started: hopping 2.4GHz channels (1-13) to sniff hidden client uplink frames...');

      scanInFlight = true;
      try {
        let waitMs = 5500;
        try {
          const res = await (await fetch(`${espBaseUrl}/deepscan`, { cache: 'no-store' })).json();
          if (res && res.waitMs) waitMs = Number(res.waitMs);
        } catch (_) {}

        const startTime = Date.now(), endTime = startTime + waitMs;
        btnDeepScan.textContent = `⏳ Sniffing (${(waitMs / 1000).toFixed(1)}s)...`;
        const countTimer = setInterval(() => {
          const remaining = Math.max(0, (endTime - Date.now()) / 1000);
          btnDeepScan.textContent = `⏳ Sniffing (${remaining.toFixed(1)}s)...`;
        }, 100);

        await new Promise(r => setTimeout(r, waitMs));
        clearInterval(countTimer);
        btnDeepScan.textContent = '⏳ Fetching results...';

        const [clients, wifi] = await Promise.all([
          fetchJsonWithRetry(`${espBaseUrl}/clients`).catch(() => []),
          fetchJsonWithRetry(`${espBaseUrl}/wifi`).catch(() => [])
        ]);

        lastClientsData = clients;
        const mergedWifi = mergeClientsIntoWifi(wifi, clients);
        lastWifiData = mergedWifi;
        wifiCount.textContent = mergedWifi.length;
        totalSignalCount.textContent = `${mergedWifi.length + lastBleData.length} signals`;
        renderWifiList(mergedWifi);

        const susCount = clients.filter(c => c.sus).length;
        logMessage(`🔍 Deep Scan finished: found ${clients.length} hidden clients (${susCount} flagged suspicious).`);
      } catch (err) {
        logMessage(`Deep scan error: ${err.message || 'Request failed'}`);
      } finally {
        scanInFlight = false;
        btnDeepScan.disabled = false;
        btnDeepScan.textContent = '🔍 Deep Scan (5.5s)';
      }
    });

    function mergeClientsIntoWifi(wifiArr, clientArr) {
      const merged = [...wifiArr];
      if (!Array.isArray(clientArr)) return merged;

      clientArr.forEach(c => {
        const existing = merged.find(w => String(w.bssid || w.mac || '').toLowerCase() === String(c.mac || '').toLowerCase());
        if (!existing) {
          merged.push({
            ssid: c.ssid || `Client (${c.mac})`,
            bssid: c.mac,
            mac: c.mac,
            routerBssid: c.bssid,
            rssi: c.rssi,
            channel: c.channel,
            uplinkPackets: c.uplinkPackets,
            uplinkBytes: c.uplinkBytes,
            isClient: true,
            sus: c.sus,
            reasons: c.reasons || []
          });
        } else {
          existing.isClient = true;
          existing.routerBssid = c.bssid;
          existing.uplinkPackets = c.uplinkPackets;
          existing.uplinkBytes = c.uplinkBytes;
          if (c.sus) {
            existing.sus = true;
            existing.reasons = Array.from(new Set([...(existing.reasons || []), ...(c.reasons || [])]));
          }
        }
      });
      return merged;
    }

    // ─── Signal Lists Rendering with Passive Sus Triage ──────────────────────
    function makeActionBtns(type, id, name, channel) {
      const wrap = document.createElement('div');
      wrap.className = 'item-actions';

      const probeBtn = document.createElement('button');
      probeBtn.className = 'btn-probe';
      probeBtn.textContent = '🔦 Probe';
      probeBtn.addEventListener('click', e => {
        e.stopPropagation();
        openProbeModal(id, channel || 0, name);
      });

      const trackBtn = document.createElement('button');
      trackBtn.className = 'btn-track';
      trackBtn.textContent = 'Track';
      trackBtn.addEventListener('click', e => {
        e.stopPropagation();
        lockTarget(type, id, name, channel);
      });

      wrap.appendChild(probeBtn);
      wrap.appendChild(trackBtn);
      return wrap;
    }

    // ─── Room Memory / Baseline Snapshot Logic ────────────────────────────────
    function getStoredBaseline() {
      try {
        const stored = localStorage.getItem('aegis_room_baseline');
        return stored ? JSON.parse(stored) : [];
      } catch (e) {
        return [];
      }
    }

    function updateBaselinePill() {
      if (!baselineCountPill) return;
      const arr = getStoredBaseline();
      baselineCountPill.textContent = `Baseline: ${arr.length} devices`;
    }

    function isDeviceInBaseline(baselineSet, ...ids) {
      if (!baselineSet || !baselineSet.size) return false;
      for (const id of ids) {
        if (!id) continue;
        const normalized = String(id).trim().toUpperCase();
        if (baselineSet.has(normalized)) return true;
      }
      return false;
    }

    function saveRoomBaseline() {
      const set = new Set();
      const addId = id => {
        if (!id) return;
        const str = String(id).trim().toUpperCase();
        if (str && str !== 'UNAVAILABLE' && str !== 'NULL' && str !== 'UNDEFINED') {
          set.add(str);
        }
      };

      (lastWifiData || []).forEach(item => {
        if (item.bssid) addId(item.bssid);
        if (item.mac) addId(item.mac);
      });
      (lastClientsData || []).forEach(item => {
        if (item.mac) addId(item.mac);
        if (item.bssid) addId(item.bssid);
      });
      (lastBleData || []).forEach(item => {
        if (item.id) addId(item.id);
        if (item.mac) addId(item.mac);
        if (item.bssid) addId(item.bssid);
      });

      const baselineArray = Array.from(set);
      localStorage.setItem('aegis_room_baseline', JSON.stringify(baselineArray));
      updateBaselinePill();
      logMessage(`✅ Room baseline saved with ${baselineArray.length} unique devices.`);
      renderWifiList(lastWifiData);
      renderBleList(lastBleData);
    }

    if (btnBaselineRoom) {
      btnBaselineRoom.addEventListener('click', saveRoomBaseline);
    }
    if (btnResetBaseline) {
      btnResetBaseline.addEventListener('click', () => {
        localStorage.removeItem('aegis_room_baseline');
        const toggle = document.getElementById('toggleFilterNewOnly');
        if (toggle) toggle.checked = false;
        filterNewOnly = false;
        renderWifiList(lastWifiData);
        renderBleList(lastBleData);
        updateBaselinePill();
        logMessage("🧹 Room memory baseline cleared successfully.");
      });
    }
    if (toggleFilterNewOnly) {
      toggleFilterNewOnly.addEventListener('change', e => {
        filterNewOnly = !!e.target.checked;
        renderWifiList(lastWifiData);
        renderBleList(lastBleData);
      });
    }

    function renderWifiList(signals) {
      wifiList.innerHTML = '';
      let list = signals;

      if (filterNewOnly) {
        const baselineArr = getStoredBaseline();
        const baselineSet = new Set(baselineArr.map(s => String(s).trim().toUpperCase()));
        list = [];
        for (const raw of signals) {
          const inBase = isDeviceInBaseline(baselineSet, raw.bssid, raw.mac);
          if (inBase) continue;
          const item = { ...raw };
          item.sus = true;
          const warnText = '⚠️ New device introduced since last room baseline';
          item.reasons = item.reasons ? [...item.reasons] : [];
          if (!item.reasons.some(r => r.includes('New device introduced since last room baseline'))) {
            item.reasons.push(warnText);
          }
          list.push(item);
        }
      }

      if (!list.length) {
        wifiList.innerHTML = filterNewOnly
          ? '<div class="empty-state"><span>✅</span>No new Wi-Fi devices found (all match baseline)</div>'
          : '<div class="empty-state"><span>📡</span>No Wi-Fi devices found</div>';
        return;
      }
      // Sort: sus devices always appear at top, sorted by RSSI descending within group
      const sorted = list.slice().sort((a, b) => {
        if (!!b.sus !== !!a.sus) return b.sus ? 1 : -1;
        return b.rssi - a.rssi;
      });

      sorted.forEach(sig => {
        const item = document.createElement('div');
        item.className = 'signal-item' + (sig.sus ? ' sus' : '');

        const isClient = !!sig.isClient;
        const icon = isClient ? '💻' : '📶';
        const nameText = sig.ssid || (isClient ? `Client (${sig.mac})` : '(Hidden)');
        const id = sig.bssid || sig.mac;
        const metaText = isClient
          ? `Ch ${sig.channel || '?'} · Client: ${esc(sig.mac)} · Router: ${esc(sig.routerBssid || sig.bssid)} · ${sig.uplinkPackets || 0} pkts`
          : `Ch ${sig.channel || '?'} · ${esc(sig.bssid)}`;

        let badgeHtml = '';
        if (sig.sus) badgeHtml += '<span class="sus-badge">⚠️ SUS</span>';
        if (isClient) badgeHtml += '<span class="client-badge">CLIENT</span>';

        let reasonsHtml = '';
        if (sig.sus && Array.isArray(sig.reasons) && sig.reasons.length) {
          reasonsHtml = `<div class="sus-reasons">${sig.reasons.map(r => (r.startsWith('⚠️') ? '' : '⚠️ ') + esc(r)).join('<br>')}</div>`;
        }

        item.innerHTML = `
          <span class="signal-icon">${icon}</span>
          <div class="signal-info">
            <div class="signal-name"><span>${esc(nameText)}</span>${badgeHtml}</div>
            <div class="signal-meta">${metaText}</div>
            ${reasonsHtml}
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${esc(sig.rssi)} dBm</span>
            <div class="rssi-bar-bg"><div class="rssi-bar-fill" style="width:${rssiToPercent(sig.rssi)}%;background:${rssiColor(sig.rssi)};"></div></div>
          </div>`;

        item.appendChild(makeActionBtns(isClient ? 'client' : 'wifi', id, nameText, sig.channel));
        wifiList.appendChild(item);
      });
    }

    function renderBleList(signals) {
      bleList.innerHTML = '';
      let list = signals;

      if (filterNewOnly) {
        const baselineArr = getStoredBaseline();
        const baselineSet = new Set(baselineArr.map(s => String(s).trim().toUpperCase()));
        list = [];
        for (const raw of signals) {
          const inBase = isDeviceInBaseline(baselineSet, raw.id, raw.mac, raw.bssid);
          if (inBase) continue;
          const item = { ...raw };
          item.sus = true;
          const warnText = '⚠️ New device introduced since last room baseline';
          item.reasons = item.reasons ? [...item.reasons] : [];
          if (!item.reasons.some(r => r.includes('New device introduced since last room baseline'))) {
            item.reasons.push(warnText);
          }
          list.push(item);
        }
      }

      if (!list.length) {
        bleList.innerHTML = filterNewOnly
          ? '<div class="empty-state"><span>✅</span>No new Bluetooth devices found (all match baseline)</div>'
          : '<div class="empty-state"><span>🔵</span>No Bluetooth devices found</div>';
        return;
      }
      // Sort: sus devices always appear at top, sorted by RSSI descending within group
      const sorted = list.slice().sort((a, b) => {
        if (!!b.sus !== !!a.sus) return b.sus ? 1 : -1;
        return b.rssi - a.rssi;
      });

      sorted.forEach(sig => {
        const item = document.createElement('div');
        item.className = 'signal-item' + (sig.sus ? ' sus' : '');

        let badgeHtml = sig.sus ? '<span class="sus-badge">⚠️ SUS</span>' : '';
        let reasonsHtml = '';
        if (sig.sus && Array.isArray(sig.reasons) && sig.reasons.length) {
          reasonsHtml = `<div class="sus-reasons">${sig.reasons.map(r => (r.startsWith('⚠️') ? '' : '⚠️ ') + esc(r)).join('<br>')}</div>`;
        }

        item.innerHTML = `
          <span class="signal-icon">🔹</span>
          <div class="signal-info">
            <div class="signal-name"><span>${esc(sig.name)}</span>${badgeHtml}</div>
            <div class="signal-meta">Bluetooth LE · ${esc(sig.id || 'unavailable')}</div>
            ${reasonsHtml}
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${esc(sig.rssi)} dBm</span>
            <div class="rssi-bar-bg"><div class="rssi-bar-fill" style="width:${rssiToPercent(sig.rssi)}%;background:${rssiColor(sig.rssi)};"></div></div>
          </div>`;

        item.appendChild(makeActionBtns('ble', sig.id, sig.name || sig.id, 0));
        bleList.appendChild(item);
      });
    }

    function pulseRefresh(ok) {
      refreshDot.classList.remove('pulsing');
      void refreshDot.offsetWidth;
      if (ok) { refreshDot.style.backgroundColor = ''; refreshDot.classList.add('pulsing'); setTimeout(() => refreshDot.classList.remove('pulsing'), 700); }
      else refreshDot.style.backgroundColor = '#e11d48';
    }

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
      if (error && error.name === 'AbortError') return 'ESP32 timed out. Check that phone and ESP32 are on the same network.';
      if (window.location.protocol === 'https:') return 'HTTPS pages cannot reach ESP32 HTTP API directly.';
      return 'Check ESP32 IP address.';
    }

    function feedTarget(wifi, ble) {
      if (!target || demoSimActive || phase !== 'READING') return;
      const signals = (target.type === 'wifi' || target.type === 'client') ? wifi : ble;
      const scanId = Number(signals.scanId);
      if (!Number.isFinite(scanId) || scanId <= 0 || scanId === lastProcessedScanId) return;
      lastProcessedScanId = scanId;
      const sig = (target.type === 'wifi' || target.type === 'client')
        ? wifi.find(item => String(item.bssid || item.mac || '').toLowerCase() === target.id.toLowerCase())
        : ble.find(item => String(item.id || '').toLowerCase() === target.id.toLowerCase());
      if (sig) {
        onSample(sig.rssi);
      } else {
        missCount++;
        if (missCount === 3 && target.type === 'wifi') setEspTrack(target.type, 0);
        render();
      }
    }

    async function fetchSignals() {
      if (!isConnected || !espBaseUrl || scanInFlight) return;
      if (!target && Date.now() - lastListFetch < POLL_LIST_MS) return;
      scanInFlight = true;
      try {
        if (target) {
          if (target.type === 'wifi' || target.type === 'client') lastWifiData = await fetchSignalArray(`${espBaseUrl}/wifi`);
          else lastBleData = await fetchSignalArray(`${espBaseUrl}/ble`);
          feedTarget(lastWifiData, lastBleData);
          refreshLabel.textContent = 'Tracking';
        } else {
          lastListFetch = Date.now();
          const [wifi, ble, clients] = await Promise.all([
            fetchSignalArray(`${espBaseUrl}/wifi`),
            fetchSignalArray(`${espBaseUrl}/ble`),
            fetchSignalArray(`${espBaseUrl}/clients`).catch(() => [])
          ]);
          lastClientsData = clients;
          const merged = mergeClientsIntoWifi(wifi, clients);
          lastWifiData = merged; lastBleData = ble;
          wifiCount.textContent = merged.length; bleCount.textContent = ble.length;
          totalSignalCount.textContent = `${merged.length + ble.length} signals`;
          renderWifiList(merged); renderBleList(ble);
          refreshLabel.textContent = `Updated - ${merged.length} Wi-Fi, ${ble.length} BLE`;
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
        lastWifiData = []; lastBleData = []; lastClientsData = [];
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
        logMessage(`❌ Could not reach ESP32 at ${url}.`);
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
      updateBaselinePill();
      if (IS_FILE_PROTOCOL) logMessage('⚠️ file:// detected — serve over HTTP for mic & location.');
      else logMessage('SYS: App initialized. Mode: Wireless Radar (3-Tier Spy Detection Active).');

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
  WiFi.macAddress(espOwnMac);

  WiFi.begin(phoneHotspotSsid, phoneHotspotPassword);
  Serial.println("\nConnecting to hotspot 'hello' in background...");

  BLEDevice::init("");

  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(IR_LED_PIN, LOW);
  jsonMutex = xSemaphoreCreateMutex();

  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/ble", handleBLE);
  server.on("/clients", handleClients);
  server.on("/deepscan", handleDeepScan);
  server.on("/probe", handleProbe);
  server.on("/probe-result", handleProbeResult);
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

  // Network Auto-Recovery Defense:
  // If sniffer is off and Wi-Fi disconnects, attempt clean background reconnection every 10 seconds without deadlocking server loop
  static unsigned long lastReconnectAttempt = 0;
  if (currentSnifferMode == SNIFFER_OFF && WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= 10000) {
      lastReconnectAttempt = now;
      Serial.println("[WIFI] Auto-recovery: Reconnecting to hotspot in background...");
      WiFi.disconnect();
      WiFi.begin(phoneHotspotSsid, phoneHotspotPassword);
    }
  }

  delay(1);
}
