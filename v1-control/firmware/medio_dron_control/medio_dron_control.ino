/*
  Medio dron - control de altura y roll
  ESP32-C3 + BNO055 (I2C) + ToF Pololu #4071 (pulso) + 2 ESC por DShot300

  Arduino IDE:
    - Placa: "ESP32C3 Dev Module" (core "esp32 by Espressif" 3.x)
    - Tools > USB CDC On Boot: "Enabled"   (si no, el Serial por USB no sale)
    - No necesita librerías extra (el BNO055 se lee directo por I2C)

  Se controla desde medio_dron_control.html (Chrome/Edge) o desde el Monitor Serie
  (115200, "New Line"). Escribe "ayuda" para ver los comandos.

  Unidades internas: SI (N, m, rad, kg). En comandos y telemetría: cm, grados, %.

  Convención: roll POSITIVO = lado del MOTOR 1 ARRIBA.  ("imuauto" lo configura solo)
*/

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

// =====================================================================
// PINES  (CAMBIAR AQUÍ cuando tengan el pinout de la placa prestada)
// Por ahora son los de su PCB principal (hardware/placa-principal).
// =====================================================================
const int PIN_SDA     = 0;        // BNO055 SDA
const int PIN_SCL     = 1;        // BNO055 SCL
const int PIN_ESC[2]  = {6, 7};   // señal DShot: [0] = MOTOR 1, [1] = MOTOR 2
const int PIN_TOF     = 4;        // salida de pulso del Pololu #4071
const int PIN_VBAT    = 3;        // divisor de batería (ADC). -1 si la placa no tiene
const int PIN_LED     = -1;       // LED de "armado". -1 = sin LED
// Evitar en ESP32-C3: GPIO 2, 8, 9 (arranque), 18-19 (USB).
// Solo GPIO 0-4 tienen ADC útil (para VBAT).

// =====================================================================
// Constantes
// =====================================================================
const float G = 9.81f;
const float D2R = 0.0174532925f, R2D = 57.2957795f;
const uint32_t CTRL_US = 10000;        // lazo de control a 100 Hz (lo que da el BNO055)
const uint32_t PARAMS_VERSION = 3;

// DShot300 con reloj del RMT a 10 MHz (igual que v0, ya probado)
const uint32_t RMT_HZ = 10000000;
const uint16_t T_BIT = 33, T1_H = 25, T0_H = 12;

// =====================================================================
// Parámetros (se guardan con "guardar")
// =====================================================================
enum { ROLL = 0, ALT = 1 };

struct Params {
  uint32_t version;
  // modelo físico (método A)
  float masa;      // kg, todo lo que sube y baja (sin batería)
  float J;         // kg·m², momento de inercia del brazo (péndulo)
  float L;         // m, del pivote al eje de cada motor
  float u0;        // %, throttle de hover
  float v0;        // V, voltaje medido en el hover (0 = sin compensación)
  // diseño: lo que ustedes eligen
  float zeta[2];   // amortiguamiento  [ROLL, ALT]
  float wn[2];     // frecuencia natural, rad/s
  float kiFac[2];  // Ki = Kp·wn·kiFac  (0.1 por defecto)
  // ganancias manuales (SI: N/rad, N/m...)
  float kp[2], ki[2], kd[2];
  uint8_t fuente[2];   // 1 = modelo (auto), 0 = manual
  // límites y seguridad
  float maxGas;    // % máximo por motor
  float gasMin;    // % mínimo con el control activo (que no se paren)
  float limAng;    // grados, máximo ángulo que se puede pedir
  float corte;     // grados, si se pasa -> aterrizaje suave
  float vmin;      // V, batería baja -> aterrizaje suave
  float vdiv;      // factor del divisor de batería (100k/27k -> 4.704)
  float tAter;     // s, duración del aterrizaje suave
  float zVel;      // cm/s, rapidez máxima del setpoint de altura
  // orientación del BNO055 ("imuauto" la llena)
  int8_t eulEje;   // 1 = roll del BNO, 2 = pitch del BNO
  int8_t imuSigno; // +1/-1 para que motor 1 arriba = positivo
  int8_t giroEje;  // 0,1,2 = X,Y,Z
  int8_t giroSigno;
  int8_t accEje;   // eje vertical del acelerómetro (0,1,2) o -1 = no usar
  int8_t accSigno;
  int8_t mezSigno; // +1/-1 por si los motores están al revés
  float rollCero;  // grados crudos del BNO con el brazo nivelado
  float zCero;     // m, lectura del ToF en el piso
};

Params P;

