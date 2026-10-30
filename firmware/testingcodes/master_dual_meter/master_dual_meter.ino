// ============================================================
//  ESP32 Modbus RTU MASTER (Dual Meter with Heartbeat & Status LED)
//  Works identically with:
//    • Real MFM384 energy meters (Slave 1 & 2)
//    • ESP32 slave simulators (slave1.ino / slave2.ino)
//  Features: Dual Modbus RTU reading, UDP Telemetry, Heartbeat monitoring, and Status LED Pulse
//  Integrated with ELFORGE Energy Monitoring System
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

/* ================= RS485 PINS ================= */
#define RXD2    32
#define TXD2    33
#define DE_RE   4

/* ================= SLAVE IDs ================= */
// To add more slaves: add ID here, increase meter[] size, add PHASE entries
#define SLAVE_ID_1  1
#define SLAVE_ID_2  2

/* ================= HEARTBEAT & STATUS LED ================= */
#define LED_HEARTBEAT       2        // Onboard LED (GPIO 2 on most ESP32 dev boards)
#define HEARTBEAT_INTERVAL  10000    // Send UDP heartbeat ping every 10 seconds
#define LED_BLINK_NORMAL    1000     // 1s blink when WiFi is connected
#define LED_BLINK_FAST      200      // 200ms rapid blink when WiFi is disconnected

unsigned long lastHeartbeat = 0;
unsigned long lastLedBlink  = 0;
bool ledState = false;
uint32_t heartbeatSeq = 0;
bool modbusHealthy1 = false;
bool modbusHealthy2 = false;

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

/* ================= METER DATA STRUCT ================= */
struct MeterData {
  float vr, vy, vb;
  float ry, yb, br;
  float ir, iy, ib;
  float power_factor, frequency, energy;
  bool cycleComplete;
};

MeterData meter[2]; // meter[0] = Slave 1, meter[1] = Slave 2

/* ================= STATE MACHINE ================= */
enum ReadState {
  R_VR, R_VY, R_VB,
  R_RY, R_YB, R_BR,
  R_IR, R_IY, R_IB,
  R_PF, R_FREQ, R_ENERGY,
  R_TOTAL_STATES
};

enum DevicePhase {
  PHASE_SLAVE1,
  PHASE_INTER_DELAY,
  PHASE_SLAVE2,
  PHASE_SEND
};

ReadState   regState    = R_VR;
DevicePhase devicePhase = PHASE_SLAVE1;
int         activeMeter = 0;

unsigned long lastRead        = 0;
unsigned long lastSend        = 0;
unsigned long lastWifiCheck   = 0;
unsigned long interDelayStart = 0;

#define INTER_DEVICE_DELAY_MS  500
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
    Serial.printf("❌ Modbus error on Slave %d, state %d: 0x%02X\n",
                  activeMeter + 1, regState, e);
    if (activeMeter == 0) modbusHealthy1 = false;
    else modbusHealthy2 = false;

    regState = (ReadState)((regState + 1) % R_TOTAL_STATES);
    return true;
  }

  if (activeMeter == 0) modbusHealthy1 = true;
  else modbusHealthy2 = true;

  float v = decodeDCBA(regBuf[0], regBuf[1]);
  MeterData& m = meter[activeMeter];
  const char* tag = (activeMeter == 0) ? "[S1]" : "[S2]";

  switch (regState) {
    case R_VR:    m.vr           = v; Serial.printf("%s VR    = %.2f V\n",  tag, v); break;
    case R_VY:    m.vy           = v; Serial.printf("%s VY    = %.2f V\n",  tag, v); break;
    case R_VB:    m.vb           = v; Serial.printf("%s VB    = %.2f V\n",  tag, v); break;
    case R_RY:    m.ry           = v; Serial.printf("%s RY    = %.2f V\n",  tag, v); break;
    case R_YB:    m.yb           = v; Serial.printf("%s YB    = %.2f V\n",  tag, v); break;
    case R_BR:    m.br           = v; Serial.printf("%s BR    = %.2f V\n",  tag, v); break;
    case R_IR:    m.ir           = v; Serial.printf("%s IR    = %.2f A\n",  tag, v); break;
    case R_IY:    m.iy           = v; Serial.printf("%s IY    = %.2f A\n",  tag, v); break;
    case R_IB:    m.ib           = v; Serial.printf("%s IB    = %.2f A\n",  tag, v); break;
    case R_PF:    m.power_factor = v; Serial.printf("%s PF    = %.3f\n",    tag, v); break;
    case R_FREQ:  m.frequency    = v; Serial.printf("%s Freq  = %.2f Hz\n", tag, v); break;
    case R_ENERGY:
      m.energy = v;
      m.cycleComplete = true;
      Serial.printf("%s Energy = %.2f kWh  ✅ cycle done\n\n", tag, v);
      break;
  }

  regState = (ReadState)((regState + 1) % R_TOTAL_STATES);
  return true;
}

