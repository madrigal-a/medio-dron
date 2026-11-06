/*
  Banco de empuje - ESP32 + ESC (BLHeli_S / Bluejay) por DShot300
  Motor: Happymodel EX1103 11000KV (1S-2S)  ->  FUENTE A 7.4 V MAX, NO 12 V

  Conexión (ESP32 DevKit de 30/38 pines):
    Cable de señal del ESC (blanco)  -> GPIO13 (D13)
    Cable de tierra del ESC (negro)  -> GND (el pin pegado a D13)
    Alimentación del ESC (rojo/negro grueso) -> fuente de laboratorio
    ESP32 -> USB a la compu
  La tierra de la fuente y la del ESP32 quedan unidas a través del ESC.

  Requiere: Arduino IDE con "esp32 by Espressif" versión 3.x

  Comandos en el Monitor Serie (115200 baudios, "Nueva línea"):
    armar        -> manda cero al ESC para armarlo (escucharás pitidos)
    0 - 100      -> acelerador en % (limitado por MAX_PCT)
    s  o  stop   -> paro inmediato (sigue armado)
    desarmar     -> para y deja de mandar señal (hay que volver a "armar")
    barrido      -> sube por escalones y espera en cada uno para leer la báscula
    max N        -> cambia el límite de acelerador (ej. "max 50")
*/

#include <Arduino.h>

// ---------------- configuración ----------------
const int   PIN_ESC   = 13;      // D13, pegado a un GND en el DevKit
int         MAX_PCT   = 35;      // límite de seguridad (fuente de 2 A)
const int   RAMPA_MS  = 8;       // ms por cada 1 % al acelerar (suaviza arranques)
const int   PASO_PCT  = 5;       // escalón del barrido
const int   PASO_MS   = 4000;    // tiempo en cada escalón del barrido

// DShot300 con reloj del RMT a 10 MHz (0.1 us por tick)
const uint32_t RMT_HZ = 10000000;
const uint16_t T_BIT  = 33;      // 3.33 us por bit
const uint16_t T1_H   = 25;      // "1": 75 % en alto
const uint16_t T0_H   = 12;      // "0": 37.5 % en alto

// ---------------- estado ----------------
bool  armado      = false;
float accActual   = 0;     // %
int   accObjetivo = 0;     // %
bool  enBarrido   = false;
int   barridoPct  = 0;
unsigned long barridoT = 0;
unsigned long ultimoFrame = 0, ultimaRampa = 0;
String linea;
bool  modoApp = false;            // se activa al recibir "hb" desde la PC
unsigned long ultimoLatido = 0;
const unsigned long LATIDO_MAX_MS = 1500;

rmt_data_t frame[16];

uint16_t pctADshot(float pct) {
  if (pct <= 0.5f) return 0;                       // 0 = motor parado
  return 48 + (uint16_t)((pct / 100.0f) * 1999.0f); // 48..2047
}

void enviarDshot(uint16_t valor) {
  uint16_t paquete = (valor << 1);                 // bit de telemetría = 0
  uint16_t crc = (paquete ^ (paquete >> 4) ^ (paquete >> 8)) & 0x0F;
  paquete = (paquete << 4) | crc;
  for (int i = 0; i < 16; i++) {
    bool uno = paquete & (0x8000 >> i);
    uint16_t alto = uno ? T1_H : T0_H;
    frame[i].level0 = 1; frame[i].duration0 = alto;
    frame[i].level1 = 0; frame[i].duration1 = T_BIT - alto;
  }
  rmtWrite(PIN_ESC, frame, 16, RMT_WAIT_FOR_EVER);
}

void parar(const char *motivo) {
  accObjetivo = 0; accActual = 0; enBarrido = false;
  Serial.printf(">> PARO (%s)\n", motivo);
}

void ponerAcelerador(int pct) {
  if (!armado) { Serial.println("Primero escribe: armar"); return; }
  if (pct > MAX_PCT) {
    Serial.printf("Limitado a %d %% (cambia con 'max N')\n", MAX_PCT);
    pct = MAX_PCT;
  }
  if (pct < 0) pct = 0;
  accObjetivo = pct;
  Serial.printf("Acelerador -> %d %%\n", pct);
}

void procesar(String c) {
  c.trim(); c.toLowerCase();
  if (c.length() == 0) return;

  // latido del programa de la PC: si deja de llegar, el motor se para solo
  if (c == "hb") { modoApp = true; ultimoLatido = millis(); return; }

  if (c == "s" || c == "stop") { parar("comando"); return; }

  if (c == "desarmar") {
    parar("desarmado");
    armado = false;
    Serial.println("Desarmado. Ya no se manda señal; el ESC se desarma solo.");
    return;
  }

  if (c == "armar") {
    parar("armado");
    Serial.println("Armando: mandando cero 3 s...");
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) { enviarDshot(0); delay(1); }
    armado = true;
    Serial.println("Armado. Escribe un % (ej. 10). 's' para parar.");
    return;
  }

  if (c.startsWith("max")) {
    int v = c.substring(3).toInt();
    if (v >= 5 && v <= 100) { MAX_PCT = v; Serial.printf("Límite = %d %%\n", MAX_PCT); }
    else Serial.println("Uso: max 5..100");
    return;
  }

  if (c == "barrido") {
    if (!armado) { Serial.println("Primero escribe: armar"); return; }
    enBarrido = true; barridoPct = PASO_PCT; barridoT = millis();
    accObjetivo = barridoPct;
    Serial.println("Barrido iniciado. Anota la báscula al final de cada escalón.");
    Serial.printf("Escalón: %d %%\n", barridoPct);
    return;
  }

  bool esNumero = true;
  for (unsigned i = 0; i < c.length(); i++) if (!isDigit(c[i])) esNumero = false;
  if (esNumero) { enBarrido = false; ponerAcelerador(c.toInt()); return; }

  Serial.println("Comandos: armar | 0-100 | s | barrido | max N");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!rmtInit(PIN_ESC, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_HZ)) {
    Serial.println("Error iniciando RMT");
    while (true) delay(1000);
  }
  Serial.println("\n=== Banco de empuje EX1103 ===");
  Serial.println("Fuente a 7.4 V (no 12 V). Báscula en tara. Lentes puestos.");
  Serial.println("Escribe: armar");
}

void loop() {
  // comandos por serie
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { procesar(linea); linea = ""; }
    else linea += ch;
  }

  // seguridad: la PC dejó de mandar latidos (programa cerrado / cable suelto)
  if (modoApp && millis() - ultimoLatido > LATIDO_MAX_MS) {
    modoApp = false;
    if (accObjetivo > 0 || accActual > 0) parar("sin latido de la PC");
  }

  // barrido automático por escalones
  if (enBarrido && millis() - barridoT >= PASO_MS) {
    barridoPct += PASO_PCT;
    if (barridoPct > MAX_PCT) { parar("fin del barrido"); }
    else {
      accObjetivo = barridoPct; barridoT = millis();
      Serial.printf("Escalón: %d %%\n", barridoPct);
    }
  }

  // rampa: sube despacio, baja de inmediato
  if (millis() - ultimaRampa >= (unsigned long)RAMPA_MS) {
    ultimaRampa = millis();
    if (accActual < accObjetivo) accActual += 1;
    if (accActual > accObjetivo) accActual = accObjetivo;
  }

  // el ESC necesita frames continuos (~1 kHz); si dejan de llegar, se desarma
  // desarmado = sin señal (el ESC pierde la señal y se desarma)
  if (armado && micros() - ultimoFrame >= 1000) {
    ultimoFrame = micros();
    enviarDshot(pctADshot(accActual));
  }
}
