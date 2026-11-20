/*
  medir_hover.ino — Medio dron: medir u0 (gas de hover) por el Monitor Serie
  2 ESC (BLHeli_S / Bluejay) por DShot300 — mismo DShot que v0 (hover_2motores)
  Core "esp32 by Espressif" 3.x. Monitor Serie a 115200, "New Line".

  Comandos:
    50       -> los DOS motores a 50 %  (también acepta decimales: 23.5)
    50 52    -> motor 1 a 50 %, motor 2 a 52 %
    +  / -   -> sube / baja 1 % los dos
    ++ / --  -> sube / baja 0.5 % los dos
    s  o Enter vacío -> PARO (motores a 0, sigue armado)
    max N    -> límite de gas (default 60 %)
  El primer número que mandes arma los ESC solo (3 s de ceros) y luego sube en rampa.

  Cómo medir u0 (carrito en el riel, brazo amarrado horizontal):
    sube de 1 en 1 % hasta que el carrito se despega y se queda flotando.
    Ese % es u0. Anota también el voltaje de la batería en ese momento.
*/

#include <Arduino.h>

// ---------------- cambiar según la placa ----------------
const int PIN_M[2] = {13, 15};      // ESP32 DevKit (v0). En un C3, por ej. {6, 7}
const float MASA_KG = 0.0265;       // todo lo que sube y baja
float MAX_PCT = 60;

// ---------------- DShot300 (RMT a 10 MHz) ----------------
const uint32_t RMT_HZ = 10000000;
const uint16_t T_BIT = 33, T1_H = 25, T0_H = 12;
const float SUBE_PCT_POR_MS = 0.05;   // rampa de subida: 50 %/s; bajar es inmediato

bool armado = false;
float objetivo[2] = {0, 0}, actual[2] = {0, 0};
unsigned long ultimoFrame = 0, ultimaRampa = 0;
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

void enviarAmbos(uint16_t a, uint16_t b) {
  enviarDshot(PIN_M[0], a);
  enviarDshot(PIN_M[1], b);
}

float limitar(float v) { return v < 0 ? 0 : (v > MAX_PCT ? MAX_PCT : v); }

void parar() {
  for (int i = 0; i < 2; i++) objetivo[i] = actual[i] = 0;
  Serial.println("PARO: motores a 0");
}

void armar() {
  Serial.println("Armando ESC (3 s de ceros)...");
  unsigned long t0 = millis();
  while (millis() - t0 < 3000) { enviarAmbos(0, 0); delay(1); }
  armado = true;
  ultimaRampa = millis();
  Serial.println("ARMADO");
}

void ponerGas(float a, float b) {
  if (!armado) armar();
  objetivo[0] = limitar(a);
  objetivo[1] = limitar(b);
  Serial.printf("Gas -> M1 %.1f %%  M2 %.1f %%\n", objetivo[0], objetivo[1]);
}

void imprimirU0() {
  float u = (objetivo[0] + objetivo[1]) / 2;
  if (u < 1) return;
  float T0 = MASA_KG * 9.81f / 2;              // N por motor en hover
  float a = T0 / ((u / 100) * (u / 100));      // T = a*u^2, u en fracción
  Serial.printf("Si aqui flota: u0 = %.1f %%  (T0 = %.3f N = %.1f gf por motor, a = %.3f N)\n",
                u, T0, T0 / 9.81f * 1000, a);
  Serial.printf("  -> en el firmware V1: u0set %.1f   (anota el voltaje de la bateria)\n", u);
}

void procesar(String c) {
  c.trim();
  c.toLowerCase();
  if (c.length() == 0 || c == "s" || c == "stop") { parar(); return; }

  if (c.startsWith("max")) {
    float v = c.substring(3).toFloat();
    if (v >= 5 && v <= 100) { MAX_PCT = v; Serial.printf("MAX %.0f %%\n", MAX_PCT); }
    return;
  }
  if (c == "+" || c == "-" || c == "++" || c == "--") {
    float paso = (c.length() == 2) ? 0.5f : 1.0f;
    if (c[0] == '-') paso = -paso;
    ponerGas(objetivo[0] + paso, objetivo[1] + paso);
    imprimirU0();
    return;
  }
  if (isdigit(c[0]) || c[0] == '.') {
    int sp = c.indexOf(' ');
    float a = c.substring(0, sp < 0 ? c.length() : sp).toFloat();
    float b = (sp < 0) ? a : c.substring(sp + 1).toFloat();
    ponerGas(a, b);
    imprimirU0();
    return;
  }
  Serial.println("? Comandos: 50 | 50 52 | + | - | ++ | -- | s | max N");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  for (int i = 0; i < 2; i++) {
    if (!rmtInit(PIN_M[i], RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_HZ)) {
      Serial.printf("ERR RMT pin %d\n", PIN_M[i]);
      while (true) delay(1000);
    }
  }
  Serial.printf("\nmedir_hover listo. M1 = GPIO%d, M2 = GPIO%d, masa %.1f g, max %.0f %%\n",
                PIN_M[0], PIN_M[1], MASA_KG * 1000, MAX_PCT);
  Serial.println("Escribe un % (ej. 20). Enter vacio o 's' = PARO.");
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') { procesar(linea); linea = ""; }
    else if (ch != '\r' && linea.length() < 32) linea += ch;
  }

  unsigned long ahora = millis();
  float dt = ahora - ultimaRampa;
  ultimaRampa = ahora;
  for (int i = 0; i < 2; i++) {
    if (actual[i] < objetivo[i]) actual[i] = min(objetivo[i], actual[i] + SUBE_PCT_POR_MS * dt);
    else actual[i] = objetivo[i];
  }

  if (armado && micros() - ultimoFrame >= 1000) {
    ultimoFrame = micros();
    enviarAmbos(pctADshot(actual[0]), pctADshot(actual[1]));
  }
}