/* ================= SEND COMBINED UDP TELEMETRY ================= */
void sendData() {
  Serial.println("\n==============================");
  Serial.println("📤 Sending combined telemetry...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected → skipped");
    Serial.println("==============================\n");
    return;
  }

  MeterData& m1 = meter[0];
  MeterData& m2 = meter[1];

  char json[1024];
  snprintf(json, sizeof(json),
    "{"
      ""device_ip":"%s","
      ""slave1":{"
        ""slave_id":%d,"energy":%.2f,"power_factor":%.3f,"frequency":%.2f,"
        ""vr":%.2f,"vy":%.2f,"vb":%.2f,"
        ""ry":%.2f,"yb":%.2f,"br":%.2f,"
        ""ir":%.2f,"iy":%.2f,"ib":%.2f"
      "},"
      ""slave2":{"
        ""slave_id":%d,"energy":%.2f,"power_factor":%.3f,"frequency":%.2f,"
        ""vr":%.2f,"vy":%.2f,"vb":%.2f,"
        ""ry":%.2f,"yb":%.2f,"br":%.2f,"
        ""ir":%.2f,"iy":%.2f,"ib":%.2f"
      "}"
    "}",
    WiFi.localIP().toString().c_str(),
    SLAVE_ID_1, m1.energy, m1.power_factor, m1.frequency,
    m1.vr, m1.vy, m1.vb, m1.ry, m1.yb, m1.br, m1.ir, m1.iy, m1.ib,
    SLAVE_ID_2, m2.energy, m2.power_factor, m2.frequency,
    m2.vr, m2.vy, m2.vb, m2.ry, m2.yb, m2.br, m2.ir, m2.iy, m2.ib
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

  // 1. Heartbeat for Slave 1
  char hbJson1[256];
  snprintf(hbJson1, sizeof(hbJson1),
    "{\"type\":\"heartbeat\","
    "\"device_ip\":\"%s\","
    "\"slave_id\":%d,"
    "\"seq\":%lu,"
    "\"uptime_sec\":%lu,"
    "\"rssi\":%d,"
    "\"modbus_ok\":%s}",
    WiFi.localIP().toString().c_str(),
    SLAVE_ID_1,
    heartbeatSeq,
    millis() / 1000,
    WiFi.RSSI(),
    modbusHealthy1 ? "true" : "false"
  );
  udp.beginPacket(SERVER_IP, UDP_PORT);
  udp.print(hbJson1);
  udp.endPacket();

  // 2. Heartbeat for Slave 2
  char hbJson2[256];
  snprintf(hbJson2, sizeof(hbJson2),
    "{\"type\":\"heartbeat\","
    "\"device_ip\":\"%s\","
    "\"slave_id\":%d,"
    "\"seq\":%lu,"
    "\"uptime_sec\":%lu,"
    "\"rssi\":%d,"
    "\"modbus_ok\":%s}",
    WiFi.localIP().toString().c_str(),
    SLAVE_ID_2,
    heartbeatSeq,
    millis() / 1000,
    WiFi.RSSI(),
    modbusHealthy2 ? "true" : "false"
  );
  udp.beginPacket(SERVER_IP, UDP_PORT);
  udp.print(hbJson2);
  udp.endPacket();

  Serial.printf("💓 Heartbeat (#%lu) | RSSI: %d dBm | Slave 1 Modbus: %s | Slave 2 Modbus: %s\n",
                heartbeatSeq, WiFi.RSSI(),
                modbusHealthy1 ? "OK" : "TIMEOUT",
                modbusHealthy2 ? "OK" : "TIMEOUT");
}

/* ================= VISUAL LED HEARTBEAT ================= */
void updateLedHeartbeat() {
  unsigned long now = millis();
  unsigned long blinkInterval = (WiFi.status() == WL_CONNECTED) ? LED_BLINK_NORMAL : LED_BLINK_FAST;

  if (now - lastLedBlink >= blinkInterval) {
    lastLedBlink = now;
    ledState = !ledState;
    digitalWrite(LED_HEARTBEAT, ledState);
  }
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
    digitalWrite(LED_HEARTBEAT, !digitalRead(LED_HEARTBEAT)); // Toggle LED while waiting
  }

  digitalWrite(LED_HEARTBEAT, HIGH);
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
  Serial.println("\n🚀 ESP32 Modbus RTU MASTER (Dual Meter with Heartbeat)");
  Serial.println("   Works with real MFM384 meters OR ESP32 slave simulators\n");

  pinMode(LED_HEARTBEAT, OUTPUT);
  digitalWrite(LED_HEARTBEAT, LOW);

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, LOW);

  setupWiFi();

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  mb.begin(&Serial2, DE_RE);
  mb.master();

  memset(meter, 0, sizeof(meter));

  Serial.println("✅ Modbus Master ready");
  Serial.println("📋 Phase: Slave 1\n");
}