void paramsDefault() {
  P.version = PARAMS_VERSION;
  P.masa = 0.100f; P.J = 1.1e-4f; P.L = 0.065f; P.u0 = 50.0f; P.v0 = 0.0f;
  P.zeta[ROLL] = 1.0f; P.wn[ROLL] = 8.0f;  P.kiFac[ROLL] = 0.1f;
  P.zeta[ALT]  = 1.0f; P.wn[ALT]  = 3.0f;  P.kiFac[ALT]  = 0.1f;
  P.kp[ROLL] = 0.05f; P.ki[ROLL] = 0.04f; P.kd[ROLL] = 0.013f;
  P.kp[ALT]  = 0.9f;  P.ki[ALT]  = 0.27f; P.kd[ALT]  = 0.6f;
  P.fuente[ROLL] = 1; P.fuente[ALT] = 1;
  P.maxGas = 70; P.gasMin = 4; P.limAng = 20; P.corte = 35;
  P.vmin = 7.0f; P.vdiv = 127.0f / 27.0f; P.tAter = 1.5f; P.zVel = 10;
  P.eulEje = 1; P.imuSigno = 1; P.giroEje = 1; P.giroSigno = 1;
  P.accEje = 2; P.accSigno = 1; P.mezSigno = 1;
  P.rollCero = 0; P.zCero = 0;
}

Preferences prefs;
void paramsCargar() {
  paramsDefault();
  prefs.begin("mdron", true);
  Params tmp;
  size_t n = prefs.getBytes("p", &tmp, sizeof(tmp));
  prefs.end();
  if (n == sizeof(tmp) && tmp.version == PARAMS_VERSION) P = tmp;
}
void paramsGuardar() {
  prefs.begin("mdron", false);
  prefs.putBytes("p", &P, sizeof(P));
  prefs.end();
}

// =====================================================================
// Estado
// =====================================================================
enum Modo { MANUAL = 0, MODO_ROLL = 1, AUTO = 2 };
const char *NOMBRE_MODO[] = {"manual", "roll", "auto"};
Modo modo = MANUAL;

bool armado = false, aterrizando = false;
uint32_t tAterInicio = 0;
float Fater = 0;                   // empuje total al empezar a aterrizar

float manualObj[2] = {0, 0};       // % pedidos en modo manual
float gasBase = 0;                 // % base en modo roll
float uOut[2] = {0, 0};            // % que se manda a cada motor
float Fcmd = 0;                    // N, empuje total actual

float rObj = 0, rSp = 0;           // grados: objetivo y setpoint con rampa
float zObj = 0, zSp = 0;           // m
float rollI = 0, altI = 0;         // integrales (N)
bool volando = false;              // AUTO: ya se despegó del piso
float Fdesp = 0, FFhover = 0;      // N: empuje durante el despegue / hover aprendido

// ganancias activas y del modelo
float kAct[2][3], kMod[2][3];      // [eje][kp,ki,kd]

// sensores
uint8_t bnoAddr = 0x28;
bool imuOk = false;
uint32_t tImuOk = 0;
float eul[3], gyr[3], lin[3];      // crudos del BNO (grados, °/s, m/s²)
float roll = 0, gyro = 0;          // grados, °/s (ya con signo y cero)
float accV = 0;                    // m/s² vertical (sin gravedad)

volatile uint32_t tofSube = 0, tofAncho = 0, tofCuenta = 0;
uint32_t tofCuentaPrev = 0, tTofOk = 0;
bool tofOk = false;
float zTof = 0;                    // m (ya con zCero)
float zEst = 0, vEst = 0;          // observador ToF + acelerómetro

float vbat = 0;
uint32_t tVbatBaja = 0;
bool vbatBaja = false;

// comunicación
String linea;
bool hbActivo = false;
uint32_t tHb = 0;
float telHz = 0;
uint32_t tTel = 0, tCtrl = 0, tDshot = 0;

rmt_data_t frame[16];

