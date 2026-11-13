# Medio dron

Proyecto de un medio dron con control: dos motores en un brazo que debe mantenerse en hover y
poder rotar de forma controlada.

## Versiones

| Carpeta | Estado | Qué es |
|---|---|---|
| [`v0-prueba-motores/`](v0-prueba-motores) | ✅ Pruebas | Test de motores EX1103: banco de empuje impreso en 3D y control manual de 2 motores desde una página web. Sirve como base y referencia. |
| [`hardware/`](hardware) | 🚧 En diseño | PCBs en KiCad 9: placa principal (ESP32-C3) y placa de sensores (IMU + barómetro). |
| _próximamente_ | 🚧 | Proyecto completo del medio dron con control. |

## Hardware actual
- Motores Happymodel **EX1103 11000KV** (1S–2S) con ESC BLHeli_S / Bluejay por **DShot300**
- **ESP32** DevKit · señal en D13 (motor 1) y D15 (motor 2)
- Fuente de laboratorio a **7.4 V** (nunca 12 V)

Detalles de conexión y uso en el README de cada carpeta.