/* ================= LOOP ================= */
void loop() {
  mb.task();
  checkWiFi();
  updateLedHeartbeat(); // Keep visual hardware LED pulse running

  switch (devicePhase) {

    case PHASE_SLAVE1:
      if (millis() - lastRead > MODBUS_POLL_INTERVAL && !mb.slave()) {
        lastRead = millis();
        activeMeter = 0;
        uint8_t id = SLAVE_ID_1;
        switch (regState) {
          case R_VR:    mb.readIreg(id, REG_VR,     regBuf, 2, cb); break;
          case R_VY:    mb.readIreg(id, REG_VY,     regBuf, 2, cb); break;
          case R_VB:    mb.readIreg(id, REG_VB,     regBuf, 2, cb); break;
          case R_RY:    mb.readIreg(id, REG_RY,     regBuf, 2, cb); break;
          case R_YB:    mb.readIreg(id, REG_YB,     regBuf, 2, cb); break;
          case R_BR:    mb.readIreg(id, REG_BR,     regBuf, 2, cb); break;
          case R_IR:    mb.readIreg(id, REG_IR,     regBuf, 2, cb); break;
          case R_IY:    mb.readIreg(id, REG_IY,     regBuf, 2, cb); break;
          case R_IB:    mb.readIreg(id, REG_IB,     regBuf, 2, cb); break;
          case R_PF:    mb.readIreg(id, REG_PF,     regBuf, 2, cb); break;
          case R_FREQ:  mb.readIreg(id, REG_FREQ,   regBuf, 2, cb); break;
          case R_ENERGY:mb.readIreg(id, REG_ENERGY, regBuf, 2, cb); break;
        }
        if (meter[0].cycleComplete) {
          meter[0].cycleComplete = false;
          regState = R_VR;
          interDelayStart = millis();
          devicePhase = PHASE_INTER_DELAY;
          Serial.println("⏳ Inter-device delay...");
        }
      }
      break;

    case PHASE_INTER_DELAY:
      if (millis() - interDelayStart >= INTER_DEVICE_DELAY_MS) {
        devicePhase = PHASE_SLAVE2;
        Serial.println("📋 Phase: Slave 2\n");
      }
      break;

    case PHASE_SLAVE2:
      if (millis() - lastRead > MODBUS_POLL_INTERVAL && !mb.slave()) {
        lastRead = millis();
        activeMeter = 1;
        uint8_t id = SLAVE_ID_2;
        switch (regState) {
          case R_VR:    mb.readIreg(id, REG_VR,     regBuf, 2, cb); break;
          case R_VY:    mb.readIreg(id, REG_VY,     regBuf, 2, cb); break;
          case R_VB:    mb.readIreg(id, REG_VB,     regBuf, 2, cb); break;
          case R_RY:    mb.readIreg(id, REG_RY,     regBuf, 2, cb); break;
          case R_YB:    mb.readIreg(id, REG_YB,     regBuf, 2, cb); break;
          case R_BR:    mb.readIreg(id, REG_BR,     regBuf, 2, cb); break;
          case R_IR:    mb.readIreg(id, REG_IR,     regBuf, 2, cb); break;
          case R_IY:    mb.readIreg(id, REG_IY,     regBuf, 2, cb); break;
          case R_IB:    mb.readIreg(id, REG_IB,     regBuf, 2, cb); break;
          case R_PF:    mb.readIreg(id, REG_PF,     regBuf, 2, cb); break;
          case R_FREQ:  mb.readIreg(id, REG_FREQ,   regBuf, 2, cb); break;
          case R_ENERGY:mb.readIreg(id, REG_ENERGY, regBuf, 2, cb); break;
        }
        if (meter[1].cycleComplete) {
          meter[1].cycleComplete = false;
          devicePhase = PHASE_SEND;
        }
      }
      break;

    case PHASE_SEND:
      if (millis() - lastSend > SEND_COOLDOWN_MS) {
        lastSend = millis();
        sendData();
      }
      regState = R_VR;
      devicePhase = PHASE_SLAVE1;
      Serial.println("📋 Phase: Slave 1\n");
      break;
  }

  /* ---- Send Standalone Heartbeat if idle / Modbus down ---- */
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }
}
