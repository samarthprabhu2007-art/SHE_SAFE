# SHE_SAFE

A personal safety and hidden-camera investigation project developed at RVCE for Semester 3 Experiential Learning. It began as the first-prize project at the sheSafe hackathon. The device name is still being decided.

An ESP32 connects to a phone hotspot and serves a mobile browser dashboard with wireless scanning, signal-based direction finding, optical infrared detection, and WhatsApp SOS alerts.

## Research paper

The team's five-page paper, *Embedded Radio Frequency and Computer Vision-Based Spy Camera Detector with Smartphone Integration*, describes the prototype's wireless, optical, and emergency-assistance features. [Read the paper (PDF)](She_safe.pdf) · [Open the LaTeX source](Reserach_Papers_latex/main.tex)

## Current features

- **Wi-Fi and Bluetooth scanning.** Lists nearby Wi-Fi access points and BLE advertisers with signal strength. A signal threshold slider filters the list.
- **Wireless direction finding.** Select a Wi-Fi access point, BLE advertiser, or Deep Scan client. The radar compares fresh readings as you move and uses the phone compass to suggest a heading. Mark blocked directions to guide the next move.
- **Inspection history.** Records devices marked Camera Found or No Camera in browser storage. All, Scanned, and Not Scanned filters organize the current device list. Saved history can be cleared.
- **Wired-camera optical mode.** The ESP32 pulses an infrared LED and the phone camera looks for a matching blink. One matching interval shows 50% progress. Two consecutive matching intervals show 100% and draw a red box around each matching spot for inspection.
- **WhatsApp SOS.** The SOS button or a configured voice phrase starts an alert. The phone uses a recent location or tries up to three times to obtain one. It sends a map link when available or includes the reason location failed. The dashboard reports when Green API accepts the message.
- **Wi-Fi Deep Scan.** Queues behind an active dashboard scan and passively listens for 2.4 GHz client traffic. Each result shows the client MAC, its access point, channel, and client signal strength when available. An **Inspect** flag highlights outgoing traffic. The results have their own strength slider, **Scanned** and **Not Scanned** filters, and **Track** button.
- **Manufacturer badges.** The offline lookup covers 160 MAC prefixes for 13 camera brands and 420 for Espressif and Tuya. Wi-Fi network badges use the access point's address. Deep Scan badges use the client's address. Public BLE addresses are also eligible. A match identifies the registered manufacturer and still requires physical inspection.

## Deep Scan status and limits

Deep Scan is implemented and awaiting further device testing. It attempts to visit the allowed 2.4 GHz channels, returning to the hotspot channel between visits. When channel switching is unavailable, it listens on the hotspot's 2.4 GHz channel and labels results **Hotspot channel only**. Devices on other routers using that same channel may also be heard.

Traffic activity alone cannot confirm a camera. Phones and laptops can trigger the Inspect flag. Quiet devices, devices on channels that were not visited, and 5 GHz clients can be missed. An empty result does not establish that a room is clear.

To follow a client, tap **Track** beside its Deep Scan result. The radar uses fresh transmissions from that client on its observed channel. If only the access point transmits, the client can appear in the list without a signal reading. The radar then waits for client traffic. Run Deep Scan again if the device changes networks or channels.

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
