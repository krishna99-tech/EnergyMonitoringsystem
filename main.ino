// Example code for ESP32 Modbus RTU client reading MFM384 registers and sending data via UDP
// This code is successfully executed in elforge on 03-03-2026
// Final code

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ModbusRTU.h>

/* ================= WIFI CONFIG ================= */
const char* WIFI_SSID = "Admin_IO";
const char* WIFI_PASS = "Mcd@2026";

IPAddress localIP(192, 168, 1, 83);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

WiFiUDP udp;

/* ================= SERVER CONFIG ================= */
IPAddress SERVER_IP(192, 168, 1, 232);
const uint16_t UDP_PORT = 50003;

/* ================= RS485 ================= */
#define RXD2   32
#define TXD2   33
#define DE_RE  4
#define SLAVE_ID 1

/* ================= MFM384 REGISTERS ================= */
#define REG_VR       0 
#define REG_VY       2
#define REG_VB       4
#define REG_RY       8
#define REG_YB       10
#define REG_BR       12
#define REG_IR       16
#define REG_IY       18
#define REG_IB       20
#define REG_PF       54
#define REG_FREQ     56
#define REG_ENERGY   96

/* ================= MODBUS ================= */
ModbusRTU mb;
uint16_t regBuf[2];

/* ================= DATA ================= */
float vr, vy, vb, ry, yb, br;
float ir, iy, ib;
float power_factor, frequency, energy;

/* ================= STATE MACHINE ================= */
enum ReadState {
  R_VR, R_VY, R_VB,
  R_RY, R_YB, R_BR,
  R_IR, R_IY, R_IB,
  R_PF, R_FREQ, R_ENERGY
};

ReadState state = R_VR;
bool cycleComplete = false;

unsigned long lastRead = 0;
unsigned long lastSend = 0;
unsigned long lastWifiCheck = 0;

/* ================= FLOAT DCBA ================= */
union { float f; uint8_t b[4]; } u;

float decodeDCBA(uint16_t r0, uint16_t r1) {
  u.b[0] = r1 & 0xFF;
  u.b[1] = r1 >> 8;
  u.b[2] = r0 & 0xFF;
  u.b[3] = r0 >> 8;
  return u.f;
}

/* ================= ADVANCE STATE ================= */
void nextState() {
  state = (ReadState)((state + 1) % 12);
}

/* ================= MODBUS CALLBACK ================= */
bool cb(Modbus::ResultCode e, uint16_t, void*) {

  if (e != Modbus::EX_SUCCESS) {
    Serial.print("❌ Modbus error: ");
    Serial.println(e);
    nextState();
    return true;
  }

  float v = decodeDCBA(regBuf[0], regBuf[1]);

  switch (state) {
    case R_VR: vr = v; Serial.println("VR = " + String(vr)); break;
    case R_VY: vy = v; Serial.println("VY = " + String(vy)); break;
    case R_VB: vb = v; Serial.println("VB = " + String(vb)); break;
    case R_RY: ry = v; Serial.println("RY = " + String(ry)); break;
    case R_YB: yb = v; Serial.println("YB = " + String(yb)); break;
    case R_BR: br = v; Serial.println("BR = " + String(br)); break;
    case R_IR: ir = v; Serial.println("IR = " + String(ir)); break;
    case R_IY: iy = v; Serial.println("IY = " + String(iy)); break;
    case R_IB: ib = v; Serial.println("IB = " + String(ib)); break;
    case R_PF: power_factor = v; Serial.println("PF = " + String(power_factor)); break;
    case R_FREQ: frequency = v; Serial.println("Freq = " + String(frequency)); break;

    case R_ENERGY:
      energy = v;
      Serial.println("Energy = " + String(energy));
      Serial.println("🔁 Modbus cycle completed.");
      cycleComplete = true;
      break;
  }

  nextState();
  return true;
}

