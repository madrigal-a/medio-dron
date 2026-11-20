// prueba_placa.ino — prueba de la placa prestada (ESP32-C3 + BNO055)
// Placa: ESP32C3 Dev Module, USB CDC On Boot = Disabled (Serial por UART0)
// Monitor Serie a 115200.
//
// 1) Revisa que SDA y SCL estén en alto (pull-ups del breakout).
// 2) Escanea TODO el bus I2C (0x08..0x77) en SDA 4 / SCL 5.
// 3) Prueba 0x28 y 0x29 por separado y dice qué pasó en cada una.
// 4) Si encuentra el BNO: autoprueba y ángulos en vivo.
//    Si no: lo vuelve a buscar cada 2 s (sirve para mover el falso contacto).

#include <Wire.h>

const int SDA_PIN = 4, SCL_PIN = 5;
uint8_t BNO = 0;        // 0 = no encontrado
bool bnoListo = false;

const char *errorI2C(uint8_t e) {
  switch (e) {
    case 0: return "responde (ACK)";
    case 2: return "no responde (NACK en la dirección)";
    case 3: return "NACK en el dato";
    case 5: return "timeout (¿SCL pegado o reloj estirado?)";
    default: return "otro error de bus";
  }
}

bool leer(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(BNO);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(BNO, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void escribir(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(BNO);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

void revisarLineas() {
  Wire.end();
  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
  delay(5);
  int sda = digitalRead(SDA_PIN), scl = digitalRead(SCL_PIN);
  Serial.printf("Lineas en reposo: SDA(GPIO%d)=%d  SCL(GPIO%d)=%d  %s\n", SDA_PIN, sda, SCL_PIN, scl,
                (sda && scl) ? "OK (con pull-up)"
                             : "<-- deberian ser 1: sin 3V en el BNO, sin pull-up o linea en corto");
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(1000);   // el BNO055 estira el reloj
}

void escanear() {
  Serial.println("Escaneo del bus:");
  int n = 0;
  for (uint8_t d = 0x08; d < 0x78; d++) {
    Wire.beginTransmission(d);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X%s\n", d, (d == 0x28 || d == 0x29) ? "  <-- BNO055" : "");
      n++;
    }
  }
  if (n == 0) Serial.println("  nada respondio");
}

bool buscarBNO() {
  revisarLineas();
  escanear();
  BNO = 0;
  for (uint8_t dir : {0x28, 0x29}) {
    Wire.beginTransmission(dir);
    uint8_t e = Wire.endTransmission();
    Serial.printf("0x%02X: %s", dir, errorI2C(e));
    if (e == 0) {
      uint8_t id = 0;
      BNO = dir;
      if (leer(0x00, &id, 1)) {
        Serial.printf(", CHIP_ID = 0x%02X %s\n", id, id == 0xA0 ? "(BNO055 OK)" : "(no es 0xA0: no es un BNO055 o esta arrancando)");
        if (id == 0xA0) return true;
      } else {
        Serial.println(", pero no se pudo leer CHIP_ID");
      }
      BNO = 0;
    } else {
      Serial.println();
    }
  }
  return false;
}

void iniciarBNO() {
  escribir(0x3D, 0x00); delay(25);          // modo CONFIG
  uint8_t st = 0;
  leer(0x36, &st, 1);
  Serial.printf("Autoprueba ST_RESULT = 0x%02X -> acc %s, mag %s, gyr %s, MCU %s\n", st,
                (st & 1) ? "OK" : "MAL", (st & 2) ? "OK" : "MAL",
                (st & 4) ? "OK" : "MAL", (st & 8) ? "OK" : "MAL");
  escribir(0x3D, 0x0C); delay(25);          // modo NDOF (fusion)
  Serial.println("Euler (grados)                  | calib sys gyr acc mag | temp");
  bnoListo = true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== PRUEBA PLACA ===");
  Serial.printf("Chip: %s rev %d, %d MHz, flash %u MB\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / (1024 * 1024));
  delay(700);   // el BNO055 tarda ~650 ms en arrancar
  if (buscarBNO()) iniciarBNO();
}

uint32_t tBusca = 0;

void loop() {
  if (!bnoListo) {
    if (millis() - tBusca > 2000) {
      tBusca = millis();
      Serial.println("\n--- BNO no encontrado, buscando otra vez ---");
      if (buscarBNO()) iniciarBNO();
    }
    return;
  }
  uint8_t b[6], cal, t;
  if (leer(0x1A, b, 6) && leer(0x35, &cal, 1) && leer(0x34, &t, 1)) {
    float h = (int16_t)(b[0] | b[1] << 8) / 16.0;
    float r = (int16_t)(b[2] | b[3] << 8) / 16.0;
    float p = (int16_t)(b[4] | b[5] << 8) / 16.0;
    Serial.printf("H %7.2f  R %7.2f  P %7.2f | %d %d %d %d | %d C\n", h, r, p,
                  cal >> 6, (cal >> 4) & 3, (cal >> 2) & 3, cal & 3, (int8_t)t);
  } else {
    Serial.println("Error leyendo el BNO: se perdio. Lo busco otra vez.");
    bnoListo = false;
  }
  delay(300);
}
