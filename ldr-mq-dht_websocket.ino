#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ======================= KONFIGURASI WIFI & CLOUD =======================

// WiFi
const char* ssid     = "yourssid";
const char* password = "password.";

// Google Apps Script Web App URL (doPost)
const char* googleScriptUrl = "url appscript "; // GANTI dengan URL /exec kamu

// Telegram (bisa dikosongkan kalau belum dipakai)
const char* telegramToken = "telegrambot-token";
const char* telegramChatId = "chat-id";

// ======================= KONFIGURASI OLED =======================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ======================= PIN SENSOR & LED =======================

#define DHTPIN     4       // DHT11 DATA di D4 (GPIO 4)
#define DHTTYPE    DHT11

const int ldrPin      = 34;   // LDR ke pin analog GPIO 34 (ADC1)
const int mq5Pin      = 35;   // MQ-5 analog out ke GPIO 35 (ADC1)

// LED traffic light untuk gas (aktif HIGH)
const int greenLedPin  = 25;  // LED HIJAU  (AMAN)
const int yellowLedPin = 26;  // LED KUNING (HATI-HATI)
const int redLedPin    = 27;  // LED MERAH  (BAHAYA)

// LED untuk indikator GELAP (pakai LED built-in)
const int darkLedPin   = 2;   // LED built-in ESP32

DHT dht(DHTPIN, DHTTYPE);

// ======================= VARIABEL SENSOR =======================

float temperature = 0.0;
float humidity    = 0.0;

int   ldrValue    = 0;
bool  isDark      = false;
int   darkThreshold = 2000;   // jika ldrValue > 2000 -> dianggap GELAP

int   mq5Value    = 0;
String gasStatus  = "AMAN";   // "AMAN", "HATI-HATI", "BAHAYA"

// Threshold MQ-5 (VERSI BARU)
// AMAN:      mq < 1500
// HATI-HATI: 1500 <= mq < 1800
// BAHAYA:    mq >= 1800
int mq5GreenMax   = 1500;
int mq5YellowMax  = 1800;

// ======================= BLINK "BAHAYA" =======================

bool warningVisible = true;
unsigned long lastBlinkToggle = 0;
const unsigned long blinkInterval = 500; // 0.5 detik

// ======================= PAGE SYSTEM OLED =======================

int currentPage = 0; // 0 = data umum, 1 = grafik gas, 2 = grafik suhu & kelembaban
unsigned long lastPageSwitch = 0;
const unsigned long pageInterval = 4000; // ganti halaman tiap 4 detik

// ======================= RIWAYAT UNTUK GRAFIK =======================

const int HISTORY_SIZE = SCREEN_WIDTH; // 128 sampel
int   gasHistory[HISTORY_SIZE];
float tempHistory[HISTORY_SIZE];
float humHistory[HISTORY_SIZE];
int   historyIndex = 0;

// ======================= WEBSERVER & WEBSOCKET =======================

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Interval push realtime & log sheet
const unsigned long REALTIME_PUSH_INTERVAL = 200;   // ms
const unsigned long SHEET_LOG_INTERVAL     = 10000; // ms

unsigned long lastRealtimePush = 0;
unsigned long lastSheetLog     = 0;

bool lastGasDanger = false; // untuk trigger Telegram sekali saja

