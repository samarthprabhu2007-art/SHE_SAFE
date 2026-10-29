#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ESPmDNS.h>
static const char SHE_SAFE_PAGE[] PROGMEM = R"SHESAFE_PAGE(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>AEGIS — Hidden Camera Detector</title>
  <style>
    :root {
      --bg-color: #fff5f8;
      --card-bg: #ffffff;
      --border-color: #f2c9d8;
      --text-main: #35212b;
      --text-muted: #80566a;
      --accent-blue: #d6336c;
      --accent-green: #098968;
      --sos-red: #e11d48;
      --sos-hover: #be123c;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      -webkit-tap-highlight-color: transparent;
    }

    body {
      background-color: var(--bg-color);
      color: var(--text-main);
      display: flex;
      flex-direction: column;
      height: 100dvh;
      overflow: hidden;
    }

    /* Top Header Bar with Always-On SOS */
    header {
      background-color: var(--card-bg);
      border-bottom: 1px solid var(--border-color);
      padding: 14px 16px;
      box-shadow: 0 2px 12px rgba(166, 27, 77, 0.08);
      display: flex;
      align-items: center;
      justify-content: space-between;
      z-index: 100;
      flex-shrink: 0;
    }

    .brand {
      display: flex;
      flex-direction: column;
    }

    .brand h1 {
      font-size: 1.1rem;
      font-weight: 700;
      letter-spacing: 0.5px;
      color: #a61b4d;
    }

    .brand span {
      font-size: 0.72rem;
      color: var(--text-muted);
      font-weight: 500;
    }

    /* Always-On SOS Button */
    .sos-btn {
      background-color: var(--sos-red);
      color: #ffffff;
      border: none;
      padding: 8px 16px;
      border-radius: 20px;
      font-weight: 700;
      font-size: 0.85rem;
      display: flex;
      align-items: center;
      gap: 6px;
      cursor: pointer;
      box-shadow: 0 0 12px rgba(239, 68, 68, 0.4);
      transition: transform 0.1s ease, background-color 0.2s ease;
      animation: pulse-red 2s infinite;
    }

    .sos-btn:active {
      transform: scale(0.95);
      background-color: var(--sos-hover);
    }

    @keyframes pulse-red {
      0% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.5); }
      70% { box-shadow: 0 0 0 8px rgba(239, 68, 68, 0); }
      100% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0); }
    }

    /* Dual Switch Mode System */
    .mode-switch-container {
      padding: 12px 16px;
      background-color: var(--bg-color);
      flex-shrink: 0;
    }

    .mode-switcher {
      background-color: var(--card-bg);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 4px;
      display: flex;
      gap: 4px;
    }

    .mode-tab {
      flex: 1;
      min-height: 46px;
      padding: 10px 8px;
      border: none;
      background: transparent;
      color: var(--text-muted);
      font-weight: 600;
      font-size: 0.85rem;
      border-radius: 8px;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      transition: all 0.25s ease;
    }

    .mode-tab.active {
      background-color: var(--accent-blue);
      color: #ffffff;
      box-shadow: 0 3px 10px rgba(214, 51, 108, 0.25);
    }

    /* Viewport Area */
    main {
      flex: 1;
      padding: 0 12px 12px;
      overflow-y: auto;
      display: flex;
      flex-direction: column;
    }

    .view-panel {
      display: none;
      flex-direction: column;
      gap: 12px;
    }

    .view-panel.active {
      display: flex;
    }

    .card {
      background-color: var(--card-bg);
      border: 1px solid var(--border-color);
      border-radius: 18px;
      padding: 14px;
      box-shadow: 0 4px 14px rgba(166, 27, 77, 0.06);
    }

    .card-title {
      font-size: 0.9rem;
      font-weight: 600;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin-bottom: 12px;
    }

    /* Placeholder Graphic Containers */
    .canvas-placeholder {
      width: 100%;
      aspect-ratio: 1;
      max-height: 280px;
      background-color: #fff0f5;
      border: 1px dashed var(--border-color);
      border-radius: 12px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      color: var(--text-muted);
      gap: 8px;
      margin: 0 auto;
    }

    .canvas-placeholder.camera-active {
      max-width: 640px;
      max-height: none;
      position: relative;
      overflow: hidden;
    }

    #detectionOverlay {
      display: none;
      position: absolute;
      inset: 0;
      width: 100%;
      height: 100%;
      pointer-events: none;
    }

    .status-badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 4px 10px;
      background-color: rgba(16, 185, 129, 0.15);
      color: var(--accent-green);
      border-radius: 20px;
      font-size: 0.75rem;
      font-weight: 600;
    }

    .status-dot {
      width: 6px;
      height: 6px;
      background-color: var(--accent-green);
      border-radius: 50%;
    }

    /* System Log Footer */
    footer {
      background-color: var(--card-bg);
      border-top: 1px solid var(--border-color);
      padding: 10px 16px;
      font-size: 0.75rem;
      color: var(--text-muted);
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-shrink: 0;
    }

    .log-line {
      font-family: monospace;
      color: #80566a;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      max-width: 80%;
    }

    /* Modal Styling */
    .modal-overlay {
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background-color: rgba(88, 23, 50, 0.35);
      display: none;
      align-items: center;
      justify-content: center;
      z-index: 1000;
      padding: 20px;
    }

    .modal-overlay.active {
      display: flex;
    }

    .modal-box {
      background-color: var(--card-bg);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 24px;
      width: 100%;
      max-width: 340px;
      text-align: center;
    }

    .modal-box h2 {
      color: var(--sos-red);
      margin-bottom: 8px;
    }

    .modal-box p {
      font-size: 0.85rem;
      color: var(--text-muted);
      margin-bottom: 20px;
    }

    .modal-actions {
      display: flex;
      gap: 10px;
    }

    .btn {
      flex: 1;
      min-height: 46px;
      padding: 10px 14px;
      border-radius: 12px;
      border: none;
      font-weight: 600;
      cursor: pointer;
    }

    .btn-danger {
      background-color: var(--sos-red);
      color: white;
    }

    .btn-secondary {
      background-color: #ffe2eb;
      color: var(--text-main);
    }

    /* Settings Gear Button */
    .settings-btn {
      background: none;
      border: none;
      font-size: 1.1rem;
      cursor: pointer;
      color: var(--text-muted);
      padding: 4px 6px;
      border-radius: 8px;
      line-height: 1;
      transition: color 0.2s ease, background-color 0.2s ease;
    }

    .settings-btn:hover {
      color: var(--text-main);
      background-color: #ffe2eb;
    }

    /* Settings Bottom Sheet */
    .sheet-overlay {
      position: fixed;
      inset: 0;
      background-color: rgba(88, 23, 50, 0.25);
      z-index: 200;
      display: none;
    }

    .sheet-overlay.active {
      display: block;
    }

    .bottom-sheet {
      position: fixed;
      bottom: 0;
      left: 0;
      right: 0;
      background-color: var(--card-bg);
      border-top: 1px solid var(--border-color);
      border-radius: 20px 20px 0 0;
      padding: 16px 20px calc(32px + env(safe-area-inset-bottom));
      z-index: 300;
      transform: translateY(100%);
      transition: transform 0.3s ease;
    }

    .bottom-sheet.active {
      transform: translateY(0);
    }

    .sheet-handle {
      width: 36px;
      height: 4px;
      background-color: #e9afc3;
      border-radius: 2px;
      margin: 0 auto 16px;
    }

    .sheet-title {
      font-size: 0.9rem;
      font-weight: 600;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin-bottom: 16px;
    }

    .settings-field {
      margin-bottom: 14px;
    }

    .settings-field label {
      display: block;
      font-size: 0.78rem;
      color: var(--text-muted);
      margin-bottom: 6px;
      font-weight: 500;
    }

    .settings-field input {
      width: 100%;
      background-color: #fff7fa;
      border: 1px solid var(--border-color);
      border-radius: 8px;
      padding: 10px 12px;
      color: var(--text-main);
      font-size: 0.9rem;
      outline: none;
      transition: border-color 0.2s ease;
    }

    .settings-field input:focus {
      border-color: var(--accent-blue);
    }

    /* Smart Code Word Card */
    .cw-hint {
      font-size: 0.82rem;
      color: var(--text-muted);
      margin-bottom: 12px;
    }

    .cw-examples {
      display: flex;
      flex-direction: row;
      flex-wrap: wrap;
      align-items: baseline;
      gap: 0;
      margin-bottom: 12px;
    }

    .cw-chip {
      font-size: 0.78rem;
      color: var(--text-muted);
      font-style: italic;
    }

    .cw-chip:not(:last-child)::after {
      content: ",\00a0";
    }

    .cw-status {
      display: none;
      flex-direction: column;
      gap: 4px;
      margin-bottom: 14px;
    }

    .round-status {
      display: none;
      font-size: 0.82rem;
      color: var(--accent-blue);
      margin-bottom: 10px;
      padding: 8px 10px;
      background-color: #fff7fa;
      border-radius: 8px;
      border: 1px solid var(--border-color);
    }

    .setup-row {
      display: flex;
      gap: 10px;
    }

    .mic-btn {
      background-color: var(--border-color);
      color: var(--text-muted);
      border: 1px solid transparent;
      transition: background-color 0.2s ease, color 0.2s ease, border-color 0.2s ease;
    }

    .mic-btn:disabled {
      opacity: 0.4;
      cursor: not-allowed;
    }

    .mic-btn.mic-active {
      background-color: rgba(16, 185, 129, 0.15);
      color: var(--accent-green);
      border-color: var(--accent-green);
    }

    /* Space the code word card away from the panels above it */
    #codeWordCard {
      margin-top: 24px;
    }

    /* ─── WIRELESS RADAR UI ──────────────────────────────────────────────────── */

    /* Connect Bar */
    .esp-connect-bar {
      display: flex;
      align-items: center;
      gap: 10px;
      flex-wrap: wrap;
    }

    .esp-ip-input {
      flex: 1;
      min-width: 130px;
      background-color: #fff7fa;
      border: 1px solid var(--border-color);
      border-radius: 10px;
      padding: 9px 12px;
      color: var(--text-main);
      font-size: 0.88rem;
      outline: none;
      transition: border-color 0.2s ease;
    }

    .esp-ip-input:focus {
      border-color: var(--accent-blue);
    }

    .btn-connect {
      background-color: var(--accent-blue);
      color: #fff;
      border: none;
      border-radius: 10px;
      padding: 9px 18px;
      font-weight: 700;
      font-size: 0.85rem;
      cursor: pointer;
      white-space: nowrap;
      transition: background-color 0.2s ease, transform 0.1s ease;
    }

    .btn-connect:hover { background-color: #b52459; }
    .btn-connect:active { transform: scale(0.96); }

    .btn-connect.connected {
      background-color: var(--accent-green);
    }

    .btn-connect.connected:hover { background-color: #076e52; }

    .esp-connected-ip {
      font-size: 0.75rem;
      color: var(--accent-green);
      font-weight: 600;
      display: none;
      align-items: center;
      gap: 5px;
      margin-top: 6px;
    }

    .esp-connected-ip.visible { display: flex; }

    /* Radar Canvas */
    .radar-wrap {
      position: relative;
      width: 100%;
      max-width: 280px;
      margin: 0 auto;
    }

    #radarCanvas {
      width: 100%;
      aspect-ratio: 1;
      border-radius: 50%;
      display: block;
    }

    /* Signal List */
    .signal-section-label {
      font-size: 0.75rem;
      font-weight: 700;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.6px;
      margin-bottom: 8px;
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .signal-count-badge {
      background-color: rgba(214, 51, 108, 0.12);
      color: var(--accent-blue);
      border-radius: 20px;
      padding: 1px 8px;
      font-size: 0.72rem;
      font-weight: 700;
    }

    .signal-list {
      display: flex;
      flex-direction: column;
      gap: 6px;
      max-height: 240px;
      overflow-y: auto;
    }

    .signal-item {
      display: flex;
      align-items: center;
      gap: 10px;
      background-color: #fff7fa;
      border: 1px solid var(--border-color);
      border-radius: 12px;
      padding: 9px 12px;
      transition: border-color 0.2s ease;
    }

    .signal-item:hover { border-color: var(--accent-blue); }

    .signal-icon {
      font-size: 1.1rem;
      flex-shrink: 0;
    }

    .signal-info {
      flex: 1;
      min-width: 0;
    }

    .signal-name {
      font-size: 0.85rem;
      font-weight: 600;
      color: var(--text-main);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .signal-meta {
      font-size: 0.72rem;
      color: var(--text-muted);
      margin-top: 1px;
    }

    /* RSSI bar */
    .rssi-bar-wrap {
      display: flex;
      flex-direction: column;
      align-items: flex-end;
      gap: 3px;
      flex-shrink: 0;
    }

    .rssi-value {
      font-size: 0.72rem;
      font-weight: 700;
      color: var(--text-muted);
    }

    .rssi-bar-bg {
      width: 56px;
      height: 6px;
      background-color: #f2c9d8;
      border-radius: 3px;
      overflow: hidden;
    }

    .rssi-bar-fill {
      height: 100%;
      border-radius: 3px;
      transition: width 0.5s ease;
    }

    /* Track button */
    .btn-track {
      background-color: #fff0f5;
      color: var(--accent-blue);
      border: 1px solid var(--border-color);
      border-radius: 8px;
      padding: 5px 10px;
      font-size: 0.72rem;
      font-weight: 700;
      cursor: pointer;
      white-space: nowrap;
      flex-shrink: 0;
      transition: background-color 0.2s ease, color 0.2s ease;
    }

    .btn-track:hover {
      background-color: var(--accent-blue);
      color: #fff;
    }

    .btn-track.tracking {
      background-color: var(--accent-blue);
      color: #fff;
    }

    /* Refresh indicator */
    .refresh-indicator {
      display: flex;
      align-items: center;
      gap: 6px;
      font-size: 0.72rem;
      color: var(--text-muted);
    }

    .refresh-dot {
      width: 7px;
      height: 7px;
      border-radius: 50%;
      background-color: #ccc;
      transition: background-color 0.3s ease;
    }

    .refresh-dot.pulsing {
      background-color: var(--accent-green);
      animation: pulse-green 0.6s ease;
    }

    @keyframes pulse-green {
      0%   { box-shadow: 0 0 0 0 rgba(9, 137, 104, 0.5); }
      70%  { box-shadow: 0 0 0 6px rgba(9, 137, 104, 0); }
      100% { box-shadow: 0 0 0 0 rgba(9, 137, 104, 0); }
    }

    /* Tabs within wireless panel */
    .signal-tabs {
      display: flex;
      gap: 6px;
      margin-bottom: 12px;
    }

    .signal-tab {
      flex: 1;
      padding: 7px 10px;
      border: 1px solid var(--border-color);
      border-radius: 10px;
      background: #fff7fa;
      color: var(--text-muted);
      font-size: 0.78rem;
      font-weight: 600;
      cursor: pointer;
      text-align: center;
      transition: all 0.2s ease;
    }

    .signal-tab.active {
      background-color: var(--accent-blue);
      color: #fff;
      border-color: var(--accent-blue);
    }

    .signal-tab-pane { display: none; }
    .signal-tab-pane.active { display: block; }

    /* Empty state */
    .empty-state {
      text-align: center;
      padding: 20px 10px;
      color: var(--text-muted);
      font-size: 0.82rem;
    }

    .empty-state span { display: block; font-size: 1.5rem; margin-bottom: 6px; }

    /* Directional guidance */
    .guidance-text {
      font-size: 1rem;
      font-weight: 700;
      color: var(--accent-blue);
      text-align: center;
      padding: 10px 0 4px;
    }

    .guidance-sub {
      font-size: 0.78rem;
      color: var(--text-muted);
      text-align: center;
    }

    /* Radar Actions & Gyro Controls */
    .radar-actions {
      display: flex;
      gap: 8px;
      margin-top: 10px;
      flex-wrap: wrap;
    }

    .btn-gyro {
      flex: 1;
      min-width: 140px;
      padding: 9px 12px;
      font-size: 0.8rem;
      font-weight: 600;
      border-radius: 8px;
      border: 1px solid var(--border-color);
      background-color: #fff;
      color: var(--text-main);
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      transition: all 0.2s ease;
    }
    .btn-gyro.linked {
      background-color: rgba(16, 185, 129, 0.12);
      border-color: var(--accent-green);
      color: var(--accent-green);
    }

    .btn-wall {
      background-color: rgba(225, 29, 72, 0.12);
      border: 1px solid #e11d48;
      color: #e11d48;
      padding: 9px 14px;
      font-size: 0.8rem;
      font-weight: 700;
      border-radius: 8px;
      cursor: pointer;
      display: none;
      align-items: center;
      gap: 5px;
      transition: all 0.2s ease;
    }
    .btn-wall:hover {
      background-color: #e11d48;
      color: #fff;
    }

    .btn-sim-target {
      padding: 9px 12px;
      font-size: 0.8rem;
      font-weight: 600;
      border-radius: 8px;
      border: 1px dashed var(--border-color);
      background: none;
      color: var(--text-muted);
      cursor: pointer;
    }
    .btn-sim-target:hover {
      color: var(--accent-blue);
      border-color: var(--accent-blue);
    }

    /* Target Lock-On Banner */
    .target-lock-banner {
      display: none;
      background: linear-gradient(135deg, rgba(214, 51, 108, 0.08) 0%, rgba(9, 137, 104, 0.08) 100%);
      border: 1px solid rgba(214, 51, 108, 0.25);
      border-radius: 10px;
      padding: 10px 12px;
      margin-top: 10px;
    }
    .target-lock-banner.active { display: block; }

    .target-lock-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 6px;
    }
    .target-lock-title {
      font-size: 0.84rem;
      font-weight: 700;
      color: var(--accent-blue);
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .btn-drop-target {
      background: none;
      border: 1px solid var(--border-color);
      border-radius: 6px;
      padding: 3px 8px;
      font-size: 0.72rem;
      color: var(--text-muted);
      cursor: pointer;
    }
    .btn-drop-target:hover { color: #e11d48; border-color: #e11d48; }

    .target-lock-meta {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 6px;
      font-size: 0.75rem;
      color: var(--text-muted);
      margin-top: 6px;
    }
    .target-meta-item strong { color: var(--text-main); }

    .delta-pill {
      display: inline-block;
      padding: 1px 6px;
      border-radius: 4px;
      font-size: 0.7rem;
      font-weight: 700;
    }
    .delta-up { background: rgba(16, 185, 129, 0.15); color: var(--accent-green); }
    .delta-down { background: rgba(225, 29, 72, 0.15); color: #e11d48; }
    .delta-flat { background: rgba(100, 116, 139, 0.15); color: #64748b; }

    .room-prediction {
      margin-top: 8px;
      padding: 6px 10px;
      border-radius: 6px;
      font-size: 0.75rem;
      font-weight: 600;
      display: flex;
      align-items: center;
      gap: 6px;
      background: rgba(255, 255, 255, 0.85);
      border: 1px dashed var(--border-color);
    }
    .room-pred-inside {
      background: rgba(16, 185, 129, 0.1);
      border-color: var(--accent-green);
      color: var(--accent-green);
    }
    .room-pred-outside {
      background: rgba(245, 158, 11, 0.1);
      border-color: #f59e0b;
      color: #b45309;
    }

    .radar-wrap.wall-warning {
      animation: wall-pulse 1.2s ease-in-out infinite;
    }
    @keyframes wall-pulse {
      0%, 100% { box-shadow: 0 0 0 0 rgba(225, 29, 72, 0); }
      50% { box-shadow: 0 0 0 10px rgba(225, 29, 72, 0.3); }
    }

    @media (max-width: 480px) {
      header { padding: 12px 14px; }
      .brand h1 { font-size: 1rem; }
      .mode-switch-container { padding: 10px 12px; }
      .mode-tab { font-size: 0.78rem; }
      .card { padding: 12px; }
      .canvas-placeholder.camera-active { max-width: 100%; }
      footer { padding: 9px 12px calc(9px + env(safe-area-inset-bottom)); }
    }
  </style>
</head>
<body>

  <!-- Top Header Bar with Always-On SOS Button -->
  <header>
    <div class="brand">
      <h1>AEGIS</h1>
      <span>Covert Detector System</span>
    </div>
    <div style="display:flex; align-items:center; gap:10px;">
      <button class="settings-btn" id="settingsBtn" title="Settings">⚙️</button>
      <button class="sos-btn" id="sosBtn">
        🚨 <span>SOS</span>
      </button>
    </div>
  </header>

  <!-- Dual Switch System -->
  <div class="mode-switch-container">
    <div class="mode-switcher">
      <button class="mode-tab active" id="tabWireless">
        📡 Wireless Radar
      </button>
      <button class="mode-tab" id="tabWired">
        📷 Wired Detector
      </button>
    </div>
  </div>

  <!-- Main View Area -->
  <main>
    <!-- View 1: Wireless Camera Radar Mode -->
    <div class="view-panel active" id="panelWireless">

      <!-- ── Connect to ESP32 ── -->
      <div class="card">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
          <span class="card-title" style="margin-bottom:0;">ESP32 Connection</span>
          <div class="refresh-indicator">
            <div class="refresh-dot" id="refreshDot"></div>
            <span id="refreshLabel">Not connected</span>
          </div>
        </div>
        <div class="esp-connect-bar">
          <input class="esp-ip-input" type="text" id="espIpInput" value="shesafe.local" placeholder="e.g. shesafe.local" autocomplete="off" spellcheck="false">
          <button class="btn-connect" id="btnConnect">Connect</button>
        </div>
        <div class="esp-connected-ip" id="connectedIpDisplay">
          <span>✅</span><span id="connectedIpText"></span>
        </div>
      </div>

      <!-- ── Radar Canvas ── -->
      <div class="card">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
          <span class="card-title" style="margin-bottom:0;">RF Sector Radar</span>
          <span class="status-badge" id="radarStatusBadge">
            <span class="status-dot" style="background-color:var(--accent-blue);"></span>
            <span id="radarStatusText">Idle</span>
          </span>
        </div>
        <div class="radar-wrap" id="radarWrap">
          <canvas id="radarCanvas" width="280" height="280"></canvas>
        </div>

        <!-- Radar Action Toolbar: Gyro Compass & Wall button & Demo simulation -->
        <div class="radar-actions">
          <button class="btn-gyro" id="btnLinkGyro" title="Connect Phone Gyroscope/Compass">
            <span>🧭</span> <span id="gyroBtnText">Link Gyro Compass</span>
          </button>
          <button class="btn-wall" id="btnHitWall" title="Mark obstacle/wall in current direction">
            🧱 Hit Wall
          </button>
          <button class="btn-sim-target" id="btnSimTarget" title="Inject demo spy camera for testing">
            ⚡ Demo Target
          </button>
        </div>
        <div id="gyroStatus" role="status" aria-live="polite" style="margin-top:8px;font-size:0.82rem;color:var(--text-muted);">Gyro status: Not checked</div>

        <!-- Target Lock-on Banner (active when user locks a Wi-Fi or BLE node) -->
        <div class="target-lock-banner" id="targetLockBanner">
          <div class="target-lock-header">
            <span class="target-lock-title">
              <span>🎯</span> <span id="lockedTargetName">TARGET LOCKED</span>
            </span>
            <button class="btn-drop-target" id="btnDropTarget">✕ Drop Target</button>
          </div>
          <div class="target-lock-meta">
            <div class="target-meta-item">Signal: <strong id="lockedTargetRssi">-- dBm</strong> <span id="lockedDeltaPill" class="delta-pill delta-flat">~ 0 dBm</span></div>
            <div class="target-meta-item">Est. Distance: <strong id="lockedTargetDist">-- m</strong></div>
            <div class="target-meta-item">Facing Sector: <strong id="currentSectorLabel">Sector 0 (N)</strong></div>
            <div class="target-meta-item">Target Vector: <strong id="targetVectorLabel">--°</strong></div>
          </div>
          <div class="room-prediction" id="roomPrediction">
            <span>📍</span> <span id="roomPredictionText">Room Analysis: Sampling signal limits...</span>
          </div>
        </div>
      </div>

      <!-- ── Signal List ── -->
      <div class="card">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
          <span class="card-title" style="margin-bottom:0;">Detected Signals</span>
          <span id="totalSignalCount" style="font-size:0.75rem;color:var(--text-muted);">0 signals</span>
        </div>

        <!-- Wi-Fi / BLE sub-tabs -->
        <div class="signal-tabs">
          <button class="signal-tab active" id="stWifi">📶 Wi-Fi</button>
          <button class="signal-tab" id="stBle">🔵 Bluetooth</button>
        </div>

        <!-- Wi-Fi pane -->
        <div class="signal-tab-pane active" id="paneWifi">
          <div class="signal-section-label">
            Wi-Fi Networks
            <span class="signal-count-badge" id="wifiCount">0</span>
          </div>
          <div class="signal-list" id="wifiList">
            <div class="empty-state"><span>📡</span>Connect to ESP32 to scan</div>
          </div>
        </div>

        <!-- BLE pane -->
        <div class="signal-tab-pane" id="paneBle">
          <div class="signal-section-label">
            Bluetooth Devices
            <span class="signal-count-badge" id="bleCount">0</span>
          </div>
          <div class="signal-list" id="bleList">
            <div class="empty-state"><span>🔵</span>Connect to ESP32 to scan</div>
          </div>
        </div>
      </div>

      <!-- ── Directional Guidance ── -->
      <div class="card">
        <div class="card-title">Directional Guidance</div>
        <div class="guidance-text" id="guidanceText">STATUS: IDLE / WAITING FOR SCAN</div>
        <div class="guidance-sub" id="guidanceSub"></div>
      </div>

    </div><!-- /panelWireless -->

    <!-- View 2: Wired Camera Detector Mode (CV) -->
    <div class="view-panel" id="panelWired">
      <div class="card">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
          <span class="card-title" style="margin-bottom: 0;">Optical IR Scanner</span>
          <span class="status-badge" id="cameraStatus" style="background-color: rgba(214, 51, 108, 0.12); color: var(--accent-blue);">
            <span class="status-dot" style="background-color: var(--accent-blue);"></span> Camera Off
          </span>
        </div>
        <div class="canvas-placeholder" id="cameraCanvasContainer">
          <video
            id="cameraPreview"
            autoplay
            muted
            playsinline
            style="display: none; width: 100%; height: 100%; object-fit: contain; background-color: #000000; border-radius: 12px;"
          ></video>
          <canvas id="processingCanvas" width="480" height="360" hidden></canvas>
          <canvas id="detectionOverlay" width="480" height="360"></canvas>
          <div id="cameraPlaceholder" style="display: flex; flex-direction: column; align-items: center; gap: 8px;">
            <span style="font-size: 2rem;">📷</span>
            <span>IR Lens Blink Detector</span>
            <span style="font-size: 0.75rem; color: var(--text-muted);">(480×360 Client-Side Computer Vision)</span>
          </div>
        </div>
        <div style="display: flex; gap: 10px; margin-top: 12px;">
          <button class="btn btn-secondary" id="cameraToggleBtn" style="flex: 1;">
            Turn Camera On
          </button>
          <button class="btn btn-secondary" id="simulationToggleBtn" style="flex: 1;" disabled>
            Run Demo Blink
          </button>
        </div>
      </div>

      <div class="card">
        <div class="card-title">IR LED Blink Verification</div>
        <div style="font-size: 0.85rem; color: var(--text-muted);">
          Blink Match: <strong id="blinkScore" style="color: var(--text-main);">0%</strong>
          <span id="blinkStatus">(No optical target detected)</span>
          <div style="margin-top: 6px; font-size: 0.75rem;">Expected pattern: 500 ms on / 500 ms off (1 Hz)</div>
        </div>
      </div>
    </div>

    <!-- Smart Code Word — always visible, independent of active tab -->
    <div class="card" id="codeWordCard">
      <div class="card-title">Smart Code Word</div>
      <p class="cw-hint">Set a natural-sounding phrase to silently trigger SOS.</p>

      <!-- Example phrases shown before a code word is set -->
      <div class="cw-examples" id="cwExamples">
        <span class="cw-chip">"Where did I put my blue bag?"</span>
        <span class="cw-chip">"I need to call mom"</span>
        <span class="cw-chip">"What time is our flight?"</span>
        <span class="cw-chip">"I think I left my keys in the car."</span>
      </div>

      <!-- Status shown once a code word is saved -->
      <div class="cw-status" id="cwStatus">
        <span id="cwStatusText" style="font-size:0.85rem; color:var(--accent-green); font-weight:600;">✅ Code Word Set</span>
        <span id="cwWordDisplay" style="font-size:0.75rem; color:var(--text-muted);"></span>
      </div>

      <!-- Inline progress during 3-round capture -->
      <div class="round-status" id="roundStatus"></div>
      <button class="btn btn-secondary" id="roundRecordBtn" style="display:none; width:100%; margin-bottom:10px;"></button>

      <div class="setup-row">
        <button class="btn btn-secondary" id="cwSetBtn">Set Code Word</button>
        <button class="btn mic-btn" id="micBtn" disabled>🎙 Mic OFF</button>
      </div>
    </div>
  </main>

  <!-- Footer System Log -->
  <footer>
    <div class="log-line" id="systemLog">SYS: AEGIS App initialized. Mode: Wireless Radar.</div>
    <span style="font-size: 0.7rem; color: var(--text-muted);">v1.0</span>
  </footer>

  <!-- SOS Confirmation Modal -->
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

  <!-- Settings Bottom Sheet -->
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
      <input type="tel" id="inputContact" value="+917406531222" readonly
        style="opacity:0.6; cursor:not-allowed; background:#fff7fa;">
      <small style="color:var(--text-muted); font-size:0.72rem;">Pre-configured. Cannot be changed.</small>
    </div>
    <button class="btn btn-danger" id="saveSettings" style="width:100%; margin-top:8px;">Save</button>
  </div>

  <script>
    // ─── Green API Credentials ────────────────────────────────────────────────
    // Replace these with real values before deployment.
    const GREEN_API_INSTANCE = '710722748361';
    const GREEN_API_TOKEN    = '303c9adf18e54cb3bce3236cec65758a6c457dacf91246a89e';

    // ─── LocalStorage Keys ────────────────────────────────────────────────────
    const KEY_NAME     = 'aegis_user_name';
    const KEY_CONTACT  = 'aegis_emergency_contact';
    const KEY_CODEWORD = 'aegis_code_word';

    // ─── Utility ──────────────────────────────────────────────────────────────
    const systemLog = document.getElementById('systemLog');

    function logMessage(msg) {
      const time = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
      systemLog.textContent = `[${time}] ${msg}`;
    }

    // ─── Tab Switching ────────────────────────────────────────────────────────
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

    function setCameraStatus(isOn) {
      cameraStatus.innerHTML = isOn
        ? '<span class="status-dot"></span> Camera On'
        : '<span class="status-dot" style="background-color: var(--accent-blue);"></span> Camera Off';
      cameraStatus.style.backgroundColor = isOn
        ? 'rgba(16, 185, 129, 0.15)'
        : 'rgba(214, 51, 108, 0.12)';
      cameraStatus.style.color = isOn ? 'var(--accent-green)' : 'var(--accent-blue)';
    }

    function rgbToHsl(r, g, b) {
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
    }

    // Detects optical light candidates across all colors (IR, Red, White, Blue, Green, Amber, etc.)
    function getOpticalCandidate(r, g, b) {
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
    }

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

    // Only outline lights confirmed to blink at the expected 500ms half-cycle.
    function drawDetections(targets) {
      overlayContext.clearRect(0, 0, detectionOverlay.width, detectionOverlay.height);
      if (!targets || targets.length === 0) return;

      const scaleX = detectionOverlay.width / processingCanvas.width;
      const scaleY = detectionOverlay.height / processingCanvas.height;

      targets.filter(target => target.status === 'CONFIRMED').forEach(target => {
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

    // Ensure the hardcoded contact is always stored.
    const FIXED_CONTACT = '+917406531222';
    localStorage.setItem(KEY_CONTACT, FIXED_CONTACT);

    function openSettings() {
      inputName.value    = localStorage.getItem(KEY_NAME) || '';
      inputContact.value = FIXED_CONTACT; // always show fixed number
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
      if (!name) {
        logMessage('⚠️ Please enter your name.');
        return;
      }
      localStorage.setItem(KEY_NAME, name);
      localStorage.setItem(KEY_CONTACT, FIXED_CONTACT); // always lock the contact
      closeSettings();
      logMessage('✅ Emergency profile saved. Requesting location permission...');

      // Request geolocation now so the browser caches the permission grant.
      // This avoids a first-SOS delay where the user has to respond to the
      // permission dialog while already in an emergency.
      if ('geolocation' in navigator) {
        navigator.geolocation.getCurrentPosition(
          () => logMessage('✅ Location permission granted. SOS ready.'),
          () => logMessage('⚠️ Location permission denied. SOS will send without GPS.'),
          { timeout: 10000, maximumAge: 60000 }
        );
      }
    });

    // ─── SOS Alert ────────────────────────────────────────────────────────────
    const sosBtn    = document.getElementById('sosBtn');
    const sosModal  = document.getElementById('sosModal');
    const sosCancel = document.getElementById('sosCancel');
    const sosConfirm = document.getElementById('sosConfirm');

    // Shared function used by both the manual SOS button and the voice trigger.
    // lat/lon may be null if geolocation was denied.
    let lastSosSentAt = 0; // debounce: prevent duplicate silent triggers

    function sendSOSAlert(lat, lon) {
      const now = Date.now();
      if (now - lastSosSentAt < 30000) return; // 30-second cooldown
      lastSosSentAt = now;

      const name    = localStorage.getItem(KEY_NAME)    || 'Unknown User';
      const contact = localStorage.getItem(KEY_CONTACT) || '';

      if (!contact) {
        logMessage('⚠️ SOS: No emergency contact set. Tap ⚙️ to add one.');
        return;
      }

      const locationStr = (lat !== null && lon !== null)
        ? `https://maps.google.com/?q=${lat},${lon}`
        : 'Unavailable';

      const message = `🚨 EMERGENCY: ${name} has triggered an SOS alert. Location: ${locationStr}`;

      // Strip non-digits from contact to build the Green API chatId.
      const chatId = contact.replace(/\D/g, '') + '@c.us';

      fetch(`https://api.green-api.com/waInstance${GREEN_API_INSTANCE}/sendMessage/${GREEN_API_TOKEN}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
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
          (pos) => {
            logMessage('🚨 GPS acquired. Sending WhatsApp alert...');
            sendSOSAlert(pos.coords.latitude, pos.coords.longitude);
          },
          () => {
            logMessage('🚨 GPS denied. Sending alert without location...');
            sendSOSAlert(null, null);
          },
          { timeout: 5000 }
        );
      } else {
        sendSOSAlert(null, null);
      }
    }

    sosBtn.addEventListener('click', () => {
      sosModal.classList.add('active');
    });

    sosCancel.addEventListener('click', () => {
      sosModal.classList.remove('active');
    });

    sosConfirm.addEventListener('click', () => {
      sosModal.classList.remove('active');
      lastSosSentAt = 0; // manual press always goes through
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

    let isCapturing     = false;
    let captureRound    = 0;
    let capturedPhrases = [];

    function refreshCodeWordUI() {
      const stored = localStorage.getItem(KEY_CODEWORD);
      if (stored) {
        cwExamples.style.display    = 'none';
        cwStatus.style.display      = 'flex';
        cwStatusText.textContent    = '✅ Code Word Set';
        cwWordDisplay.textContent   = `"${stored}"`;
        cwSetBtn.textContent        = 'Change Code Word';
        micBtn.disabled             = false;
      } else {
        cwExamples.style.display    = 'flex';
        cwStatus.style.display      = 'none';
        cwSetBtn.textContent        = 'Set Code Word';
        micBtn.disabled             = true;
      }
    }

    // Pick most frequently seen phrase; fall back to longest if all differ.
    function getBestPhrase(phrases) {
      const freq = {};
      phrases.forEach(p => { freq[p] = (freq[p] || 0) + 1; });
      return Object.entries(freq).sort((a, b) => b[1] - a[1] || b[0].length - a[0].length)[0][0];
    }

    // Each call sets up the button for the next round (or the confirm step).
    // The user manually taps the button to trigger recording — no auto-advance.
    function startCaptureRound() {
      if (!SpeechRecognition) {
        logMessage('⚠️ Voice features require Chrome or Edge. Firefox does not support Web Speech API.');
        cwSetBtn.disabled = false;
        isCapturing = false;
        return;
      }

      if (captureRound >= 3) {
        // All 3 rounds done — show best match and let user confirm.
        const best = getBestPhrase(capturedPhrases);
        roundStatus.style.display = 'block';
        roundStatus.textContent   = `Best match: "${best}"`;
        roundRecordBtn.textContent = '✅ Use this phrase';
        roundRecordBtn.style.display = 'block';
        roundRecordBtn.disabled = false;
        roundRecordBtn.onclick = () => {
          localStorage.setItem(KEY_CODEWORD, best);
          isCapturing = false; captureRound = 0; capturedPhrases = [];
          roundStatus.style.display    = 'none';
          roundRecordBtn.style.display = 'none';
          roundRecordBtn.onclick       = null;
          cwSetBtn.disabled = false;
          refreshCodeWordUI();
          logMessage('✅ Code word saved.');
        };
        return;
      }

      captureRound++;
      roundRecordBtn.textContent   = `🎙 Record Round ${captureRound} of 3`;
      roundRecordBtn.style.display = 'block';
      roundRecordBtn.disabled      = false;

      roundRecordBtn.onclick = () => {
        roundRecordBtn.disabled    = true;
        roundRecordBtn.textContent = '⏺ Listening...';
        roundStatus.style.display  = 'block';
        roundStatus.textContent    = `Round ${captureRound} of 3 — say your phrase now...`;

        const rec = new SpeechRecognition();
        rec.lang            = 'en-US';
        rec.interimResults  = false;
        rec.maxAlternatives = 1;

        rec.onresult = (e) => {
          const phrase = e.results[0][0].transcript.trim();
          capturedPhrases.push(phrase);
          roundStatus.textContent      = `✓ Round ${captureRound} of 3: "${phrase}"`;
          roundRecordBtn.style.display = 'none';
          startCaptureRound(); // set up the next round button
        };

        rec.onerror = () => {
          roundStatus.textContent = `⚠️ Could not hear — tap to try again.`;
          captureRound--; // retry the same round
          startCaptureRound();
        };

        rec.start();
      };
    }

    cwSetBtn.addEventListener('click', () => {
      if (isCapturing) return;
      // Stop continuous mic if running before capturing a new code word.
      if (isMicOn) stopContinuousListening();
      isCapturing = true; captureRound = 0; capturedPhrases = [];
      cwSetBtn.disabled            = true;
      roundStatus.style.display    = 'none';
      roundRecordBtn.style.display = 'none';
      roundRecordBtn.onclick       = null;
      startCaptureRound();
    });

    // ─── Continuous Voice Trigger ─────────────────────────────────────────────
    let isMicOn       = false;
    let continuousRec = null;

    // On file:// protocol, Chrome re-prompts for mic on every .start() call and
    // blocks geolocation. Auto-restart is disabled in that case to stop the spam.
    const IS_FILE_PROTOCOL = location.protocol === 'file:';

    function startContinuousListening() {
      if (!SpeechRecognition) {
        logMessage('⚠️ Voice features require Chrome or Edge. Firefox does not support Web Speech API.');
        isMicOn = false;
        micBtn.textContent = '🎙 Mic OFF';
        micBtn.classList.remove('mic-active');
        return;
      }

      continuousRec                = new SpeechRecognition();
      continuousRec.lang           = 'en-US';
      continuousRec.continuous     = true;
      continuousRec.interimResults = true;

      continuousRec.onresult = (e) => {
        const codeWord = localStorage.getItem(KEY_CODEWORD);
        if (!codeWord) return;
        const codeWordLower = codeWord.toLowerCase();

        for (let i = e.resultIndex; i < e.results.length; i++) {
          const transcript = e.results[i][0].transcript.toLowerCase();
          if (transcript.includes(codeWordLower)) {
            logMessage('🔑 Code word detected — silent SOS triggered.');
            // Silently send — no visible alarm.
            if ('geolocation' in navigator) {
              navigator.geolocation.getCurrentPosition(
                (pos) => sendSOSAlert(pos.coords.latitude, pos.coords.longitude),
                ()    => sendSOSAlert(null, null),
                { timeout: 5000 }
              );
            } else {
              sendSOSAlert(null, null);
            }
            break;
          }
        }
      };

      continuousRec.onerror = (e) => {
        if (e.error === 'not-allowed') {
          logMessage('MIC: Permission denied. Enable microphone access and try again.');
          isMicOn = false;
          micBtn.textContent = '🎙 Mic OFF';
          micBtn.classList.remove('mic-active');
        }
      };

      // On file:// protocol, auto-restarting causes Chrome to re-prompt for mic
      // permission on every silence gap. Skip auto-restart; mic pauses on silence.
      if (!IS_FILE_PROTOCOL) {
        continuousRec.onend = () => {
          if (isMicOn) continuousRec.start();
        };
      } else {
        continuousRec.onend = () => {
          if (isMicOn) {
            isMicOn = false;
            micBtn.textContent = '🎙 Mic OFF';
            micBtn.classList.remove('mic-active');
            logMessage('MIC: Paused after silence. Serve over HTTP for continuous listening.');
          }
        };
      }

      continuousRec.start();
      logMessage('MIC: Listening for code word...');
    }

    function stopContinuousListening() {
      if (continuousRec) {
        continuousRec.onend = null; // prevent auto-restart
        continuousRec.stop();
        continuousRec = null;
      }
      logMessage('MIC: Stopped.');
    }

    micBtn.addEventListener('click', () => {
      if (isMicOn) {
        isMicOn = false;
        micBtn.textContent = '🎙 Mic OFF';
        micBtn.classList.remove('mic-active');
        stopContinuousListening();
      } else {
        isMicOn = true;
        micBtn.textContent = '🎙 Mic ON';
        micBtn.classList.add('mic-active');
        startContinuousListening();
      }
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // ─── WIRELESS RADAR MODULE ────────────────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════════════

    const btnConnect        = document.getElementById('btnConnect');
    const espIpInput        = document.getElementById('espIpInput');
    const connectedIpDisplay = document.getElementById('connectedIpDisplay');
    const connectedIpText   = document.getElementById('connectedIpText');
    const refreshDot        = document.getElementById('refreshDot');
    const refreshLabel      = document.getElementById('refreshLabel');
    const radarStatusBadge  = document.getElementById('radarStatusBadge');
    const radarStatusText   = document.getElementById('radarStatusText');
    const guidanceText      = document.getElementById('guidanceText');
    const guidanceSub       = document.getElementById('guidanceSub');
    const wifiList          = document.getElementById('wifiList');
    const bleList           = document.getElementById('bleList');
    const wifiCount         = document.getElementById('wifiCount');
    const bleCount          = document.getElementById('bleCount');
    const totalSignalCount  = document.getElementById('totalSignalCount');
    const radarCanvas       = document.getElementById('radarCanvas');
    const rctx              = radarCanvas.getContext('2d');
    const radarWrap         = document.getElementById('radarWrap');
    const btnLinkGyro       = document.getElementById('btnLinkGyro');
    const gyroBtnText       = document.getElementById('gyroBtnText');
    const gyroStatusText    = document.getElementById('gyroStatus');
    const btnHitWall        = document.getElementById('btnHitWall');
    const btnSimTarget      = document.getElementById('btnSimTarget');
    const targetLockBanner  = document.getElementById('targetLockBanner');
    const lockedTargetName  = document.getElementById('lockedTargetName');
    const lockedTargetRssi  = document.getElementById('lockedTargetRssi');
    const lockedDeltaPill   = document.getElementById('lockedDeltaPill');
    const lockedTargetDist  = document.getElementById('lockedTargetDist');
    const currentSectorLabel= document.getElementById('currentSectorLabel');
    const targetVectorLabel = document.getElementById('targetVectorLabel');
    const roomPrediction    = document.getElementById('roomPrediction');
    const roomPredictionText= document.getElementById('roomPredictionText');
    const btnDropTarget     = document.getElementById('btnDropTarget');

    // Signal sub-tabs
    document.getElementById('stWifi').addEventListener('click', () => {
      document.getElementById('stWifi').classList.add('active');
      document.getElementById('stBle').classList.remove('active');
      document.getElementById('paneWifi').classList.add('active');
      document.getElementById('paneBle').classList.remove('active');
    });
    document.getElementById('stBle').addEventListener('click', () => {
      document.getElementById('stBle').classList.add('active');
      document.getElementById('stWifi').classList.remove('active');
      document.getElementById('paneBle').classList.add('active');
      document.getElementById('paneWifi').classList.remove('active');
    });

    // ─── State ─────────────────────────────────────────────────────────────
    let espBaseUrl      = '';
    let isConnected     = false;
    let scanInterval    = null;
    let lastWifiData    = [];
    let lastBleData     = [];
    let radarAngle      = 0;
    let radarAnimId     = null;
    let demoSimActive   = false;

    // ─── Issue 3: Sensor & 8-Sector Gradient-Ascent State ──────────────────
    let gyroLinked        = false;
    let gyroListenerAttached = false;
    let gyroCheckTimer = null;
    let relativeHeading   = 0;          // 0 to 360 deg
    let baselineAlpha     = null;
    let currentAlpha      = 0;

    const NUM_SECTORS     = 8;          // 8 Sectors of 45 deg each
    const SECTOR_NAMES    = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
    let sectorData        = Array.from({ length: NUM_SECTORS }, () => ({ rssi: -Infinity, avg: -100, count: 0, momentum: 0 }));
    let wallZones         = new Array(NUM_SECTORS).fill(false);
    let wallActive        = false;
    let targetVectorAngle = 0;          // Best target angle (0-360)
    let trackingState     = 'IDLE';     // 'IDLE' | 'NAVIGATING' | 'FOUND'

    // Locked target
    let lockedTarget      = null;       // { type: 'wifi'|'ble', id: '...', name: '...' }
    let prevLockedRSSI    = null;
    let lockedRssiHistory = [];         // Array of { time, rssi }
    let maxLockedRSSI     = -Infinity;
    let minLockedRSSI     = Infinity;
    let wallHitCount      = 0;

    // ─── Math & Helper Functions ───────────────────────────────────────────
    function rssiToPercent(rssi) {
      return Math.max(0, Math.min(100, ((rssi + 100) / 70) * 100));
    }

    function rssiColor(rssi) {
      if (rssi >= -55) return '#098968';   // strong green
      if (rssi >= -70) return '#f59e0b';   // medium amber
      return '#e11d48';                    // weak red
    }

    function rssiLabel(rssi) {
      if (rssi >= -55) return 'Strong';
      if (rssi >= -70) return 'Medium';
      return 'Weak';
    }

    function estimateDistance(rssi) {
      // Log-distance path loss model: d = 10 ^ ((-40 - RSSI) / (10 * 2.2))
      const d = Math.pow(10, (-40 - rssi) / 22);
      return Math.max(0.2, Math.min(25.0, d));
    }

    function getCurrentSector(heading) {
      const h = ((heading % 360) + 360) % 360;
      return Math.floor(((h + 22.5) % 360) / 45);
    }

    // ─── Task 1: Gyroscope & Compass Integration ───────────────────────────
    function initSensors() {
      function setGyroStatus(message, connected = false) {
        gyroStatusText.textContent = `Gyro status: ${message}`;
        gyroStatusText.style.color = connected ? 'var(--accent-green)' : '#b45309';
      }

      if (!window.isSecureContext) {
        setGyroStatus('Blocked: this page is not a secure context. Open it over HTTPS or enable the Chrome flag for this exact ESP32 address.');
        gyroBtnText.textContent = 'Gyro Blocked';
        btnLinkGyro.classList.remove('linked');
        logMessage('Gyroscope unavailable: browser blocks orientation sensors on this HTTP page.');
        return;
      }

      if (typeof DeviceOrientationEvent === 'undefined') {
        setGyroStatus('Unsupported: this browser does not expose device orientation.');
        gyroBtnText.textContent = 'Gyro Unsupported';
        logMessage('This browser does not support device orientation.');
        return;
      }

      gyroLinked = false;
      baselineAlpha = null;
      btnLinkGyro.classList.remove('linked');
      gyroBtnText.textContent = 'Waiting for gyro…';
      setGyroStatus('Waiting for permission and sensor data…');

      if (!gyroListenerAttached) {
        window.addEventListener('deviceorientation', function(event) {
          const raw = (event.webkitCompassHeading != null)
            ? (360 - event.webkitCompassHeading)
            : event.alpha;
          if (raw === null || raw === undefined) {
            if (!gyroLinked) setGyroStatus('Event received, but no heading value is available.');
            return;
          }

          gyroLinked = true;
          clearTimeout(gyroCheckTimer);
          btnLinkGyro.classList.add('linked');
          currentAlpha = raw;
          if (baselineAlpha === null) baselineAlpha = currentAlpha;
          let delta = currentAlpha - baselineAlpha;
          if (delta < 0) delta += 360;
          if (delta >= 360) delta -= 360;
          relativeHeading = delta;

          const sec = getCurrentSector(relativeHeading);
          currentSectorLabel.textContent = `Sec ${sec} (${SECTOR_NAMES[sec]}) | ${Math.round(relativeHeading)}°`;
          gyroBtnText.textContent = `Gyro OK (${Math.round(relativeHeading)}°)`;
          setGyroStatus(`Connected — live heading data received (${Math.round(relativeHeading)}°).`, true);

          if (lockedTarget) updateDirectionalGuidance();
        }, true);
        gyroListenerAttached = true;
      }

      clearTimeout(gyroCheckTimer);
      gyroCheckTimer = setTimeout(() => {
        if (!gyroLinked) {
          setGyroStatus('No sensor data received. Check Chrome sensor permission, the secure-context flag, and that the phone has orientation sensors.');
          gyroBtnText.textContent = 'Gyro Not Detected';
          btnLinkGyro.classList.remove('linked');
          logMessage('Gyroscope check timed out: no deviceorientation data arrived.');
        }
      }, 5000);

      if (typeof DeviceOrientationEvent !== 'undefined' &&
          typeof DeviceOrientationEvent.requestPermission === 'function') {
        DeviceOrientationEvent.requestPermission()
          .then(permission => {
            if (permission === 'granted') logMessage('Gyroscope permission granted; waiting for sensor data.');
            else {
              clearTimeout(gyroCheckTimer);
              setGyroStatus('Permission denied. Allow motion/orientation access in browser settings.');
              gyroBtnText.textContent = 'Gyro Permission Denied';
              logMessage('Gyroscope permission was denied.');
            }
          })
          .catch(err => {
            clearTimeout(gyroCheckTimer);
            console.error(err);
            setGyroStatus(`Permission error: ${err.message || 'request failed'}.`);
            gyroBtnText.textContent = 'Gyro Error';
            logMessage('Error requesting Gyroscope permission.');
          });
      } else logMessage('Waiting for device orientation data.');
    }

    btnLinkGyro.addEventListener('click', initSensors);

    // ─── Task 1 & 2: Lock Target & Gradient-Ascent Tracking ─────────────────
    function lockTarget(type, id, name) {
      lockedTarget = { type, id, name };
      prevLockedRSSI = null;
      lockedRssiHistory = [];
      maxLockedRSSI = -Infinity;
      minLockedRSSI = Infinity;
      wallHitCount = 0;
      wallActive = false;
      wallZones.fill(false);
      sectorData = Array.from({ length: NUM_SECTORS }, () => ({ rssi: -Infinity, avg: -100, count: 0, momentum: 0 }));
      targetVectorAngle = 0;
      trackingState = 'NAVIGATING';

      targetLockBanner.classList.add('active');
      btnHitWall.style.display = 'inline-flex';
      radarWrap.classList.remove('wall-warning');
      lockedTargetName.textContent = name || id;

      logMessage(`Target locked: ${name} [${id}]. Initializing 8-sector gradient ascent.`);
      renderWifiList(lastWifiData);
      renderBleList(lastBleData);
      updateDirectionalGuidance();
    }

    function dropTarget() {
      if (!lockedTarget) return;
      logMessage(`Target ${lockedTarget.name} dropped. Returning to idle scan.`);
      lockedTarget = null;
      prevLockedRSSI = null;
      trackingState = 'IDLE';
      wallActive = false;
      targetLockBanner.classList.remove('active');
      btnHitWall.style.display = 'none';
      radarWrap.classList.remove('wall-warning');
      guidanceText.textContent = 'STATUS: IDLE / WAITING FOR TARGET LOCK';
      guidanceSub.textContent = 'Select any detected Wi-Fi or Bluetooth signal below to begin directional navigation.';

      renderWifiList(lastWifiData);
      renderBleList(lastBleData);
    }

    btnDropTarget.addEventListener('click', dropTarget);

    function executeGradientAscent(currentRSSI) {
      const now = Date.now();
      const delta = (prevLockedRSSI !== null) ? (currentRSSI - prevLockedRSSI) : 0;
      prevLockedRSSI = currentRSSI;

      lockedRssiHistory.push({ time: now, rssi: currentRSSI });
      if (lockedRssiHistory.length > 30) lockedRssiHistory.shift();

      if (currentRSSI > maxLockedRSSI) maxLockedRSSI = currentRSSI;
      if (currentRSSI < minLockedRSSI) minLockedRSSI = currentRSSI;

      // Update UI Header
      lockedTargetRssi.textContent = `${currentRSSI} dBm`;
      lockedTargetDist.textContent = `${estimateDistance(currentRSSI).toFixed(1)} m`;

      if (delta > 1.5) {
        lockedDeltaPill.className = 'delta-pill delta-up';
        lockedDeltaPill.textContent = `▲ +${delta.toFixed(1)} dBm (Warmer!)`;
      } else if (delta < -1.5) {
        lockedDeltaPill.className = 'delta-pill delta-down';
        lockedDeltaPill.textContent = `▼ ${delta.toFixed(1)} dBm (Colder)`;
      } else {
        lockedDeltaPill.className = 'delta-pill delta-flat';
        lockedDeltaPill.textContent = `~ ${delta.toFixed(1)} dBm (Stable)`;
      }

      // Check proximity threshold (>= -50 dBm)
      if (currentRSSI >= -50) {
        trackingState = 'FOUND';
        guidanceText.textContent = '🎯 TARGET PROXIMITY REACHED (>= -50 dBm)!';
        guidanceSub.textContent = 'Stop walking. Target is within 1-2 metres of your current position.';
        btnHitWall.style.display = 'none';
        updateRoomPrediction();
        return;
      }

      // Map to facing sector
      const sec = getCurrentSector(relativeHeading);
      const sd  = sectorData[sec];

      if (sd.count === 0) {
        sd.avg = currentRSSI;
        sd.rssi = currentRSSI;
      } else {
        sd.avg = currentRSSI * 0.65 + sd.avg * 0.35;
        if (currentRSSI > sd.rssi) sd.rssi = currentRSSI;
      }
      sd.count++;

      // Update momentum based on directional delta
      if (Math.abs(delta) > 1.5) {
        sd.momentum = (delta > 0) ? 1 : -1;
      }
      if (sd.momentum < 0 && sd.count > 1) {
        sd.avg = Math.max(sd.avg - 3, -100);
      }

      // If sharp positive gradient (+3 dBm), snap directly to this direction
      if (delta > 3) {
        sd.momentum = 1;
        targetVectorAngle = sec * 45;
        updateDirectionalGuidance();
        updateRoomPrediction();
        return;
      }

      // Find sector with highest score = avg + (momentum * 5)
      let bestSector = sec;
      let bestScore = -Infinity;
      let hasVisited = false;

      for (let i = 0; i < NUM_SECTORS; i++) {
        if (wallZones[i]) continue;
        if (sectorData[i].count === 0) continue;
        hasVisited = true;
        const score = sectorData[i].avg + sectorData[i].momentum * 5;
        if (score > bestScore) {
          bestScore = score;
          bestSector = i;
        }
      }

      if (!hasVisited) bestSector = sec;
      targetVectorAngle = bestSector * 45;
      targetVectorLabel.textContent = `Sec ${bestSector} (${SECTOR_NAMES[bestSector]}) ~${Math.round(targetVectorAngle)}°`;

      updateDirectionalGuidance();
      updateRoomPrediction();
    }

    // ─── Task 3: Obstacle & Room Logic ─────────────────────────────────────
    function reportWall() {
      if (!lockedTarget || trackingState === 'FOUND') return;
      const sec = getCurrentSector(relativeHeading);
      wallZones[sec] = true;
      sectorData[sec].momentum = -99;
      wallActive = true;
      wallHitCount++;
      radarWrap.classList.add('wall-warning');

      logMessage(`Obstacle reported in Sector ${sec} (${SECTOR_NAMES[sec]}). Blacklisting path and recalculating.`);
      updateDirectionalGuidance();
      updateRoomPrediction();
    }

    btnHitWall.addEventListener('click', reportWall);

    function updateDirectionalGuidance() {
      if (!lockedTarget) return;

      if (trackingState === 'FOUND') {
        guidanceText.innerHTML = '<span style="color:var(--accent-green);">🎯 DEVICE FOUND NEARBY!</span>';
        guidanceSub.textContent = 'Signal >= -50 dBm. Look within 1-2 metres around you.';
        return;
      }

      const displayAngle = (targetVectorAngle - relativeHeading + 360) % 360;

      if (wallActive) {
        const sec = getCurrentSector(relativeHeading);
        const leftS = (sec - 1 + NUM_SECTORS) % NUM_SECTORS;
        const rightS = (sec + 1) % NUM_SECTORS;
        const leftScore  = !wallZones[leftS]  ? (sectorData[leftS].avg  + sectorData[leftS].momentum * 5) : -Infinity;
        const rightScore = !wallZones[rightS] ? (sectorData[rightS].avg + sectorData[rightS].momentum * 5) : -Infinity;

        let escapeText = '';
        if (leftScore > rightScore && leftScore > -Infinity) {
          escapeText = 'Obstacle ahead! Suggestion: <b>TURN LEFT ⬅️</b>';
        } else if (rightScore > leftScore && rightScore > -Infinity) {
          escapeText = 'Obstacle ahead! Suggestion: <b>TURN RIGHT ➡️</b>';
        } else {
          escapeText = 'Path blocked by wall. <b>BACKTRACK</b> and try another direction.';
        }

        guidanceText.innerHTML = '<span style="color:#e11d48;">🧱 WALL BLOCKED PATH</span>';
        guidanceSub.innerHTML = escapeText;

        // If user has rotated away from the wall by > 75 deg, clear active warning
        if (displayAngle > 75 && displayAngle < 285) {
          wallActive = false;
          radarWrap.classList.remove('wall-warning');
        }
        return;
      }

      if (displayAngle <= 25 || displayAngle >= 335) {
        guidanceText.innerHTML = '<span style="color:var(--accent-green);">ALIGNED - PROCEED FORWARD ⬆️</span>';
        guidanceSub.textContent = `Heading matches target vector (Sector ${getCurrentSector(targetVectorAngle)}). Move forward!`;
      } else if (displayAngle > 25 && displayAngle <= 150) {
        guidanceText.innerHTML = 'TURN RIGHT ➡️';
        guidanceSub.textContent = `Rotate ~${Math.round(displayAngle)}° to align with signal peak.`;
      } else if (displayAngle > 150 && displayAngle < 210) {
        guidanceText.innerHTML = '<span style="color:#e11d48;">WRONG WAY - TURN AROUND ⬇️</span>';
        guidanceSub.textContent = 'Signal is weakening in this direction. Reverse heading.';
      } else {
        guidanceText.innerHTML = '⬅️ TURN LEFT';
        guidanceSub.textContent = `Rotate ~${Math.round(360 - displayAngle)}° to align with signal peak.`;
      }
    }

    function updateRoomPrediction() {
      if (!lockedTarget) return;

      const samples = lockedRssiHistory.length;
      if (samples < 3) {
        roomPrediction.className = 'room-prediction';
        roomPredictionText.textContent = 'Room Analysis: Sampling signal limits...';
        return;
      }

      // If peak RSSI reached >= -55 dBm, signal is definitely unobstructed in the room
      if (maxLockedRSSI >= -55) {
        roomPrediction.className = 'room-prediction room-pred-inside';
        roomPredictionText.innerHTML = '<b>Inside Room</b> (Strong direct path, &lt;2.5m)';
      } else if (maxLockedRSSI >= -65) {
        roomPrediction.className = 'room-prediction room-pred-inside';
        roomPredictionText.innerHTML = '<b>Likely Inside Room</b> (Good signal gradient)';
      } else if (wallHitCount >= 1 || (samples >= 8 && maxLockedRSSI < -68)) {
        // Repeated scans never exceed -68 dBm and user encountered walls -> transmitter is beyond a boundary
        roomPrediction.className = 'room-prediction room-pred-outside';
        roomPredictionText.innerHTML = '<b>Behind Wall / Outside Room</b> (High attenuation, max ' + maxLockedRSSI + ' dBm)';
      } else {
        roomPrediction.className = 'room-prediction';
        roomPredictionText.textContent = `Room Analysis: Max ${maxLockedRSSI} dBm across ${samples} scans`;
      }
    }

    // ─── Demo Spy Camera Simulator ─────────────────────────────────────────
    btnSimTarget.addEventListener('click', () => {
      demoSimActive = !demoSimActive;
      btnSimTarget.textContent = demoSimActive ? '⚡ Stop Demo' : '⚡ Demo Target';
      btnSimTarget.style.borderColor = demoSimActive ? 'var(--accent-blue)' : '';
      btnSimTarget.style.color = demoSimActive ? 'var(--accent-blue)' : '';

      if (demoSimActive) {
        logMessage('Demo Target injected: SPY_CAM_WIFI at 90° (East). Turn phone to test gyro radar!');
        // Inject mock networks into lists
        const mockWifi = [
          { ssid: 'SPY_CAM_WIFI', bssid: 'AA:BB:CC:DD:EE:01', rssi: -62, channel: 6, freq: 2437 },
          { ssid: 'LivingRoom_5G', bssid: '12:34:56:78:9A:BC', rssi: -74, channel: 36, freq: 5180 },
          { ssid: 'Neighbor_Wifi', bssid: '98:76:54:32:10:FE', rssi: -85, channel: 1, freq: 2412 }
        ];
        const mockBle = [
          { name: 'SPY_BEACON_BLE', rssi: -65 },
          { name: 'SmartWatch_X', rssi: -79 }
        ];
        lastWifiData = mockWifi;
        lastBleData = mockBle;
        renderWifiList(mockWifi);
        renderBleList(mockBle);
        lockTarget('wifi', 'AA:BB:CC:DD:EE:01', 'SPY_CAM_WIFI');
      } else {
        dropTarget();
      }
    });

    // ─── Task 2 & 4: Enhanced Radar Canvas Drawing ──────────────────────────
    function drawRadar(wifiSignals, bleSignals) {
      const W = radarCanvas.width;
      const H = radarCanvas.height;
      const cx = W / 2;
      const cy = H / 2;
      const maxR = Math.min(cx, cy) - 10;

      rctx.clearRect(0, 0, W, H);

      // 1. Radar background
      rctx.beginPath();
      rctx.arc(cx, cy, maxR, 0, Math.PI * 2);
      rctx.fillStyle = '#fff0f5';
      rctx.fill();

      // 2. Draw 8 Sector Wedges & Wall / Heatmap Overlay
      for (let i = 0; i < NUM_SECTORS; i++) {
        const startAngle = i * (Math.PI / 4) - (Math.PI / 8) - (Math.PI / 2);
        const endAngle   = startAngle + (Math.PI / 4);

        rctx.beginPath();
        rctx.moveTo(cx, cy);
        rctx.arc(cx, cy, maxR, startAngle, endAngle);
        rctx.closePath();

        if (wallZones[i]) {
          // Blacklisted sector (Hit Wall)
          rctx.fillStyle = 'rgba(225, 29, 72, 0.35)';
          rctx.fill();
        } else if (lockedTarget && sectorData[i].count > 0) {
          // Heatmap based on signal average
          const avg = sectorData[i].avg;
          if (avg >= -60) rctx.fillStyle = 'rgba(16, 185, 129, 0.28)';
          else if (avg >= -75) rctx.fillStyle = 'rgba(245, 158, 11, 0.22)';
          else rctx.fillStyle = 'rgba(214, 51, 108, 0.12)';
          rctx.fill();
        }
      }

      // 3. Concentric range rings
      const rings = 4;
      for (let i = 1; i <= rings; i++) {
        const r = (maxR / rings) * i;
        rctx.beginPath();
        rctx.arc(cx, cy, r, 0, Math.PI * 2);
        rctx.strokeStyle = 'rgba(242, 201, 216, 0.75)';
        rctx.lineWidth = 1;
        rctx.stroke();
      }

      // 4. 8 Sector Radial Dividers & Compass Labels
      rctx.font = 'bold 9px system-ui, sans-serif';
      rctx.textAlign = 'center';
      rctx.textBaseline = 'middle';

      for (let i = 0; i < NUM_SECTORS; i++) {
        const rad = i * (Math.PI / 4) - (Math.PI / 2);
        rctx.beginPath();
        rctx.moveTo(cx, cy);
        rctx.lineTo(cx + maxR * Math.cos(rad), cy + maxR * Math.sin(rad));
        rctx.strokeStyle = 'rgba(242, 201, 216, 0.7)';
        rctx.lineWidth = 1;
        rctx.stroke();

        // Label on outer rim
        const labelR = maxR - 7;
        const lx = cx + labelR * Math.cos(rad);
        const ly = cy + labelR * Math.sin(rad);
        rctx.fillStyle = wallZones[i] ? '#e11d48' : 'rgba(128, 86, 106, 0.8)';
        rctx.fillText(wallZones[i] ? '🧱' : SECTOR_NAMES[i], lx, ly);
      }

      // 5. Sweep trail and line (Idle / Scanning mode)
      const trailLen = Math.PI / 4;
      rctx.save();
      rctx.beginPath();
      rctx.moveTo(cx, cy);
      rctx.arc(cx, cy, maxR - 2, radarAngle - trailLen, radarAngle, false);
      rctx.closePath();
      const sweepGrad = rctx.createLinearGradient(cx, cy, cx + maxR, cy);
      sweepGrad.addColorStop(0, 'rgba(9, 137, 104, 0)');
      sweepGrad.addColorStop(1, 'rgba(9, 137, 104, 0.25)');
      rctx.fillStyle = sweepGrad;
      rctx.fill();
      rctx.restore();

      rctx.save();
      rctx.strokeStyle = '#098968';
      rctx.lineWidth = 2;
      rctx.shadowColor = '#098968';
      rctx.shadowBlur = 8;
      rctx.beginPath();
      rctx.moveTo(cx, cy);
      rctx.lineTo(cx + maxR * Math.cos(radarAngle), cy + maxR * Math.sin(radarAngle));
      rctx.stroke();
      rctx.restore();

      // 6. Draw Directional Nav Arrow if a target is locked
      if (lockedTarget) {
        const displayAngle = (targetVectorAngle - relativeHeading + 360) % 360;
        const arrowRad = (displayAngle - 90) * (Math.PI / 180);
        const arrowLen = maxR * 0.65;
        const tipX = cx + arrowLen * Math.cos(arrowRad);
        const tipY = cy + arrowLen * Math.sin(arrowRad);

        rctx.save();
        rctx.strokeStyle = (trackingState === 'FOUND') ? '#10b981' : '#d6336c';
        rctx.fillStyle = rctx.strokeStyle;
        rctx.lineWidth = 4;
        rctx.shadowColor = rctx.strokeStyle;
        rctx.shadowBlur = 10;

        // Vector line
        rctx.beginPath();
        rctx.moveTo(cx, cy);
        rctx.lineTo(tipX, tipY);
        rctx.stroke();

        // Arrow head
        const headAngle = Math.PI / 6;
        const headLen = 14;
        rctx.beginPath();
        rctx.moveTo(tipX, tipY);
        rctx.lineTo(
          tipX - headLen * Math.cos(arrowRad - headAngle),
          tipY - headLen * Math.sin(arrowRad - headAngle)
        );
        rctx.lineTo(
          tipX - headLen * Math.cos(arrowRad + headAngle),
          tipY - headLen * Math.sin(arrowRad + headAngle)
        );
        rctx.closePath();
        rctx.fill();
        rctx.restore();
      }

      // 7. Signal Blips (Wi-Fi & BLE)
      const allSignals = [
        ...wifiSignals.map(s => ({ ...s, type: 'wifi' })),
        ...bleSignals.map(s => ({ ...s, type: 'ble' }))
      ];

      allSignals.forEach((sig, idx) => {
        const isLocked = lockedTarget && (lockedTarget.id === (sig.bssid || sig.name));
        const angle = (idx / Math.max(allSignals.length, 1)) * Math.PI * 2;
        const dist  = (rssiToPercent(sig.rssi) / 100) * (maxR - 22) + 12;
        const sx    = cx + dist * Math.cos(angle);
        const sy    = cy + dist * Math.sin(angle);
        const color = rssiColor(sig.rssi);

        if (isLocked) {
          // Pulsing beacon ring for locked target
          rctx.save();
          rctx.shadowColor = '#d6336c';
          rctx.shadowBlur = 16;
          rctx.beginPath();
          rctx.arc(sx, sy, 10, 0, Math.PI * 2);
          rctx.fillStyle = '#d6336c';
          rctx.fill();
          rctx.strokeStyle = '#fff';
          rctx.lineWidth = 2;
          rctx.stroke();
          rctx.restore();
        } else {
          // Standard blip
          rctx.beginPath();
          rctx.arc(sx, sy, 4.5, 0, Math.PI * 2);
          rctx.fillStyle = color;
          rctx.fill();
        }
      });

      // 8. Center User Indicator
      rctx.save();
      rctx.shadowColor = '#d6336c';
      rctx.shadowBlur = 12;
      rctx.beginPath();
      rctx.arc(cx, cy, 7, 0, Math.PI * 2);
      rctx.fillStyle = '#d6336c';
      rctx.fill();
      rctx.restore();

      rctx.fillStyle = '#fff';
      rctx.font = 'bold 8px system-ui, sans-serif';
      rctx.textAlign = 'center';
      rctx.textBaseline = 'middle';
      rctx.fillText('YOU', cx, cy);

      // Status indicator when not connected and no demo
      if (!isConnected && !demoSimActive) {
        rctx.fillStyle = 'rgba(128, 86, 106, 0.6)';
        rctx.font = 'bold 12px system-ui, sans-serif';
        rctx.textAlign = 'center';
        rctx.textBaseline = 'middle';
        rctx.fillText('Connect ESP32 / Click Demo', cx, cy + maxR * 0.52);
      }
    }

    function animateRadar() {
      radarAngle = (radarAngle + 0.03) % (Math.PI * 2);
      drawRadar(lastWifiData, lastBleData);
      radarAnimId = requestAnimationFrame(animateRadar);
    }

    // ─── Task 1: Functional "Track" Button & List Rendering ────────────────
    function makeTrackBtn(type, id, name) {
      const btn = document.createElement('button');
      const isCurrentlyLocked = lockedTarget && lockedTarget.id === id;
      btn.className = 'btn-track' + (isCurrentlyLocked ? ' tracking' : '');
      btn.textContent = isCurrentlyLocked ? '✓ Tracking' : 'Track';

      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        if (isCurrentlyLocked) {
          dropTarget();
        } else {
          lockTarget(type, id, name);
        }
      });
      return btn;
    }

    function renderWifiList(signals) {
      wifiList.innerHTML = '';
      if (!signals.length) {
        wifiList.innerHTML = '<div class="empty-state"><span>📡</span>No Wi-Fi networks found</div>';
        return;
      }

      // Check if locked Wi-Fi device is present in this scan
      if (lockedTarget && lockedTarget.type === 'wifi') {
        const lockedSig = signals.find(s => s.bssid === lockedTarget.id || s.ssid === lockedTarget.id);
        if (lockedSig) {
          executeGradientAscent(lockedSig.rssi);
        }
      }

      signals.slice().sort((a, b) => b.rssi - a.rssi).forEach(sig => {
        const pct   = rssiToPercent(sig.rssi);
        const color = rssiColor(sig.rssi);
        const item  = document.createElement('div');
        const isThisLocked = lockedTarget && lockedTarget.id === sig.bssid;
        item.className = 'signal-item' + (isThisLocked ? ' locked-item' : '');

        item.innerHTML = `
          <span class="signal-icon">📶</span>
          <div class="signal-info">
            <div class="signal-name" title="${sig.ssid}">${sig.ssid || '(Hidden)'}</div>
            <div class="signal-meta">Ch ${sig.channel} · ${sig.bssid} · ${rssiLabel(sig.rssi)}</div>
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${sig.rssi} dBm</span>
            <div class="rssi-bar-bg">
              <div class="rssi-bar-fill" style="width:${pct}%;background:${color};"></div>
            </div>
          </div>
        `;
        item.appendChild(makeTrackBtn('wifi', sig.bssid, sig.ssid || sig.bssid));
        wifiList.appendChild(item);
      });
    }

    function renderBleList(signals) {
      bleList.innerHTML = '';
      if (!signals.length) {
        bleList.innerHTML = '<div class="empty-state"><span>📶</span>No Bluetooth devices found</div>';
        return;
      }

      // Check if locked BLE device is present in this scan
      if (lockedTarget && lockedTarget.type === 'ble') {
        const lockedSig = signals.find(s => s.name === lockedTarget.id);
        if (lockedSig) {
          executeGradientAscent(lockedSig.rssi);
        }
      }

      signals.slice().sort((a, b) => b.rssi - a.rssi).forEach(sig => {
        const pct   = rssiToPercent(sig.rssi);
        const color = rssiColor(sig.rssi);
        const item  = document.createElement('div');
        const isThisLocked = lockedTarget && lockedTarget.id === sig.name;
        item.className = 'signal-item' + (isThisLocked ? ' locked-item' : '');

        item.innerHTML = `
          <span class="signal-icon">🔹</span>
          <div class="signal-info">
            <div class="signal-name" title="${sig.name}">${sig.name}</div>
            <div class="signal-meta">BLE · ${rssiLabel(sig.rssi)}</div>
          </div>
          <div class="rssi-bar-wrap">
            <span class="rssi-value">${sig.rssi} dBm</span>
            <div class="rssi-bar-bg">
              <div class="rssi-bar-fill" style="width:${pct}%;background:${color};"></div>
            </div>
          </div>
        `;
        item.appendChild(makeTrackBtn('ble', sig.name, sig.name));
        bleList.appendChild(item);
      });
    }

    function updateGuidance(wifi, ble) {
      if (lockedTarget) {
        updateDirectionalGuidance();
        return;
      }
      const all = [...wifi, ...ble];
      if (!all.length) {
        guidanceText.textContent = 'STATUS: NO SIGNALS DETECTED';
        guidanceSub.textContent  = 'Connect to ESP32 or click Demo Target to begin scan.';
        return;
      }
      const strongest = all.reduce((best, s) => s.rssi > best.rssi ? s : best, all[0]);
      const name = strongest.ssid || strongest.name || 'Unknown';
      const label = rssiLabel(strongest.rssi);
      if (strongest.rssi >= -55) {
        guidanceText.textContent = `🚨 STRONG SIGNAL: ${name}`;
        guidanceSub.textContent  = `${strongest.rssi} dBm · Possible nearby device. Click "Track" to lock on and locate.`;
      } else if (strongest.rssi >= -70) {
        guidanceText.textContent = `⚠️ MEDIUM SIGNAL: ${name}`;
        guidanceSub.textContent  = `${strongest.rssi} dBm · ${label} signal. Click "Track" to start 8-sector vector navigation.`;
      } else {
        guidanceText.textContent = `✅ ALL SIGNALS WEAK`;
        guidanceSub.textContent  = `Strongest: ${name} at ${strongest.rssi} dBm. Area appears clear.`;
      }
    }

    // ── Pulse refresh dot ─────────────────────────────────────────────────────
    function pulseRefresh(ok) {
      refreshDot.classList.remove('pulsing');
      void refreshDot.offsetWidth;
      if (ok) {
        refreshDot.style.backgroundColor = '';
        refreshDot.classList.add('pulsing');
        setTimeout(() => refreshDot.classList.remove('pulsing'), 700);
      } else {
        refreshDot.style.backgroundColor = '#e11d48';
      }
    }

    // ── Fetch from ESP32 ──────────────────────────────────────────────────────
    async function fetchSignals() {
      if (!isConnected || !espBaseUrl) return;
      try {
        const [wRes, bRes] = await Promise.all([
          fetch(`${espBaseUrl}/wifi`, { signal: AbortSignal.timeout(4000) }),
          fetch(`${espBaseUrl}/ble`,  { signal: AbortSignal.timeout(4000) })
        ]);
        const wifi = await wRes.json();
        const ble  = await bRes.json();

        lastWifiData = wifi;
        lastBleData  = ble;

        wifiCount.textContent = wifi.length;
        bleCount.textContent  = ble.length;
        totalSignalCount.textContent = `${wifi.length + ble.length} signals`;

        renderWifiList(wifi);
        renderBleList(ble);
        updateGuidance(wifi, ble);

        const total = wifi.length + ble.length;
        refreshLabel.textContent = `Updated · ${wifi.length}W ${ble.length}BT`;
        radarStatusText.textContent = `${total} signal${total !== 1 ? 's' : ''}`;
        radarStatusBadge.querySelector('.status-dot').style.backgroundColor = 'var(--accent-green)';
        pulseRefresh(true);
        logMessage(`📡 Scan: ${wifi.length} Wi-Fi, ${ble.length} BLE devices found.`);
      } catch (err) {
        pulseRefresh(false);
        refreshLabel.textContent = 'Scan failed';
        logMessage('⚠️ ESP32 scan failed. Check connection.');
      }
    }

    btnConnect.addEventListener('click', async () => {
      if (isConnected) {
        // Disconnect
        isConnected = false;
        espBaseUrl  = '';
        clearInterval(scanInterval);
        scanInterval = null;
        btnConnect.textContent = 'Connect';
        btnConnect.classList.remove('connected');
        connectedIpDisplay.classList.remove('visible');
        refreshLabel.textContent = 'Not connected';
        refreshDot.style.backgroundColor = '#ccc';
        radarStatusText.textContent = 'Idle';
        radarStatusBadge.querySelector('.status-dot').style.backgroundColor = 'var(--accent-blue)';
        lastWifiData = []; lastBleData = [];
        wifiList.innerHTML = '<div class="empty-state"><span>📡</span>Connect to ESP32 to scan</div>';
        bleList.innerHTML  = '<div class="empty-state"><span>🔵</span>Connect to ESP32 to scan</div>';
        wifiCount.textContent = '0';
        bleCount.textContent  = '0';
        totalSignalCount.textContent = '0 signals';
        guidanceText.textContent = 'STATUS: IDLE / WAITING FOR SCAN';
        guidanceSub.textContent  = '';
        logMessage('Disconnected from ESP32.');
        return;
      }

      // Connect
      const ip = espIpInput.value.trim();
      if (!ip) { logMessage('⚠️ Enter an IP address first.'); return; }

      const url = ip.startsWith('http') ? ip : `http://${ip}`;
      btnConnect.textContent = '⏳ Connecting...';
      btnConnect.disabled    = true;
      logMessage(`Connecting to ESP32 at ${url}…`);

      try {
        const res = await fetch(`${url}/wifi`, { signal: AbortSignal.timeout(5000) });
        if (!res.ok) throw new Error('Bad response');

        isConnected = true;
        espBaseUrl  = url;
        btnConnect.textContent = '⏏ Disconnect';
        btnConnect.classList.add('connected');
        connectedIpText.textContent = `Connected to ${ip}`;
        connectedIpDisplay.classList.add('visible');
        refreshLabel.textContent = 'Connected';
        logMessage(`✅ Connected to ESP32 at ${ip}. Auto-scanning every 3s.`);

        // Immediate scan
        await fetchSignals();
        // Schedule every 3 seconds
        scanInterval = setInterval(fetchSignals, 3000);
      } catch (err) {
        if (window.location.protocol === 'https:' && url.startsWith('http:')) {
          alert("⚠️ BROWSER SECURITY BLOCK ⚠️\n\nYour browser is blocking the connection to the ESP32 because Vercel uses HTTPS and the ESP32 uses HTTP.\n\nTo fix this:\n1. Tap the Lock/Settings icon in your URL address bar.\n2. Go to Site Settings / Permissions.\n3. Find 'Insecure Content' (or Mixed Content) and change it to ALLOW.\n4. Reload the page.");
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
      // Open settings sheet on first run if profile is incomplete.
      if (!localStorage.getItem(KEY_NAME) || !localStorage.getItem(KEY_CONTACT)) {
        openSettings();
      }
      refreshCodeWordUI();
      // Warn early if running from file:// — mic auto-restart and geolocation
      // are blocked by Chrome on this protocol. Serve via HTTP for full features.
      if (IS_FILE_PROTOCOL) {
        logMessage('⚠️ file:// detected — serve over HTTP for mic & location to work fully.');
      } else {
        logMessage('SYS: App initialized. Mode: Wireless Radar.');
      }

      // When served by the ESP32 itself, its API shares this HTTP origin.
      // This avoids the HTTPS-to-HTTP mixed-content block and the manual IP field.
      const isDeviceHost = window.location.protocol === 'http:' &&
        (window.location.hostname.endsWith('.local') || /^\d{1,3}(\.\d{1,3}){3}$/.test(window.location.hostname));
      if (isDeviceHost) {
        espIpInput.value = window.location.origin;
        btnConnect.click();
      }

      // Start radar animation
      animateRadar();
    });
  </script>
</body>
</html>


)SHESAFE_PAGE";


// ============================================================================
// Network Configuration
// Enter your phone's Mobile Hotspot or local Wi-Fi credentials:
// ============================================================================
const char* ssid = "hello";         // Your Mobile Hotspot SSID
const char* password = "12345678";  // Your Mobile Hotspot Password

WebServer server(80);

String wifiJson = "[]";
String bleJson = "[]";

SemaphoreHandle_t jsonMutex;

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", SHE_SAFE_PAGE, sizeof(SHE_SAFE_PAGE) - 1);
}

// ============================================================================
// IR LED Blink Configuration (500ms ON / 500ms OFF = 1.0 Hz)
// ============================================================================
const uint8_t IR_LED_PIN = 23;     // GPIO23
bool mdnsStarted = false;
bool mdnsAttempted = false;

void irLedBlinkTask(void *parameter) {
  bool ledState = false;
  for (;;) {
    ledState = !ledState;
    digitalWrite(IR_LED_PIN, ledState ? HIGH : LOW);
    Serial.printf("IR LED GPIO%d -> %s\n", (int)IR_LED_PIN, ledState ? "HIGH" : "LOW");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void startMdnsWhenConnected() {
  if (mdnsStarted || mdnsAttempted || WiFi.status() != WL_CONNECTED) return;
  mdnsAttempted = true;
  if (MDNS.begin("shesafe")) {
    mdnsStarted = true;
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://shesafe.local");
  } else {
    Serial.println("mDNS start failed; use the ESP32 IP address.");
  }
}

// ============================================================================
// Background Wi-Fi & BLE Scanner Task (Runs in background)
// ============================================================================
void scanTask(void* parameter) {
  for (;;) {
    // 1. Wi-Fi scan
    int n = WiFi.scanNetworks(false, true); 
    String tempWifi = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) tempWifi += ",";
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

    // 2. BLE scan using native ESP32 BLE
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    
    // Scan for 2 seconds
    BLEScanResults* results = scan->start(2, false); 
    String tempBle = "[";
    if (results != nullptr) {
      int count = results->getCount();
      for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice dev = results->getDevice(i);
        if (i > 0) tempBle += ",";
        String name = dev.getName();
        if (name.length() == 0) name = dev.getAddress().toString();
        tempBle += "{";
        tempBle += "\"name\":\"" + name + "\",";
        tempBle += "\"rssi\":" + String(dev.getRSSI());
        tempBle += "}";
      }
      scan->clearResults(); 
    }
    tempBle += "]";

    // Update shared JSON strings safely using mutex
    if (xSemaphoreTake(jsonMutex, portMAX_DELAY) == pdTRUE) {
      wifiJson = tempWifi;
      bleJson = tempBle;
      xSemaphoreGive(jsonMutex);
    }

    // Delay before next scan cycle to prevent CPU hogging
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ============================================================================
// HTTP API Handlers
// ============================================================================
void handleWifi() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
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
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String response;
  if (xSemaphoreTake(jsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    response = bleJson;
    xSemaphoreGive(jsonMutex);
  } else {
    response = "[]";
  }
  server.send(200, "application/json", response);
}

// Handle preflight CORS requests
void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  
  // Setup IR LED pin
  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(IR_LED_PIN, LOW);

  Serial.printf("XIAO pin map: D1=%d D2=%d D3=%d D4=%d D5=%d\n", D1, D2, D3, D4, D5);
  Serial.printf("IR LED output: GPIO%d\n", (int)IR_LED_PIN);

  jsonMutex = xSemaphoreCreateMutex();
  
  // Try Wi-Fi for up to 15 seconds.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  
  Serial.println();
  Serial.print("Connecting to Mobile Hotspot: ");
  Serial.println(ssid);
  
  const unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 15000) {
    delay(10);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to Hotspot!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi timeout after 15 seconds; continuing offline. LED and BLE scan remain active.");
  }

  // Initialize mDNS (http://shesafe.local)
  startMdnsWhenConnected();

  // Initialize native ESP32 Bluetooth
  BLEDevice::init("");

  // Setup Web Server Endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/ble", HTTP_GET, handleBLE);
  
  // Respond to preflight CORS requests globally
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.println("Web Server running on port 80.");

  // Blink independently so web requests and scanning cannot stall the LED.
  xTaskCreate(irLedBlinkTask, "IrLedBlink", 2048, NULL, 1, NULL);

  // Start the background Wi-Fi/BLE scanning task (universal xTaskCreate)
  xTaskCreate(
    scanTask,      // Task function
    "ScanTask",    // Task name
    8192,          // Stack size (bytes)
    NULL,          // Parameter
    1,             // Priority
    NULL           // Task handle
  );
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  // Keep the HTTP API server responsive
  server.handleClient();
  startMdnsWhenConnected();
}
