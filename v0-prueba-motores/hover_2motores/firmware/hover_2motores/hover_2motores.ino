/*
  Medio dron - 2 motores - ESP32 + 2 ESC (BLHeli_S / Bluejay) por DShot300
  Motores: Happymodel EX1103 11000KV (1S-2S)  ->  FUENTE A 7.4 V MAX, NO 12 V

  Conexión (ESP32 DevKit de 30 pines):
    ESC 1 (motor izquierdo):  señal (blanco) -> D13   | tierra (negro) -> GND junto a D13
    ESC 2 (motor derecho):    señal (blanco) -> D15   | tierra (negro) -> GND junto a D15
    Alimentación de ambos ESC -> fuente de laboratorio (7.4 V)
    ESP32 -> USB a la compu

  Se controla desde hover_2motores.html (Chrome o Edge) o desde el Monitor Serie
  (115200, "New Line"):
    armar            -> arma los dos ESC
    desarmar         -> para y deja de mandar señal
    s                -> paro inmediato (sigue armado)
    m A B            -> motor 1 = A %, motor 2 = B %  (ej. "m 20 22.5")
    max N            -> límite de acelerador en % (default 40)
    hb               -> latido de la página; si deja de llegar 1 s, para todo

  Requiere: Arduino IDE con "esp32 by Espressif" versión 3.x
*/

#include <Arduino.h>

// ---------------- configuración ----------------
const int PIN_M[2]   = {13, 15};
float     MAX_PCT    = 40;          // límite de seguridad
const float SUBE_PCT_POR_MS = 0.15; // rampa de subida (150 %/s); bajar es inmediato
const unsigned long LATIDO_MAX_MS = 1000;

// DShot300 con reloj del RMT a 10 MHz (0.1 us por tick)
const uint32_t RMT_HZ = 10000000;
const uint16_t T_BIT  = 33;
const uint16_t T1_H   = 25;
const uint16_t T0_H   = 12;

// ---------------- estado ----------------
bool  armado = false;
float objetivo[2] = {0, 0};
float actual[2]   = {0, 0};
bool  modoApp = false;
unsigned long ultimoLatido = 0, ultimoFrame = 0, ultimaRampa = 0, ultimoEstado = 0;
String linea;
rmt_data_t frame[16];

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

void enviarAmbos(uint16_t v1, uint16_t v2) {
  enviarDshot(PIN_M[0], v1);
  enviarDshot(PIN_M[1], v2);
}

void parar(const char *motivo) {
  for (int i = 0; i < 2; i++) { objetivo[i] = 0; actual[i] = 0; }
  Serial.printf("PARO %s\n", motivo);
}

float limitar(float v) { return v < 0 ? 0 : (v > MAX_PCT ? MAX_PCT : v); }

void procesar(String c) {
  c.trim(); c.toLowerCase();
  if (c.length() == 0) return;

  if (c == "hb") { modoApp = true; ultimoLatido = millis(); return; }

  if (c == "s" || c == "stop") { parar("comando"); return; }

  if (c == "desarmar") {
    parar("desarmado"); armado = false;
    Serial.println("DESARMADO");
    return;
  }

  if (c == "armar") {
    parar("armando");
    Serial.println("ARMANDO 3 s");
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) { enviarAmbos(0, 0); delay(1); }
    armado = true;
    ultimoLatido = millis();      // no disparar el paro por el tiempo de armado
    Serial.println("ARMADO");
    return;
  }

  if (c.startsWith("max")) {
    float v = c.substring(3).toFloat();
    if (v >= 5 && v <= 100) { MAX_PCT = v; Serial.printf("MAX %.0f\n", MAX_PCT); }
    return;
  }

  if (c.startsWith("m ")) {
    if (!armado) { Serial.println("ERR no armado"); return; }
    int sp = c.indexOf(' ', 2);
    if (sp < 0) { Serial.println("ERR uso: m A B"); return; }
    objetivo[0] = limitar(c.substring(2, sp).toFloat());
    objetivo[1] = limitar(c.substring(sp + 1).toFloat());
    return;
  }

  Serial.println("ERR comando? armar | desarmar | s | m A B | max N");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  for (int i = 0; i < 2; i++) {
    if (!rmtInit(PIN_M[i], RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_HZ)) {
      Serial.printf("ERR RMT pin %d\n", PIN_M[i]);
      while (true) delay(1000);
    }
  }
  Serial.println("LISTO hover_2motores (D13 = M1, D15 = M2). Escribe: armar");
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { procesar(linea); linea = ""; }
    else if (linea.length() < 64) linea += ch;
  }

  // seguridad: la página dejó de mandar latidos
  if (modoApp && millis() - ultimoLatido > LATIDO_MAX_MS) {
    modoApp = false;
    if (armado) { parar("sin latido"); armado = false; Serial.println("DESARMADO"); }
  }

  // rampa: sube limitado, baja inmediato
  unsigned long ahora = millis();
  float dt = ahora - ultimaRampa; ultimaRampa = ahora;
  for (int i = 0; i < 2; i++) {
    if (actual[i] < objetivo[i]) actual[i] = min(objetivo[i], actual[i] + SUBE_PCT_POR_MS * dt);
    else actual[i] = objetivo[i];
  }

  // frames continuos a ~1 kHz solo si está armado
  if (armado && micros() - ultimoFrame >= 1000) {
    ultimoFrame = micros();
    enviarAmbos(pctADshot(actual[0]), pctADshot(actual[1]));
  }

  // estado para la página (5 veces por segundo)
  if (modoApp && millis() - ultimoEstado >= 200) {
    ultimoEstado = millis();
    Serial.printf("EST %d %.1f %.1f %.0f\n", armado ? 1 : 0, actual[0], actual[1], MAX_PCT);
  }
}
