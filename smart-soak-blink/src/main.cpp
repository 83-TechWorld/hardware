#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HX711.h>
#include <Preferences.h>

// --- PIN DEFINITIONS ---
#define PIN_SERVO_ALMONDS 13
#define PIN_SERVO_WALNUTS  14
#define PIN_SERVO_CASHEWS  4

#define GATE_CLOSED        0
#define GATE_OPEN         90

#define PIN_HX711_DOUT    19
#define PIN_HX711_PD_SCK  18

#define PIN_VALVE_INLET   25
#define PIN_VALVE_DRAIN   26
#define PIN_UVC           15

#define PIN_BUZZER        17

#define PIN_BTN_UP        32
#define PIN_BTN_DOWN      33
#define PIN_BTN_ENTER     27

// --- DISPLAY DEFINITIONS ---
#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT     64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- GLOBAL MODULES & STATE ---
Servo servoAlmonds;
Servo servoWalnuts;
Servo servoCashews;
HX711 scale;
Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;
bool isSimulation = false;
float currentWeight = 0.0;
unsigned long lastWeightRead = 0;

// User settings structure
struct Settings {
  int targetAlmonds = 20;  // grams
  int targetWalnuts = 20;  // grams
  int targetCashews = 20;  // grams
  int soakTimeHours = 8;   // hours
  bool rinseEnabled = true;
  bool drainAfterSoak = true;
  char wifiSSID[32] = "";
  char wifiPass[64] = "";
} settings;

// State Machine States
enum MachineState {
  STATE_IDLE,
  STATE_MENU,
  STATE_CHECK_CUP,
  STATE_DISPENSE_ALMONDS,
  STATE_DISPENSE_WALNUTS,
  STATE_DISPENSE_CASHEWS,
  STATE_RINSE_FILL,
  STATE_RINSE_WAIT,
  STATE_RINSE_DRAIN,
  STATE_SOAK_FILL,
  STATE_SOAKING,
  STATE_SOAK_DRAIN,
  STATE_READY,
  STATE_ERROR
};
MachineState state = STATE_IDLE;

// State tracking variables
unsigned long stateStartMillis = 0;
unsigned long soakStartMillis = 0;
unsigned long lastJamCheckMillis = 0;
float lastJamWeight = 0.0;

float almondsDispensed = 0.0;
float walnutsDispensed = 0.0;
float cashewsDispensed = 0.0;
float waterDispensed = 0.0;

String errorMsg = "";

// Offline Menu variables
int menuIndex = 0;
bool menuEditing = false;

// Button structure
struct Button {
  int pin;
  bool lastState;
  bool isPressed;
};
Button btnUp = { PIN_BTN_UP, HIGH, false };
Button btnDown = { PIN_BTN_DOWN, HIGH, false };
Button btnEnter = { PIN_BTN_ENTER, HIGH, false };

