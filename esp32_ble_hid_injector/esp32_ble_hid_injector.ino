/*
 * ESP32-S3 BLE HID (Combo: Mouse + Keyboard)
 * 用 BleCombo 单 BLE 栈，解决 BleMouse + BleKeyboard 冲突崩溃
 */
#include <WiFi.h>
#include <WebServer.h>
#include <BleCombo.h>

const char* WIFI_SSID = "ChinaUnicom-1003";
const char* WIFI_PASS = "WIFI_PASSWORD_HERE";
float PX_PER_UNIT_X = 1.0f;
float PX_PER_UNIT_Y = 1.0f;
const int MAX_STEP = 40;

BleComboKeyboard keyboard("ESP32-Combo", "Espressif", 100);
BleComboMouse    mouse(&keyboard);

WebServer server(80);

void homePointer() {
  for (int i = 0; i < 60; i++) { mouse.move(-MAX_STEP, -MAX_STEP); delay(6); }
  delay(120);
}
void moveBy(int dx, int dy) {
  int sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
  int rx = abs(dx), ry = abs(dy);
  while (rx > 0 || ry > 0) {
    int stepX = min(rx, MAX_STEP) * sx, stepY = min(ry, MAX_STEP) * sy;
    mouse.move(stepX, stepY);
    rx -= abs(stepX); ry -= abs(stepY);
    delay(8);
  }
  delay(60);
}
void moveTo(int x, int y) { homePointer(); moveBy((int)(x / PX_PER_UNIT_X), (int)(y / PX_PER_UNIT_Y)); }

void handleStatus() {
  String s = "{\"ok\":true,\"combo\":" + String(keyboard.isConnected() ? "true" : "false");
  s += ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", s);
}
void handleMove()  { moveBy((int)(server.arg("x").toInt() / PX_PER_UNIT_X), (int)(server.arg("y").toInt() / PX_PER_UNIT_Y)); server.send(200,"application/json","{\"ok\":true}"); }
void handleHome()  { homePointer(); server.send(200,"application/json","{\"ok\":true}"); }
void handleClick() { mouse.click(MOUSE_LEFT); server.send(200,"application/json","{\"ok\":true}"); }
void handleTap()   { moveTo(server.arg("x").toInt(), server.arg("y").toInt()); mouse.click(MOUSE_LEFT); server.send(200,"application/json","{\"ok\":true}"); }
void handleType()  { keyboard.print(server.arg("text")); server.send(200,"application/json","{\"ok\":true}"); }
void handlePress() {
  String k = server.arg("key");
  if (k=="home") keyboard.write(KEY_HOME);
  else if (k=="enter") keyboard.write(KEY_RETURN);
  else if (k=="escape") keyboard.write(KEY_ESC);
  server.send(200,"application/json","{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[BOOT] starting...");
  keyboard.begin();
  Serial.println("[BLE] combo advertising");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] connecting");
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) { Serial.print("[WiFi] IP = "); Serial.println(WiFi.localIP()); }
  else Serial.println("[WiFi] failed (BLE still works)");
  server.on("/status", handleStatus);
  server.on("/move", handleMove);
  server.on("/home", handleHome);
  server.on("/click", handleClick);
  server.on("/tap", handleTap);
  server.on("/type", handleType);
  server.on("/press", handlePress);
  server.begin();
  Serial.println("[HTTP] ready");
}

void loop() { server.handleClient(); delay(2); }