// =====================================================================
// Utilidades
// =====================================================================
float lim(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
void msg(const char *fmt, ...) {
  char buf[200];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  Serial.print("M,"); Serial.println(buf);
}

// =====================================================================
// DShot (igual que v0)
// =====================================================================
uint16_t pctADshot(float pct) {
  if (pct <= 0.3f) return 0;
  return 48 + (uint16_t)((pct / 100.0f) * 1999.0f);
}
void enviarDshot(int pin, uint16_t valor) {
  uint16_t p = (valor << 1);
  uint16_t crc = (p ^ (p >> 4) ^ (p >> 8)) & 0x0F;
  p = (p << 4) | crc;
  for (int i = 0; i < 16; i++) {
    uint16_t alto = (p & (0x8000 >> i)) ? T1_H : T0_H;
    frame[i].level0 = 1; frame[i].duration0 = alto;
    frame[i].level1 = 0; frame[i].duration1 = T_BIT - alto;
  }
  rmtWrite(pin, frame, 16, RMT_WAIT_FOR_EVER);
}
void enviarAmbos(float u1, float u2) {
  enviarDshot(PIN_ESC[0], pctADshot(u1));
  enviarDshot(PIN_ESC[1], pctADshot(u2));
}

// =====================================================================
// BNO055 (directo por registros, modo IMU = giroscopio + acelerómetro,
// sin magnetómetro porque cerca de los motores se vuelve loco)
// =====================================================================
bool bnoW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(bnoAddr); Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
bool bnoR(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(bnoAddr); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)bnoAddr, (int)n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
int16_t le16(uint8_t *b) { return (int16_t)(b[0] | (b[1] << 8)); }

bool bnoBuscar() {
  const uint8_t dirs[2] = {0x28, 0x29};
  for (int intento = 0; intento < 20; intento++) {
    for (int k = 0; k < 2; k++) {
      bnoAddr = dirs[k];
      uint8_t id = 0;
      if (bnoR(0x00, &id, 1) && id == 0xA0) return true;
    }
    delay(50);
  }
  return false;
}

bool bnoIniciar() {
  if (!bnoBuscar()) return false;
  bnoW(0x3D, 0x00); delay(25);          // OPR_MODE = CONFIG
  bnoW(0x3F, 0x20); delay(700);         // reset
  if (!bnoBuscar()) return false;
  bnoW(0x3E, 0x00); delay(10);          // PWR_MODE normal
  bnoW(0x07, 0x00);                     // PAGE 0
  bnoW(0x3B, 0x00);                     // UNIT_SEL: m/s², °/s, grados
  bnoW(0x3F, 0x00); delay(10);          // oscilador interno
  bnoW(0x3D, 0x08); delay(25);          // OPR_MODE = IMU (fusión sin magnetómetro)
  return true;
}

bool bnoLeer() {
  uint8_t b[12];
  if (!bnoR(0x14, b, 12)) return false;           // giro XYZ + euler H,R,P
  for (int i = 0; i < 3; i++) gyr[i] = le16(b + 2 * i) / 16.0f;
  for (int i = 0; i < 3; i++) eul[i] = le16(b + 6 + 2 * i) / 16.0f;
  uint8_t a[6];
  if (!bnoR(0x28, a, 6)) return false;            // aceleración lineal XYZ
  for (int i = 0; i < 3; i++) lin[i] = le16(a + 2 * i) / 100.0f;
  return true;
}

// =====================================================================
// ToF Pololu #4071: d(mm) = (t_us - 1000) * 2 ; ~2000 us = nada detectado
// =====================================================================
void IRAM_ATTR isrTof() {
  uint32_t t = micros();
  if (digitalRead(PIN_TOF)) tofSube = t;
  else { tofAncho = t - tofSube; tofCuenta = tofCuenta + 1; }
}

// =====================================================================
// Modelo: empuje <-> throttle   (T = a·u², con compensación de batería)
// =====================================================================
float T0() { return P.masa * G / 2.0f; }
float compBat() { return (P.v0 > 5 && vbat > 5) ? P.v0 / vbat : 1.0f; }
float aEmpuje() { float u = P.u0 / 100.0f; return T0() / (u * u); }
float gasDeEmpuje(float T) {            // N -> %
  if (T <= 0) return 0;
  return 100.0f * compBat() * sqrtf(T / aEmpuje());
}
float empujeDeGas(float g) {            // % -> N
  float u = (g / 100.0f) / compBat();
  return aEmpuje() * u * u;
}

// Método A: asignación de polos sobre b/s²
void calcGanancias() {
  float b[2] = {2.0f * P.L / P.J, 1.0f / P.masa};
  for (int e = 0; e < 2; e++) {
    float wn = P.wn[e], z = P.zeta[e];
    kMod[e][0] = wn * wn / b[e];
    kMod[e][1] = kMod[e][0] * wn * P.kiFac[e];
    kMod[e][2] = 2.0f * z * wn / b[e];
    if (P.fuente[e]) { for (int k = 0; k < 3; k++) kAct[e][k] = kMod[e][k]; }
    else { kAct[e][0] = P.kp[e]; kAct[e][1] = P.ki[e]; kAct[e][2] = P.kd[e]; }
  }
}

// =====================================================================
// Sensores
// =====================================================================
void leerSensores(float dt) {
  if (bnoLeer()) {
    imuOk = true; tImuOk = millis();
    float e = eul[P.eulEje];
    roll = P.imuSigno * (e - P.rollCero);
    gyro = P.imuSigno * P.giroSigno * gyr[P.giroEje];
    accV = (P.accEje >= 0) ? P.accSigno * lin[P.accEje] : 0;
  } else if (millis() - tImuOk > 100) imuOk = false;

  // ToF
  uint32_t c, w;
  noInterrupts(); c = tofCuenta; w = tofAncho; interrupts();
  if (c != tofCuentaPrev) {
    tofCuentaPrev = c;
    if (w > 1000 && w < 1900) {
      zTof = (w - 1000) * 2.0f / 1000.0f - P.zCero;
      tofOk = true; tTofOk = millis();
    }
  }
  if (millis() - tTofOk > 150) tofOk = false;

  // observador de altura: predice con el acelerómetro, corrige con el ToF
  const float wo = 10.0f, L1 = 2 * 0.8f * wo, L2 = wo * wo;
  float err = tofOk ? (zTof - zEst) : 0;
  zEst += (vEst + L1 * err) * dt;
  vEst += (accV + L2 * err) * dt;

  // batería
  if (PIN_VBAT >= 0) {
    float v = analogReadMilliVolts(PIN_VBAT) / 1000.0f * P.vdiv;
    vbat = (vbat < 1) ? v : vbat + 0.05f * (v - vbat);
  }
}

// =====================================================================
// Armar / desarmar / aterrizar
// =====================================================================
void resetInt() { rollI = 0; altI = 0; }

void paro(const char *motivo) {
  manualObj[0] = manualObj[1] = 0; uOut[0] = uOut[1] = 0;
  gasBase = 0; aterrizando = false; resetInt();
  msg("PARO %s", motivo);
}
void desarmar(const char *motivo) {
  paro(motivo);
  if (armado) { enviarAmbos(0, 0); enviarAmbos(0, 0); }
  armado = false;
  if (PIN_LED >= 0) digitalWrite(PIN_LED, LOW);
  msg("DESARMADO");
}
void aterrizar(const char *motivo) {
  if (!armado || aterrizando) return;
  if (modo == MANUAL) { desarmar(motivo); return; }
  aterrizando = true; tAterInicio = millis(); Fater = Fcmd; rObj = 0;
  msg("ATERRIZANDO (%s)", motivo);
}
void armar() {
  if (armado) { msg("ya armado"); return; }
  if (!imuOk && modo != MANUAL) { msg("ERR IMU no responde, no armo"); return; }
  paro("armando");
  msg("ARMANDO 3 s");
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) { enviarAmbos(0, 0); delay(1); }
  armado = true; tHb = millis();
  zObj = 0; zSp = zEst; rObj = 0; rSp = roll;
  volando = false; Fdesp = 0.6f * P.masa * G; FFhover = P.masa * G;
  if (PIN_LED >= 0) digitalWrite(PIN_LED, HIGH);
  msg("ARMADO modo %s", NOMBRE_MODO[modo]);
}

