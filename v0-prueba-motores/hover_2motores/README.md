# Medio dron · Hover (2 motores EX1103)

```
hover_2motores/
├── hover_2motores.html                      ← página de control (Chrome o Edge)
└── firmware/hover_2motores/hover_2motores.ino ← código del ESP32 (Arduino IDE)
```

## Conexión
| ESC | Señal (blanco) | Tierra (negro) |
|---|---|---|
| Motor 1 (izquierdo) | D13 | GND junto a D13 |
| Motor 2 (derecho)   | D15 | GND junto a D15 |

Alimentación de los dos ESC → fuente a **7.4 V** (nunca 12 V). Con dos motores, 2 A se acaban rápido:
si la fuente llega al límite, los ESC se reinician.

Los motores deben girar en **sentidos opuestos** (uno CW y otro CCW, con su hélice correspondiente);
si no, el medio dron gira solo por el torque. Para invertir uno, intercambia 2 de sus 3 cables.

## Uso
1. Sube `hover_2motores.ino` con el Arduino IDE y **cierra el Monitor Serie**.
2. Abre `hover_2motores.html` con doble clic en **Chrome o Edge** (Firefox no soporta Web Serial).
3. **Conectar ESP32** → elige el COM → espera "LISTO" → **Armar**.
4. Sube la potencia base con el slider o `W`/`S` (`Shift` = pasos de 2 %).
5. Si se inclina, corrige con k1/k2 o los ajustes. `A`/`D` = rotación suave.
6. Pestaña **Individual** para mover cada motor por separado.

`Espacio`/`Esc` = paro. Si cierras la página, cambias de pestaña o se suelta el USB, el ESP32 para y
desarma en 1 s (hay que volver a Armar).

Fórmula: `motor = base × k + ajuste ± rotación`, limitada a 0 – límite %.
