# [Device Name TBD] 🛡️
*(built for/at hackathon: sheSafe)*

**[Device Name TBD]** is a personal safety and covert-camera detection system built as our Semester 3 Experiential Learning (EL) project at RVCE. It started as our project for the **sheSafe hackathon** (🏆 1st Prize) and is now being developed further into a more research-grade system. The device itself doesn't have a finalized name yet.

## What it does

Hidden cameras are increasingly used to violate people's privacy in places like hotel rooms, Airbnbs, changing rooms, and washrooms. This device helps a user detect these covert cameras and stay safe using just an ESP32 microcontroller and their smartphone — no extra hardware, no internet dependency, fully portable.

It works in two parts:

1. **Wireless Camera Radar** — Scans for Wi-Fi and Bluetooth Low Energy (BLE) signals typically broadcast by wireless spy cameras, then guides the user toward the source using their phone's compass and a directional "getting warmer / colder" signal-tracking algorithm.

2. **Wired Camera Detector** — Uses the phone's camera and real-time computer vision (running entirely in the browser) to spot the invisible infrared glow and characteristic blinking pattern of hidden camera IR LEDs — while filtering out false positives like pink clothing or static lights.

It also includes a one-tap **SOS panic button** that sends the user's live location to a trusted contact over WhatsApp.

## Team

- Samarth Prabhu
- Amogh Neeli
- Saamarth S
- Prajwal V Jois

## Status

Actively being improved from the hackathon-winning prototype toward a more robust, research-quality implementation as part of our RVCE EL coursework.

## Tech Stack

- **Firmware:** ESP32 (Arduino/C++), FreeRTOS dual-core task scheduling, NimBLE
- **Dashboard:** HTML/CSS/JavaScript, served directly from the ESP32
- **Alerts:** Green API (WhatsApp gateway), HTML5 Geolocation API