// =====================================================================
// Control
// =====================================================================
void rampa(float &x, float obj, float vel, float dt) {
  float d = obj - x, m = vel * dt;
  x += (d > m) ? m : (d < -m ? -m : d);
}

void control(float dt) {
  calcGanancias();
  if (!armado) { uOut[0] = uOut[1] = 0; resetInt(); Fcmd = 0; return; }

  if (modo == MANUAL) {                         // directo, sube con rampa
    for (int i = 0; i < 2; i++) {
      float obj = lim(manualObj[i], 0, P.maxGas);
      if (uOut[i] < obj) uOut[i] = min(obj, uOut[i] + 150.0f * dt);
      else uOut[i] = obj;
    }
    Fcmd = empujeDeGas(uOut[0]) + empujeDeGas(uOut[1]);
    return;
  }

  float th = roll * D2R, w = gyro * D2R;
  float idle = P.gasMin;
  float F;

  // ---- empuje total ----
  if (aterrizando) {
    float s = (millis() - tAterInicio) / (P.tAter * 1000.0f);
    if (s >= 1) { desarmar("aterrizaje terminado"); return; }
    F = Fater * (1.0f - 0.6f * s);
  } else if (modo == MODO_ROLL) {
    if (gasBase < P.gasMin + 1) { uOut[0] = uOut[1] = idle; resetInt(); Fcmd = 0; return; }
    F = 2.0f * empujeDeGas(gasBase);
  } else {                                       // AUTO: PID de altura
    float mg = P.masa * G;
    if (!volando) {
      // en el piso: espera a que pidan altura; luego sube el empuje despacio
      // hasta que el carrito se despega y ahí APRENDE el empuje de hover real
      if (zObj <= 0.005f) { uOut[0] = uOut[1] = idle; resetInt(); Fdesp = 0.6f * mg; Fcmd = 0; return; }
      Fdesp += 0.2f * mg * dt;
      if (zEst > 0.01f || vEst > 0.03f) {
        volando = true; FFhover = 0.93f * Fdesp; zSp = zEst; altI = 0;
        msg("DESPEGUE: hover aprendido %.3f N = gas %.1f %% (u0 configurado %.1f %%)",
            FFhover, gasDeEmpuje(FFhover / 2), P.u0);
      } else if (Fdesp > 1.6f * mg) {
        aterrizar("no despega: revisa masa, u0 o bateria");
        return;
      }
      F = Fdesp / max(cosf(th), 0.7f);
    }
    if (volando) {
      rampa(zSp, zObj, P.zVel / 100.0f, dt);
      if (zObj <= 0.005f && zSp <= 0.005f && zEst < 0.01f) {   // ya aterrizó
        volando = false; Fdesp = 0.6f * mg;
        uOut[0] = uOut[1] = idle; resetInt(); Fcmd = 0; return;
      }
      float e = zSp - zEst;
      altI = lim(altI + kAct[ALT][1] * e * dt, -0.5f * mg, 0.5f * mg);
      float dF = kAct[ALT][0] * e + altI - kAct[ALT][2] * vEst;
      dF = lim(dF, -0.6f * mg, 0.8f * mg);
      F = (FFhover + dF) / max(cosf(th), 0.7f);
    }
  }

  // ---- PID de roll (D con el giroscopio) ----
  rampa(rSp, lim(rObj, -P.limAng, P.limAng), 60.0f, dt);
  float T0m = F / 2.0f;
  float eR = rSp * D2R - th;
  rollI = lim(rollI + kAct[ROLL][1] * eR * dt, -0.2f * T0(), 0.2f * T0());
  float dT = kAct[ROLL][0] * eR + rollI - kAct[ROLL][2] * w;
  dT = lim(dT, -0.8f * T0m, 0.8f * T0m);

  float T1 = T0m + P.mezSigno * dT, T2 = T0m - P.mezSigno * dT;
  uOut[0] = lim(gasDeEmpuje(T1), idle, P.maxGas);
  uOut[1] = lim(gasDeEmpuje(T2), idle, P.maxGas);
  Fcmd = F;
}

