// ============================================================
//  ESP32 Modbus RTU SLAVE 2  —  MFM384 simulator  (FIXED)
//  Slave ID : 2  |  Baud: 9600  |  Parity: None (8N1)
//  Fix: Registers declared individually to match MFM384 gaps
// ============================================================

#include <ModbusRTU.h>

/* ================= RS485 PINS ================= */
#define RXD2   32
#define TXD2   33
#define DE_RE  4

/* ================= SLAVE CONFIG ================= */
#define SLAVE_ID   2
#define BAUD_RATE  9600

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

ModbusRTU mb;

unsigned long lastUpdate = 0;
#define UPDATE_INTERVAL_MS  2000

/* ================= FLOAT → DCBA ENCODE ================= */
void encodeDCBA(float val, uint16_t* r0, uint16_t* r1) {
  union { float f; uint8_t b[4]; } u;
  u.f  = val;
  *r0  = ((uint16_t)u.b[2]) | ((uint16_t)u.b[3] << 8);
  *r1  = ((uint16_t)u.b[0]) | ((uint16_t)u.b[1] << 8);
}

/* ================= WRITE FLOAT TO MODBUS IREGS ================= */
void writeFloat(uint16_t reg, float val) {
  uint16_t r0, r1;
  encodeDCBA(val, &r0, &r1);
  mb.Ireg(reg,     r0);
  mb.Ireg(reg + 1, r1);
}

/* ================= RANDOM FLOAT ================= */
float randFloat(float lo, float hi) {
  return lo + ((float)random(10000) / 10000.0f) * (hi - lo);
}

/* ================= UPDATE ALL REGISTERS ================= */
void updateRegisters() {
  writeFloat(REG_VR,  randFloat(215.0f, 235.0f));
  writeFloat(REG_VY,  randFloat(215.0f, 235.0f));
  writeFloat(REG_VB,  randFloat(215.0f, 235.0f));

  writeFloat(REG_RY,  randFloat(370.0f, 408.0f));
  writeFloat(REG_YB,  randFloat(370.0f, 408.0f));
  writeFloat(REG_BR,  randFloat(370.0f, 408.0f));

  writeFloat(REG_IR,  randFloat(10.0f, 80.0f));
  writeFloat(REG_IY,  randFloat(10.0f, 80.0f));
  writeFloat(REG_IB,  randFloat(10.0f, 80.0f));

  writeFloat(REG_PF,  randFloat(0.80f, 0.98f));
  writeFloat(REG_FREQ,randFloat(49.8f, 50.2f));

  static float energy = 5000.0f;
  energy += randFloat(0.02f, 0.08f);
  writeFloat(REG_ENERGY, energy);

  Serial.println("📊 [SLAVE 2] Registers refreshed");
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n🟡 ESP32 Modbus RTU SLAVE 2 — MFM384 Simulator (Fixed)");
  Serial.printf("   Slave ID : %d\n", SLAVE_ID);
  Serial.printf("   Baud     : %d  |  Parity: None (8N1)\n\n", BAUD_RATE);

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, LOW);

  randomSeed(analogRead(2) ^ analogRead(3));

  Serial2.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);
  mb.begin(&Serial2, DE_RE);
  mb.slave(SLAVE_ID);

  // -------------------------------------------------------
  // Declare ONLY the register PAIRS that the master reads.
  // Each float uses 2 consecutive registers → addIreg(addr, count=2)
  // -------------------------------------------------------
  mb.addIreg(REG_VR,     2);   // 0,1
  mb.addIreg(REG_VY,     2);   // 2,3
  mb.addIreg(REG_VB,     2);   // 4,5
  mb.addIreg(REG_RY,     2);   // 8,9
  mb.addIreg(REG_YB,     2);   // 10,11
  mb.addIreg(REG_BR,     2);   // 12,13
  mb.addIreg(REG_IR,     2);   // 16,17
  mb.addIreg(REG_IY,     2);   // 18,19
  mb.addIreg(REG_IB,     2);   // 20,21
  mb.addIreg(REG_PF,     2);   // 54,55
  mb.addIreg(REG_FREQ,   2);   // 56,57
  mb.addIreg(REG_ENERGY, 2);   // 96,97

  updateRegisters();

  Serial.println("✅ Slave ready — listening on RS485 bus\n");
}

/* ================= LOOP ================= */
void loop() {
  mb.task();

  if (millis() - lastUpdate > UPDATE_INTERVAL_MS) {
    lastUpdate = millis();
    updateRegisters();
  }
}