// ======================= HTML DASHBOARD =======================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>Room Monitoring</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    :root {
      --bg: #0f172a;
      --card-bg: #111827;
      --text: #e5e7eb;
      --subtext: #9ca3af;
      --primary: #1d4ed8;
      --primary-text: #ffffff;
      --secondary: #374151;
      --border: #1f2937;
      --shadow: rgba(0, 0, 0, 0.4);
    }
    body.light {
      --bg: #f3f4f6;
      --card-bg: #ffffff;
      --text: #111827;
      --subtext: #6b7280;
      --primary: #2563eb;
      --primary-text: #ffffff;
      --secondary: #e5e7eb;
      --border: #d1d5db;
      --shadow: rgba(15, 23, 42, 0.12);
    }
    body {
      margin: 0;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    .container { max-width: 1200px; margin: 0 auto; padding: 16px; }
    .topbar { display: flex; justify-content: space-between; align-items: flex-start; gap: 8px; margin-bottom: 8px; }
    h1 { font-size: 1.6rem; margin: 0 0 4px 0; }
    .subtitle { font-size: 0.8rem; color: var(--subtext); margin-bottom: 0; }
    .top-buttons { display: flex; gap: 6px; }
    .grid { display: grid; gap: 12px; }
    .grid-4 { grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); }
    .card {
      background: var(--card-bg);
      border-radius: 12px;
      padding: 14px 16px;
      box-shadow: 0 4px 10px var(--shadow);
      border: 1px solid var(--border);
    }
    .label { font-size: 0.8rem; color: var(--subtext); text-transform: uppercase; letter-spacing: 0.07em; }
    .value { font-size: 1.4rem; margin-top: 4px; }
    .badge { display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 0.7rem; margin-left: 6px; }
    .badge.safe { background: #064e3b; color: #6ee7b7; }
    .badge.warn { background: #78350f; color: #facc15; }
    .badge.danger { background: #7f1d1d; color: #fecaca; }
    canvas { width: 100%; height: 200px; }
    .controls { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    button {
      border: none;
      border-radius: 999px;
      padding: 8px 14px;
      background: var(--primary);
      color: var(--primary-text);
      cursor: pointer;
      font-size: 0.9rem;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 4px;
    }
    button.secondary { background: var(--secondary); color: var(--text); }
    .icon-btn { width: 34px; height: 34px; padding: 0; font-size: 1.1rem; border-radius: 50%; }
    button:hover { filter: brightness(1.05); }
    #lastUpdate, #wsStatus { font-size: 0.75rem; color: var(--subtext); }
    .modal-backdrop {
      position: fixed; inset: 0;
      background: rgba(0, 0, 0, 0.7);
      display: none; align-items: center; justify-content: center;
      z-index: 50;
    }
    .modal {
      background: var(--card-bg);
      padding: 20px;
      border-radius: 12px;
      max-width: 320px;
      text-align: center;
      border: 1px solid var(--border);
      box-shadow: 0 10px 30px var(--shadow);
    }
    .modal h2 { margin-top: 0; margin-bottom: 8px; }
    .modal-backdrop.show { display: flex; }
    @media (max-width: 640px) {
      h1 { font-size: 1.3rem; }
      .topbar { flex-direction: column; align-items: flex-start; }
      .top-buttons { align-self: flex-end; }
    }
  </style>
</head>
<body class="dark">
  <div class="container">
    <div class="topbar">
      <div>
        <h1 id="titleText">ROOM MONITORING</h1>
        <div class="subtitle" id="subtitleText">Realtime environment & gas monitoring dashboard</div>
      </div>
      <div class="top-buttons">
        <button id="downloadCsvBtn" class="icon-btn secondary" type="button" title="Download CSV">⬇</button>
        <button id="resetChartsBtn"   class="icon-btn secondary" type="button" title="Reset Data">♻</button>
        <button id="themeToggleBtn"   class="icon-btn secondary" type="button" title="Theme">🌙</button>
        <button id="langToggleBtn"    class="icon-btn secondary" type="button" title="Language">🌐</button>
      </div>
    </div>

    <!-- Kartu realtime -->
    <div class="grid grid-4">
      <div class="card">
        <div class="label" id="labelGas">Gas (MQ-5)</div>
        <div class="value">
          <span id="gasValue">-</span>
          <span id="gasStatusBadge" class="badge safe">-</span>
        </div>
      </div>
      <div class="card">
        <div class="label" id="labelTemp">Temperature</div>
        <div class="value"><span id="tempValue">-</span> °C</div>
      </div>
      <div class="card">
        <div class="label" id="labelHum">Humidity</div>
        <div class="value"><span id="humValue">-</span> %</div>
      </div>
      <div class="card">
        <div class="label" id="labelLight">Light</div>
        <div class="value"><span id="lightStatus">-</span></div>
      </div>
    </div>

    <!-- Grafik custom -->
    <div class="grid" style="margin-top:12px;">
      <div class="card">
        <div class="label" id="labelGasRealtime">Gas Realtime</div>
        <canvas id="gasChart"></canvas>
      </div>
      <div class="card">
        <div class="label" id="labelTHRealtime">Temp & Hum Realtime</div>
        <canvas id="thChart"></canvas>
      </div>
    </div>

    <div class="card" style="margin-top:12px;">
      <div class="controls">
        <span id="lastUpdate">Last update: -</span>
        <span id="wsStatus" style="margin-left:auto;">WS: -</span>
      </div>
    </div>
  </div>

  <!-- Popup Alarm -->
  <div class="modal-backdrop" id="alarmModal">
    <div class="modal">
      <h2 id="alarmTitle">⚠ GAS WARNING</h2>
      <p id="alarmMessage">Gas level high!</p>
      <button id="alarmOkBtn" type="button">OK</button>
    </div>
  </div>

<script>
  const translations = {
    id: {
      title: 'ROOM MONITORING',
      subtitle: 'Dashboard pemantauan lingkungan & gas realtime',
      labels: {
        gas: 'Gas (MQ-5)',
        temp: 'Suhu',
        hum: 'Kelembaban',
        light: 'Cahaya',
        gasRealtime: 'Grafik Gas Realtime',
        thRealtime: 'Grafik Suhu & Kelembaban'
      },
      tooltips: {
        download: 'Unduh CSV',
        reset: 'Reset Data & Grafik',
        theme: 'Ganti Tema',
        lang: 'Ganti Bahasa'
      },
      lastUpdate: 'Pembaruan terakhir',
      wsConnected: 'WS: Terhubung',
      wsDisconnected: 'WS: Terputus',
      alarmTitle: '⚠ PERINGATAN GAS',
      alarmDefaultMsg: 'Kadar gas tinggi!',
      alarmOk: 'OK'
    },
    en: {
      title: 'ROOM MONITORING',
      subtitle: 'Realtime environment & gas monitoring dashboard',
      labels: {
        gas: 'Gas (MQ-5)',
        temp: 'Temperature',
        hum: 'Humidity',
        light: 'Light',
        gasRealtime: 'Gas Realtime',
        thRealtime: 'Temp & Hum Realtime'
      },
      tooltips: {
        download: 'Download CSV',
        reset: 'Reset Data & Charts',
        theme: 'Toggle Theme',
        lang: 'Toggle Language'
      },
      lastUpdate: 'Last update',
      wsConnected: 'WS: Connected',
      wsDisconnected: 'WS: Disconnected',
      alarmTitle: '⚠ GAS WARNING',
      alarmDefaultMsg: 'Gas level high!',
      alarmOk: 'OK'
    }
  };

  let currentLang = 'id';
  let wsConnected = false;

  const themeBtn   = document.getElementById('themeToggleBtn');
  const langBtn    = document.getElementById('langToggleBtn');
  const downloadBtn= document.getElementById('downloadCsvBtn');
  const resetBtn   = document.getElementById('resetChartsBtn');

  function applyTheme(theme) {
    const body = document.body;
    if (theme === 'light') {
      body.classList.remove('dark');
      body.classList.add('light');
      themeBtn.textContent = '☀';
    } else {
      body.classList.remove('light');
      body.classList.add('dark');
      themeBtn.textContent = '🌙';
    }
  }

  function applyLanguage(lang) {
    currentLang = (lang === 'en') ? 'en' : 'id';
    const t = translations[currentLang];

    document.getElementById('titleText').textContent = t.title;
    document.getElementById('subtitleText').textContent = t.subtitle;

    document.getElementById('labelGas').textContent = t.labels.gas;
    document.getElementById('labelTemp').textContent = t.labels.temp;
    document.getElementById('labelHum').textContent = t.labels.hum;
    document.getElementById('labelLight').textContent = t.labels.light;
    document.getElementById('labelGasRealtime').textContent = t.labels.gasRealtime;
    document.getElementById('labelTHRealtime').textContent = t.labels.thRealtime;

    downloadBtn.title = t.tooltips.download;
    resetBtn.title    = t.tooltips.reset;
    themeBtn.title    = t.tooltips.theme;
    langBtn.title     = t.tooltips.lang;

    document.getElementById('alarmTitle').textContent = t.alarmTitle;
    document.getElementById('alarmOkBtn').textContent = t.alarmOk;

    document.getElementById('wsStatus').textContent =
      wsConnected ? t.wsConnected : t.wsDisconnected;
    document.getElementById('lastUpdate').textContent = t.lastUpdate + ': -';
  }

  (function initThemeAndLang() {
    const savedTheme = localStorage.getItem('rm_theme');
    const theme = (savedTheme === 'light') ? 'light' : 'dark';
    applyTheme(theme);

    const savedLang = localStorage.getItem('rm_lang');
    const lang = (savedLang === 'en') ? 'en' : 'id';
    applyLanguage(lang);
  })();

  themeBtn.addEventListener('click', function() {
    var isLight = document.body.classList.contains('light');
    var next = isLight ? 'dark' : 'light';
    localStorage.setItem('rm_theme', next);
    applyTheme(next);
  });

  langBtn.addEventListener('click', function() {
    var next = (currentLang === 'id') ? 'en' : 'id';
    localStorage.setItem('rm_lang', next);
    applyLanguage(next);
  });

  // ===== DATA & GRAFIK MANUAL =====
  var logData = [];
  var gasHistory = [];
  var tempHistory = [];
  var humHistory = [];
  var maxPoints = 200;

  function pushHistory(arr, value) {
    arr.push(value);
    if (arr.length > maxPoints) arr.shift();
  }

  function drawLineChart(canvas, dataSets) {
    if (!canvas || !canvas.getContext) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;

    ctx.clearRect(0, 0, w, h);

    var all = [];
    for (var i = 0; i < dataSets.length; i++) {
      all = all.concat(dataSets[i].data);
    }
    all = all.filter(function(v){ return typeof v === 'number' && !isNaN(v); });
    if (all.length === 0) return;

    var minY = Math.min.apply(null, all);
    var maxY = Math.max.apply(null, all);
    if (maxY === minY) maxY = minY + 1;

    var leftPad = 10;
    var rightPad = 5;
    var topPad = 5;
    var bottomPad = 10;

    ctx.lineWidth = 1;

    for (var s = 0; s < dataSets.length; s++) {
      var ds = dataSets[s];
      var data = ds.data;
      if (data.length < 2) continue;
      ctx.beginPath();
      ctx.strokeStyle = ds.color;
      for (var i = 0; i < data.length; i++) {
        var x = leftPad + ( (w - leftPad - rightPad) * (i / (maxPoints - 1)) );
        var norm = (data[i] - minY) / (maxY - minY);
        var y = h - bottomPad - norm * (h - topPad - bottomPad);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
  }

  function updateCharts() {
    var gasCanvas = document.getElementById('gasChart');
    var thCanvas  = document.getElementById('thChart');

    drawLineChart(gasCanvas, [
      { data: gasHistory, color: '#38bdf8' }
    ]);

    drawLineChart(thCanvas, [
      { data: tempHistory, color: '#f97316' },
      { data: humHistory,  color: '#22c55e' }
    ]);
  }

  // ===== WEBSOCKET =====
  var ws;

  function connectWS() {
    var proto = (location.protocol === 'https:') ? 'wss://' : 'ws://';
    var url = proto + location.host + '/ws';
    ws = new WebSocket(url);

    ws.onopen = function() {
      wsConnected = true;
      document.getElementById('wsStatus').textContent =
        translations[currentLang].wsConnected;
      console.log('WS connected');
    };

    ws.onclose = function() {
      wsConnected = false;
      document.getElementById('wsStatus').textContent =
        translations[currentLang].wsDisconnected;
      console.log('WS disconnected, retrying...');
      setTimeout(connectWS, 2000);
    };

    ws.onmessage = function(event) {
      try {
        var msg = JSON.parse(event.data);
        if (msg.type === 'realtime') {
          handleRealtime(msg);
        } else if (msg.type === 'alarm') {
          handleAlarm(msg);
        }
      } catch(e) {
        console.error('Invalid WS message', e);
      }
    };
  }

  function handleRealtime(d) {
    var tLang = translations[currentLang];

    var gas  = (typeof d.gas  === 'number') ? d.gas  : null;
    var temp = (typeof d.temp === 'number') ? d.temp : null;
    var hum  = (typeof d.hum  === 'number') ? d.hum  : null;

    if (gas !== null) document.getElementById('gasValue').textContent  = gas;
    if (temp !== null) document.getElementById('tempValue').textContent = temp.toFixed(1);
    if (hum !== null)  document.getElementById('humValue').textContent  = hum.toFixed(1);
    if (d.lightStatus) document.getElementById('lightStatus').textContent = d.lightStatus;

    var badge = document.getElementById('gasStatusBadge');
    if (d.gasStatus) {
      badge.textContent = d.gasStatus;
      var cls = 'badge safe';
      if (d.gasStatus === 'BAHAYA') cls = 'badge danger';
      else if (d.gasStatus === 'HATI-HATI') cls = 'badge warn';
      badge.className = cls;
    }

    if (typeof d.ts === 'number') {
      var t = new Date(d.ts * 1000);
      document.getElementById('lastUpdate').textContent =
        tLang.lastUpdate + ': ' + t.toLocaleTimeString();
    }

    if (gas !== null)  pushHistory(gasHistory, gas);
    if (temp !== null) pushHistory(tempHistory, temp);
    if (hum !== null)  pushHistory(humHistory, hum);
    updateCharts();

    logData.push({
      ts: d.ts,
      gas: gas,
      gasStatus: d.gasStatus,
      temp: temp,
      hum: hum,
      ldr: d.ldr,
      lightStatus: d.lightStatus
    });
  }

  function handleAlarm(msg) {
    var tLang = translations[currentLang];
    var modal = document.getElementById('alarmModal');
    var text  = document.getElementById('alarmMessage');
    text.textContent = msg.message || tLang.alarmDefaultMsg;
    modal.classList.add('show');
  }

  document.getElementById('alarmOkBtn').addEventListener('click', function() {
    document.getElementById('alarmModal').classList.remove('show');
  });

  // ===== CSV & RESET =====
  downloadBtn.addEventListener('click', function() {
    if (!logData.length) return;
    var header = ['timestamp','gas','gasStatus','temp','hum','ldr','lightStatus'];
    var rows = logData.map(function(r) {
      var tsIso = r.ts ? new Date(r.ts * 1000).toISOString() : '';
      return [
        tsIso,
        r.gas,
        r.gasStatus || '',
        r.temp,
        r.hum,
        r.ldr,
        r.lightStatus || ''
      ];
    });
    var csv = [header].concat(rows).map(function(a){ return a.join(','); }).join('\n');
    var blob = new Blob([csv], { type: 'text/csv' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = 'room_monitoring_log.csv';
    a.click();
    URL.revokeObjectURL(url);
  });

  resetBtn.addEventListener('click', function() {
    logData.length = 0;
    gasHistory.length = 0;
    tempHistory.length = 0;
    humHistory.length = 0;
    updateCharts();
    console.log('Data & charts reset');
  });

  document.addEventListener('DOMContentLoaded', function() {
    connectWS();
  });
</script>
</body>
</html>
)rawliteral";

// ======================= FUNGSI BANTU CLOUD =======================

String lightStatusFromBool(bool dark) {
  return dark ? "GELAP" : "TERANG";
}

String simpleUrlEncode(const String& text) {
  String out;
  for (uint16_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (isalnum(c) || c == '_' || c == '-') out += c;
    else if (c == ' ') out += "%20";
    else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

void sendTelegramAlert(const String& msg) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (String(telegramToken).length() < 10) return; // kalau token kosong, lewati

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Failed to connect Telegram");
    return;
  }

  String textEnc = simpleUrlEncode(msg);
  String url = "/bot" + String(telegramToken) +
               "/sendMessage?chat_id=" + telegramChatId +
               "&text=" + textEnc;

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected() && client.available()) {
    client.read();
  }
  client.stop();
}

void logToGoogleSheet() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (String(googleScriptUrl).length() < 10) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, googleScriptUrl)) {
    Serial.println("HTTP begin failed (Sheet)");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  String lightStatus = lightStatusFromBool(isDark);

  String payload = "{";
  payload += "\"gas\":" + String(mq5Value) + ",";
  payload += "\"gasStatus\":\"" + gasStatus + "\",";
  payload += "\"temp\":" + String(temperature, 1) + ",";
  payload += "\"hum\":" + String(humidity, 1) + ",";
  payload += "\"ldr\":" + String(ldrValue) + ",";
  payload += "\"lightStatus\":\"" + lightStatus + "\"";
  payload += "}";

  int code = http.POST(payload);
  Serial.print("Sheet log HTTP code: ");
  Serial.println(code);
  http.end();
}

// ======================= WEBSOCKET BROADCAST =======================

void broadcastRealtime() {
  if (ws.count() == 0) return;

  unsigned long ts = millis() / 1000;
  String lightStatus = lightStatusFromBool(isDark);

  String json = "{";
  json += "\"type\":\"realtime\",";
  json += "\"ts\":" + String(ts) + ",";
  json += "\"gas\":" + String(mq5Value) + ",";
  json += "\"gasStatus\":\"" + gasStatus + "\",";
  json += "\"temp\":" + String(temperature,1) + ",";
  json += "\"hum\":" + String(humidity,1) + ",";
  json += "\"ldr\":" + String(ldrValue) + ",";
  json += "\"lightStatus\":\"" + lightStatus + "\"";
  json += "}";

  ws.textAll(json);
}

void sendAlarmToWeb(const String& msg) {
  if (ws.count() == 0) return;
  String json = "{";
  json += "\"type\":\"alarm\",";
  json += "\"source\":\"gas\",";
  json += "\"gas\":" + String(mq5Value) + ",";
  json += "\"gasStatus\":\"" + gasStatus + "\",";
  json += "\"message\":\"" + msg + "\"";
  json += "}";
  ws.textAll(json);
}

void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS Client %u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS Client %u disconnected\n", client->id());
  }
}

// ======================= FUNGSI SENSOR (DARI KODEMU) =======================

void readDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature(); // Celcius

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT11!");
    return;
  }

  humidity = h;
  temperature = t;

  Serial.print("Temp: ");
  Serial.print(temperature);
  Serial.print(" C  Hum: ");
  Serial.print(humidity);
  Serial.println(" %");
}