// =====================================================================
// Seguridad
// =====================================================================
void seguridad() {
  if (!armado) return;
  if (modo != MANUAL && !imuOk) { desarmar("sin IMU"); return; }
  if (modo != MANUAL && fabsf(roll) > P.corte) aterrizar("angulo > corte");
  if (modo == AUTO && !tofOk && !aterrizando && Fcmd > 0) aterrizar("sin ToF");
  if (PIN_VBAT >= 0 && vbat > 5 && vbat < P.vmin) {   // >5 V: solo si de verdad hay batería conectada
    if (!vbatBaja) { vbatBaja = true; tVbatBaja = millis(); }
    else if (millis() - tVbatBaja > 1000) aterrizar("bateria baja");
  } else vbatBaja = false;
  if (hbActivo && millis() - tHb > 1000) {
    hbActivo = false;
    aterrizar("sin latido de la pagina");
  }
}

// =====================================================================
// Detección automática de ejes del BNO055 (con motores apagados)
// =====================================================================
void imuAuto() {
  if (armado) { msg("ERR desarma primero"); return; }
  uint8_t b[6];
  float g[3] = {0, 0, 0}, base[3] = {0, 0, 0};
  int n = 0;
  msg("IMUAUTO 1/3: deja el brazo NIVELADO y quieto (2 s)");
  uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    if (bnoLeer() && bnoR(0x2E, b, 6)) {          // vector de gravedad
      for (int i = 0; i < 3; i++) { g[i] += le16(b + 2 * i) / 100.0f; base[i] += eul[i]; }
      n++;
    }
    delay(10);
  }
  if (n < 20) { msg("ERR IMU no responde"); return; }
  for (int i = 0; i < 3; i++) { g[i] /= n; base[i] /= n; }
  int ax = 0;
  for (int i = 1; i < 3; i++) if (fabsf(g[i]) > fabsf(g[ax])) ax = i;
  int8_t accEje = ax, accSigno = g[ax] > 0 ? 1 : -1;

  msg("IMUAUTO 2/3: MUEVE el brazo de lado a lado, sin parar (4 s)");
  delay(300);
  float s1[3] = {0, 0, 0}, s2[3] = {0, 0, 0}, corr[2][3] = {{0}};
  float prev[3]; bool hayPrev = false; n = 0;
  t0 = millis();
  uint32_t tp = micros();
  while (millis() - t0 < 4000) {
    if (bnoLeer()) {
      uint32_t t = micros(); float dt = (t - tp) * 1e-6f; tp = t;
      for (int i = 1; i < 3; i++) { s1[i] += eul[i]; s2[i] += eul[i] * eul[i]; }
      if (hayPrev && dt > 0.001f) {
        for (int e = 1; e < 3; e++) {
          float de = (eul[e] - prev[e]) / dt;
          for (int k = 0; k < 3; k++) corr[e - 1][k] += gyr[k] * de;
        }
      }
      for (int i = 0; i < 3; i++) prev[i] = eul[i];
      hayPrev = true; n++;
    }
    delay(10);
  }
  float var1 = s2[1] / n - sq(s1[1] / n), var2 = s2[2] / n - sq(s1[2] / n);
  int8_t eulEje = (var1 >= var2) ? 1 : 2;
  float amp = sqrtf(max(var1, var2));
  if (amp < 5) { msg("ERR casi no se movio (%.1f grados). Repite imuauto", amp); return; }
  int gk = 0;
  for (int k = 1; k < 3; k++) if (fabsf(corr[eulEje - 1][k]) > fabsf(corr[eulEje - 1][gk])) gk = k;
  int8_t giroEje = gk, giroSigno = corr[eulEje - 1][gk] > 0 ? 1 : -1;

  msg("IMUAUTO 3/3: sostén el lado del MOTOR 1 ARRIBA, quieto (3 s)");
  delay(1500);
  float m = 0; n = 0; t0 = millis();
  while (millis() - t0 < 1500) { if (bnoLeer()) { m += eul[eulEje]; n++; } delay(10); }
  m = m / max(n, 1) - base[eulEje];
  if (fabsf(m) < 5) { msg("ERR no detecte inclinacion del motor 1 (%.1f). Repite imuauto", m); return; }

  P.accEje = accEje; P.accSigno = accSigno;
  P.eulEje = eulEje; P.giroEje = giroEje; P.giroSigno = giroSigno;
  P.imuSigno = m > 0 ? 1 : -1;
  P.rollCero = base[eulEje];
  msg("IMUAUTO listo: euler=%s giro=%c%c vertical=%c%c motor1arriba=%+d cero=%.1f",
      eulEje == 1 ? "roll" : "pitch", giroSigno > 0 ? '+' : '-', "XYZ"[giroEje],
      accSigno > 0 ? '+' : '-', "XYZ"[accEje], P.imuSigno, P.rollCero);
  msg("Revisa con la telemetria y escribe 'guardar' si esta bien");
}

