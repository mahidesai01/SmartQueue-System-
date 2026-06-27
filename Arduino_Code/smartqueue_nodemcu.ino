// ═══════════════════════════════════════════════
//  SmartQueue — NodeMCU ESP8266 V3
//  IR1=D2, IR2=D1, Green=D4, Red=D0
//  Firebase Anonymous Auth
// ═══════════════════════════════════════════════

#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ── FILL THESE ──────────────────────────────────
#define WIFI_SSID  "mahi"
#define WIFI_PASS  "987654321"
#define API_KEY    "AIzaSyBnxgIYK7eLGl5PvDurj6LuOZj3i9meqdE"
#define DB_URL     "https://smartqueue-d1306-default-rtdb.firebaseio.com"

// ── PINS (NodeMCU D-pins) ───────────────────────
#define IR1  D2   // GPIO4  - Entry sensor
#define IR2  D1   // GPIO5  - Exit sensor
#define GLED D4   // GPIO2  - Green LED (space ok)
#define RLED D0   // GPIO16 - Red LED (full)
#define MAX  15   // Max capacity

// ── FIREBASE ────────────────────────────────────
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ── STATE ───────────────────────────────────────
int  count = 0;
bool ir1t  = false;
bool ir2t  = false;
unsigned long t1 = 0, t2 = 0;
bool fbReady = false;

// ── Send data to Firebase ────────────────────────
void sendFirebase(int c) {
  if (!Firebase.ready()) {
    Serial.println("[FB] Not ready!");
    return;
  }
  int wait  = c * 5;
  int spots = MAX - c;
  bool full = (c >= MAX);

  Firebase.RTDB.setInt( &fbdo, "/queue/count",    c);
  Firebase.RTDB.setInt( &fbdo, "/queue/waitMins", wait);
  Firebase.RTDB.setInt( &fbdo, "/queue/spots",    spots);
  Firebase.RTDB.setInt( &fbdo, "/queue/capacity", MAX);
  Firebase.RTDB.setBool(&fbdo, "/queue/isFull",   full);

  Serial.printf("[FB OK] count=%d wait=%dmin spots=%d\n", c, wait, spots);
}

// ── Update LEDs ──────────────────────────────────
void updateLED(int c) {
  if (c >= MAX) {
    digitalWrite(GLED, HIGH); // D4 active LOW on NodeMCU
    digitalWrite(RLED, LOW);
  } else {
    digitalWrite(GLED, LOW);  // Green ON
    digitalWrite(RLED, HIGH); // Red OFF
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SmartQueue NodeMCU V3 ===");

  // Pins
  pinMode(IR1,  INPUT);
  pinMode(IR2,  INPUT);
  pinMode(GLED, OUTPUT);
  pinMode(RLED, OUTPUT);

  // LED test
  Serial.println("LED test...");
  digitalWrite(GLED, LOW);  delay(400); // Green ON
  digitalWrite(GLED, HIGH); delay(200);
  digitalWrite(RLED, LOW);  delay(400); // Red ON
  digitalWrite(RLED, HIGH); delay(200);
  Serial.println("LED test done!");

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.println("IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAILED! Check name/password");
    return;
  }

  // Firebase setup
  config.api_key      = API_KEY;
  config.database_url = DB_URL;
  config.token_status_callback = tokenStatusCallback;

  // Anonymous signin — no email/password needed
  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase connecting...");

  // Wait up to 10 sec for Firebase
  unsigned long startWait = millis();
  while (!Firebase.ready() && millis() - startWait < 10000) {
    delay(300);
    Serial.print(".");
  }

  if (Firebase.ready()) {
    fbReady = true;
    Serial.println("\nFirebase READY! ✓");
    sendFirebase(0); // push initial zero
  } else {
    Serial.println("\nFirebase timeout — will retry");
  }

  Serial.println("=== System Live! ===");
  Serial.println("D2(IR1)→D1(IR2) = ENTRY");
  Serial.println("D1(IR2)→D2(IR1) = EXIT");

  updateLED(count);
}

void loop() {
  // Read sensors
  bool ir1 = digitalRead(IR1);
  bool ir2 = digitalRead(IR2);

  // ── IR1 edge detect ──
  if (!ir1t && ir1 == LOW) {
    ir1t = true;
    t1 = millis();
  }
  if (ir1 == HIGH) ir1t = false;

  // ── IR2 edge detect ──
  if (!ir2t && ir2 == LOW) {
    ir2t = true;
    t2 = millis();
  }
  if (ir2 == HIGH) ir2t = false;

  // ── ENTRY: IR1 first then IR2 ──
  if (ir1t && ir2 == LOW && (millis() - t1) < 2000) {
    if (count < MAX) {
      count++;
      Serial.printf(">>> ENTRY! Count = %d\n", count);
      updateLED(count);
      sendFirebase(count);
    } else {
      Serial.println(">>> BLOCKED — Full!");
    }
    ir1t = ir2t = false;
    delay(700);
  }

  // ── EXIT: IR2 first then IR1 ──
  if (ir2t && ir1 == LOW && (millis() - t2) < 2000) {
    if (count > 0) {
      count--;
      Serial.printf("<<< EXIT! Count = %d\n", count);
      updateLED(count);
      sendFirebase(count);
    } else {
      Serial.println("<<< Already 0!");
    }
    ir1t = ir2t = false;
    delay(700);
  }

  // ── Timeout reset ──
  if (ir1t && (millis() - t1) > 2000) ir1t = false;
  if (ir2t && (millis() - t2) > 2000) ir2t = false;

  // ── Firebase retry if not ready ──
  if (!Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    Serial.println("[FB] Reconnecting...");
    delay(1000);
  }

  delay(20);
}
