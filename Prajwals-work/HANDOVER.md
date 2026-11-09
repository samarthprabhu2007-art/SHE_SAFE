# AEGIS — Hidden Camera Detector (SheSafe)
## Engineering Handover & Architectural Reference Document

---

## 1. Project Overview & Context

* **Project:** AEGIS — An Integrated Wireless & Optical Hidden Camera Detection System (RVCE Semester 3 Experiential Learning).
* **Hardware:** Espressif ESP32 / Seeed Studio XIAO ESP32-C5 board + Smartphone Mobile Hotspot.
* **Active Codebase File:** `new_arduino6.ino` (147.9 KB, 3,364 lines).
* **Reference Legacy File:** `arduino.ino`.
* **Communication Model:** ESP32 operates as a Wi-Fi station (STA) connecting to a smartphone hotspot (`hello` / `12345678`), hosting an embedded web application via HTTP REST on Port 80.

---

## 2. System Architecture & Resource Distribution

```
                           ┌─────────────────────────────────────────────────────────┐
                           │                     ESP32 HARDWARE                      │
                           │                                                         │
                           │  • GPIO 23 (D6)  ──► 1Hz Pulsing IR LED Array          │
                           │  • Wi-Fi Radio   ──► Promiscuous Sniffer & STA Client  │
                           │  • Bluetooth BLE ──► Passive Beacon Advertised Listener │
                           └────────────────────────────▲────────────────────────────┘
                                                        │
                      ┌─────────────────────────────────┴─────────────────────────────────┐
                      │                                                                   │
               CORE 0 (FreeRTOS)                                                   CORE 1 (FreeRTOS)
    ┌────────────────────────────────────────┐                          ┌────────────────────────────────────────┐
    │ Task: scanTask                         │                          │ Arduino loop()                         │
    │   • Stack: 10,000 bytes (Core 0)       │                          │   • server.handleClient()              │
    │   • Passive AP Scanning (1s cycle)     │                          │   • Auto-Recovery Watchdog (10s)       │
    │   • BLE Active Scanning (1s cycle)     │                          │                                        │
    │   • Promiscuous Deep Scan (Ch 1–13)    │                          │ Task: irLedBlinkTask                   │
    │   • Promiscuous Active VBR Probe       │                          │   • Stack: 2,048 bytes                 │
    │   • Promiscuous Target RSSI Track      │                          │   • 1Hz Blinker (500ms ON / 500ms OFF) │
    └────────────────────────────────────────┘                          └────────────────────────────────────────┘
                      │                                                                   │
                      └───────────────────────────────┬───────────────────────────────────┘
                                                      │
                                                      ▼
                                         [Dual-Lock Concurrency]
                               • jsonMutex (FreeRTOS SemaphoreHandle_t)
                               • sniffMux  (portMUX_TYPE spinlock with ISR guard)
                                                      │
                                                      ▼
                                       ┌─────────────────────────────┐
                                       │   HTTP REST API (Port 80)   │
                                       └──────────────┬──────────────┘
                                                      │ Wi-Fi STA Link
                                                      ▼
                           ┌─────────────────────────────────────────────────────────┐
                           │               SMARTPHONE CLIENT (BROWSER)               │
                           │  • HTML5 / Canvas Optical Retro-Reflection Blob CV Engine│
                           │  • 8-Sector Differential RSSI Walking Radar             │
                           │  • 3-Tier Wireless Detection UI (Triage, Deep, Probe)   │
                           │  • Room Memory Baseline (Differential localStorage)     │
                           │  • Emergency SOS (Web Speech Mic + Green API WhatsApp)  │
                           └─────────────────────────────────────────────────────────┘
```

---

## 3. Endpoints & REST API Specifications

The web server on Core 1 exposes the following endpoints:

| Route | Method | Parameters / Headers | Format | Description |
| :--- | :--- | :--- | :--- | :--- |
| `/` | `GET` | None | `text/html` (Chunked) | Streams `index_html` in **2,048-byte chunks** via `handleRoot()`. Chunking prevents heap exhaustion when serving large HTML strings (~140KB) on the ESP32. |
| `/wifi` | `GET` | Header: `X-Scan-Id` | `application/json` | Discovered Wi-Fi AP array (`ssid`, `bssid`, `rssi`, `channel`, `freq`, `band`, `sus`, `reasons[]`). |
| `/ble` | `GET` | Header: `X-Scan-Id` | `application/json` | Discovered BLE beacons (`id`, `name`, `rssi`, `sus`, `reasons[]`). |
| `/clients` | `GET` | None | `application/json` | Discovered promiscuous clients (`mac`, `bssid`, `ssid`, `rssi`, `channel`, `uplinkPackets`, `uplinkBytes`, `sus`, `reasons[]`). |
| `/deepscan` | `GET` | None | `{"ok":true,"waitMs":5500}` | Triggers 2.4GHz 13-channel promiscuous deep scan (400ms per channel). Flushes the TCP socket and introduces a 250ms delay before execution. |
| `/probe` | `GET` | `?mac=XX:XX:..&ch=N&phase=(lock\|baseline\|motion)` | `{"ok":true,"waitMs":2500}` | Sets active target MAC, channel, and sniffing phase for VBR analysis. |
| `/probe-result` | `GET` | None | `application/json` | Returns `{phase, mac, channel, packets, bytes, pps, bps, lastRssi}` (or `{phase:"lock", found:bool, channel, packets}`). |
| `/track` | `GET` | `?type=client&mac=..&ch=N`<br>`?type=wifi&ch=N`<br>`?type=ble`<br>`?off=1` | `{"ok":true}` | Sets background scanning into focused tracking mode (`trackMode`: 0=Full, 1=Wi-Fi AP, 2=BLE, 3=Client Promiscuous). |
| `/pir-status` | `GET` | None | `{"motion":bool,"time":sec}` | Mock PIR sensor simulator: returns `motion: true` for 4 seconds out of every 15-second cycle. |
| `/trigger-whatsapp` | `POST` | None | `application/json` | Fallback ESP32 serial logger for WhatsApp alert trigger simulations. |