// =====================================================================
// Mensajes de parámetros / telemetría
// =====================================================================
void imprimirParams() {
  calcGanancias();
  Serial.printf("P,masa,%.4f\nP,J,%.6g\nP,L,%.4f\nP,u0,%.1f\nP,v0,%.2f\n", P.masa, P.J, P.L, P.u0, P.v0);
  Serial.printf("P,zeta_roll,%.2f\nP,wn_roll,%.2f\nP,zeta_alt,%.2f\nP,wn_alt,%.2f\n",
                P.zeta[ROLL], P.wn[ROLL], P.zeta[ALT], P.wn[ALT]);
  Serial.printf("P,kifac_roll,%.3f\nP,kifac_alt,%.3f\n", P.kiFac[ROLL], P.kiFac[ALT]);
  Serial.printf("P,kp_roll,%.5g\nP,ki_roll,%.5g\nP,kd_roll,%.5g\n", P.kp[ROLL], P.ki[ROLL], P.kd[ROLL]);
  Serial.printf("P,kp_alt,%.5g\nP,ki_alt,%.5g\nP,kd_alt,%.5g\n", P.kp[ALT], P.ki[ALT], P.kd[ALT]);
  Serial.printf("P,pid_roll,%s\nP,pid_alt,%s\n", P.fuente[ROLL] ? "auto" : "manual", P.fuente[ALT] ? "auto" : "manual");
  Serial.printf("P,max,%.0f\nP,gasmin,%.1f\nP,limang,%.0f\nP,corte,%.0f\nP,vmin,%.2f\nP,vdiv,%.3f\nP,tater,%.1f\nP,zvel,%.0f\n",
                P.maxGas, P.gasMin, P.limAng, P.corte, P.vmin, P.vdiv, P.tAter, P.zVel);
  Serial.printf("P,imu,euler=%s giro=%c%c vertical=%s signo=%+d mez=%+d cero=%.1f\n",
                P.eulEje == 1 ? "roll" : "pitch", P.giroSigno > 0 ? '+' : '-', "XYZ"[P.giroEje],
                P.accEje < 0 ? "off" : (P.accSigno > 0 ? (P.accEje == 0 ? "+X" : P.accEje == 1 ? "+Y" : "+Z")
                                                        : (P.accEje == 0 ? "-X" : P.accEje == 1 ? "-Y" : "-Z")),
                P.imuSigno, P.mezSigno, P.rollCero);
  Serial.printf("P,zcero,%.3f\n", P.zCero);
  // ganancias: G,eje,kp,ki,kd (activas)  GM,eje,kp,ki,kd (modelo)
  for (int e = 0; e < 2; e++) {
    const char *n = e == ROLL ? "roll" : "alt";
    Serial.printf("G,%s,%.5g,%.5g,%.5g\n", n, kAct[e][0], kAct[e][1], kAct[e][2]);
    Serial.printf("GM,%s,%.5g,%.5g,%.5g\n", n, kMod[e][0], kMod[e][1], kMod[e][2]);
  }
  Serial.printf("M,T0 por motor = %.3f N, hover a %.1f %%\n", T0(), P.u0);
}

void telemetria() {
  if (telHz <= 0) return;
  uint32_t per = (uint32_t)(1000.0f / telHz);
  if (millis() - tTel < per) return;
  tTel = millis();
  int flags = (imuOk ? 1 : 0) | (tofOk ? 2 : 0) | (aterrizando ? 4 : 0) | (hbActivo ? 8 : 0) | (vbatBaja ? 16 : 0);
  // T,t,modo,armado,roll,gyro,rSp,z,v,zSp,zTof,u1,u2,vbat,flags
  Serial.printf("T,%lu,%d,%d,%.2f,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%.2f,%d\n",
                (unsigned long)millis(), (int)modo, armado ? 1 : 0, roll, gyro, rSp,
                zEst * 100, vEst * 100, zSp * 100, zTof * 100, uOut[0], uOut[1], vbat, flags);
}

// =====================================================================
// Comandos
// =====================================================================
const char *AYUDA =
  "M,--- comandos (separar con espacios) ---\n"
  "M,armar | desarmar | s (paro inmediato) | aterrizar\n"
  "M,modo manual|roll|auto\n"
  "M,gas P        manual: ambos motores a P %  | roll: throttle base\n"
  "M,mot A B      manual: motor 1 = A %, motor 2 = B %\n"
  "M,r G          roll deseado en grados (roll/auto)\n"
  "M,z CM         altura deseada en cm (auto)\n"
  "M,masa KG | j VAL | l M | u0 P | v0 V | u0set (u0 y v0 = gas y bateria actuales)\n"
  "M,zeta roll|alt V | wn roll|alt V | kifac roll|alt V\n"
  "M,pid roll|alt auto|manual | kp/ki/kd roll|alt V | copiar roll|alt\n"
  "M,max P | gasmin P | limang G | corte G | vmin V | vdiv F | tater S | zvel CM/S\n"
  "M,imuauto | cero | zcero | imu | mezsigno 1|-1 | acc on|off\n"
  "M,tel HZ | ver | guardar | fabrica";

