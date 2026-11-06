# v0 · Prueba de motores EX1103

Herramientas para probar motores **Happymodel EX1103 11000KV** (1S–2S) con ESC BLHeli_S / Bluejay
controlados por un **ESP32** usando **DShot300**.

| Carpeta | Qué es |
|---|---|
| [`banco_empuje/`](banco_empuje) | Banco de empuje impreso en 3D + báscula. Firmware para 1 motor, programa de PC para pruebas por escalones (CSV + gráfica) y STL de la torre y el disco portamotor. |
| [`hover_2motores/`](hover_2motores) | Medio dron con 2 motores. Firmware para 2 ESC y página web (Chrome/Edge, Web Serial) para hover, control individual, coeficientes por motor y rotación suave con A/D. |

## ⚠️ Seguridad
- Fuente a **7.4 V** (equivalente a 2S). **Nunca 12 V**: el motor es 1S–2S.
- Lentes de protección y nadie cerca de las hélices.
- Los dos firmwares paran el motor si se pierde la comunicación con la PC.

## Requisitos
- Arduino IDE con **esp32 by Espressif** 3.x
- ESP32 DevKit (pines de señal: D13 y D15, junto a GND)
- Para el banco: Python 3.9+ (`pyserial`, `matplotlib`)
- Para el hover: Google Chrome o Microsoft Edge

Cada carpeta tiene su propio README con la conexión y los pasos.