// --- GORGEOUS GLASSMORPHIC WEB INTERFACE ---
const char HTML_INDEX[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Soak Dashboard</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0f172a;
            --primary: #8b5cf6;
            --primary-glow: rgba(139, 92, 246, 0.4);
            --accent: #06b6d4;
            --success: #10b981;
            --warning: #f59e0b;
            --danger: #ef4444;
            --text: #f8fafc;
            --text-muted: #94a3b8;
            --glass: rgba(15, 23, 42, 0.65);
            --glass-border: rgba(255, 255, 255, 0.08);
        }
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: 'Inter', sans-serif;
            -webkit-tap-highlight-color: transparent;
        }
        body {
            background: linear-gradient(135deg, #090d16 0%, #120e25 50%, #1c0a2a 100%);
            color: var(--text);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 20px;
            overflow-x: hidden;
        }
        .container {
            width: 100%;
            max-width: 500px;
            display: flex;
            flex-direction: column;
            gap: 20px;
            padding-bottom: 40px;
        }
        header {
            text-align: center;
            margin: 10px 0 5px 0;
        }
        h1 {
            font-size: 2.2rem;
            font-weight: 700;
            background: linear-gradient(to right, #a78bfa, #22d3ee);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            margin-bottom: 5px;
            letter-spacing: -0.02em;
        }
        .subtitle {
            font-size: 0.9rem;
            color: var(--text-muted);
        }
        .card {
            background: var(--glass);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid var(--glass-border);
            border-radius: 20px;
            padding: 20px;
            box-shadow: 0 15px 35px rgba(0, 0, 0, 0.4);
            transition: all 0.3s ease;
        }
        .card:hover {
            border-color: rgba(255, 255, 255, 0.15);
            box-shadow: 0 15px 35px rgba(139, 92, 246, 0.12);
        }
        .status-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
        }
        .status-badge {
            background: var(--primary-glow);
            color: #c084fc;
            padding: 6px 14px;
            border-radius: 9999px;
            font-size: 0.8rem;
            font-weight: 600;
            letter-spacing: 0.05em;
            text-transform: uppercase;
            border: 1px solid rgba(167, 139, 250, 0.3);
        }
        .weight-display {
            font-size: 3.2rem;
            font-weight: 700;
            text-align: center;
            margin: 15px 0;
            color: var(--text);
            text-shadow: 0 0 20px rgba(255, 255, 255, 0.05);
        }
        .weight-unit {
            font-size: 1.3rem;
            color: var(--text-muted);
            font-weight: 400;
            margin-left: 2px;
        }
        .progress-container {
            position: relative;
            width: 160px;
            height: 160px;
            margin: 20px auto;
        }
        .progress-svg {
            transform: rotate(-90deg);
            width: 100%;
            height: 100%;
        }
        .progress-bg {
            fill: none;
            stroke: rgba(255, 255, 255, 0.04);
            stroke-width: 8;
        }
        .progress-bar {
            fill: none;
            stroke: url(#progressGradient);
            stroke-width: 8;
            stroke-dasharray: 440;
            stroke-dashoffset: 440;
            stroke-linecap: round;
            transition: stroke-dashoffset 0.5s ease;
        }
        .progress-text {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            text-align: center;
        }
        .progress-time {
            font-size: 1.6rem;
            font-weight: 700;
            color: var(--accent);
        }
        .progress-label {
            font-size: 0.75rem;
            color: var(--text-muted);
            margin-top: 2px;
        }
        .grid-3 {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 12px;
            margin: 20px 0;
        }
        .grid-item {
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 14px;
            padding: 12px 8px;
            text-align: center;
        }
        .grid-item-label {
            font-size: 0.75rem;
            color: var(--text-muted);
            margin-bottom: 6px;
            text-transform: uppercase;
            letter-spacing: 0.02em;
        }
        .grid-item-val {
            font-size: 1.15rem;
            font-weight: 600;
        }
        .form-group {
            margin-bottom: 22px;
        }
        .form-label-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }
        label {
            font-size: 0.9rem;
            font-weight: 600;
            color: var(--text);
        }
        .label-val {
            font-size: 0.95rem;
            color: var(--accent);
            font-weight: 700;
        }
        input[type="range"] {
            -webkit-appearance: none;
            width: 100%;
            height: 6px;
            background: rgba(255, 255, 255, 0.08);
            border-radius: 3px;
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: var(--primary);
            box-shadow: 0 0 12px var(--primary);
            cursor: pointer;
            transition: transform 0.1s;
        }
        input[type="range"]::-webkit-slider-thumb:hover {
            transform: scale(1.25);
        }
        .switch-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 14px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
        }
        .switch-row:last-child {
            border-bottom: none;
        }
        .switch {
            position: relative;
            display: inline-block;
            width: 46px;
            height: 26px;
        }
        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(255, 255, 255, 0.08);
            transition: .3s;
            border-radius: 26px;
        }
        .slider:before {
            position: absolute;
            content: "";
            height: 20px;
            width: 20px;
            left: 3px;
            bottom: 3px;
            background-color: white;
            transition: .3s;
            border-radius: 50%;
        }
        input:checked + .slider {
            background-color: var(--success);
        }
        input:checked + .slider:before {
            transform: translateX(20px);
        }
        .btn {
            display: block;
            width: 100%;
            padding: 15px;
            border: none;
            border-radius: 14px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
            text-align: center;
            margin-top: 12px;
        }
        .btn-primary {
            background: linear-gradient(135deg, #8b5cf6 0%, #6366f1 100%);
            color: white;
            box-shadow: 0 4px 15px rgba(139, 92, 246, 0.35);
        }
        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(139, 92, 246, 0.5);
        }
        .btn-danger {
            background: rgba(239, 68, 68, 0.25);
            color: #fca5a5;
            border: 1px solid rgba(239, 68, 68, 0.45);
        }
        .btn-danger:hover {
            background: rgba(239, 68, 68, 0.35);
        }
        .btn-secondary {
            background: rgba(255, 255, 255, 0.05);
            color: var(--text);
            border: 1px solid var(--glass-border);
            margin-top: 5px;
        }
        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.1);
        }
        .wifi-form {
            display: flex;
            flex-direction: column;
            gap: 12px;
        }
        .wifi-input {
            width: 100%;
            padding: 12px 14px;
            background: rgba(0, 0, 0, 0.25);
            border: 1px solid var(--glass-border);
            border-radius: 10px;
            color: white;
            outline: none;
            font-size: 0.9rem;
        }
        .wifi-input:focus {
            border-color: var(--primary);
        }
        .footer {
            text-align: center;
            font-size: 0.75rem;
            color: var(--text-muted);
            margin-top: 15px;
        }
        .valve-status {
            display: flex;
            justify-content: space-around;
            margin-top: 15px;
            font-size: 0.85rem;
            padding: 10px;
            background: rgba(255, 255, 255, 0.01);
            border-radius: 10px;
            border: 1px solid rgba(255, 255, 255, 0.02);
        }
        .valve-indicator {
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .led-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #475569;
            transition: all 0.3s ease;
        }
        .led-dot.active-blue {
            background: var(--accent);
            box-shadow: 0 0 10px var(--accent), 0 0 20px var(--accent);
        }
        .led-dot.active-orange {
            background: var(--warning);
            box-shadow: 0 0 10px var(--warning), 0 0 20px var(--warning);
        }
        .led-dot.active-purple {
            background: #a855f7;
            box-shadow: 0 0 10px #a855f7, 0 0 20px #a855f7;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Smart Soak</h1>
            <div class="subtitle">Premium Automatic Soaking Dispenser</div>
        </header>

        <!-- Status Card -->
        <div class="card">
            <div class="status-header">
                <span style="font-weight:600; letter-spacing:0.02em;">System Monitor</span>
                <span class="status-badge" id="state-badge">IDLE</span>
            </div>
            
            <div id="weight-view">
                <div class="weight-display"><span id="live-weight">0.0</span><span class="weight-unit">g</span></div>
            </div>
            
            <div id="soak-progress-view" style="display:none;">
                <div class="progress-container">
                    <svg class="progress-svg" viewBox="0 0 160 160">
                        <defs>
                            <linearGradient id="progressGradient" x1="0%" y1="0%" x2="100%" y2="100%">
                                <stop offset="0%" stop-color="#8b5cf6" />
                                <stop offset="100%" stop-color="#06b6d4" />
                            </linearGradient>
                        </defs>
                        <circle class="progress-bg" cx="80" cy="80" r="70" />
                        <circle class="progress-bar" id="progress-circle" cx="80" cy="80" r="70" />
                    </svg>
                    <div class="progress-text">
                        <div class="progress-time" id="remaining-time">00:00</div>
                        <div class="progress-label">Remaining</div>
                    </div>
                </div>
            </div>
            
            <div class="grid-3">
                <div class="grid-item">
                    <div class="grid-item-label">Almonds</div>
                    <div class="grid-item-val" id="disp-almonds">0g</div>
                </div>
                <div class="grid-item">
                    <div class="grid-item-label">Walnuts</div>
                    <div class="grid-item-val" id="disp-walnuts">0g</div>
                </div>
                <div class="grid-item">
                    <div class="grid-item-label">Cashews</div>
                    <div class="grid-item-val" id="disp-cashews">0g</div>
                </div>
            </div>
            
            <div class="valve-status">
                <div class="valve-indicator">
                    <div class="led-dot" id="inlet-dot"></div>
                    <span>Water Inlet</span>
                </div>
                <div class="valve-indicator">
                    <div class="led-dot" id="drain-dot"></div>
                    <span>Drain Outlet</span>
                </div>
                <div class="valve-indicator">
                    <div class="led-dot" id="uvc-dot"></div>
                    <span>UV-C Sanitizer</span>
                </div>
            </div>
            
            <button class="btn btn-primary" id="action-btn" onclick="triggerAction('start')">START SOAK CYCLE</button>
            <button class="btn btn-danger" id="stop-btn" onclick="triggerAction('stop')" style="display:none;">ABORT / RESET</button>
        </div>

        <!-- Recipe/Grams Config Card -->
        <div class="card" id="config-card">
            <h3 style="margin-bottom:18px; font-weight:600; letter-spacing:-0.01em;">Dispenser Recipe</h3>
            
            <div class="form-group">
                <div class="form-label-row">
                    <label for="range-almonds">Almonds Target</label>
                    <span class="label-val"><span id="val-almonds">20</span>g</span>
                </div>
                <input type="range" id="range-almonds" min="0" max="100" step="5" value="20" oninput="updateRangeLabel('almonds')">
            </div>

            <div class="form-group">
                <div class="form-label-row">
                    <label for="range-walnuts">Walnuts Target</label>
                    <span class="label-val"><span id="val-walnuts">20</span>g</span>
                </div>
                <input type="range" id="range-walnuts" min="0" max="100" step="5" value="20" oninput="updateRangeLabel('walnuts')">
            </div>

            <div class="form-group">
                <div class="form-label-row">
                    <label for="range-cashews">Cashews Target</label>
                    <span class="label-val"><span id="val-cashews">20</span>g</span>
                </div>
                <input type="range" id="range-cashews" min="0" max="100" step="5" value="20" oninput="updateRangeLabel('cashews')">
            </div>

            <div class="form-group">
                <div class="form-label-row">
                    <label for="range-soak">Soak Time</label>
                    <span class="label-val"><span id="val-soak">8</span> hrs</span>
                </div>
                <input type="range" id="range-soak" min="1" max="12" step="1" value="8" oninput="updateRangeLabel('soak')">
            </div>
            
            <div class="switch-row">
                <div>
                    <div style="font-weight: 600; font-size: 0.9rem;">Rinse First</div>
                    <div style="font-size:0.75rem; color:var(--text-muted);">Wash dust off nuts before soaking</div>
                </div>
                <label class="switch">
                    <input type="checkbox" id="check-rinse" checked>
                    <span class="slider"></span>
                </label>
            </div>

            <div class="switch-row">
                <div>
                    <div style="font-weight: 600; font-size: 0.9rem;">Drain After Soak</div>
                    <div style="font-size:0.75rem; color:var(--text-muted);">Automatically empty water when done</div>
                </div>
                <label class="switch">
                    <input type="checkbox" id="check-drain" checked>
                    <span class="slider"></span>
                </label>
            </div>
            
            <button class="btn btn-primary" onclick="saveRecipe()">SAVE RECIPE</button>
        </div>

        <!-- System & WiFi Config Card -->
        <div class="card">
            <h3 style="margin-bottom:18px; font-weight:600; letter-spacing:-0.01em;">System Settings</h3>
            <div class="wifi-form">
                <input type="text" class="wifi-input" id="wifi-ssid" placeholder="Home WiFi SSID">
                <input type="password" class="wifi-input" id="wifi-pass" placeholder="Home WiFi Password">
                <button class="btn btn-secondary" onclick="saveWifi()">CONNECT WIFI</button>
            </div>
            <div class="grid-3" style="margin-top: 18px; margin-bottom: 0;">
                <button class="btn btn-secondary" style="margin-top:0;" onclick="triggerAction('clean')">Rinse Test</button>
                <button class="btn btn-secondary" style="margin-top:0;" onclick="triggerAction('drain')">Drain Valve</button>
                <button class="btn btn-secondary" style="margin-top:0;" onclick="triggerAction('fill')">Fill Water</button>
            </div>
        </div>

        <div class="footer">
            Smart Soak Dispenser Project &copy; 2026. Made in India.
        </div>
    </div>

    <script>
        function updateRangeLabel(id) {
            const range = document.getElementById('range-' + id);
            const val = document.getElementById('val-' + id);
            val.innerText = range.value;
        }

        function triggerAction(cmd) {
            fetch(`/action?cmd=${cmd}`)
                .then(res => res.text())
                .then(data => console.log('Action response:', data))
                .catch(err => console.error(err));
        }

        function saveRecipe() {
            const almonds = document.getElementById('range-almonds').value;
            const walnuts = document.getElementById('range-walnuts').value;
            const cashews = document.getElementById('range-cashews').value;
            const soak = document.getElementById('range-soak').value;
            const rinse = document.getElementById('check-rinse').checked ? 1 : 0;
            const drain = document.getElementById('check-drain').checked ? 1 : 0;
            
            fetch(`/config?almonds=${almonds}&walnuts=${walnuts}&cashews=${cashews}&soak=${soak}&rinse=${rinse}&drain=${drain}`)
                .then(res => res.text())
                .then(data => alert('Recipe saved successfully!'))
                .catch(err => console.error(err));
        }

        function saveWifi() {
            const ssid = document.getElementById('wifi-ssid').value;
            const pass = document.getElementById('wifi-pass').value;
            
            if(!ssid) {
                alert('WiFi SSID is required!');
                return;
            }
            
            const formData = new FormData();
            formData.append('ssid', ssid);
            formData.append('pass', pass);
            
            fetch('/wifi', {
                method: 'POST',
                body: formData
            })
            .then(res => res.text())
            .then(data => alert('WiFi settings saved! Device is reconnecting...'))
            .catch(err => console.error(err));
        }

        function updateUI(status) {
            // Update State Badge
            const stateBadge = document.getElementById('state-badge');
            stateBadge.innerText = status.state;
            
            // Adjust styles based on state
            if (status.state === 'IDLE') {
                stateBadge.style.background = 'rgba(139, 92, 246, 0.4)';
                stateBadge.style.color = '#c084fc';
                stateBadge.style.borderColor = 'rgba(167, 139, 250, 0.3)';
                document.getElementById('weight-view').style.display = 'block';
                document.getElementById('soak-progress-view').style.display = 'none';
                document.getElementById('action-btn').style.display = 'block';
                document.getElementById('stop-btn').style.display = 'none';
                document.getElementById('config-card').style.display = 'block';
            } else if (status.state.startsWith('ERROR')) {
                stateBadge.style.background = 'rgba(239, 68, 68, 0.25)';
                stateBadge.style.color = '#fca5a5';
                stateBadge.style.borderColor = 'rgba(239, 68, 68, 0.45)';
                document.getElementById('weight-view').style.display = 'block';
                document.getElementById('soak-progress-view').style.display = 'none';
                document.getElementById('action-btn').style.display = 'none';
                document.getElementById('stop-btn').style.display = 'block';
            } else if (status.state === 'SOAKING') {
                stateBadge.style.background = 'rgba(6, 182, 212, 0.2)';
                stateBadge.style.color = '#67e8f9';
                stateBadge.style.borderColor = 'rgba(6, 182, 212, 0.4)';
                document.getElementById('weight-view').style.display = 'none';
                document.getElementById('soak-progress-view').style.display = 'block';
                document.getElementById('action-btn').style.display = 'none';
                document.getElementById('stop-btn').style.display = 'block';
                document.getElementById('config-card').style.display = 'none';
                
                // Update countdown progress
                const rem = status.soakTimeRemaining;
                const total = status.soakHours * 3600; // in seconds
                
                // Format time string MM:SS or HH:MM:SS
                let timeStr = "";
                if (rem > 3600) {
                    const hrs = Math.floor(rem / 3600);
                    const mins = Math.floor((rem % 3600) / 60);
                    timeStr = `${hrs}h ${mins}m`;
                } else {
                    const mins = Math.floor(rem / 60);
                    const secs = rem % 60;
                    timeStr = `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
                }
                document.getElementById('remaining-time').innerText = timeStr;
                
                // Update circular progress offset (circumference = 440)
                const percent = total > 0 ? (rem / total) : 0;
                const offset = 440 * (1 - percent);
                document.getElementById('progress-circle').style.strokeDashoffset = offset;
            } else {
                stateBadge.style.background = 'rgba(16, 185, 129, 0.25)';
                stateBadge.style.color = '#6ee7b7';
                stateBadge.style.borderColor = 'rgba(16, 185, 129, 0.45)';
                document.getElementById('weight-view').style.display = 'block';
                document.getElementById('soak-progress-view').style.display = 'none';
                document.getElementById('action-btn').style.display = 'none';
                document.getElementById('stop-btn').style.display = 'block';
            }
            
            // Update live weight
            document.getElementById('live-weight').innerText = status.weight.toFixed(1);
            
            // Update individual recipe targets
            document.getElementById('disp-almonds').innerText = status.almondsTarget + 'g';
            document.getElementById('disp-walnuts').innerText = status.walnutsTarget + 'g';
            document.getElementById('disp-cashews').innerText = status.cashewsTarget + 'g';
            
            // Update Valve indicators
            const inletDot = document.getElementById('inlet-dot');
            const drainDot = document.getElementById('drain-dot');
            const uvcDot = document.getElementById('uvc-dot');
            
            if(status.inlet) {
                inletDot.className = 'led-dot active-blue';
            } else {
                inletDot.className = 'led-dot';
            }
            
            if(status.drainValve) {
                drainDot.className = 'led-dot active-orange';
            } else {
                drainDot.className = 'led-dot';
            }
            
            if(status.uvc) {
                uvcDot.className = 'led-dot active-purple';
            } else {
                uvcDot.className = 'led-dot';
            }
        }

        // Initialize sliders based on server values
        let initialized = false;
        function getStatus() {
            fetch('/status')
                .then(res => res.json())
                .then(data => {
                    updateUI(data);
                    if (!initialized) {
                        document.getElementById('range-almonds').value = data.almondsTarget;
                        document.getElementById('range-walnuts').value = data.walnutsTarget;
                        document.getElementById('range-cashews').value = data.cashewsTarget;
                        document.getElementById('range-soak').value = data.soakHours;
                        document.getElementById('check-rinse').checked = data.rinse;
                        document.getElementById('check-drain').checked = data.drain;
                        
                        updateRangeLabel('almonds');
                        updateRangeLabel('walnuts');
                        updateRangeLabel('cashews');
                        updateRangeLabel('soak');
                        initialized = true;
                    }
                })
                .catch(err => console.error('Error polling status:', err));
        }

        // Poll status every 1000ms
        setInterval(getStatus, 1000);
        getStatus();
    </script>
</body>
</html>
)rawhtml";

// --- HELPER FUNCTIONS ---

void saveSettings() {
  preferences.begin("smartsoak", false);
  preferences.putInt("almonds", settings.targetAlmonds);
  preferences.putInt("walnuts", settings.targetWalnuts);
  preferences.putInt("cashews", settings.targetCashews);
  preferences.putInt("soak_hr", settings.soakTimeHours);
  preferences.putBool("rinse", settings.rinseEnabled);
  preferences.putBool("drain", settings.drainAfterSoak);
  preferences.putString("ssid", String(settings.wifiSSID));
  preferences.putString("pass", String(settings.wifiPass));
  preferences.end();
  Serial.println("Settings saved to preferences.");
}

void loadSettings() {
  preferences.begin("smartsoak", false);
  settings.targetAlmonds = preferences.getInt("almonds", 20);
  settings.targetWalnuts = preferences.getInt("walnuts", 20);
  settings.targetCashews = preferences.getInt("cashews", 20);
  settings.soakTimeHours = preferences.getInt("soak_hr", 8);
  settings.rinseEnabled = preferences.getBool("rinse", true);
  settings.drainAfterSoak = preferences.getBool("drain", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  strncpy(settings.wifiSSID, ssid.c_str(), sizeof(settings.wifiSSID));
  strncpy(settings.wifiPass, pass.c_str(), sizeof(settings.wifiPass));
  preferences.end();
  Serial.println("Settings loaded from preferences.");
}

void soundAlarm(int count, int frequency, int durationMs) {
  for (int i = 0; i < count; i++) {
    tone(PIN_BUZZER, frequency);
    delay(durationMs);
    noTone(PIN_BUZZER);
    if (i < count - 1) {
      delay(durationMs);
    }
  }
}

void transitionTo(MachineState newState) {
  Serial.printf("State transition: %d -> %d\n", state, newState);
  state = newState;
  stateStartMillis = millis();
  lastJamCheckMillis = millis();
  
  // Safety override: close all servos & valves by default on transition
  servoAlmonds.write(GATE_CLOSED);
  servoWalnuts.write(GATE_CLOSED);
  servoCashews.write(GATE_CLOSED);
  digitalWrite(PIN_VALVE_INLET, LOW);
  digitalWrite(PIN_VALVE_DRAIN, LOW);
  digitalWrite(PIN_UVC, LOW);
  
  if (state == STATE_ERROR) {
    soundAlarm(3, 800, 200);
  } else if (state == STATE_READY) {
    soundAlarm(3, 1500, 150);
  } else {
    soundAlarm(1, 2000, 80);
  }
}

void updateWeight() {
  if (millis() - lastWeightRead > 200) { // read every 200ms
    if (scale.is_ready()) {
      currentWeight = scale.get_units(1); // will be in grams for both simulation and hardware
    }
    lastWeightRead = millis();
  }
}

void updateButton(Button &btn) {
  bool reading = digitalRead(btn.pin);
  if (reading != btn.lastState) {
    delay(10); // debounce
    reading = digitalRead(btn.pin);
    if (reading == LOW && btn.lastState == HIGH) {
      btn.isPressed = true;
    }
    btn.lastState = reading;
  }
}

void updateButtons() {
  btnUp.isPressed = false;
  btnDown.isPressed = false;
  btnEnter.isPressed = false;
  updateButton(btnUp);
  updateButton(btnDown);
  updateButton(btnEnter);
}

String stateToString(MachineState s) {
  switch (s) {
    case STATE_IDLE:             return "IDLE";
    case STATE_MENU:             return "MENU";
    case STATE_CHECK_CUP:        return "CHECKING CUP";
    case STATE_DISPENSE_ALMONDS: return "DISPENSING ALMONDS";
    case STATE_DISPENSE_WALNUTS:  return "DISPENSING WALNUTS";
    case STATE_DISPENSE_CASHEWS:  return "DISPENSING CASHEWS";
    case STATE_RINSE_FILL:       return "RINSING (FILLING)";
    case STATE_RINSE_WAIT:       return "RINSING (WASHING)";
    case STATE_RINSE_DRAIN:      return "RINSING (DRAINING)";
    case STATE_SOAK_FILL:        return "SOAK FILLING";
    case STATE_SOAKING:          return "SOAKING";
    case STATE_SOAK_DRAIN:       return "DRAINING SOAK";
    case STATE_READY:            return "READY!";
    case STATE_ERROR:            return "ERROR: " + errorMsg;
    default:                     return "UNKNOWN";
  }
}

// --- DISPLAY RENDERERS ---

void showOledText(const char* line1, const char* line2, const char* line3, const char* line4) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println(line2);
  
  display.setCursor(0, 32);
  display.println(line3);
  
  display.setCursor(0, 48);
  display.println(line4);
  
  display.display();
}

void renderDisplay() {
  if (state == STATE_MENU) {
    // Menu is rendered separately
    return;
  }
  
  char l1[25], l2[25], l3[25], l4[25];
  sprintf(l1, "STATE: %s", stateToString(state).c_str());
  
  if (isSimulation) {
    sprintf(l1, "SIM | %s", stateToString(state).c_str());
  }

  // Row 2: Weight Info
  sprintf(l2, "Scale Wt: %.1fg", currentWeight);
  
  // Row 3: Target recipe preview
  sprintf(l3, "A:%dg W:%dg C:%dg", settings.targetAlmonds, settings.targetWalnuts, settings.targetCashews);

  // Row 4: Contextual action prompt
  if (state == STATE_IDLE) {
    sprintf(l4, "Press ENTER to Start");
  } else if (state == STATE_CHECK_CUP) {
    sprintf(l4, "Place Cup (>50g)...");
  } else if (state == STATE_DISPENSE_ALMONDS) {
    sprintf(l4, "Almonds: %.1f/%dg", currentWeight, settings.targetAlmonds);
  } else if (state == STATE_DISPENSE_WALNUTS) {
    sprintf(l4, "Walnuts: %.1f/%dg", currentWeight - almondsDispensed, settings.targetWalnuts);
  } else if (state == STATE_DISPENSE_CASHEWS) {
    sprintf(l4, "Cashews: %.1f/%dg", currentWeight - almondsDispensed - walnutsDispensed, settings.targetCashews);
  } else if (state == STATE_SOAKING) {
    unsigned long elapsed = millis() - soakStartMillis;
    unsigned long totalDuration = settings.soakTimeHours * 3600ULL * 1000ULL;
    if (isSimulation) totalDuration = settings.soakTimeHours * 5000ULL; // 5s per hour in simulation
    
    long remaining = 0;
    if (totalDuration > elapsed) {
      remaining = (totalDuration - elapsed) / 1000;
    }
    
    if (remaining > 3600) {
      sprintf(l4, "Time left: %ldh %ldm", remaining / 3600, (remaining % 3600) / 60);
    } else {
      sprintf(l4, "Time left: %02ld:%02ld", remaining / 60, remaining % 60);
    }
  } else if (state == STATE_READY) {
    sprintf(l4, "Ready! Take cup.");
  } else if (state == STATE_ERROR) {
    sprintf(l4, "Ent to Reset");
  } else {
    sprintf(l4, "Processing...");
  }

  showOledText(l1, l2, l3, l4);
}

void renderMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("--- SETTINGS MENU ---");
  
  char buf[30];
  switch (menuIndex) {
    case 0:
      display.setCursor(0, 20);
      display.println("> [Save & Exit]");
      display.setCursor(0, 38);
      display.println("  Return to monitor");
      break;
    case 1:
      sprintf(buf, "> Almonds: %d g", settings.targetAlmonds);
      display.setCursor(0, 20);
      display.println(buf);
      display.setCursor(0, 38);
      display.println(menuEditing ? "[EDIT MODE - UP/DN]" : "[ENTER to Edit]");
      break;
    case 2:
      sprintf(buf, "> Walnuts: %d g", settings.targetWalnuts);
      display.setCursor(0, 20);
      display.println(buf);
      display.setCursor(0, 38);
      display.println(menuEditing ? "[EDIT MODE - UP/DN]" : "[ENTER to Edit]");
      break;
    case 3:
      sprintf(buf, "> Cashews: %d g", settings.targetCashews);
      display.setCursor(0, 20);
      display.println(buf);
      display.setCursor(0, 38);
      display.println(menuEditing ? "[EDIT MODE - UP/DN]" : "[ENTER to Edit]");
      break;
    case 4:
      sprintf(buf, "> Soak Time: %d hr", settings.soakTimeHours);
      display.setCursor(0, 20);
      display.println(buf);
      display.setCursor(0, 38);
      display.println(menuEditing ? "[EDIT MODE - UP/DN]" : "[ENTER to Edit]");
      break;
    case 5:
      sprintf(buf, "> Rinse: %s", settings.rinseEnabled ? "ON" : "OFF");
      display.setCursor(0, 20);
      display.println(buf);
      display.setCursor(0, 38);
      display.println(menuEditing ? "[EDIT MODE - UP/DN]" : "[ENTER to Edit]");
      break;
    case 6:
      sprintf(buf, "> Auto Drain: %s", settings.drainAfterSoak ? "ON" : "OFF");
      display.setCursor(0, 20);
      display.println(buf);
      display.setCursor(0, 38);
      display.println(menuEditing ? "[EDIT MODE - UP/DN]" : "[ENTER to Edit]");
      break;
  }
  display.display();
}

// --- WEB SERVER ENDPOINTS ---

void handleRoot() {
  server.send(200, "text/html", HTML_INDEX);
}

void handleStatus() {
  // Compute remaining soak time
  long remaining = 0;
  if (state == STATE_SOAKING) {
    unsigned long elapsed = millis() - soakStartMillis;
    unsigned long totalDuration = settings.soakTimeHours * 3600ULL * 1000ULL;
    if (isSimulation) totalDuration = settings.soakTimeHours * 5000ULL;
    
    if (totalDuration > elapsed) {
      remaining = (totalDuration - elapsed) / 1000;
    }
  }

  String json = "{";
  json += "\"state\":\"" + stateToString(state) + "\",";
  json += "\"weight\":" + String(currentWeight, 1) + ",";
  json += "\"almondsTarget\":" + String(settings.targetAlmonds) + ",";
  json += "\"walnutsTarget\":" + String(settings.targetWalnuts) + ",";
  json += "\"cashewsTarget\":" + String(settings.targetCashews) + ",";
  json += "\"soakHours\":" + String(settings.soakTimeHours) + ",";
  json += "\"soakTimeRemaining\":" + String(remaining) + ",";
  json += "\"rinse\":" + String(settings.rinseEnabled ? "true" : "false") + ",";
  json += "\"drain\":" + String(settings.drainAfterSoak ? "true" : "false") + ",";
  json += "\"inlet\":" + String(digitalRead(PIN_VALVE_INLET) == HIGH ? "true" : "false") + ",";
  json += "\"drainValve\":" + String(digitalRead(PIN_VALVE_DRAIN) == HIGH ? "true" : "false") + ",";
  json += "\"uvc\":" + String(digitalRead(PIN_UVC) == HIGH ? "true" : "false") + ",";
  json += "\"isSimulation\":" + String(isSimulation ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleConfig() {
  if (server.hasArg("almonds")) settings.targetAlmonds = server.arg("almonds").toInt();
  if (server.hasArg("walnuts")) settings.targetWalnuts = server.arg("walnuts").toInt();
  if (server.hasArg("cashews")) settings.targetCashews = server.arg("cashews").toInt();
  if (server.hasArg("soak")) settings.soakTimeHours = server.arg("soak").toInt();
  if (server.hasArg("rinse")) settings.rinseEnabled = server.arg("rinse").toInt() == 1;
  if (server.hasArg("drain")) settings.drainAfterSoak = server.arg("drain").toInt() == 1;
  
  saveSettings();
  server.send(200, "text/plain", "OK");
}

void handleAction() {
  String cmd = server.arg("cmd");
  Serial.print("Web Action: ");
  Serial.println(cmd);
  
  if (cmd == "start" && state == STATE_IDLE) {
    scale.tare();
    transitionTo(STATE_CHECK_CUP);
    server.send(200, "text/plain", "Started");
  } 
  else if (cmd == "stop") {
    transitionTo(STATE_IDLE);
    server.send(200, "text/plain", "Stopped");
  } 
  else if (cmd == "clean") {
    // Run short rinse cycle directly
    if (state == STATE_IDLE) {
      transitionTo(STATE_RINSE_FILL);
      server.send(200, "text/plain", "Rinse started");
    } else {
      server.send(400, "text/plain", "Must be IDLE");
    }
  } 
  else if (cmd == "drain") {
    if (state == STATE_IDLE) {
      digitalWrite(PIN_VALVE_DRAIN, !digitalRead(PIN_VALVE_DRAIN));
      server.send(200, "text/plain", "Drain toggled");
    } else {
      server.send(400, "text/plain", "Must be IDLE");
    }
  } 
  else if (cmd == "fill") {
    if (state == STATE_IDLE) {
      digitalWrite(PIN_VALVE_INLET, !digitalRead(PIN_VALVE_INLET));
      server.send(200, "text/plain", "Inlet toggled");
    } else {
      server.send(400, "text/plain", "Must be IDLE");
    }
  }
  else {
    server.send(400, "text/plain", "Invalid command or state");
  }
}

void handleWifiSave() {
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    strncpy(settings.wifiSSID, ssid.c_str(), sizeof(settings.wifiSSID));
    strncpy(settings.wifiPass, pass.c_str(), sizeof(settings.wifiPass));
    saveSettings();
    server.send(200, "text/plain", "WiFi config saved. Reconnecting ESP32...");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "SSID missing");
  }
}

void handleNotFound() {
  // Captive Portal redirect
  String host = server.hostHeader();
  if (WiFi.softAPIP().toString() == server.client().localIP().toString()) {
    // Redirect to AP homepage
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "File Not Found");
  }
}

// --- CORE SETUP & LOOP ---

void setup() {
  Serial.begin(115200);
  Serial.println("Smart Soak Starting...");

  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  showOledText("Smart Soak", "System Booting...", "Readying sensors...", "Made in India");

  // Load preferences
  loadSettings();

  // Setup pin modes
  pinMode(PIN_VALVE_INLET, OUTPUT);
  pinMode(PIN_VALVE_DRAIN, OUTPUT);
  pinMode(PIN_UVC, OUTPUT);
  digitalWrite(PIN_VALVE_INLET, LOW);
  digitalWrite(PIN_VALVE_DRAIN, LOW);
  digitalWrite(PIN_UVC, LOW);

  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_ENTER, INPUT_PULLUP);

  // Attach servos
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoAlmonds.setPeriodHertz(50);
  servoWalnuts.setPeriodHertz(50);
  servoCashews.setPeriodHertz(50);

  servoAlmonds.attach(PIN_SERVO_ALMONDS, 500, 2400);
  servoWalnuts.attach(PIN_SERVO_WALNUTS, 500, 2400);
  servoCashews.attach(PIN_SERVO_CASHEWS, 500, 2400);

  servoAlmonds.write(GATE_CLOSED);
  servoWalnuts.write(GATE_CLOSED);
  servoCashews.write(GATE_CLOSED);

  // Initialize load cell scale
  scale.begin(PIN_HX711_DOUT, PIN_HX711_PD_SCK);

  // Smart WiFi Startup & Simulation Check
  Serial.println("Scanning networks to check for Wokwi simulation...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  isSimulation = false;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == "Wokwi-GUEST") {
      isSimulation = true;
      Serial.println("Wokwi Simulation Environment detected.");
      break;
    }
  }

  // If scan is empty and no saved wifi, we assume Wokwi simulator
  if (n == 0 && strlen(settings.wifiSSID) == 0) {
    isSimulation = true;
    Serial.println("Assuming simulation: scan empty & saved SSID empty.");
  }

  if (isSimulation) {
    Serial.println("Setting simulation scale factor to 0.42 (grams).");
    scale.set_scale(0.42); // Wokwi-specific scale factor (outputs grams directly matching Wokwi slider)
    WiFi.begin("Wokwi-GUEST", "");
  } else {
    Serial.println("Setting physical scale factor to 420.0 (grams).");
    scale.set_scale(420.0); // Real hardware calibration factor
    if (strlen(settings.wifiSSID) > 0) {
      WiFi.begin(settings.wifiSSID, settings.wifiPass);
    }
  }
  scale.tare();

  showOledText("Smart Soak", "Connecting to WiFi...", isSimulation ? "SSID: Wokwi-GUEST" : settings.wifiSSID, "Please wait...");
  
  // Wait up to 8 seconds
  int connectTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && connectTimeout < 16) {
    delay(500);
    connectTimeout++;
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    soundAlarm(2, 2000, 100);
  } else {
    // Start AP Mode
    Serial.println("\nWiFi Connection failed. Starting Access Point.");
    WiFi.mode(WIFI_AP);
    String apName = "SmartSoak_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    WiFi.softAP(apName.c_str());
    
    // Captive Portal DNS Server Setup
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    
    Serial.print("SoftAP Hotspot active: ");
    Serial.println(apName);
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
    soundAlarm(1, 1000, 300);
  }

  // --- Setup WebServer Routes ---
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/config", handleConfig);
  server.on("/action", handleAction);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  
  // Captive Portal routing redirects
  server.on("/generate_204", handleRoot);
  server.on("/fwlink", handleRoot);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("HTTP server started.");
  
  transitionTo(STATE_IDLE);
}

void loop() {
  server.handleClient();
  if (WiFi.status() != WL_CONNECTED) {
    dnsServer.processNextRequest();
  }

  // Read sensor and buttons
  updateWeight();
  updateButtons();

  // Local OLED Screen updates
  if (state == STATE_MENU) {
    renderMenu();
  } else {
    renderDisplay();
  }

  // --- STATE MACHINE ---
  switch (state) {
    
    case STATE_IDLE:
      if (btnEnter.isPressed) {
        scale.tare();
        transitionTo(STATE_CHECK_CUP);
      } 
      else if (btnUp.isPressed || btnDown.isPressed) {
        menuIndex = 0;
        menuEditing = false;
        transitionTo(STATE_MENU);
      }
      break;

    case STATE_MENU:
      if (!menuEditing) {
        if (btnUp.isPressed) {
          menuIndex = (menuIndex - 1 + 7) % 7;
        } 
        else if (btnDown.isPressed) {
          menuIndex = (menuIndex + 1) % 7;
        } 
        else if (btnEnter.isPressed) {
          if (menuIndex == 0) {
            saveSettings();
            transitionTo(STATE_IDLE);
          } else {
            menuEditing = true;
          }
        }
      } 
      else { // Editing setting value
        if (btnUp.isPressed) {
          switch (menuIndex) {
            case 1: settings.targetAlmonds += 5; break;
            case 2: settings.targetWalnuts += 5; break;
            case 3: settings.targetCashews += 5; break;
            case 4: settings.soakTimeHours = min(12, settings.soakTimeHours + 1); break;
            case 5: settings.rinseEnabled = !settings.rinseEnabled; break;
            case 6: settings.drainAfterSoak = !settings.drainAfterSoak; break;
          }
        } 
        else if (btnDown.isPressed) {
          switch (menuIndex) {
            case 1: settings.targetAlmonds = max(0, settings.targetAlmonds - 5); break;
            case 2: settings.targetWalnuts = max(0, settings.targetWalnuts - 5); break;
            case 3: settings.targetCashews = max(0, settings.targetCashews - 5); break;
            case 4: settings.soakTimeHours = max(1, settings.soakTimeHours - 1); break;
            case 5: settings.rinseEnabled = !settings.rinseEnabled; break;
            case 6: settings.drainAfterSoak = !settings.drainAfterSoak; break;
          }
        } 
        else if (btnEnter.isPressed) {
          menuEditing = false; // confirm and exit item edit
        }
      }
      break;

    case STATE_CHECK_CUP:
      // A cup should weigh at least 50g in simulation or physical scale.
      // If cup is not placed, wait.
      if (currentWeight >= 50.0) {
        scale.tare(); // tare the cup weight
        delay(300);
        updateWeight();
        almondsDispensed = 0.0;
        walnutsDispensed = 0.0;
        cashewsDispensed = 0.0;
        
        Serial.println("Cup detected & tared. Moving to Dispensing.");
        transitionTo(STATE_DISPENSE_ALMONDS);
      } 
      else if (btnEnter.isPressed) {
        // Bypass cup detection manually on button press
        scale.tare();
        delay(300);
        updateWeight();
        almondsDispensed = 0.0;
        walnutsDispensed = 0.0;
        cashewsDispensed = 0.0;
        transitionTo(STATE_DISPENSE_ALMONDS);
      }
      else if (millis() - stateStartMillis > 30000) {
        errorMsg = "Cup Timeout";
        transitionTo(STATE_ERROR);
      }
      break;

    case STATE_DISPENSE_ALMONDS:
      if (settings.targetAlmonds == 0) {
        almondsDispensed = 0;
        transitionTo(STATE_DISPENSE_WALNUTS);
      } else {
        servoAlmonds.write(GATE_OPEN);
        
        // Blockage check: every 4 seconds, check if weight increases
        if (millis() - lastJamCheckMillis > 4000) {
          if (currentWeight - lastJamWeight < 0.5) {
            errorMsg = "Almond Jam/Empty";
            transitionTo(STATE_ERROR);
          } else {
            lastJamWeight = currentWeight;
            lastJamCheckMillis = millis();
          }
        }

        if (currentWeight >= settings.targetAlmonds) {
          servoAlmonds.write(GATE_CLOSED);
          almondsDispensed = currentWeight;
          delay(500); // let scale settle
          updateWeight();
          lastJamWeight = currentWeight;
          transitionTo(STATE_DISPENSE_WALNUTS);
        }
      }
      break;

    case STATE_DISPENSE_WALNUTS:
      if (settings.targetWalnuts == 0) {
        walnutsDispensed = 0;
        transitionTo(STATE_DISPENSE_CASHEWS);
      } else {
        servoWalnuts.write(GATE_OPEN);
        
        // Jam check
        if (millis() - lastJamCheckMillis > 4000) {
          if (currentWeight - lastJamWeight < 0.5) {
            errorMsg = "Walnut Jam/Empty";
            transitionTo(STATE_ERROR);
          } else {
            lastJamWeight = currentWeight;
            lastJamCheckMillis = millis();
          }
        }

        float walnutWeight = currentWeight - almondsDispensed;
        if (walnutWeight >= settings.targetWalnuts) {
          servoWalnuts.write(GATE_CLOSED);
          walnutsDispensed = walnutWeight;
          delay(500);
          updateWeight();
          lastJamWeight = currentWeight;
          transitionTo(STATE_DISPENSE_CASHEWS);
        }
      }
      break;

    case STATE_DISPENSE_CASHEWS:
      if (settings.targetCashews == 0) {
        cashewsDispensed = 0;
        transitionTo(STATE_RINSE_FILL);
      } else {
        servoCashews.write(GATE_OPEN);
        
        // Jam check
        if (millis() - lastJamCheckMillis > 4000) {
          if (currentWeight - lastJamWeight < 0.5) {
            errorMsg = "Cashew Jam/Empty";
            transitionTo(STATE_ERROR);
          } else {
            lastJamWeight = currentWeight;
            lastJamCheckMillis = millis();
          }
        }

        float cashewWeight = currentWeight - almondsDispensed - walnutsDispensed;
        if (cashewWeight >= settings.targetCashews) {
          servoCashews.write(GATE_CLOSED);
          cashewsDispensed = cashewWeight;
          delay(500);
          updateWeight();
          transitionTo(STATE_RINSE_FILL);
        }
      }
      break;

    case STATE_RINSE_FILL:
      if (!settings.rinseEnabled) {
        transitionTo(STATE_SOAK_FILL);
      } else {
        digitalWrite(PIN_VALVE_INLET, HIGH);
        
        float dryNutsWeight = almondsDispensed + walnutsDispensed + cashewsDispensed;
        float addedWater = currentWeight - dryNutsWeight;
        
        // Fill water up to 100g (100ml) for rinsing, or timeout after 10s
        if (addedWater >= 100.0 || (millis() - stateStartMillis > 10000)) {
          digitalWrite(PIN_VALVE_INLET, LOW);
          transitionTo(STATE_RINSE_WAIT);
        }
      }
      break;

    case STATE_RINSE_WAIT:
      // Let it sit for 15 seconds
      if (millis() - stateStartMillis > 15000) {
        transitionTo(STATE_RINSE_DRAIN);
      }
      break;

    case STATE_RINSE_DRAIN:
      digitalWrite(PIN_VALVE_DRAIN, HIGH);
      
      {
        float dryNutsWeight = almondsDispensed + walnutsDispensed + cashewsDispensed;
        // Drain until weight returns close to dry nuts weight, or timeout after 12s
        if ((currentWeight <= dryNutsWeight + 10.0) || (millis() - stateStartMillis > 12000)) {
          digitalWrite(PIN_VALVE_DRAIN, LOW);
          delay(1000); // let settling
          transitionTo(STATE_SOAK_FILL);
        }
      }
      break;

    case STATE_SOAK_FILL:
      digitalWrite(PIN_VALVE_INLET, HIGH);
      
      {
        float dryNutsWeight = almondsDispensed + walnutsDispensed + cashewsDispensed;
        float addedWater = currentWeight - dryNutsWeight;
        
        // Fill 200g (200ml) water for soaking, or timeout after 15s
        if (addedWater >= 200.0 || (millis() - stateStartMillis > 15000)) {
          digitalWrite(PIN_VALVE_INLET, LOW);
          soakStartMillis = millis();
          transitionTo(STATE_SOAKING);
        }
      }
      break;

    case STATE_SOAKING:
      {
        unsigned long elapsed = millis() - soakStartMillis;
        unsigned long totalDuration = settings.soakTimeHours * 3600ULL * 1000ULL;
        if (isSimulation) totalDuration = settings.soakTimeHours * 5000ULL; // 5s per hour in simulation
        
        // Active UV-C sanitization during soaking to prevent bacterial growth
        // Runs for 2 minutes at the start of every hour (or 1 second every 5 seconds in simulation)
        unsigned long hourInterval = 3600ULL * 1000ULL;
        unsigned long activeDuration = 120ULL * 1000ULL; // 2 minutes
        if (isSimulation) {
          hourInterval = 5000ULL; // 5 seconds
          activeDuration = 1000ULL; // 1 second
        }
        if (elapsed % hourInterval < activeDuration) {
          digitalWrite(PIN_UVC, HIGH);
        } else {
          digitalWrite(PIN_UVC, LOW);
        }

        if (elapsed >= totalDuration) {
          if (settings.drainAfterSoak) {
            transitionTo(STATE_SOAK_DRAIN);
          } else {
            transitionTo(STATE_READY);
          }
        }
      }
      break;

    case STATE_SOAK_DRAIN:
      digitalWrite(PIN_VALVE_DRAIN, HIGH);
      // Drain for 15 seconds to empty out the water completely
      if (millis() - stateStartMillis > 15000) {
        digitalWrite(PIN_VALVE_DRAIN, LOW);
        transitionTo(STATE_READY);
      }
      break;

    case STATE_READY:
      // Alarm when ready is handled by transitionTo()
      // Wait for user to remove cup (weight drops below 30g, i.e. container lifted off)
      // If cup is removed, automatically return to IDLE!
      if (currentWeight < 30.0) {
        delay(2000); // wait for scale to settle
        updateWeight();
        if (currentWeight < 30.0) {
          transitionTo(STATE_IDLE);
        }
      }
      // Or user can press Enter to return to idle
      if (btnEnter.isPressed) {
        transitionTo(STATE_IDLE);
      }
      break;

    case STATE_ERROR:
      // Blink buzzer every 2 seconds
      if ((millis() - stateStartMillis) % 2000 < 200) {
        tone(PIN_BUZZER, 800, 100);
      }
      
      // Press ENTER button to clear error, tare, and go to IDLE
      if (btnEnter.isPressed) {
        scale.tare();
        transitionTo(STATE_IDLE);
      }
      break;
  }
}