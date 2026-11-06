# Banco de empuje — Happymodel EX1103 11000KV

Banco impreso en 3D + ESP32 + báscula para medir el empuje de motores de micro dron.

```
banco_empuje/
├── INICIAR.bat                  ← doble clic para abrir el programa de prueba
├── app/banco_empuje_app.py      ← programa de PC (Python + Tkinter)
├── firmware/banco_empuje/       ← código del ESP32 (Arduino IDE)
├── stl/                         ← torre y disco portamotor para imprimir
└── resultados/                  ← aquí se guardan los CSV y gráficas
```

## ⚠️ Antes de empezar
- **Fuente a 7.4 V, límite 2 A. Nunca 12 V**: el motor es 1S–2S (máx. 8.4 V).
- Lentes puestos, base pegada a la báscula, nadie cerca de la hélice.
- La hélice debe **soplar hacia arriba** (la báscula marca más al acelerar).

## 1. Firmware (una sola vez)
1. Arduino IDE → instala **esp32 by Espressif** (versión 3.x).
2. Abre `firmware/banco_empuje/banco_empuje.ino`, elige tu placa ESP32 y súbelo.

Conexión: señal del ESC (blanco) → **GPIO13 (D13)**, tierra del ESC (negro) → **GND** pegado al D13.
Alimentación del ESC → fuente. ESP32 → USB de la compu.

## 2. Prueba
1. Cierra el Monitor Serie del Arduino IDE (si está abierto, el puerto queda ocupado).
2. Doble clic en `INICIAR.bat` (la primera vez instala `pyserial` y `matplotlib`).
3. Elige el puerto COM → **Conectar** → **Armar ESC** (pitidos).
4. Báscula en tara → **Iniciar prueba**. En cada escalón espera, escribe los gramos
   (y la corriente de la fuente si quieres) y presiona **Enter**.
5. **Guardar CSV** → queda en `resultados/` junto con la gráfica.

**Espacio o Esc = paro inmediato.** Si el programa se cierra o el cable se suelta, el
ESP32 para el motor solo en 1.5 s.

El firmware usa **DShot300** (BLHeli_S / Bluejay). También se puede usar solo con el
Monitor Serie: `armar`, `0`–`100`, `s`, `barrido`, `max N`.

## Referencia del fabricante (7.4 V, hélice 2023)
| Corriente | Empuje |
|---|---|
| 1.07 A | 23 g |
| 5.04 A | 81.8 g |
| 9.20 A | 121.9 g |
