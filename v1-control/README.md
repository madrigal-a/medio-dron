# Medio dron · v1 control (altura + roll)

```
v1-control/
├── medio_dron_control.html                         ← página (Chrome o Edge, por USB)
├── COMANDOS.md                                     ← comandos del serial, de lo simple a lo completo
└── firmware/medio_dron_control/medio_dron_control.ino  ← firmware ESP32-C3
```

Compilado y revisado con el core **esp32 3.3.4 y 4.0.0** para ESP32-C3. No necesita librerías extra.

## Subir el firmware
1. Arduino IDE → placa **ESP32C3 Dev Module**.
2. **Tools → USB CDC On Boot → Enabled** (si no, no sale nada por el USB).
3. Subir. Abrir el Monitor Serie (115200, *New Line*) y debe decir `LISTO ...` y `BNO055 OK`.
4. Cerrar el Monitor Serie antes de abrir la página.

## Pines (provisionales: son los de su PCB principal)
Cámbialos al inicio del `.ino` (bloque **PINES**) cuando tengan el pinout de la placa prestada.

| Qué | GPIO | Nota |
|---|---|---|
| BNO055 SDA | 0 | I²C a 100 kHz |
| BNO055 SCL | 1 | |
| ESC motor 1 (señal) | 6 | DShot300 |
| ESC motor 2 (señal) | 7 | DShot300 |
| ToF Pololu #4071 (OUT) | 4 | pulso |
| Batería (divisor) | 3 | `-1` si la placa no tiene. Factor: comando `vdiv` |
| LED armado | -1 | opcional |

En el ESP32-C3 **no usen** GPIO 2, 8, 9 (arranque) ni 18-19 (USB). Solo GPIO 0-4 sirven para el ADC.
El BNO055 se alimenta a 3.3 V. Su dirección (0x28 o 0x29) se detecta sola.
El ToF mide mínimo ~4 cm: móntenlo para que en el piso vea **más de 5 cm**.

## Modos
| Modo | Qué hace | Para qué |
|---|---|---|
| `manual` | Motores directos (`gas P` o `mot A B`). Sin control. | Probar motores, buscar u₀ |
| `roll` | Controla el roll; tú pones el gas base (`gas 50`). | **Primero**: con el carrito amarrado al riel |
| `auto` | Controla altura (`z 15`) y roll. Despega solo. | La prueba final |

## Ganancias: modelo (auto) o manual
Cada eje (`roll`, `alt`) tiene **dos juegos de ganancias** y eliges cuál usar:
- `pid roll auto`: las calcula del modelo con **masa, J, L** y lo que eliges: **ζ (zeta)** y **ωn (wn)**.
  Kp = ωn²/b · Kd = 2ζωn/b · Ki = Kp·ωn·kifac, con b = 2L/J (roll) o 1/m (altura).
- `pid roll manual`: usa `kp/ki/kd roll ...` tal cual.
- `copiar roll`: copia las del modelo a las manuales para afinar desde ahí.

Las unidades son SI (el PID calcula **newtons** de empuje y el firmware los convierte a throttle con u₀).

**Despegue automático (modo auto):** al pedir altura, el empuje sube despacio hasta que el carrito se despega
y en ese momento **aprende el empuje de hover real**. Así un u₀ mal medido (±15 %) no lo hace salir disparado.
El mensaje `DESPEGUE: hover aprendido ... gas X %` les dice el u₀ real: pueden ponerlo con `u0 X`.

## Seguridad
- Hay que `armar` antes de que giren los motores (3 s de ceros, como en v0).
- Ángulo pedido limitado a `limang` (20°). Gas entre `gasmin` y `max`.
- Si el roll pasa de `corte` (35°), la batería baja de `vmin`, se pierde el ToF (auto) o la página deja de mandar latido:
  **aterrizaje suave** (empuje baja en `tater` s y desarma).
- Si se pierde el IMU: corte inmediato. **Espacio / Esc / PARO** en la página: paro inmediato.

## Procedimiento sugerido
1. **Pesar y medir**: `masa` (kg, todo lo que se mueve), `l` (m, pivote → eje del motor), `j` (péndulo: la página tiene calculadora).
2. **Ejes del IMU**: motores apagados, botón *Detectar ejes del IMU* (`imuauto`) y seguir las instrucciones (9 s).
   Revisa que al subir el lado del motor 1 el roll salga **positivo**. `guardar`.
3. **Sentido de los motores** (modo manual, sin hélices o con cuidado): `gas 10` y confirmar que el motor 1 es el del pin ESC1.
4. **Roll con el carrito amarrado** (sirve la fuente de 3 A): `modo roll`, `armar`, `gas 25`… El brazo debe nivelarse.
   Si se va hacia un lado y se acelera, `desarmar` y `mezsigno -1` (motores al revés).
5. **u₀** (con batería, brazo amarrado horizontal): modo manual, subir `gas` de 1 en 1 hasta que flote → `u0set` (guarda gas y voltaje).
6. **Altura**: soltar el carrito, `modo auto`, `armar`, `z 10`. Grabar CSV para el reporte.
7. Si algo no convence: mover **ζ** o **ωn**, no las tres ganancias. `guardar` al final.

## Comandos (Monitor Serie o consola de la página)
Guía paso a paso, de lo más simple a lo más completo: [COMANDOS.md](COMANDOS.md).

```
armar | desarmar | s (paro) | aterrizar | modo manual|roll|auto
gas P | mot A B | r GRADOS | z CM
masa KG | j VALOR | l M | u0 P | v0 V | u0set
zeta roll|alt V | wn roll|alt V | kifac roll|alt V
pid roll|alt auto|manual | kp|ki|kd roll|alt V | copiar roll|alt
max P | gasmin P | limang G | corte G | vmin V | vdiv F | tater S | zvel CM/S
imuauto | cero | zcero | imu | mezsigno 1|-1 | acc on|off
tel HZ | ver | guardar | fabrica | ayuda
```

Telemetría: `T,t_ms,modo,armado,roll,giro,roll_sp,z_cm,v_cms,z_sp,z_tof,motor1,motor2,vbat,flags`
(flags: 1 IMU ok, 2 ToF ok, 4 aterrizando, 8 latido, 16 batería baja).
