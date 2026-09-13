# [Device Name TBD] — Complete Project Description

**Project type:** RVCE Semester 3 Experiential Learning (EL) Project
**Origin:** Built for and won 1st Prize at the **sheSafe hackathon** last semester. Currently being expanded into a more research-grade system. (Note: "sheSafe" is the name of the hackathon this project was built for — the device/system itself does not yet have a finalized name.)
**Team:** Samarth Prabhu, Amogh Neeli, Saamarth S, Prajwal V Jois

---

## 1. Problem Statement

Covert surveillance devices (hidden cameras) are a growing personal-safety and privacy threat, especially in short-stay accommodations (hotels, Airbnbs), changing rooms, and washrooms. These cameras fall into two broad categories:

- **Wireless spy cameras** — stream footage over Wi-Fi or advertise via BLE.
- **Wired/SD-card spy cameras** — do not transmit any RF signal, so they must be found visually (typically via their IR illumination used for night vision).

This device is a self-contained, portable detection system that addresses both categories using only an ESP32 microcontroller and the user's own smartphone — no app installation, no cloud backend, no dependency on existing Wi-Fi infrastructure.

---

## 2. System Architecture Overview

The system has two detection subsystems plus an emergency alert feature, all served from a single ESP32 as a self-hosted web dashboard the user opens in their phone's browser:

1. **Wireless Camera Radar** — RF detection + directional guidance
2. **Wired Camera Detector** — computer-vision-based IR lens detection
3. **Emergency SOS & Networking** — GPS-based panic button and portable networking

---

## 3. Part 1: Wireless Camera Radar

### 3.1 Core Concept
Wireless spy cameras must transmit RF signals (2.4 GHz Wi-Fi or BLE) to stream footage. This device scans these bands, tracks signal strength, and guides the user to the source.

### 3.2 Hardware Architecture (Dual-Core ESP32 + FreeRTOS)
- **Core 0 — Background Scanning (`scanTask`)**: Continuously sweeps Wi-Fi (`WiFi.scanNetworks`) and BLE (`NimBLEDevice`) in a dedicated FreeRTOS task pinned to core 0. Collects SSID/device name, MAC/BSSID, RSSI, channel, and frequency. Runs a scan cycle roughly every 2 seconds (Wi-Fi scan + a 2s BLE scan window).
- **Core 1 — Web Server**: Hosts a local HTTP server (Arduino `WebServer` library) that serves the dashboard and responds to polling requests, running on the main loop/core 1.
- **Synchronization**: A mutex semaphore (`jsonMutex`) guards the shared `wifiJson`/`bleJson` strings so the scan task (core 0) and web-request handlers (core 1) never read/write them simultaneously. Without this split, scanning would block/freeze the web server for 3–5 seconds per cycle.

### 3.3 REST API (ESP32 → Browser)
- `GET /wifi` → JSON array of nearby Wi-Fi networks: `{ssid, rssi, channel, freq, bssid}`
- `GET /ble` → JSON array of nearby BLE devices: `{name, rssi}`
- The browser dashboard polls `/wifi` every 3s and `/ble` every 5s.

### 3.4 Directional Guidance Engine (Gradient Ascent over 8 Sectors)
Since standard Wi-Fi/BLE antennas are **omnidirectional**, a single RSSI reading carries no directional information. This is solved with **spatial memory / differential tracking**:

- **Sector mapping**: The 360° space around the user is divided into 8 sectors of 45° each.
- **Sensor fusion**: The phone's `deviceorientation` API (compass heading / gyroscope) tracks the user's real-time heading, mapped relative to a baseline set when tracking starts.
- **Per-sector RSSI averaging**: As the user walks/rotates, each new RSSI reading updates a running average and "momentum" value for whichever sector the user is currently facing.
- **Gradient ascent logic**: If RSSI clearly increases (delta > +3 dBm) in the current sector, that sector is marked positive-momentum and becomes the new target vector. If the signal decreases or plateaus, the algorithm scans across all previously visited (non-wall) sectors and picks the one with the highest recorded average + momentum score as the new target direction.
- **Guidance states**: `IDLE` → `NAVIGATING` → `FOUND`. Arrow/text feedback: "PROCEED FORWARD", "TURN LEFT/RIGHT", "WRONG WAY – TURN AROUND".
- **Proximity threshold**: Once RSSI crosses **-50 dBm**, state becomes `FOUND` — target is within ~1–2 meters.