void readLDR() {
  ldrValue = analogRead(ldrPin); // 0 - 4095 (ADC ESP32)

  // Rangkaianmu: GELAP jika nilai LDR LEBIH BESAR dari threshold
  isDark = (ldrValue > darkThreshold);

  Serial.print("LDR: ");
  Serial.print(ldrValue);
  Serial.print(" -> ");
  Serial.println(isDark ? "GELAP" : "TERANG");
}

void readMQ5() {
  mq5Value = analogRead(mq5Pin); // 0 - 4095

  if (mq5Value < mq5GreenMax) {
    gasStatus = "AMAN";
  } else if (mq5Value < mq5YellowMax) {
    gasStatus = "HATI-HATI";
  } else {
    gasStatus = "BAHAYA";
  }

  Serial.print("MQ-5: ");
  Serial.print(mq5Value);
  Serial.print(" -> ");
  Serial.println(gasStatus);
}

void updateGasTrafficLEDs() {
  // Aktif HIGH
  if (gasStatus == "AMAN") {
    digitalWrite(greenLedPin, HIGH);
    digitalWrite(yellowLedPin, LOW);
    digitalWrite(redLedPin, LOW);
  } else if (gasStatus == "HATI-HATI") {
    digitalWrite(greenLedPin, LOW);
    digitalWrite(yellowLedPin, HIGH);
    digitalWrite(redLedPin, LOW);
  } else { // BAHAYA
    digitalWrite(greenLedPin, LOW);
    digitalWrite(yellowLedPin, LOW);
    digitalWrite(redLedPin, HIGH);
  }
}

