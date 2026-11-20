// Jemuran Otomatis Anti Hujan - ESP32 + ThingsBoard (MQTT TLS 8883)
// Library (Library Manager): PubSubClient, ESP32Servo
// Salin secrets.h.example menjadi secrets.h lalu isi. JANGAN commit secrets.h.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include "secrets.h"

// ---------- PIN ----------
const int PIN_RAIN_AO  = 34;  // ADC1, input saja
const int PIN_RAIN_PWR = 25;  // memberi daya modul hujan hanya saat membaca
const int PIN_LDR      = 35;  // ADC1, input saja
const int PIN_SERVO    = 18;
const int PIN_BUZZER   = 23;  // buzzer aktif

// ---------- KALIBRASI (WAJIB DICEK SAAT HARDWARE JADI) ----------
const int  RAIN_WET_BELOW     = 2000;   // nilai ADC di bawah ini = basah
const bool LDR_DARK_IS_LOW    = true;   // ubah ke false jika nilai LDR naik saat gelap
const int  LDR_DARK_THRESHOLD = 1000;
const bool CLOSE_ON_DARK      = true;   // gelap/mendung ikut menutup atap
const int  ANGLE_OPEN         = 0;
const int  ANGLE_CLOSED       = 90;
const unsigned long REOPEN_DELAY_MS   = 30000;  // aman selama ini baru atap dibuka
const unsigned long SEND_INTERVAL_MS  = 30000;  // kirim berkala (mode edge)
const bool BASELINE_MODE      = false;  // true = kirim data mentah tiap 2 detik (untuk tabel pembanding)

// ---------- STATE ----------
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);
Servo roof;

int  rainRaw = 4095, ldrRaw = 4095;
bool isRaining = false, isDark = false;
bool roofClosed = false;
bool manualMode = false;
unsigned long lastHazardMs = 0, lastSenseMs = 0, lastSendMs = 0, lastNetTry = 0;
unsigned long msgCount = 0;
bool timeSynced = false;

int readAvg(int pin) {
  long sum = 0;
  for (int i = 0; i < 8; i++) { sum += analogRead(pin); delay(2); }
  return sum / 8;
}

void beep(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(PIN_BUZZER, HIGH); delay(120);
    digitalWrite(PIN_BUZZER, LOW);  delay(120);
  }
}

void publishTelemetry() {
  if (!mqtt.connected()) return;
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"rain\":%d,\"ldr\":%d,\"raining\":%s,\"dark\":%s,\"roofClosed\":%s,\"mode\":\"%s\",\"msgCount\":%lu}",
    rainRaw, ldrRaw,
    isRaining ? "true" : "false", isDark ? "true" : "false",
    roofClosed ? "true" : "false", manualMode ? "manual" : "auto", msgCount + 1);
  if (mqtt.publish("v1/devices/me/telemetry", buf)) {
    msgCount++;
    lastSendMs = millis();
  }
}

void setRoof(bool close) {
  roof.write(close ? ANGLE_CLOSED : ANGLE_OPEN);
  roofClosed = close;
  if (close) beep(3);
  publishTelemetry();  // kirim segera saat status berubah
}

void readSensors() {
  digitalWrite(PIN_RAIN_PWR, HIGH); delay(10);
  rainRaw = readAvg(PIN_RAIN_AO);
  digitalWrite(PIN_RAIN_PWR, LOW);
  ldrRaw = readAvg(PIN_LDR);
  isRaining = rainRaw < RAIN_WET_BELOW;
  isDark = LDR_DARK_IS_LOW ? (ldrRaw < LDR_DARK_THRESHOLD) : (ldrRaw > LDR_DARK_THRESHOLD);
}

// Keputusan edge: jalan tanpa internet
void decide() {
  unsigned long now = millis();
  bool hazard = isRaining || (CLOSE_ON_DARK && isDark);
  if (hazard) lastHazardMs = now;

  if (isRaining && !roofClosed) {          // hujan selalu menang, termasuk atas mode manual
    manualMode = false;
    setRoof(true);
  } else if (!manualMode) {
    if (hazard && !roofClosed) setRoof(true);
    else if (!hazard && roofClosed && now - lastHazardMs >= REOPEN_DELAY_MS) setRoof(false);
  }
}

// RPC dari ThingsBoard: setRoof(true=tutup/false=buka), setAuto
void onMessage(char* topic, byte* payload, unsigned int len) {
  String t = topic, body;
  for (unsigned int i = 0; i < len; i++) body += (char)payload[i];
  String id = t.substring(t.lastIndexOf('/') + 1);

  if (body.indexOf("setAuto") >= 0) {
    manualMode = false;
  } else if (body.indexOf("setRoof") >= 0) {
    bool wantClose = body.indexOf("true") >= 0;
    if (!wantClose && isRaining) {
      // ditolak: tidak boleh membuka atap saat hujan
    } else {
      manualMode = true;
      setRoof(wantClose);
    }
  }
  String resp = String("{\"roofClosed\":") + (roofClosed ? "true" : "false") + "}";
  mqtt.publish(("v1/devices/me/rpc/response/" + id).c_str(), resp.c_str());
}

void networkTask() {
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastNetTry > 10000) { lastNetTry = now; WiFi.begin(WIFI_SSID, WIFI_PASS); }
    return;
  }
  if (!timeSynced) { configTime(7 * 3600, 0, "pool.ntp.org"); timeSynced = true; }
  if (!mqtt.connected()) {
    if (now - lastNetTry > 15000) {
      lastNetTry = now;
      if (mqtt.connect(DEVICE_ID, TB_TOKEN, NULL)) mqtt.subscribe("v1/devices/me/rpc/request/+");
    }
    return;
  }
  mqtt.loop();
  unsigned long interval = BASELINE_MODE ? 2000 : SEND_INTERVAL_MS;
  if (now - lastSendMs >= interval) publishTelemetry();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RAIN_PWR, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_RAIN_PWR, LOW);
  roof.attach(PIN_SERVO, 500, 2400);
  roof.write(ANGLE_OPEN);

  secureClient.setCACert(TB_ROOT_CA);
  mqtt.setServer(TB_HOST, 8883);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);
  mqtt.setSocketTimeout(3);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
  if (millis() - lastSenseMs >= 500) {
    lastSenseMs = millis();
    readSensors();
    decide();
    Serial.printf("rain=%d ldr=%d raining=%d dark=%d closed=%d wifi=%d mqtt=%d\n",
      rainRaw, ldrRaw, isRaining, isDark, roofClosed,
      WiFi.status() == WL_CONNECTED, mqtt.connected());
  }
  networkTask();
}