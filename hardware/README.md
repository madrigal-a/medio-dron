# Medio dron — PCBs (KiCad 9)

Dos proyectos de KiCad que comparten una librería propia:

```
lib/                      símbolos y huellas propias (medio_dron)
placa-principal/          ESP32-C3-MINI-1U, USB-C, LDO, ESC, Qwiic, ToF
placa-sensores/           LSM6DSV16X + ICP-20100, Qwiic
```

Abrir `placa-principal/placa-principal.kicad_pro` o `placa-sensores/placa-sensores.kicad_pro`
con **KiCad 9 o más nuevo**. Cada carpeta trae el esquemático en PDF y el `bom.csv`.

## Estado
- [x] Esquemáticos v1.0 (ERC sin errores)
- [ ] Huella del ICP-20100 (pendiente, se hace con el layout)
- [ ] Layout de las dos placas
- [ ] Gerbers + esténcil

## Pines del ESP32-C3
| GPIO | Uso |
|---|---|
| 0 | VBAT por divisor 100k/27k (ADC1_CH0) |
| 1 | libre (pad) |
| 2 | pull-up 10k (pin de arranque) |
| 3 | ToF Pololu OUT |
| 4, 5 | DShot ESC1, ESC2 |
| 6, 7 | SDA, SCL (Qwiic, pull-ups 2.2k) |
| 8 | pull-up 10k (pin de arranque) |
| 9 | botón BOOT |
| 10 | LED |
| 18, 19 | USB D−, D+ |
| 20, 21 | UART RX, TX (pads) |
