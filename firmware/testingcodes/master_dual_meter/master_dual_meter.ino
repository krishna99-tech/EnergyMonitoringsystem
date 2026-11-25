// ============================================================
//  ESP32 Modbus RTU MASTER (Single Meter with Heartbeat)
//  Features: Modbus RTU MFM384 Reading, UDP Telemetry, Auto-Reconnect
//  Integrated with ELFORGE Energy Monitoring System (app.py)
// ============================================================

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ModbusRTU.h>

/* ================= WIFI CONFIG ================= */
const char* WIFI_SSID = "JioFiber-hgNd9";
const char* WIFI_PASS = "E6PTEfgebeeYDPeR";

IPAddress localIP(192, 168, 29, 83);
IPAddress gateway(192, 168, 29, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

WiFiUDP udp;

/* ================= SERVER CONFIG ================= */
IPAddress SERVER_IP(192, 168, 29, 139);
const uint16_t UDP_PORT = 50004; // Matches app.py UDP_PORT (50004)

/* ================= RS485 PINS & SLAVE ================= */
#define RXD2      32
#define TXD2      33
#define DE_RE     4
#define SLAVE_ID  1

/* ================= HEARTBEAT & TIMING CONFIG ================= */
#define HEARTBEAT_INTERVAL    10000     // Send UDP heartbeat ping every 10 seconds
#define TELEMETRY_INTERVAL_MS 300000UL  // 5 Minutes (5 * 60 * 1000 = 300,000 ms)

unsigned long lastHeartbeat = 0;
uint32_t heartbeatSeq = 0;
bool modbusHealthy = false;
bool firstSendDone = false;

/* ================= MFM384 REGISTER MAP ================= */
#define REG_VR      0
#define REG_VY      2
#define REG_VB      4
#define REG_RY      8
#define REG_YB      10
#define REG_BR      12
#define REG_IR      16
#define REG_IY      18
#define REG_IB      20
#define REG_PF      54
#define REG_FREQ    56
#define REG_ENERGY  96

/* ================= MODBUS ================= */
ModbusRTU mb;
uint16_t regBuf[2];

/* ================= METER DATA ================= */
struct MeterData {
  float vr, vy, vb;
  float ry, yb, br;
  float ir, iy, ib;
  float power_factor, frequency, energy;
  bool cycleComplete;
};

MeterData meter;

/* ================= STATE MACHINE ================= */
enum ReadState {
  R_VR, R_VY, R_VB,
  R_RY, R_YB, R_BR,
  R_IR, R_IY, R_IB,
  R_PF, R_FREQ, R_ENERGY,
  R_TOTAL_STATES
};

ReadState regState = R_VR;

unsigned long lastRead        = 0;
unsigned long lastSend        = 0;
unsigned long lastWifiCheck   = 0;

#define MODBUS_POLL_INTERVAL   300
#define SEND_COOLDOWN_MS      5000

/* ================= FLOAT DCBA DECODE ================= */
union { float f; uint8_t b[4]; } u;

float decodeDCBA(uint16_t r0, uint16_t r1) {
  u.b[0] = r1 & 0xFF;
  u.b[1] = r1 >> 8;
  u.b[2] = r0 & 0xFF;
  u.b[3] = r0 >> 8;
  return u.f;
}

/* ================= MODBUS CALLBACK ================= */
bool cb(Modbus::ResultCode e, uint16_t, void*) {
  if (e != Modbus::EX_SUCCESS) {
    Serial.printf("❌ Modbus error on state %d: 0x%02X\n", regState, e);
    modbusHealthy = false;
    regState = (ReadState)((regState + 1) % R_TOTAL_STATES);
    return true;
  }

  modbusHealthy = true;
  float v = decodeDCBA(regBuf[0], regBuf[1]);

  switch (regState) {
    case R_VR:    meter.vr           = v; Serial.printf("VR    = %.2f V\n",  v); break;
    case R_VY:    meter.vy           = v; Serial.printf("VY    = %.2f V\n",  v); break;
    case R_VB:    meter.vb           = v; Serial.printf("VB    = %.2f V\n",  v); break;
    case R_RY:    meter.ry           = v; Serial.printf("RY    = %.2f V\n",  v); break;
    case R_YB:    meter.yb           = v; Serial.printf("YB    = %.2f V\n",  v); break;
    case R_BR:    meter.br           = v; Serial.printf("BR    = %.2f V\n",  v); break;
    case R_IR:    meter.ir           = v; Serial.printf("IR    = %.2f A\n",  v); break;
    case R_IY:    meter.iy           = v; Serial.printf("IY    = %.2f A\n",  v); break;
    case R_IB:    meter.ib           = v; Serial.printf("IB    = %.2f A\n",  v); break;
    case R_PF:    meter.power_factor = v; Serial.printf("PF    = %.3f\n",    v); break;
    case R_FREQ:  meter.frequency    = v; Serial.printf("Freq  = %.2f Hz\n", v); break;
    case R_ENERGY:
      meter.energy = v;
      meter.cycleComplete = true;
      Serial.printf("Energy = %.2f kWh  ✅ Cycle complete\n\n", v);
      break;
  }

  regState = (ReadState)((regState + 1) % R_TOTAL_STATES);
  return true;
}

/* ================= SEND UDP TELEMETRY ================= */
void sendData() {
  Serial.println("\n==============================");
  Serial.println("📤 Sending telemetry...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected → skipped");
    Serial.println("==============================\n");
    return;
  }

  char json[512];
  snprintf(json, sizeof(json),
    "{\"slave_id\":%d,"
    "\"energy\":%.2f,"
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
    SLAVE_ID,
    meter.energy,
    meter.power_factor,
    meter.frequency,
    meter.vr, meter.vy, meter.vb,
    meter.ry, meter.yb, meter.br,
    meter.ir, meter.iy, meter.ib
  );

  Serial.println("📦 Payload:");
  Serial.println(json);

  udp.beginPacket(SERVER_IP, UDP_PORT);
  udp.print(json);

  if (udp.endPacket()) {
    Serial.println("✅ UDP Packet Sent Successfully");
    lastHeartbeat = millis(); // Reset heartbeat timer since live telemetry was sent
  } else {
    Serial.println("❌ UDP Packet Send FAILED");
  }

  Serial.printf("📡 → %s:%d\n", SERVER_IP.toString().c_str(), UDP_PORT);
  Serial.println("==============================\n");
}

