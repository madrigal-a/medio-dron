# Herramientas de prueba (20 nov 2026)

Sketches cortos para probar la placa y medir datos antes de usar el firmware completo
(`../firmware/medio_dron_control`). **Ninguno se ha probado en hardware todavía.**

Todos: Arduino IDE con "esp32 by Espressif" 3.x, Monitor Serie a **115200, New Line**.
Si la placa se programa por un adaptador UART (no por su USB), poner
**USB CDC On Boot = Disabled** para que `Serial` salga por los pines TX/RX.

| Sketch | Para qué | Antes de subir |
|---|---|---|
| `buscar_pines/` | Encontrar el pinout: escanea todas las parejas SDA/SCL (GPIO 0–10) y dice dónde responde algo por I²C (BNO055 = 0x28/0x29). Escribiendo un número, ese GPIO parpadea a 1 Hz para seguirlo con el multímetro. | Nada (pensado para ESP32-C3) |
| `prueba_placa/` | Probar el BNO055: revisa que SDA/SCL estén en alto, escanea el bus, prueba 0x28 y 0x29 por separado, lee CHIP_ID (0xA0), autoprueba de cada sensor y ángulos en vivo. Si no lo encuentra, lo busca cada 2 s. | `SDA_PIN`, `SCL_PIN` (4 y 5) |
| `medir_hover/` | Medir **u₀** (gas de hover). Se escribe un % y los dos motores van a ese gas. Imprime u₀, T₀ y el comando `u0set` para el firmware V1. | `PIN_M` (pines de los ESC), `MASA_KG` |

## medir_hover — comandos

| Comando | Qué hace |
|---|---|
| `50` | Los dos motores al 50 % |
| `50 52` | Motor 1 al 50 %, motor 2 al 52 % |
| `+` / `-` | Sube / baja 1 % |
| `++` / `--` | Sube / baja 0.5 % |
| `s` o Enter vacío | **PARO** (motores a 0) |
| `max N` | Límite de gas (default 60 %) |

El primer número arma los ESC (3 s de ceros); la subida es en rampa (50 %/s), la bajada inmediata.
DShot300 por RMT, el mismo de `v0-prueba-motores/hover_2motores`.

**Cómo medir u₀:** carrito en el riel, brazo amarrado horizontal. Empezar en `10` y subir con `+`
hasta que el carrito se despegue y flote. Ese % es u₀. Anotar el voltaje de la batería en ese momento.
Con m = 26.5 g cada motor da T₀ = m·g/2 ≈ 0.130 N (13 gf), así que se espera poco gas (~15–25 %).

## Datos medidos

| Dato | Valor |
|---|---|
| m (todo lo que sube y baja) | 26.5 g |
| L (pivote → eje del motor) | 72.3 mm |
| J | pendiente (péndulo con moneda) |
| u₀ | pendiente (`medir_hover`) |
