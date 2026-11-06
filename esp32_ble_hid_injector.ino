/*
 * ESP32-S3 BLE HID Injector  —  给 iOS/Android 做"硬件鼠标键盘"
 *
 * 架构:
 *   笔记本 --WiFi/HTTP--> 本固件 --BLE HID--> 手机
 *
 * 为什么需要它:
 *   iOS 不开放触摸注入(perform_press 需要 task_for_pid-allow),
 *   但系统本身支持蓝牙鼠标/键盘(辅助功能→触控→辅助触控→设备)。
 *   所以用硬件冒充输入设备 -> 绕开软件限制。
 *
 * 依赖库 (Arduino IDE -> 库管理器):
 *   - ESP32-BLE-Mouse     (T-vK)
 *   - ESP32-BLE-Keyboard  (T-vK)
 *   开发板: "ESP32S3 Dev Module"
 *   分区:   "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   PSRAM:  按板子选 (OPI PSRAM for N16R8)
 *
 * HTTP API:
 *   GET /status              当前状态
 *   GET /move?x=10&y=-5      相对移动
 *   GET /click?btn=left      单击 (left/right)
 *   GET /home                指针归位(往左上推到底) —— 绝对定位前必做
 *   GET /tap?x=..&y=..       绝对定位并点击(内部自动 home)
 *   GET /swipe?x1=..&y1=..&x2=..&y2=..&steps=20
 *   GET /type?text=hello     键盘输入(ASCII)
 *   GET /press?key=home      按键 (home/enter/escape/backspace)
 *
 * 校准:
 *   首次使用先 /home, 然后 /move?x=100&y=0 量出实际像素,
 *   把 PX_PER_UNIT 改成 测量像素/100。
 */

#include <WiFi.h>
#include <WebServer.h>
#include <BleMouse.h>
#include <BleKeyboard.h>

// ======== 改这里 ========
const char* WIFI_SSID = "ChinaUnicom-1003";
const char* WIFI_PASS = "你的WiFi密码";
// 屏幕上 1 个单位移动对应多少像素(需实测校准, 见文件头说明)
float PX_PER_UNIT_X = 1.0f;
float PX_PER_UNIT_Y = 1.0f;
// 单次 move 的最大步长(太大指针会丢步)
const int MAX_STEP = 40;
// ========================

BleMouse    mouse("ESP32-Mouse",    "Espressif", 100);
BleKeyboard keyboard("ESP32-Keyboard", "Espressif", 100);
WebServer   server(80);

// ---------- 工具 ----------
void homePointer() {
  // iOS 的指针会撞到屏幕边界后停住, 连续往左上推即可归位
  for (int i = 0; i < 60; i++) {
    mouse.move(-MAX_STEP, -MAX_STEP);
    delay(6);
  }
  delay(120);
}

void moveBy(int dx, int dy) {
  int sx = (dx > 0) ? 1 : -1;
  int sy = (dy > 0) ? 1 : -1;
  int rx = abs(dx), ry = abs(dy);
  while (rx > 0 || ry > 0) {
    int stepX = min(rx, MAX_STEP) * sx;
    int stepY = min(ry, MAX_STEP) * sy;
    mouse.move(stepX, stepY);
    rx -= abs(stepX); ry -= abs(stepY);
    delay(8);
  }
  delay(60);
}

void moveTo(int x, int y) {
  // 相对移动设备没有绝对坐标, 只能"归位 + 走步"
  homePointer();
  moveBy((int)(x / PX_PER_UNIT_X), (int)(y / PX_PER_UNIT_Y));
}

// ---------- HTTP 处理 ----------
void handleStatus() {
  String s = "{\"ok\":true,\"ble_mouse\":" + String(mouse.isConnected() ? "true" : "false");
  s += ",\"ble_kb\":" + String(keyboard.isConnected() ? "true" : "false");
  s += ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", s);
}

void handleMove() {
  int x = server.arg("x").toInt();
  int y = server.arg("y").toInt();
  moveBy((int)(x / PX_PER_UNIT_X), (int)(y / PX_PER_UNIT_Y));
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleHome() {
  homePointer();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleClick() {
  String b = server.arg("btn");
  if (!mouse.isConnected()) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"ble not connected\"}"); return; }
  if (b == "right") mouse.click(MOUSE_RIGHT);
  else              mouse.click(MOUSE_LEFT);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleTap() {
  int x = server.arg("x").toInt();
  int y = server.arg("y").toInt();
  moveTo(x, y);
  mouse.click(MOUSE_LEFT);
  server.send(200, "application/json", "{\"ok\":true,\"x\":" + String(x) + ",\"y\":" + String(y) + "}");
}

void handleSwipe() {
  int x1 = server.arg("x1").toInt(), y1 = server.arg("y1").toInt();
  int x2 = server.arg("x2").toInt(), y2 = server.arg("y2").toInt();
  int steps = server.arg("steps").toInt(); if (steps <= 0) steps = 20;
  moveTo(x1, y1);
  mouse.press(MOUSE_LEFT);
  delay(60);
  for (int i = 1; i <= steps; i++) {
    int cx = x1 + (x2 - x1) * i / steps;
    int cy = y1 + (y2 - y1) * i / steps;
    int px = x1 + (x2 - x1) * (i - 1) / steps;
    int py = y1 + (y2 - y1) * (i - 1) / steps;
    moveBy((int)((cx - px) / PX_PER_UNIT_X), (int)((cy - py) / PX_PER_UNIT_Y));
  }
  delay(60);
  mouse.release(MOUSE_LEFT);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleType() {
  String t = server.arg("text");
  if (!keyboard.isConnected()) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"ble kb not connected\"}"); return; }
  // 注意: BLE 键盘只能输 ASCII, 中文需要系统输入法配合(paste 或拼音)
  keyboard.print(t);
  server.send(200, "application/json", "{\"ok\":true,\"len\":" + String(t.length()) + "}");
}

void handlePress() {
  String k = server.arg("key");
  if (!keyboard.isConnected()) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"ble kb not connected\"}"); return; }
  if      (k == "home")      keyboard.write(KEY_HOME);
  else if (k == "enter")     keyboard.write(KEY_RETURN);
  else if (k == "escape")    keyboard.write(KEY_ESC);
  else if (k == "backspace") keyboard.write(KEY_BACKSPACE);
  else if (k == "tab")       keyboard.write(KEY_TAB);
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------- 主流程 ----------
void setup() {
  Serial.begin(115200);
  delay(300);

  mouse.begin();
  keyboard.begin();
  Serial.println("[BLE] advertising as ESP32-Mouse / ESP32-Keyboard");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] connecting");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] IP = "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] 连接失败, 仍可走串口命令");
  }

  server.on("/status", handleStatus);
  server.on("/move",   handleMove);
  server.on("/home",   handleHome);
  server.on("/click",  handleClick);
  server.on("/tap",    handleTap);
  server.on("/swipe",  handleSwipe);
  server.on("/type",   handleType);
  server.on("/press",  handlePress);
  server.begin();
  Serial.println("[HTTP] http://<ip>/status");
}

void loop() {
  server.handleClient();
}
