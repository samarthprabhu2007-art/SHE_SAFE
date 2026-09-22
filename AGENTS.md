# agents.md

Instructions for any AI coding agent (Claude Code, Cursor, Copilot, etc.) working in this repository.

## Before touching any code
Read these two files in full first — they're the source of truth for this project's architecture, algorithms, and open questions:
- `Context_Prompt_for_AI.md` — project summary, current status, coding conventions
- `Complete_Description_of_Project.md` — full algorithmic detail for every subsystem

## Project in one line
Hidden-camera detector: an ESP32 sensor unit + a phone app (RF/Wi-Fi/BLE radar with directional guidance, camera-based IR-blink CV detection, and an SOS/WhatsApp alert), for an RVCE Semester 3 Experiential Learning project.

## Current status
- `arduino.ino` — the only code that exists so far (ESP32 firmware).
- Phone app — not started yet. Will be a standalone HTML/CSS/JS project, packaged into an installable Android app via **Capacitor**.

## Stack & conventions
- **Firmware** (ESP32): Arduino/C++. Use `WiFi.h`, `WebServer.h`, `NimBLEDevice.h` as already in use. Keep long-running work (scans/sensor reads) off the core serving HTTP requests — dual-core FreeRTOS split, mutex-guarded shared state.
- **Phone app**: vanilla HTML/CSS/JavaScript — no frameworks. Capacitor is a packaging layer only; don't introduce React/Vue/etc. on its account.
- The ESP32 and phone app talk over Wi-Fi via a JSON REST API (`GET /wifi`, `GET /ble`, etc.) — no Bluetooth pairing between phone and ESP32 required.

## Do not assume without checking
- Whether the Wireless Camera Radar uses software Wi-Fi/BLE scanning (as documented) or a physical RF sensor (mentioned separately by the team, not yet reconciled) — ask before changing this subsystem. See the ⚠️ note at the top of both docs above.
- That any "demo mode" / simulated beacon logic is meant to ship in the final version — it's explicitly a fallback for demos without a live target.

## Style notes
- Match existing code style in whichever file you're editing rather than introducing a new pattern.
- Prefer small, explainable changes — this is a student research project; evaluators may ask the team to explain any code shown.