int eje(const String &s) { return s == "roll" ? ROLL : (s == "alt" ? ALT : -1); }

void procesar(String c) {
  c.trim(); c.toLowerCase();
  if (c.length() == 0) return;
  String t[4]; int nt = 0, ini = 0;
  while (nt < 4 && ini < (int)c.length()) {
    int sp = c.indexOf(' ', ini);
    if (sp < 0) sp = c.length();
    if (sp > ini) t[nt++] = c.substring(ini, sp);
    ini = sp + 1;
  }
  String &k = t[0];
  float v1 = nt > 1 ? t[1].toFloat() : 0, v2 = nt > 2 ? t[2].toFloat() : 0;

  if (k == "hb") { hbActivo = true; tHb = millis(); return; }
  if (k == "s" || k == "stop") {           // en roll/auto el control volvería a encender: desarmar
    if (!armado) return;
    if (modo == MANUAL) paro("comando"); else desarmar("PARO");
    return;
  }
  if (k == "desarmar") { desarmar("comando"); return; }
  if (k == "armar") { armar(); return; }
  if (k == "aterrizar") { aterrizar("comando"); return; }
  if (k == "ayuda" || k == "help") { Serial.println(AYUDA); return; }
  if (k == "ver") { imprimirParams(); return; }
  if (k == "tel") { telHz = lim(v1, 0, 100); return; }

  if (k == "modo" && nt > 1) {
    int ni = t[1] == "manual" ? MANUAL : (t[1] == "roll" ? MODO_ROLL : (t[1] == "auto" ? AUTO : -1));
    if (ni < 0) { msg("ERR modo manual|roll|auto"); return; }
    Modo n = (Modo)ni;
    if (armado && n != modo) {
      // cambio en caliente: sin brincos
      if (n == MODO_ROLL) gasBase = (uOut[0] + uOut[1]) / 2;
      if (n == AUTO) {
        zSp = zEst; zObj = zEst;
        volando = zEst > 0.01f;                 // si ya flotaba con gas fijo, sigue de ahí
        FFhover = volando ? Fcmd : P.masa * G;
        Fdesp = 0.6f * P.masa * G;
      }
      if (n == MANUAL) { manualObj[0] = uOut[0]; manualObj[1] = uOut[1]; }
      resetInt(); rSp = roll; rObj = 0;
    }
    modo = n; msg("MODO %s", NOMBRE_MODO[modo]); return;
  }
  if (k == "gas" && nt > 1) {
    float g = lim(v1, 0, P.maxGas);
    if (modo == MANUAL) { manualObj[0] = manualObj[1] = g; }
    else gasBase = g;
    msg("gas %.1f", g); return;
  }
  if (k == "mot" && nt > 2) {
    if (modo != MANUAL) { msg("ERR mot solo en modo manual"); return; }
    manualObj[0] = lim(v1, 0, P.maxGas); manualObj[1] = lim(v2, 0, P.maxGas); return;
  }
  if (k == "r" && nt > 1) { rObj = lim(v1, -P.limAng, P.limAng); return; }
  if (k == "z" && nt > 1) { zObj = lim(v1, 0, 30) / 100.0f; return; }

  // modelo físico
  if (k == "masa" && nt > 1 && v1 > 0.01f) { P.masa = v1; msg("masa %.4f kg", P.masa); return; }
  if (k == "j" && nt > 1 && v1 > 0) { P.J = v1; msg("J %.6g kg m2", P.J); return; }
  if (k == "l" && nt > 1 && v1 > 0.005f) { P.L = v1; msg("L %.4f m", P.L); return; }
  if (k == "u0" && nt > 1 && v1 > 5 && v1 < 100) { P.u0 = v1; msg("u0 %.1f %%", P.u0); return; }
  if (k == "v0" && nt > 1) { P.v0 = v1; msg("v0 %.2f V", P.v0); return; }
  if (k == "u0set") {
    float g = (uOut[0] + uOut[1]) / 2;
    if (g < 5) { msg("ERR los motores no estan girando"); return; }
    P.u0 = g;                       // throttle de hover...
    P.v0 = vbat > 5 ? vbat : 0;     // ...a este voltaje (referencia de la compensación)
    msg("u0 %.1f %% a %.2f V", P.u0, P.v0); return;
  }

  // diseño y ganancias
  int e = nt > 1 ? eje(t[1]) : -1;
  float v = nt > 2 ? v2 : 0;
  if (k == "zeta" && e >= 0 && nt > 2 && v > 0) { P.zeta[e] = v; return; }
  if (k == "wn" && e >= 0 && nt > 2 && v > 0) { P.wn[e] = v; return; }
  if (k == "kifac" && e >= 0 && nt > 2 && v >= 0) { P.kiFac[e] = v; return; }
  if (k == "kp" && e >= 0 && nt > 2) { P.kp[e] = v; return; }
  if (k == "ki" && e >= 0 && nt > 2) { P.ki[e] = v; return; }
  if (k == "kd" && e >= 0 && nt > 2) { P.kd[e] = v; return; }
  if (k == "pid" && e >= 0 && nt > 2) {
    P.fuente[e] = (t[2] == "auto") ? 1 : 0;
    if (armado) { if (e == ROLL) rollI = 0; else altI = 0; }
    msg("pid %s %s", t[1].c_str(), P.fuente[e] ? "auto (modelo)" : "manual"); return;
  }
  if (k == "copiar" && e >= 0) {
    calcGanancias();
    P.kp[e] = kMod[e][0]; P.ki[e] = kMod[e][1]; P.kd[e] = kMod[e][2];
    msg("ganancias del modelo copiadas a manual (%s)", t[1].c_str()); return;
  }

  // límites
  if (k == "max" && nt > 1) { P.maxGas = lim(v1, 5, 100); return; }
  if (k == "gasmin" && nt > 1) { P.gasMin = lim(v1, 0, 20); return; }
  if (k == "limang" && nt > 1) { P.limAng = lim(v1, 0, 35); return; }
  if (k == "corte" && nt > 1) { P.corte = lim(v1, 5, 60); return; }
  if (k == "vmin" && nt > 1) { P.vmin = v1; return; }
  if (k == "vdiv" && nt > 1) { P.vdiv = v1; return; }
  if (k == "tater" && nt > 1) { P.tAter = lim(v1, 0.3f, 5); return; }
  if (k == "zvel" && nt > 1) { P.zVel = lim(v1, 1, 50); return; }

  // sensores
  if (k == "imuauto") { imuAuto(); return; }
  if (k == "cero") { if (armado) { msg("ERR desarma primero"); return; }
    P.rollCero = eul[P.eulEje]; msg("roll cero = %.1f", P.rollCero); return; }
  if (k == "zcero") { if (armado) { msg("ERR desarma primero"); return; }
    P.zCero += zTof; zEst = 0; vEst = 0; msg("altura cero, zcero = %.3f m", P.zCero); return; }
  if (k == "imu") {
    msg("eul H%.1f R%.1f P%.1f | giro X%.1f Y%.1f Z%.1f | lin X%.2f Y%.2f Z%.2f | tof %.1f cm ok=%d",
        eul[0], eul[1], eul[2], gyr[0], gyr[1], gyr[2], lin[0], lin[1], lin[2], zTof * 100, tofOk);
    return;
  }
  if (k == "mezsigno" && nt > 1) { if (armado) { msg("ERR desarma primero"); return; }
    P.mezSigno = v1 < 0 ? -1 : 1; return; }
  if (k == "acc" && nt > 1) { if (t[1] == "off") P.accEje = -1; else if (P.accEje < 0) P.accEje = 2; return; }

  if (k == "guardar") { paramsGuardar(); msg("guardado"); return; }
  if (k == "fabrica") { if (armado) { msg("ERR desarma primero"); return; }
    paramsDefault(); msg("valores de fabrica (escribe 'guardar' para conservarlos)"); return; }

  msg("ERR no entiendo '%s'. Escribe ayuda", c.c_str());
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  paramsCargar();

  if (PIN_LED >= 0) { pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW); }
  for (int i = 0; i < 2; i++) {
    if (!rmtInit(PIN_ESC[i], RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_HZ))
      msg("ERR RMT pin %d", PIN_ESC[i]);
  }
  pinMode(PIN_TOF, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_TOF), isrTof, CHANGE);
  if (PIN_VBAT >= 0) analogSetPinAttenuation(PIN_VBAT, ADC_11db);

  Wire.begin(PIN_SDA, PIN_SCL, 100000);   // 100 kHz: el BNO055 estira el reloj
  Wire.setTimeOut(50);
  if (bnoIniciar()) msg("BNO055 OK en 0x%02X", bnoAddr);
  else msg("ERR BNO055 no encontrado (SDA=%d SCL=%d)", PIN_SDA, PIN_SCL);

  calcGanancias();
  msg("LISTO medio_dron_control. ESC1=%d ESC2=%d ToF=%d VBAT=%d. Escribe ayuda",
      PIN_ESC[0], PIN_ESC[1], PIN_TOF, PIN_VBAT);
  tCtrl = micros();
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { procesar(linea); linea = ""; }
    else if (linea.length() < 80) linea += ch;
  }

  uint32_t ahora = micros();
  if (ahora - tCtrl >= CTRL_US) {
    float dt = (ahora - tCtrl) * 1e-6f;
    tCtrl = ahora;
    if (dt > 0.05f) dt = 0.05f;
    leerSensores(dt);
    control(dt);
    seguridad();
  }

  if (armado && micros() - tDshot >= 1000) {
    tDshot = micros();
    enviarAmbos(uOut[0], uOut[1]);
  }

  telemetria();
}
