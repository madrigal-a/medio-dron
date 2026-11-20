// buscar_pines.ino — encuentra el pinout de la placa prestada (ESP32-C3)
// Arduino IDE: placa "ESP32C3 Dev Module", USB CDC On Boot = DISABLED
// (se programa por UART con el adaptador -> Serial sale por GPIO20/21).
// Monitor Serie a 115200, "New Line".
//
// Al arrancar:
//   1) Lee qué pines están en ALTO sin pull-up interno (las líneas I2C
//      tienen pull-up en la placa del BNO -> deben salir en 1).
//   2) Prueba todas las parejas SDA/SCL y reporta dónde responde algo
//      (BNO055 = 0x28 o 0x29).
// Comandos:
//   N      (ej. 7)  -> el GPIO N parpadea a 1 Hz (medir con multímetro en
//                      los headers 7/8/9/0 o en los pads M1..M4)
//   x               -> apaga el parpadeo
//   e               -> repite el escaneo

#include <Wire.h>

const int PINES[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
const int NP = sizeof(PINES) / sizeof(PINES[0]);

int pinParpadeo = -1;
uint32_t tParpadeo = 0;
bool nivel = false;

void leerNiveles() {
  Serial.println("\n--- Niveles en reposo (INPUT, sin pull-up) ---");
  for (int i = 0; i < NP; i++) {
    pinMode(PINES[i], INPUT);
  }
  delay(20);
  for (int i = 0; i < NP; i++) {
    Serial.printf("GPIO%-2d = %d\n", PINES[i], digitalRead(PINES[i]));
  }
  Serial.println("(Los que dan 1 son candidatos a SDA/SCL)");
}

void escanearI2C() {
  Serial.println("\n--- Escaneo I2C de todas las parejas (100 kHz) ---");
  int hallazgos = 0;
  for (int a = 0; a < NP; a++) {
    for (int b = 0; b < NP; b++) {
      if (a == b) continue;
      int sda = PINES[a], scl = PINES[b];
      Wire.begin(sda, scl, 100000);
      Wire.setTimeOut(50);
      for (uint8_t dir = 0x08; dir < 0x78; dir++) {
        Wire.beginTransmission(dir);
        if (Wire.endTransmission() == 0) {
          Serial.printf("SDA=%d SCL=%d -> dispositivo en 0x%02X%s\n", sda, scl, dir,
                        (dir == 0x28 || dir == 0x29) ? "  <-- BNO055" : "");
          hallazgos++;
        }
      }
      Wire.end();
      pinMode(sda, INPUT);
      pinMode(scl, INPUT);
    }
  }
  if (hallazgos == 0) Serial.println("Nada respondió. ¿La placa del BNO tiene 3V?");
  Serial.println("--- Fin. Escribe un número de GPIO para hacerlo parpadear ---");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nbuscar_pines — placa prestada");
  leerNiveles();
  escanearI2C();
}

void loop() {
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s == "x") {
      if (pinParpadeo >= 0) { digitalWrite(pinParpadeo, LOW); pinMode(pinParpadeo, INPUT); }
      pinParpadeo = -1;
      Serial.println("Parpadeo apagado");
    } else if (s == "e") {
      leerNiveles();
      escanearI2C();
    } else if (s.length() > 0) {
      int n = s.toInt();
      bool valido = false;
      for (int i = 0; i < NP; i++) if (PINES[i] == n) valido = true;
      if (!valido) { Serial.println("GPIO no válido (0..10)"); return; }
      if (pinParpadeo >= 0) { digitalWrite(pinParpadeo, LOW); pinMode(pinParpadeo, INPUT); }
      pinParpadeo = n;
      pinMode(n, OUTPUT);
      Serial.printf("GPIO%d parpadea a 1 Hz (3.3 V / 0 V)\n", n);
    }
  }
  if (pinParpadeo >= 0 && millis() - tParpadeo > 500) {
    tParpadeo = millis();
    nivel = !nivel;
    digitalWrite(pinParpadeo, nivel);
  }
}