/* ================= SEND UDP HEARTBEAT ================= */
void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;

  heartbeatSeq++;

  char hbJson[256];
  snprintf(hbJson, sizeof(hbJson),
    "{\"type\":\"heartbeat\","
    "\"slave_id\":%d,"
    "\"seq\":%lu,"
    "\"uptime_sec\":%lu,"
    "\"rssi\":%d,"
    "\"modbus_ok\":%s}",
    SLAVE_ID,
    heartbeatSeq,
    millis() / 1000,
    WiFi.RSSI(),
    modbusHealthy ? "true" : "false"
  );

  udp.beginPacket(SERVER_IP, UDP_PORT);
  udp.print(hbJson);
  udp.endPacket();

  Serial.printf("💓 Heartbeat (#%lu) | RSSI: %d dBm | Modbus: %s\n",
                heartbeatSeq, WiFi.RSSI(),
                modbusHealthy ? "OK" : "TIMEOUT");
}

/* ================= WIFI SETUP ================= */
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(localIP, gateway, subnet, dns))
    Serial.println("❌ Static IP config failed");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.printf("\n✅ WiFi Connected — IP: %s\n\n", WiFi.localIP().toString().c_str());
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
  Serial.println("\n🚀 ESP32 Modbus RTU MASTER (Single Meter)");
  Serial.println("   Works with real MFM384 meter via RS485\n");

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, LOW);

  setupWiFi();

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  mb.begin(&Serial2, DE_RE);
  mb.master();

  memset(&meter, 0, sizeof(meter));

  Serial.println("✅ Modbus Master ready\n");
}

/* ================= LOOP ================= */
void loop() {
  mb.task();
  checkWiFi();

  /* ---- Poll Modbus registers sequentially ---- */
  if (millis() - lastRead > MODBUS_POLL_INTERVAL && !mb.slave()) {
    lastRead = millis();
    switch (regState) {
      case R_VR:     mb.readIreg(SLAVE_ID, REG_VR,     regBuf, 2, cb); break;
      case R_VY:     mb.readIreg(SLAVE_ID, REG_VY,     regBuf, 2, cb); break;
      case R_VB:     mb.readIreg(SLAVE_ID, REG_VB,     regBuf, 2, cb); break;
      case R_RY:     mb.readIreg(SLAVE_ID, REG_RY,     regBuf, 2, cb); break;
      case R_YB:     mb.readIreg(SLAVE_ID, REG_YB,     regBuf, 2, cb); break;
      case R_BR:     mb.readIreg(SLAVE_ID, REG_BR,     regBuf, 2, cb); break;
      case R_IR:     mb.readIreg(SLAVE_ID, REG_IR,     regBuf, 2, cb); break;
      case R_IY:     mb.readIreg(SLAVE_ID, REG_IY,     regBuf, 2, cb); break;
      case R_IB:     mb.readIreg(SLAVE_ID, REG_IB,     regBuf, 2, cb); break;
      case R_PF:     mb.readIreg(SLAVE_ID, REG_PF,     regBuf, 2, cb); break;
      case R_FREQ:   mb.readIreg(SLAVE_ID, REG_FREQ,   regBuf, 2, cb); break;
      case R_ENERGY: mb.readIreg(SLAVE_ID, REG_ENERGY, regBuf, 2, cb); break;
    }
  }

  /* ---- Send telemetry once a full register cycle has finished (Every 5 Minutes) ---- */
  if (meter.cycleComplete && (!firstSendDone || (millis() - lastSend >= TELEMETRY_INTERVAL_MS))) {
    lastSend = millis();
    firstSendDone = true;
    meter.cycleComplete = false;
    sendData();
  }

  /* ---- Standalone Heartbeat (keepalive / status ping) ---- */
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }
}