### 3.5 Obstacle Avoidance ("HIT WALL")
- Pressing **HIT WALL** blacklists the current 45° sector (`wallZones[sec] = true`) and sets its momentum to a strongly negative value so it's never chosen again.
- The system then looks at the two neighboring sectors' recorded average+momentum scores and suggests "MEMORY SUGGESTS: TURN LEFT" or "TURN RIGHT" — or "BACKTRACK" if neither neighbor has useful data.

### 3.6 Demo/Simulation Mode
For demos without a real transmitting spy camera, the dashboard software simulates a virtual beacon (`SPY_CAM_WIFI` / `SPY_BEACON_BLE`) fixed at a mock bearing (90° / East). As the linked gyro compass detects the user rotating toward/away from that bearing, simulated RSSI is computed as a function of angular difference (with small random noise) — proving the steering logic works end-to-end without physical hardware.

### 3.7 Live Radar Visualization
A canvas-based circular radar view renders all 8 sectors color-coded by recorded signal strength/momentum, a rotating sweep line when idle, a directional dot/chevron pointing toward the current best-guess target sector, and a green "found" state with crosshairs when the target is within range.

---

## 4. Part 2: Wired Camera Detector (Computer Vision)

### 4.1 Core Concept
Wired/offline spy cameras don't transmit RF signals but almost always use IR LEDs for night vision. This IR light is invisible to the naked eye but visible to camera image sensors, appearing as a pink/magenta glow.

### 4.2 Pipeline

**Step A — Capture & Downsampling**
- Phone camera accessed via `getUserMedia` (WebRTC), rear-facing camera preferred.
- Each frame is drawn onto a hidden canvas downsampled to **320×240** (a 90% pixel reduction from typical 1080p) so the CV pipeline can run at 30+ FPS entirely client-side in the browser.

**Step B — Color Filtering (RGB → HSL)**
- Each pixel (sampled at a stride of 2 for performance) is checked for a magenta/pink IR signature: `R > 50 && B > 50 && R > G+2 && B > G+2`.
- Qualifying pixels are converted to HSL, since HSL separates hue (color) from lightness (brightness), making color-matching more robust than raw RGB.
- A pixel is accepted if its hue falls within a configurable tolerance (default center 300–315°, ±30–45°) and meets minimum saturation/lightness thresholds.
- Configurable presets exist for different IR glow appearances: standard pink/purple, bright white-pink, and deep violet/blue.

**Step C — Blob Clustering**
- Matching pixels are grouped via a queue-based BFS clustering algorithm (adjacent matches within a small pixel radius are merged into one blob).
- Noise rejection: blobs are discarded if they're too large (>250 pixels) or too wide/tall (>50×50 px) — this filters out large uniform pink surfaces like clothing or walls, keeping only small, concentrated lens-like reflections.
- Each accepted blob's centroid, bounding box, and average intensity are recorded.

**Step D — Temporal Frequency (Blink) Analysis — the core anti-false-positive mechanism**
- Each tracked blob's brightness is logged over a rolling ~5.5-second window.
- A dynamic ON/OFF threshold (midpoint between min/max recorded brightness) is used to detect state transitions (blob turning "on" or "off").
- Transition durations are recorded (default target: 500ms ON / 500ms OFF = 1.0 Hz).
- **Consistency check**: Of the last several transitions, if ≥75% fall within tolerance (~±20% of target duration) *and* jitter between consecutive transitions is low (~±25% or less), the target's threat score increases; otherwise it decays.
- At a threat score ≥ **75%**, the target is confirmed as a camera: a red bounding box/reticle locks onto it, an alarm sounds, and a WhatsApp alert is triggered automatically.
- Static lights or randomly moving pink objects fail the consistency check (irregular or absent transitions) and never accumulate enough threat score to trigger an alert.

