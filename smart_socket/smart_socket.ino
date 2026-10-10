/*
 * ESP32 Power Monitor v4
 * MQTT + Firebase + Schedule + Timer + NTP + Button + WiFi AP Config
 *
 * PZEM : Serial1  RX=16  TX=17  (do chung 2 o cam)
 * Relay1: GPIO 25  (LOW=BAT, opto cach ly)
 * Relay2: GPIO 26
 * Btn1  : GPIO 33  (INPUT_PULLUP)
 * Btn2  : GPIO 35  (INPUT_PULLUP)
 * LED1  : GPIO 32
 * LED2  : GPIO 27
 *
 * Thu vien can cai (Library Manager):
 *   - PZEM004Tv30  (by Olexi)
 *   - PubSubClient (by Nick O'Leary)
 *   - ArduinoJson  (by Benoit Blanchon)
 *
 * WiFi AP Config:
 *   - Lan dau hoac chua co WiFi: ESP32 bat AP "SmartSocket-Setup"
 *   - Ket noi dien thoai vao AP, vao http://192.168.4.1 de nhap WiFi
 *   - SSID/Pass luu vao NVS (Preferences), giu sau khi mat dien
 *   - Giu dong thoi Btn1+Btn2 > 3 giay -> xoa WiFi, vao lai AP mode
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <PZEM004Tv30.h>
#include <ArduinoJson.h>
#include <time.h>

// ================================================
//  CAU HINH MQTT / FIREBASE
// ================================================

const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;
const char* MQTT_CID    = "esp32_pm_v4_001";

#include "secrets.h"   // SECRET_FB_HOST, SECRET_FB_SECRET, SECRET_AP_PASSWORD (see secrets.h.example)
const char* FB_HOST   = SECRET_FB_HOST;
const char* FB_SECRET = SECRET_FB_SECRET;

// ================================================
//  PHAN CUNG
// ================================================

#define RELAY1_PIN  25
#define RELAY2_PIN  26
#define RELAY_ON    LOW
#define RELAY_OFF   HIGH

#define BTN1_PIN    33
#define BTN2_PIN    35
#define LED1_PIN    32
#define LED2_PIN    34

#define PZEM_RX     19
#define PZEM_TX     18

// ================================================
//  WIFI AP CONFIG
// ================================================

#define AP_SSID     "SmartSocket-Setup"
#define AP_PASSWORD SECRET_AP_PASSWORD
#define AP_IP_ADDR  IPAddress(192, 168, 4, 1)
#define WIFI_TIMEOUT_MS  15000
#define PREFS_NS    "wifi_cfg"

// HTML trang cau hinh WiFi
static const char CONFIG_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="vi"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Socket - Cau hinh WiFi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#f0f4f8;display:flex;align-items:center;
     justify-content:center;min-height:100vh;padding:16px}
.card{background:#fff;border-radius:16px;padding:32px;max-width:380px;
      width:100%;box-shadow:0 4px 20px rgba(0,0,0,.1)}
h1{font-size:20px;font-weight:600;color:#1a202c;margin-bottom:4px}
p.sub{font-size:13px;color:#718096;margin-bottom:24px}
label{display:block;font-size:13px;font-weight:500;color:#4a5568;margin-bottom:6px}
input{width:100%;padding:10px 12px;border:1.5px solid #e2e8f0;border-radius:8px;
      font-size:14px;outline:none;transition:.2s;margin-bottom:16px}
input:focus{border-color:#667eea}
button{width:100%;padding:12px;background:#667eea;color:#fff;border:none;
       border-radius:8px;font-size:15px;font-weight:600;cursor:pointer}
button:disabled{opacity:.6}
.msg{padding:10px;border-radius:8px;font-size:13px;text-align:center;
     margin-bottom:16px;display:none}
.ok{background:#c6f6d5;color:#276749;display:block}
.err{background:#fed7d7;color:#9b2c2c;display:block}
</style></head><body>
<div class="card">
  <h1>&#9889; Smart Socket</h1>
  <p class="sub">Ket noi thiet bi vao mang WiFi cua ban</p>
  <div id="msg" class="msg"></div>
  <label>Ten WiFi (SSID)</label>
  <input id="ssid" placeholder="Nhap ten mang WiFi" required>
  <label>Mat khau WiFi</label>
  <input id="pass" type="password" placeholder="Mat khau mang WiFi">
  <button id="btn" onclick="save()">Luu &amp; Ket noi</button>
</div>
<script>
function save(){
  var s=document.getElementById('ssid').value.trim();
  var p=document.getElementById('pass').value;
  var m=document.getElementById('msg');
  var b=document.getElementById('btn');
  if(!s){m.className='msg err';m.textContent='Vui long nhap ten WiFi';return;}
  b.disabled=true;b.textContent='Dang luu...';m.className='msg';
  fetch('/save',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)
  }).then(r=>r.text()).then(t=>{
    if(t==='ok'){
      m.className='msg ok';
      m.textContent='Da luu! Thiet bi dang khoi dong lai...';
    } else {
      m.className='msg err';m.textContent='Loi: '+t;
      b.disabled=false;b.textContent='Luu & Ket noi';
    }
  }).catch(()=>{
    m.className='msg err';m.textContent='Khong the ket noi toi thiet bi';
    b.disabled=false;b.textContent='Luu & Ket noi';
  });
}
</script></body></html>
)rawhtml";

// ================================================
//  MQTT TOPICS
// ================================================

const char* TOPIC_DATA      = "pm/data";        // PZEM chung
const char* TOPIC_D1_STATUS = "pm/d1/status";
const char* TOPIC_D2_STATUS = "pm/d2/status";
const char* TOPIC_D1_CMD    = "pm/d1/cmd";
const char* TOPIC_D2_CMD    = "pm/d2/cmd";
const char* TOPIC_D1_TIMER  = "pm/d1/timer";
const char* TOPIC_D2_TIMER  = "pm/d2/timer";
const char* TOPIC_HB        = "pm/esp32/heartbeat";
const char* TOPIC_ALERT     = "pm/alerts";

// ================================================
//  DOI TUONG
// ================================================

HardwareSerial pzemSerial(1);
PZEM004Tv30    pzem(pzemSerial, PZEM_RX, PZEM_TX);

Preferences    prefs;
WebServer      apServer(80);
DNSServer      dnsServer;

WiFiClient     wifiClient;
PubSubClient   mqtt(wifiClient);

// ================================================
//  TRANG THAI
// ================================================

bool apMode = false;   // dang o AP config mode

bool relay1 = false;
bool relay2 = false;

// Chong rung nut bam
bool     btn1Last        = HIGH;
bool     btn2Last        = HIGH;
unsigned long btn1PressT = 0;
unsigned long btn2PressT = 0;
#define DEBOUNCE_MS  50

// Reset WiFi: giu dong thoi 2 nut > 3 giay
unsigned long bothPressT = 0;
bool          bothHeld   = false;
#define RESET_HOLD_MS 3000

struct SensorData {
  float v = 0, i = 0, p = 0, e = 0, f = 0, pf = 0;
  bool  err = true;
};
SensorData sd;  // 1 PZEM duy nhat

struct TimerData {
  bool          active  = false;
  unsigned long endMs   = 0;
  bool          onAfter = false;
};
TimerData timer1, timer2;

unsigned long tMqtt     = 0;
unsigned long tFb       = 0;
unsigned long tHist     = 0;
unsigned long tSch      = 0;
unsigned long tHb       = 0;
unsigned long tRecon    = 0;
unsigned long tTimerPub = 0;

unsigned long INTERVAL_MQTT     = 2000;
unsigned long INTERVAL_FB       = 10000;
unsigned long INTERVAL_HISTORY  = 300000;
unsigned long INTERVAL_SCHEDULE = 30000;
unsigned long INTERVAL_HB       = 60000;

int lastSchH[3] = {-1, -1, -1};
int lastSchM[3] = {-1, -1, -1};

float alertThresh = 2000.0;   // nguong chung cho toan bo he thong
bool  alertFired  = false;

// ================================================
//  WIFI AP CONFIG
// ================================================

void startAPMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP_ADDR, AP_IP_ADDR, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("[AP] SSID: %s  IP: %s\n", AP_SSID, AP_IP_ADDR.toString().c_str());

  // DNS captive portal -> redirect tat ca domain ve AP
  dnsServer.start(53, "*", AP_IP_ADDR);

  apServer.on("/", HTTP_GET, [](){
    apServer.send_P(200, "text/html", CONFIG_HTML);
  });
  apServer.on("/save", HTTP_POST, [](){
    if (!apServer.hasArg("ssid") || apServer.arg("ssid").isEmpty()) {
      apServer.send(400, "text/plain", "SSID trong");
      return;
    }
    String newSSID = apServer.arg("ssid");
    String newPass = apServer.arg("pass");
    prefs.begin(PREFS_NS, false);
    prefs.putString("ssid", newSSID);
    prefs.putString("pass", newPass);
    prefs.end();
    apServer.send(200, "text/plain", "ok");
    Serial.printf("[AP] Da luu SSID: %s -> restart\n", newSSID.c_str());
    delay(1000);
    ESP.restart();
  });
  // Captive portal redirect cho Android / iOS
  apServer.onNotFound([](){
    apServer.sendHeader("Location", "http://192.168.4.1/");
    apServer.send(302);
  });
  apServer.begin();
}

void clearWiFiConfig() {
  prefs.begin(PREFS_NS, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  Serial.println("[WiFi] Da xoa cau hinh -> restart vao AP mode");
  delay(500);
  ESP.restart();
}

bool connectWiFi() {
  prefs.begin(PREFS_NS, true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.isEmpty()) {
    Serial.println("[WiFi] Chua co cau hinh -> AP mode");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("[WiFi] Dang ket noi '%s'", ssid.c_str());
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t > WIFI_TIMEOUT_MS) {
      Serial.println("\n[WiFi] Timeout -> AP mode");
      WiFi.disconnect();
      return false;
    }
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] OK - IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// ================================================
//  NTP
// ================================================

void syncNTP() {
  configTime(7 * 3600, 0, "pool.ntp.org");
  Serial.print("[NTP] Syncing");
  struct tm t;
  int n = 0;
  while (!getLocalTime(&t) && n++ < 20) { delay(500); Serial.print("."); }
  if (n < 20) {
    char buf[32];
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &t);
    Serial.printf("\n[NTP] OK - %s\n", buf);
  } else {
    Serial.println("\n[NTP] Failed");
  }
}

// ================================================
//  RELAY + LED
// ================================================

void setRelay(int dev, bool on) {
  if (dev == 1) {
    relay1 = on;
    digitalWrite(RELAY1_PIN, on ? RELAY_ON : RELAY_OFF);
    digitalWrite(LED1_PIN,   on ? HIGH : LOW);
    mqtt.publish(TOPIC_D1_STATUS, on ? "ON" : "OFF", true);
  } else {
    relay2 = on;
    digitalWrite(RELAY2_PIN, on ? RELAY_ON : RELAY_OFF);
    digitalWrite(LED2_PIN,   on ? HIGH : LOW);
    mqtt.publish(TOPIC_D2_STATUS, on ? "ON" : "OFF", true);
  }
  Serial.printf("[Relay%d] %s\n", dev, on ? "ON" : "OFF");
}

// ================================================
//  NUT BAM - chong rung + dong bo MQTT
// ================================================

void handleButtons() {

  unsigned long now = millis();

  bool b1 = digitalRead(BTN1_PIN); // HIGH khi nhấn
  bool b2 = digitalRead(BTN2_PIN);

  // ===== Giữ 2 nút để reset WiFi =====
  if (b1 == HIGH && b2 == HIGH) {

    if (!bothHeld) {
      bothHeld = true;
      bothPressT = now;
    }
    else if (now - bothPressT >= RESET_HOLD_MS) {

      Serial.println("[Button] Giữ 2 nút -> xoá WiFi");

      for (int i = 0; i < 6; i++) {
        digitalWrite(LED1_PIN, i % 2);
        digitalWrite(LED2_PIN, i % 2);
        delay(200);
      }

      clearWiFiConfig();
    }

    return;
  }
  else {
    bothHeld = false;
  }

  // ===== BUTTON 1 =====
  if (b1 == HIGH && btn1Last == LOW) {
    btn1PressT = now;
  }

  if (b1 == LOW && btn1Last == HIGH) {

    if (now - btn1PressT >= DEBOUNCE_MS) {

      Serial.println("[Button1] Nhấn -> toggle relay1");
      setRelay(1, !relay1);
    }
  }

  btn1Last = b1;

  // ===== BUTTON 2 =====
  if (b2 == HIGH && btn2Last == LOW) {
    btn2PressT = now;
  }

  if (b2 == LOW && btn2Last == HIGH) {

    if (now - btn2PressT >= DEBOUNCE_MS) {

      Serial.println("[Button2] Nhấn -> toggle relay2");
      setRelay(2, !relay2);
    }
  }

  btn2Last = b2;
}

// ================================================
//  PUBLISH TIMER STATUS
// ================================================

void publishTimer(int dev) {
  TimerData& tmr    = (dev == 1) ? timer1 : timer2;
  const char* topic = (dev == 1) ? TOPIC_D1_TIMER : TOPIC_D2_TIMER;
  StaticJsonDocument<96> doc;
  if (tmr.active) {
    long rem = (long)tmr.endMs - (long)millis();
    if (rem < 0) rem = 0;
    doc["active"]    = true;
    doc["remaining"] = rem / 1000;
    doc["action"]    = tmr.onAfter ? "ON" : "OFF";
  } else {
    doc["active"]    = false;
    doc["remaining"] = 0;
  }
  char buf[96];
  serializeJson(doc, buf);
  mqtt.publish(topic, buf);
}

// ================================================
//  MQTT CALLBACK
// ================================================

void onMqttMsg(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int j = 0; j < len; j++) msg += (char)payload[j];
  msg.trim();

  int dev = (strstr(topic, "d1") != NULL) ? 1 : 2;
  TimerData& tmr = (dev == 1) ? timer1 : timer2;

  if (msg.startsWith("{")) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) return;
    String action  = doc["action"] | "OFF";
    long delaySec  = doc["delay"]  | 0L;
    if (delaySec > 0) {
      tmr.active  = true;
      tmr.endMs   = millis() + (unsigned long)delaySec * 1000UL;
      tmr.onAfter = (action == "ON");
      publishTimer(dev);
      Serial.printf("[Timer%d] Set %s after %lds\n", dev, action.c_str(), delaySec);
    } else {
      setRelay(dev, action == "ON");
    }
  } else {
    msg.toUpperCase();
    if      (msg == "ON")     setRelay(dev, true);
    else if (msg == "OFF")    setRelay(dev, false);
    else if (msg == "CANCEL") {
      tmr.active = false;
      publishTimer(dev);
      Serial.printf("[Timer%d] Cancelled\n", dev);
    }
  }
}

// ================================================
//  MQTT CONNECT
// ================================================

void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMqttMsg);
  mqtt.setBufferSize(512);
  if (mqtt.connect(MQTT_CID)) {
    mqtt.subscribe(TOPIC_D1_CMD);
    mqtt.subscribe(TOPIC_D2_CMD);
    mqtt.publish(TOPIC_D1_STATUS, relay1 ? "ON" : "OFF", true);
    mqtt.publish(TOPIC_D2_STATUS, relay2 ? "ON" : "OFF", true);
    Serial.println("[MQTT] Connected");
  } else {
    Serial.printf("[MQTT] Failed rc=%d\n", mqtt.state());
  }
}

// ================================================
//  DOC PZEM (1 cam bien duy nhat - do chung)
// ================================================

void readPZEM() {
  sd.v   = pzem.voltage();
  sd.i   = pzem.current();
  sd.p   = pzem.power();
  sd.e   = pzem.energy();
  sd.f   = pzem.frequency();
  sd.pf  = pzem.pf();
  sd.err = isnan(sd.v);

  if (!sd.err) Serial.printf("[PZEM] V=%.1f I=%.3f P=%.1f E=%.3f\n",
                              sd.v, sd.i, sd.p, sd.e);
  else         Serial.println("[PZEM] No response");
}

// ================================================
//  PUBLISH MQTT DATA
// ================================================

void publishData() {
  StaticJsonDocument<256> doc;
  doc["relay1"] = relay1 ? "ON" : "OFF";
  doc["relay2"] = relay2 ? "ON" : "OFF";
  doc["err"]    = sd.err;
  if (!sd.err) {
    doc["v"]  = round(sd.v  * 10)   / 10.0;
    doc["i"]  = round(sd.i  * 1000) / 1000.0;
    doc["p"]  = round(sd.p  * 10)   / 10.0;
    doc["e"]  = round(sd.e  * 1000) / 1000.0;
    doc["f"]  = round(sd.f  * 10)   / 10.0;
    doc["pf"] = round(sd.pf * 100)  / 100.0;
  }
  char buf[256];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_DATA, buf);
}

// ================================================
//  CANH BAO CONG SUAT TONG
// ================================================

void checkAlerts() {
  if (sd.err || alertThresh <= 0) return;
  if (sd.p > alertThresh && !alertFired) {
    alertFired = true;
    StaticJsonDocument<96> doc;
    doc["power"]     = round(sd.p * 10) / 10.0;
    doc["threshold"] = alertThresh;
    char buf[96];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_ALERT, buf);
    Serial.printf("[ALERT] Tong: %.1fW > %.0fW\n", sd.p, alertThresh);
  } else if (sd.p < alertThresh * 0.85) {
    alertFired = false;
  }
}

// ================================================
//  FIREBASE REST
// ================================================

bool fbGET(const String& path, String& resp) {
  WiFiClientSecure sc; sc.setInsecure();
  HTTPClient http;
  http.begin(sc, "https://" + String(FB_HOST) + path + ".json?auth=" + String(FB_SECRET));
  http.setTimeout(8000);
  int code = http.GET();
  bool ok = (code == 200);
  if (ok) resp = http.getString();
  else Serial.printf("[FB] GET %d\n", code);
  http.end();
  return ok;
}

bool fbPUT(const String& path, const String& body) {
  WiFiClientSecure sc; sc.setInsecure();
  HTTPClient http;
  http.begin(sc, "https://" + String(FB_HOST) + path + ".json?auth=" + String(FB_SECRET));
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  int code = http.PUT(body);
  bool ok = (code >= 200 && code < 300);
  if (!ok) Serial.printf("[FB] PUT %d\n", code);
  http.end();
  return ok;
}

bool fbPATCH(const String& path, const String& body) {
  WiFiClientSecure sc; sc.setInsecure();
  HTTPClient http;
  http.begin(sc, "https://" + String(FB_HOST) + path + ".json?auth=" + String(FB_SECRET));
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  int code = http.PATCH(body);
  bool ok = (code >= 200 && code < 300);
  if (!ok) Serial.printf("[FB] PATCH %d\n", code);
  http.end();
  return ok;
}

void writeFbLatest() {
  struct tm t;
  if (!getLocalTime(&t)) return;
  char ts[24];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &t);
  if (sd.err) return;

  StaticJsonDocument<256> doc;
  doc["v"]      = round(sd.v  * 10)   / 10.0;
  doc["i"]      = round(sd.i  * 1000) / 1000.0;
  doc["p"]      = round(sd.p  * 10)   / 10.0;
  doc["e"]      = round(sd.e  * 1000) / 1000.0;
  doc["f"]      = round(sd.f  * 10)   / 10.0;
  doc["pf"]     = round(sd.pf * 100)  / 100.0;
  doc["relay1"] = relay1 ? "ON" : "OFF";
  doc["relay2"] = relay2 ? "ON" : "OFF";
  doc["ts"]     = ts;
  char body[256];
  serializeJson(doc, body);
  if (fbPUT("/pm/latest", body))
    Serial.println("[FB] Latest OK");
}

void writeFbHistory() {
  struct tm t;
  if (!getLocalTime(&t)) return;
  if (sd.err) return;
  char date[12];
  strftime(date, sizeof(date), "%Y-%m-%d", &t);
  int roundMin = (t.tm_min / 5) * 5;
  char tkey[8];
  snprintf(tkey, sizeof(tkey), "%02d-%02d", t.tm_hour, roundMin);

  StaticJsonDocument<256> outer;
  StaticJsonDocument<128> inner;
  inner["v"] = round(sd.v  * 10)   / 10.0;
  inner["i"] = round(sd.i  * 1000) / 1000.0;
  inner["p"] = round(sd.p  * 10)   / 10.0;
  inner["e"] = round(sd.e  * 1000) / 1000.0;
  outer[tkey] = inner;
  char body[256];
  serializeJson(outer, body);
  if (fbPATCH("/pm/history/" + String(date), body))
    Serial.println("[FB] History OK");
}

void checkSchedules() {
  struct tm t;
  if (!getLocalTime(&t)) return;
  int H = t.tm_hour, M = t.tm_min, DOW = t.tm_wday;

  for (int dev = 1; dev <= 2; dev++) {
    String resp;
    if (!fbGET("/pm/schedules/" + String(dev), resp)) continue;
    if (resp == "null" || resp.isEmpty()) continue;

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, resp) != DeserializationError::Ok) continue;

    for (JsonPair kv : doc.as<JsonObject>()) {
      JsonObject sch = kv.value().as<JsonObject>();
      if (!(sch["enabled"] | false)) continue;
      int sh = sch["hour"] | -1, sm = sch["minute"] | -1;
      if (sh < 0 || sm < 0) continue;

      bool dayOk = true;
      if (sch.containsKey("days") && sch["days"].is<JsonArray>()) {
        dayOk = false;
        for (JsonVariant dv : sch["days"].as<JsonArray>())
          if (dv.as<int>() == DOW) { dayOk = true; break; }
      }
      if (!dayOk) continue;

      if (sh == H && sm == M && !(lastSchH[dev] == H && lastSchM[dev] == M)) {
        String action = sch["action"] | "OFF";
        setRelay(dev, action == "ON");
        lastSchH[dev] = H;
        lastSchM[dev] = M;
        Serial.printf("[Schedule] Dev%d -> %s at %02d:%02d\n", dev, action.c_str(), H, M);
      }
    }
  }
}

// ================================================
//  TIMER
// ================================================

void handleTimers() {
  unsigned long now = millis();
  TimerData* timers[] = {nullptr, &timer1, &timer2};
  for (int dev = 1; dev <= 2; dev++) {
    TimerData& tmr = *timers[dev];
    if (!tmr.active) continue;
    if (now >= tmr.endMs) {
      setRelay(dev, tmr.onAfter);
      tmr.active = false;
      publishTimer(dev);
      Serial.printf("[Timer%d] Done -> %s\n", dev, tmr.onAfter ? "ON" : "OFF");
    }
  }
  if (now - tTimerPub >= 10000) {
    tTimerPub = now;
    if (timer1.active) publishTimer(1);
    if (timer2.active) publishTimer(2);
  }
}

// ================================================
//  SETUP
// ================================================

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32 Power Monitor v4 ===");

  // Relay + LED init
  pinMode(RELAY1_PIN, OUTPUT); digitalWrite(RELAY1_PIN, RELAY_OFF);
  pinMode(RELAY2_PIN, OUTPUT); digitalWrite(RELAY2_PIN, RELAY_OFF);
  pinMode(LED1_PIN,   OUTPUT); digitalWrite(LED1_PIN, LOW);
  pinMode(LED2_PIN,   OUTPUT); digitalWrite(LED2_PIN, LOW);
  Serial.println("[Relay] Init - All OFF");

  // Nut bam
  pinMode(BTN1_PIN, INPUT_PULLDOWN);
  pinMode(BTN2_PIN, INPUT_PULLDOWN);
  Serial.println("[Button] Init OK");

  // PZEM
  pzemSerial.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);
  Serial.println("[PZEM] Init OK");

  // WiFi: thu ket noi, neu that bai -> AP mode
  if (!connectWiFi()) {
    startAPMode();
    return;   // khong chay cac buoc sau khi o AP mode
  }

  syncNTP();
  connectMQTT();
  Serial.println("[Setup] Ready!\n");
}

// ================================================
//  LOOP
// ================================================

void loop() {
  // AP mode: chi xu ly DNS + WebServer + nut bam
  if (apMode) {
    dnsServer.processNextRequest();
    apServer.handleClient();
    handleButtons();   // van cho phep giu 2 nut de thoat AP mode
    return;
  }

  unsigned long now = millis();

  // Nut bam (moi lan lap)
  handleButtons();

  // Reconnect WiFi
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  // Reconnect MQTT
  if (!mqtt.connected() && now - tRecon >= 5000) {
    tRecon = now;
    connectMQTT();
  }
  mqtt.loop();

  handleTimers();

  // Doc PZEM + publish MQTT moi 2s
  if (now - tMqtt >= INTERVAL_MQTT) {
    tMqtt = now;
    readPZEM();
    if (mqtt.connected()) {
      publishData();
      checkAlerts();
    }
  }

  // Heartbeat moi 60s
  if (now - tHb >= INTERVAL_HB) {
    tHb = now;
    if (mqtt.connected()) {
      struct tm t;
      char ts[10] = "--:--:--";
      if (getLocalTime(&t)) strftime(ts, sizeof(ts), "%H:%M:%S", &t);
      mqtt.publish(TOPIC_HB, ts);
    }
  }

  // Firebase latest moi 10s
  if (now - tFb >= INTERVAL_FB) {
    tFb = now;
    writeFbLatest();
  }

  // Firebase history moi 5 phut
  if (now - tHist >= INTERVAL_HISTORY) {
    tHist = now;
    writeFbHistory();
  }

  // Schedule moi 30s
  if (now - tSch >= INTERVAL_SCHEDULE) {
    tSch = now;
    checkSchedules();
  }
}