/* ================= SEND DATA ================= */
void sendData() {

  Serial.println("\n==============================");
  Serial.println("📤 Preparing to send data...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi NOT connected → Data NOT sent");
    Serial.println("==============================");
    return;
  }

  char json[512];

  snprintf(json, sizeof(json),
    "{\"energy\":%.2f,"
    "\"power_factor\":%.3f,"
    "\"frequency\":%.2f,"
    "\"vr\":%.2f,"
    "\"vy\":%.2f,"
    "\"vb\":%.2f,"
    "\"ry\":%.2f,"
    "\"yb\":%.2f,"
    "\"br\":%.2f,"
    "\"ir\":%.2f,"
    "\"iy\":%.2f,"
    "\"ib\":%.2f}",
    energy,
    power_factor,
    frequency,
    vr, vy, vb,
    ry, yb, br,
    ir, iy, ib
  );

  Serial.println("📦 JSON Payload:");
  Serial.println(json);

  udp.beginPacket(SERVER_IP, UDP_PORT);
  udp.print(json);

  if (udp.endPacket()) {
    Serial.println("✅ UDP Packet Sent Successfully");
  } else {
    Serial.println("❌ UDP Packet Send FAILED");
  }

  Serial.print("📡 Destination: ");
  Serial.print(SERVER_IP);
  Serial.print(":");
  Serial.println(UDP_PORT);

  Serial.println("==============================");
}

/* ================= WIFI SETUP ================= */
void setupWiFi() {

  WiFi.mode(WIFI_STA);

  if (!WiFi.config(localIP, gateway, subnet, dns)) {
    Serial.println("❌ Static IP config failed");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi Connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

/* ================= AUTO WIFI RECONNECT ================= */
void checkWiFi() {

  if (millis() - lastWifiCheck < 5000) return;
  lastWifiCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("🔄 Reconnecting WiFi...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

/* ================= SETUP ================= */
void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println("\n🚀 ESP32 MFM384 Industrial Modbus Client");

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, LOW);

  setupWiFi();

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  mb.begin(&Serial2, DE_RE);
  mb.master();

  Serial.println("✅ Modbus Master Initialized\n");
}

/* ================= LOOP ================= */
void loop() {

  mb.task();
  checkWiFi();

  /* ---- Modbus Polling ---- */
  if (millis() - lastRead > 300 && !mb.slave()) {

    lastRead = millis();

    switch (state) {
      case R_VR: mb.readIreg(SLAVE_ID, REG_VR, regBuf, 2, cb); break;
      case R_VY: mb.readIreg(SLAVE_ID, REG_VY, regBuf, 2, cb); break;
      case R_VB: mb.readIreg(SLAVE_ID, REG_VB, regBuf, 2, cb); break;
      case R_RY: mb.readIreg(SLAVE_ID, REG_RY, regBuf, 2, cb); break;
      case R_YB: mb.readIreg(SLAVE_ID, REG_YB, regBuf, 2, cb); break;
      case R_BR: mb.readIreg(SLAVE_ID, REG_BR, regBuf, 2, cb); break;
      case R_IR: mb.readIreg(SLAVE_ID, REG_IR, regBuf, 2, cb); break;
      case R_IY: mb.readIreg(SLAVE_ID, REG_IY, regBuf, 2, cb); break;
      case R_IB: mb.readIreg(SLAVE_ID, REG_IB, regBuf, 2, cb); break;
      case R_PF: mb.readIreg(SLAVE_ID, REG_PF, regBuf, 2, cb); break;
      case R_FREQ: mb.readIreg(SLAVE_ID, REG_FREQ, regBuf, 2, cb); break;
      case R_ENERGY: mb.readIreg(SLAVE_ID, REG_ENERGY, regBuf, 2, cb); break;
    }
  }

  /* ---- Send after complete cycle ---- */
  if (cycleComplete && millis() - lastSend > 5000) {
    lastSend = millis();
    cycleComplete = false;
    sendData();
  }
}