void updateWarningBlink() {
  unsigned long now = millis();
  if (gasStatus == "BAHAYA") {
    if (now - lastBlinkToggle >= blinkInterval) {
      lastBlinkToggle = now;
      warningVisible = !warningVisible;
    }
  } else {
    warningVisible = true;
  }
}

void updatePageSwitch() {
  unsigned long now = millis();
  if (now - lastPageSwitch >= pageInterval) {
    lastPageSwitch = now;
    currentPage = (currentPage + 1) % 4; // 0 -> 1 -> 2 -> 3 -> 0
  }
}


// ======================= OLED DISPLAY =======================

void showPage1() {
  display.setTextSize(1);
  display.setCursor(16, 2);
  display.println("ROOM MONITORING");

  int barWidth = map(mq5Value, 0, 4095, 0, 100);
  if (barWidth < 0) barWidth = 0;
  if (barWidth > 100) barWidth = 100;

  int barX = 14;
  int barY = 14;
  int barH = 8;
  int barW = 100;

  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  display.fillRect(barX, barY, barWidth, barH, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(2, 24);
  display.print("Gas:");
  display.print(mq5Value);
  display.print(" (");
  if (gasStatus == "BAHAYA" && !warningVisible) {
    display.print("      ");
  } else {
    display.print(gasStatus);
  }
  display.println(")");

  display.setCursor(2, 34);
  display.print("LDR:");
  display.print(ldrValue);
  display.print(" ");
  display.println(isDark ? "GELAP" : "TERANG");

  int tx = 2;
  int ty = 44;
  display.drawRect(tx, ty, 4, 12, SSD1306_WHITE);
  display.fillCircle(tx + 2, ty + 12, 2, SSD1306_WHITE);

  display.setCursor(10, 44);
  display.print("T:");
  display.print(temperature, 1);
  display.print("C");

  int hx = 70;
  int hy = 44;
  display.drawCircle(hx + 2, hy + 4, 3, SSD1306_WHITE);
  display.drawPixel(hx + 2, hy, SSD1306_WHITE);

  display.setCursor(80, 44);
  display.print("H:");
  display.print(humidity, 1);
  display.print("%");
}

void showPage2() {
  // PAGE 2: MQ-5 GAS LEVEL BAR
  display.setTextSize(1);
  display.setCursor(18, 2);
  display.println("MQ-5 GAS LEVEL");

  // Normalisasi nilai MQ-5
  int mq = mq5Value;
  if (mq < 0)    mq = 0;
  if (mq > 4095) mq = 4095;

  // Parameter bar
  int baseY   = 54;      // posisi bawah bar
  int barHmax = 40;      // tinggi maksimum bar
  int barW    = 20;      // lebar bar
  int xBar    = 20;      // posisi x bar

  // Tinggi bar dari nilai MQ
  int barH = map(mq, 0, 4095, 0, barHmax);

  // Frame bar
  display.drawRect(xBar, baseY - barHmax, barW, barHmax, SSD1306_WHITE);
  
  // Isi bar
  display.fillRect(xBar + 1, baseY - barH, barW - 2, barH, SSD1306_WHITE);

  // Teks nilai & status MQ-5 di samping bar
  int textX = xBar + barW + 6;
  int textY = baseY - barHmax + 4;

  display.setCursor(textX, textY);
  display.print("Gas");

  display.setCursor(textX, textY + 10);
  display.print(mq5Value);

  display.setCursor(textX, textY + 20);
  display.print(gasStatus);
}




void drawOledTempHumGraph() {
  // Slide 3: dua bar + angka & ikon
  display.setTextSize(1);
  display.setCursor(10, 2);
  display.println("TEMP & HUM GRAPH");

  int tempVal = isnan(temperature) ? 0 : (int)temperature;
  int humVal  = isnan(humidity)    ? 0 : (int)humidity;

  if (tempVal < 0)  tempVal = 0;
  if (tempVal > 50) tempVal = 50;
  if (humVal < 0)   humVal = 0;
  if (humVal > 100) humVal = 100;

  int baseY   = 54;
  int barHmax = 40;
  int barW    = 18;

  int tempH = map(tempVal, 0, 50, 0, barHmax);
  int xT = 10;
  display.drawRect(xT, baseY - barHmax, barW, barHmax, SSD1306_WHITE);
  display.fillRect(xT + 1, baseY - tempH, barW - 2, tempH, SSD1306_WHITE);

  int textXT = xT + barW + 4;
  int textYT = baseY - barHmax + 6;
  display.setCursor(textXT, textYT);
  display.print(F("T"));
  display.setCursor(textXT, textYT + 10);
  display.print(tempVal);
  display.print(F("C"));

  int humH = map(humVal, 0, 100, 0, barHmax);
  int xH = 70;
  display.drawRect(xH, baseY - barHmax, barW, barHmax, SSD1306_WHITE);
  display.fillRect(xH + 1, baseY - humH, barW - 2, humH, SSD1306_WHITE);

  int textXH = xH + barW + 4;
  int textYH = baseY - barHmax + 6;
  display.setCursor(textXH, textYH);
  display.print(F("H"));
  display.setCursor(textXH, textYH + 10);
  display.print(humVal);
  display.print(F("%"));
}

void showPage3() {
  drawOledTempHumGraph();
}


void showPage4_LDR() {
  // PAGE 4: STATUS LDR DENGAN ICON MATAHARI / BULAN
  display.setTextSize(1);
  display.setCursor(20, 2);
  display.println("LDR LIGHT STATUS");

  // Nilai & status
  display.setCursor(4, 16);
  display.print("LDR: ");
  display.print(ldrValue);

  display.setCursor(4, 26);
  display.print("Status: ");
  display.print(isDark ? "GELAP" : "TERANG");

  // Icon di tengah layar
  int cx = 90;
  int cy = 38;
  int r  = 10;

  if (!isDark) {
    // === Matahari ===
    // lingkaran tengah
    display.drawCircle(cx, cy, r, SSD1306_WHITE);
    // sinar (8 arah)
    display.drawLine(cx, cy - r - 4, cx, cy - r - 10, SSD1306_WHITE); // atas
    display.drawLine(cx, cy + r + 4, cx, cy + r + 10, SSD1306_WHITE); // bawah
    display.drawLine(cx - r - 4, cy, cx - r - 10, cy, SSD1306_WHITE); // kiri
    display.drawLine(cx + r + 4, cy, cx + r + 10, cy, SSD1306_WHITE); // kanan

    display.drawLine(cx - 7, cy - 7, cx - 11, cy - 11, SSD1306_WHITE);
    display.drawLine(cx + 7, cy - 7, cx + 11, cy - 11, SSD1306_WHITE);
    display.drawLine(cx - 7, cy + 7, cx - 11, cy + 11, SSD1306_WHITE);
    display.drawLine(cx + 7, cy + 7, cx + 11, cy + 11, SSD1306_WHITE);

    display.setCursor(70, 54);
    display.print("TERANG");
  } else {
    // === Bulan ===
    // bulan sabit: dua lingkaran
    display.fillCircle(cx, cy, r, SSD1306_WHITE);
    display.fillCircle(cx + 5, cy - 3, r, SSD1306_BLACK);

    // sedikit bintang kecil
    display.drawPixel(cx - 20, cy - 10, SSD1306_WHITE);
    display.drawPixel(cx - 18, cy - 8,  SSD1306_WHITE);
    display.drawPixel(cx - 15, cy - 12, SSD1306_WHITE);

    display.setCursor(72, 54);
    display.print("GELAP");
  }
}


void showOnOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // FRAME / BORDER
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  if (currentPage == 0) {
    showPage1();        // data umum
  } else if (currentPage == 1) {
    showPage2();        // grafik MQ-5 BAR
  } else if (currentPage == 2) {
    showPage3();        // grafik suhu & kelembaban
  } else if (currentPage == 3) {
    showPage4_LDR();    // icon matahari / bulan
  }

  display.display();
}