---

## 4. Key Architectural Evolution: `new_arduino6.ino` vs `arduino.ino`

### Firmware / C++ Backend

1. **Promiscuous Filter Mask & Frame Ingress:**
   * *Earlier:* Only accepted `WIFI_PKT_DATA`, enforced `len >= 24`, and required `frameType == 2`.
   * *Current:* Uses `WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL`. Accepts `DATA`, `MGMT`, `CTRL`, and `MISC`. Lowered frame length guard to `len < 16` to allow 16-byte Control frames (BlockAck, RTS). Safely guards the third address pointer:
     ```cpp
     const uint8_t* addr3 = (len >= 24) ? &payload[16] : nullptr;
     ```
2. **Bi-Directional Deep Scan Sniffer:**
   * *Earlier:* Only captured packets with `To-DS == 1 && From-DS == 0` (Client to AP).
   * *Current:* Extracts `clientMac` and `apBssid` for **both** directions (`To-DS=1, From-DS=0` and `To-DS=0, From-DS=1`). Filters out broadcast (`FF:FF:FF:FF:FF:FF`), multicast (`LSB of Byte 0 == 1`), all-zero addresses, and the ESP32's own MAC address (`espOwnMac`).
3. **Hotspot Association During Sniffing:**
   * *Earlier:* Disconnected the station (`esp_wifi_disconnect()`) prior to hopping channels.
   * *Current:* Enables promiscuous mode *first* (`esp_wifi_set_promiscuous(true)`). This allows `esp_wifi_set_channel()` to succeed across channels 1–13 without dropping the active smartphone hotspot connection, ensuring continuous UI responsiveness.
4. **Channel Dwell Timing:**
   * *Earlier:* Swept 28 channels across 2.4GHz and 5GHz at 150ms dwell time.
   * *Current:* Sweeps strictly 2.4GHz Channels 1–13 at **400ms** dwell time (5,200ms total sniff window), allowing sufficient time to catch slow-beaconing spy cameras.
5. **Suspicious Uplink Thresholds:**
   * *Earlier:* Required `uplinkPackets >= 8 || uplinkBytes >= 4000`.
   * *Current:* Adjusted to `uplinkPackets >= 4 || uplinkBytes >= 1500` to catch low-bitrate standby/sleeping cameras.
6. **Hardware Synthesizer Yielding in `scanTask`:**
   * *Current:* Added `if (triggerDeepScan || triggerProbe) continue;` after Wi-Fi scanning and before `vTaskDelayUntil()`, yielding Core 0 immediately to promiscuous sniffing.
7. **HTTP Handshake Flushes:**
   * *Current:* `handleDeepScan()` and `handleProbe()` invoke `server.client().flush()` followed by `vTaskDelay(pdMS_TO_TICKS(250))` *before* setting trigger flags, ensuring HTTP responses are delivered before channel hopping begins.
8. **Auto-Recovery Watchdog:**
   * *Current:* Added background reconnect routine in `loop()` that automatically reconnects to the hotspot every 10 seconds if disconnected when sniffer is idle.
9. **Expanded Espressif Camera OUIs:**
   * Added 4 new Espressif MAC blocks to `KNOWN_CAMERA_OUIS`:
     * `{0x34, 0x85, 0x18}` (ESP32-CAM)
     * `{0x48, 0x27, 0xE2}` (Espressif Inc)
     * `{0xEC, 0xDA, 0x3B}` (Espressif Inc)
     * `{0xD4, 0x8A, 0xFC}` (Espressif Inc)

### Embedded Frontend (HTML5/CSS3/JavaScript)

1. **Room Memory Baseline Reset (`↺ Reset Baseline`):**
   * Added button `#btnResetBaseline` next to `#btnBaselineRoom`. Clears `aegis_room_baseline` from `localStorage`, unchecks the filter toggle, and updates UI badges.
