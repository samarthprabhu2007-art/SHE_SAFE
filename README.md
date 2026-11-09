# SHE_SAFE

A personal safety and hidden-camera investigation project developed at RVCE for Semester 3 Experiential Learning. It began as the first-prize project at the sheSafe hackathon. The device name is still being decided.

An ESP32 connects to a phone hotspot and serves a mobile browser dashboard with wireless scanning, signal-based direction finding, optical infrared detection, and WhatsApp SOS alerts.

## Current features

- **Wi-Fi and Bluetooth scanning.** Lists nearby Wi-Fi access points and BLE advertisers with signal strength. A signal threshold slider filters the list.
- **Wireless direction finding.** Select a Wi-Fi or BLE target, record a starting signal reading, then move and take further readings. The radar combines signal changes with the phone compass to suggest a direction. Wall controls help guide the search.
- **Inspection history.** Records devices marked Camera Found or No Camera in browser storage. All, Scanned, and Not Scanned filters organize the current device list. Saved history can be cleared.
- **Wired-camera optical mode.** Uses the phone camera and browser-based image processing to look for infrared bright spots and blinking patterns. It includes a blink score and attempts to reject static light and color interference. The firmware also drives a pulsing IR LED.
- **WhatsApp SOS.** A panic button sends a location link through Green API. Voice code-word setup and listening are included for supported browsers.
- **Wi-Fi Deep Scan.** Passively listens for traffic involving Wi-Fi clients on 2.4 GHz. Results show client MAC addresses, associated access points, channels, and client signal strength when available. Network names are matched from the regular scan. An Inspect flag highlights outgoing traffic and shows frame and byte counts for manual investigation.

## Deep Scan status and limits

Deep Scan is implemented and awaiting further device testing. It attempts to visit the allowed 2.4 GHz channels, returning to the hotspot channel between visits. When channel switching is unavailable, it listens on the hotspot's 2.4 GHz channel and labels results **Hotspot channel only**. Devices on other routers using that same channel may also be heard.

Traffic activity alone cannot confirm a camera. Phones and laptops can trigger the Inspect flag. Quiet devices, devices on channels that were not visited, and 5 GHz clients can be missed. An empty result does not establish that a room is clear.

Deep Scan client tracking, motion-based traffic probing, and Room Baseline are not integrated into the active sketch.

## Setup

1. Open `arduino/arduino.ino` in Arduino IDE and select your ESP32 board. The current handover targets the XIAO ESP32-C5.
2. Install the ESP32 Arduino board package and the libraries needed by the sketch. It uses `WiFi`, `WebServer`, and the ESP32 `BLEDevice` API.
3. Configure `phoneHotspotSsid` and `phoneHotspotPassword` to match your phone hotspot. Use a 2.4 GHz hotspot for the Deep Scan fallback.
4. For SOS, create and authorize a Green API WhatsApp instance. In your private working copy, replace `ENTER_INSTANCE_ID_HERE` and `ENTER_API_KEY_HERE` with your credentials. Review `FIXED_CONTACT`, which supplies the preset emergency contact.
5. Upload the sketch, open Serial Monitor, and open the printed ESP32 IP address in the phone browser.
6. Grant camera, location, microphone, and orientation permissions for the features you use. Browser support varies. Camera and location access can require a secure browser context, so access through a local HTTP address may need browser-specific configuration.

Wireless scanning and optical processing run locally. WhatsApp delivery requires internet access, an authorized Green API instance, and valid credentials. Phone cameras differ in their infrared sensitivity.

The current dashboard sends SOS requests directly from the browser, so its configured API token is accessible to users who can load that dashboard.

## Repository layout

- `arduino/arduino.ino` — active firmware and embedded HTML/CSS/JavaScript dashboard.
- `Prajwals-work/` — reference prototype and engineering handover, including experimental functionality beyond the active sketch.
- `Hackathon_materials/` — earlier project material.
- `Research_Papers/` and `Reserach_Papers_latex/` — research references and paper sources.

## Development and publishing

Development uses the private working sketch at `../arduino_testing/arduino_testing.ino`, outside this repository. That copy keeps local WhatsApp settings.

When publishing, copy the working sketch to `arduino/arduino.ino` and replace the Green API instance ID and token with the placeholders above. Apply the same credential removal to reference sketches included in a commit. Keep real credentials out of Git commits.

Features are integrated one at a time. Arduino compilation and hardware validation are performed by the project team.

## Team

- Samarth Prabhu
- Amogh Neeli
- Saamarth S
- Prajwal V Jois