### 4.3 Demo/Simulation Mode
A "Simulate Spy Cam" button draws a virtual pink dot that orbits in a circle on the processing canvas and blinks at exactly 500ms, allowing the entire detection → tracking → scoring → alert pipeline to be demonstrated without a real IR-emitting camera.

### 4.4 Privacy Note
All video processing happens **entirely client-side in the browser** — no video frames are ever transmitted over the network, preserving user privacy and requiring zero bandwidth for the CV pipeline itself.

---

## 5. Part 3: Emergency SOS & Networking

### 5.1 SOS Emergency Button
- Tapping the SOS button (with a confirmation prompt) triggers the HTML5 Geolocation API to fetch the phone's current GPS coordinates.
- Constructs an emergency message: `🚨 EMERGENCY SOS ALERT — Time: [timestamp], Location: https://maps.google.com/?q=[lat],[lon]`.
- If GPS is unavailable/denied, the alert is still sent immediately with `Location Unavailable` rather than being delayed — safety-first fallback.

### 5.2 WhatsApp Alerts via Green API
- The alert message (from either the SOS button or a confirmed camera detection) is POSTed directly from the browser (client-side `fetch`) to a Green API instance endpoint.
- Green API relays the message to WhatsApp's servers, which deliver it to a pre-configured recipient number as a normal WhatsApp message.
- Chosen over WhatsApp's official Business API because Green API allows linking a personal WhatsApp number without business verification, template approval, or per-message cost — suited for a free, personal-safety tool.
- *(Note: the ESP32 firmware also exposes a `/trigger-whatsapp` endpoint that currently only logs a simulated alert to Serial — the actual WhatsApp delivery in the current build happens directly from the browser via Green API, not through the ESP32.)*

### 5.3 Portability
- The ESP32 runs its own Access Point (`WiFi.softAP`) so the dashboard is reachable directly, and additionally attempts to join a phone's mobile hotspot in the background (`WiFi.begin(...)`) for internet-dependent features (Green API, Geolocation-linked alerts). This means the whole system can be deployed anywhere — hotel room, Airbnb, unfamiliar location — with just the ESP32 and a phone, no home network required.

---

## 6. Current Implementation Details (as built)

- **Firmware**: Arduino/C++ on ESP32, using `WiFi.h`, `WebServer.h`, and `NimBLEDevice.h`. Dual-core task split via `xTaskCreatePinnedToCore` (scan task on core 0), synchronized via `SemaphoreHandle_t jsonMutex`.
- **Dashboard**: A single embedded HTML/CSS/JS page (stored in flash via `PROGMEM`, streamed to the browser in 2KB chunks) implementing both the Wireless Radar and Wired Detector UIs as switchable "modes," a unified system log console, canvas-based radar and signal-plot visualizations, and all client-side logic (compass tracking, CV pipeline, blink analysis, Green API calls).
- **Known mocked/simulated components** (candidates for the "research-level" upgrade):
  - The wireless radar's directional demo relies on a software-simulated beacon at a fixed bearing rather than a real triangulated signal.
  - The ESP32's own `/trigger-whatsapp` handler simulates the alert via Serial log rather than actually sending it — real WhatsApp delivery currently happens client-side via Green API from the browser.
  - IR LED pin (`IR_LED_PIN`, pin 14) blinks locally on the ESP32 at 500ms — its exact role in the current detection demo vs. future test-rig use should be clarified/expanded as the project matures.

## 7. Possible Directions for the "Research-Level" Upgrade

*(For the team to discuss/refine — not yet decided, listed here as prompts for future planning:)*
- Improving the RSSI-based distance/direction model with more rigorous RF propagation modeling or triangulation from multiple vantage points.
- Benchmarking the IR-blink CV detector's false-positive/false-negative rates against a labeled dataset of real hidden cameras vs. non-camera light sources.
- Formal write-up/evaluation suitable for a research paper or technical report (methodology, test conditions, accuracy metrics).
- Possibly moving from a demo Green API sandbox integration to a more production-appropriate alerting mechanism.

---

*Prepared for RVCE EL Semester 3 by Samarth Prabhu, Amogh Neeli, Saamarth S, and Prajwal V Jois. Original technical Q&A explanation authored by Amogh Neeli.*