// ======================= SETUP & LOOP =======================

void setup() {
  Serial.begin(115200);

  pinMode(greenLedPin, OUTPUT);
  pinMode(yellowLedPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);
  digitalWrite(greenLedPin, LOW);
  digitalWrite(yellowLedPin, LOW);
  digitalWrite(redLedPin, LOW);

  pinMode(darkLedPin, OUTPUT);
  digitalWrite(darkLedPin, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 initialization failed"));
    while (true);
  }

  dht.begin();

  for (int i = 0; i < HISTORY_SIZE; i++) {
    gasHistory[i]  = 0;
    tempHistory[i] = 0;
    humHistory[i]  = 0;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 20);
  display.println("ROOM MONITORING");
  display.display();
  delay(1500);

  // ===== WiFi & Server =====
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("ROOM MONITORING");
  display.setCursor(0, 16);
  display.println("WiFi connecting...");
  display.display();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("ROOM MONITORING");
  display.setCursor(0, 16);
  display.println("WiFi connected");
  display.setCursor(0, 26);
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(1500);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.onNotFound([](AsyncWebServerRequest *req){
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("HTTP & WebSocket server started");
}

void loop() {
  readDHT();
  readLDR();
  readMQ5();

  // riwayat untuk grafik OLED
  gasHistory[historyIndex]  = mq5Value;
  tempHistory[historyIndex] = temperature;
  humHistory[historyIndex]  = humidity;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;

  updateGasTrafficLEDs();

  if (isDark) {
    digitalWrite(darkLedPin, HIGH);
  } else {
    digitalWrite(darkLedPin, LOW);
  }

  updateWarningBlink();
  updatePageSwitch();
  showOnOLED();

  unsigned long now = millis();

  // Telegram & popup alarm
  bool isDanger = (gasStatus == "BAHAYA");
  if (isDanger && !lastGasDanger) {
    String msg = "Gas BAHAYA! Nilai MQ-5: " + String(mq5Value);
    sendTelegramAlert(msg);
    sendAlarmToWeb(msg);
    lastGasDanger = true;
  } else if (!isDanger && lastGasDanger) {
    lastGasDanger = false;
  }

  // WebSocket realtime dashboard
  if (now - lastRealtimePush >= REALTIME_PUSH_INTERVAL) {
    lastRealtimePush = now;
    broadcastRealtime();
  }

  // Logging ke Google Sheet
  if (now - lastSheetLog >= SHEET_LOG_INTERVAL) {
    lastSheetLog = now;
    logToGoogleSheet();
  }

  delay(200); // refresh cukup cepat untuk OLED & blink
}