2. **Retry Fetcher (`fetchJsonWithRetry`):**
   * Replaced raw `fetch()` calls for deep scan and probe results with a retry helper:
     ```javascript
     async function fetchJsonWithRetry(url, retries = 15, delayMs = 1000, requiredKey)
     ```
   * Passing `requiredKey: 'phase'` prevents reading unpopulated `{}` JSON results before the ESP32 backend has completed its sampling window.
3. **Safe Number Formatter (`fmtNum`):**
   * Replaced raw `.toFixed(1)` calls on `res.bps` and `res.pps` with `fmtNum(val, decimals)`, returning `'—'` instead of `NaN` if values are missing or zero.
4. **Race-Condition & Background Polling Locks:**
   * Probe handlers (`btnProbeLock`, `btnProbeBaseline`, `btnProbeMotion`) now set `scanInFlight = true` before issuing probe requests and release `scanInFlight = false` inside `finally` blocks, suspending regular radar polling until probing completes.
5. **Accurate Countdown Timer:**
   * Deep scan button uses dynamic duration `res.waitMs` returned by the ESP32 backend, preventing UI freezes caused by mismatched hardcoded timeouts.

---

## 5. Technical Trade-Offs & Why Certain Approaches Were Changed

* **5GHz Promiscuous Sniffing:**
  * *Reasoning:* The ESP32-C5 radio in the current ESP-IDF version cannot reliably hop across 5GHz DFS channels in promiscuous mode while associated with a 2.4GHz STA. Sweeping 28 channels at 150ms also left insufficient dwell time to catch sparse packets. Since the vast majority of consumer covert cameras operate on 2.4GHz, prioritizing 2.4GHz with 400ms dwell time provided significantly better detection reliability.
* **STA Disassociation Before Sniffing (`esp_wifi_disconnect()`):**
  * *Reasoning:* Disconnecting STA dropped the smartphone's Wi-Fi hotspot link, aborting browser HTTP requests with `Scan error: signal is aborted without reason`. Promiscuous mode allows channel hopping without dropping association.
* **Data-Only Sniffer Mask (`WIFI_PROMIS_FILTER_MASK_DATA`):**
  * *Reasoning:* Real spy cameras use 802.11n aggregated frames, BlockAck (Control), and Null-Function keepalives (Management). A data-only filter missed these and reported 0 packets during probe tests.
* **Hardcoded Frontend Sleep Timers:**
  * *Reasoning:* A hardcoded 10.2s frontend wait paired with a 5.2s backend sweep created an artificial 5-second UI freeze. A dynamic `waitMs` response resolved this.

---

## 6. Context on the "Laptop Probe Baseline = 0" Issue

When probing a laptop connected to campus enterprise Wi-Fi (`58:61:63:--:--:--`) on Channel 1, the probe returned **0 packets / 0 B/s**, while deep scan only discovered clients on BSSIDs `30:BD:13:22:F7:26` and `CE:B1:8F:C3:46:39`.

**Relevant Factors:**
1. **Discovered BSSIDs are Mobile Hotspots:**
   * `30:BD:13:22:F7:26` and `CE:B1:8F:C3:46:39` have the locally-administered bit set (bit 1 of first byte = 1). These were personal mobile hotspots operating nearby on 2.4GHz.
2. **Campus Wi-Fi Operates on 5GHz:**
   * Enterprise campus networks (`58:61:63:...`, Cisco/Meraki) broadcast dual-band. Most modern laptops automatically connect over **5GHz** (Channels 36–165). Since the sniffer is tuned to 2.4GHz (Channels 1–13), 5GHz transmissions are not captured over the air.
3. **MAC Randomization:**
   * Windows enables randomized hardware addresses by default, which can cause the active MAC to differ from the physical hardware MAC.
4. **Client Power Saving:**
   * Laptops in idle states generate minimal uplink traffic. Generating active traffic (e.g. streaming or pinging) during the sampling window is necessary to observe meaningful packet counts.

---

## 7. Practical Engineering Guidelines for Next Steps

* **Testing Probe & Deep Scan Functionality:**
  * For predictable testing, ensure the target client device is connected to a **2.4GHz network** (or directly to the phone's hotspot) and verify its active MAC address (e.g., via `netsh wlan show interfaces` on Windows).
  * Generate active traffic (such as playing a video or running a speed test) during the "Read Motion" phase to test the VBR compression spike logic.
* **HTML & Memory Considerations:**
  * The web interface is currently embedded as a raw string literal (`index_html`) in flash memory via `PROGMEM`. If adding large assets or expanding the UI, maintain chunked transmission (`handleRoot()`) or consider offloading large static assets to SPIFFS/LittleFS if flash memory limits are approached.
* **Concurrency & Task Management:**
  * Core 0 handles sensor and sniffer loops (`scanTask`), while Core 1 handles network serving (`server.handleClient()`). Use `jsonMutex` or `sniffMux` when reading or updating shared data structures across cores.
* **Mobile Packaging:**
  * The frontend is built using standard Vanilla HTML/CSS/JS with no build-step dependencies, making it straightforward to wrap into an installable Android package using Capacitor if desired.
