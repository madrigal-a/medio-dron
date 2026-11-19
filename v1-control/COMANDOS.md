# Comandos por el puerto serie — de lo más simple a lo más completo

Monitor Serie del Arduino IDE a **115200** con **New Line**, o la consola de la página.
Las mayúsculas no importan. Un comando por línea. `ayuda` muestra el resumen.

> **Siempre a la mano:** `s` = paro (en roll/auto también desarma) · `desarmar` · `aterrizar`
> En la página: barra espaciadora o Esc = paro.

---

## Nivel 0 — ¿Está vivo? (sin motores)

| Comando | Qué hace |
|---|---|
| `ayuda` | Lista de comandos |
| `ver` | Imprime todos los parámetros y las ganancias (activas y del modelo) |
| `imu` | Una lectura de los sensores: ángulos, giroscopio, aceleración, ToF |
| `tel 10` | Telemetría continua a 10 Hz (`tel 0` la apaga) |

Al encender debe salir `LISTO ...` y `BNO055 OK en 0x28`. Si dice `ERR BNO055`, revisen SDA/SCL.

Formato de la telemetría:
`T,t_ms,modo,armado,roll,giro,roll_sp,z_cm,v_cms,z_sp,z_tof,motor1,motor2,vbat,flags`

---

## Nivel 1 — Sensores (sin motores)

```
imuauto        ← detecta solo cómo está montado el IMU (seguir instrucciones, 9 s)
cero           ← con el brazo nivelado: roll = 0 aquí
zcero          ← con el carrito abajo: altura = 0 aquí
tel 10         ← mover el brazo y el carrito con la mano y ver que cambien
guardar
```

Comprobar: **lado del motor 1 arriba → roll positivo**. Si sale al revés, repetir `imuauto`.
Si el ToF no marca nada (`ok=0` en `imu`), revisar el pin y que vea más de 5 cm.

---

## Nivel 2 — Motores a mano (modo manual) ⚠ hélices: cuidado

```
modo manual
armar          ← 3 s de ceros; luego "ARMADO"
gas 5          ← los dos motores al 5 %
gas 10
mot 10 0       ← solo el motor 1 (confirmar que es el del pin ESC1)
mot 0 10       ← solo el motor 2
gas 0
desarmar
```

| Comando | Qué hace |
|---|---|
| `gas P` | Los dos motores a P % (sube con rampa, baja inmediato) |
| `mot A B` | Motor 1 a A %, motor 2 a B % |
| `max P` | Límite de gas (default 70 %) |

---

## Nivel 3 — Datos del modelo (lo que midieron)

```
masa 0.105         ← kg, todo lo que sube y baja (sin batería)
l 0.065            ← m, del centro del pivote al eje del motor
j 0.00012          ← kg·m², del péndulo (la página tiene calculadora)
ver                ← revisar las ganancias "GM" que salen
guardar
```

---

## Nivel 4 — Encontrar u₀, el throttle de hover (con batería, brazo amarrado horizontal)

```
modo manual
armar
gas 40
gas 41             ← subir de 1 en 1 hasta que el carrito apenas flote
gas 42
...
u0set              ← guarda el gas actual como u0 y el voltaje como v0
gas 0
desarmar
guardar
```

O a mano: `u0 48` y `v0 7.6`.

---

## Nivel 5 — Control de roll (carrito AMARRADO al riel)

```
modo roll
pid roll auto      ← ganancias calculadas del modelo
zeta roll 1
wn roll 8
armar
gas 20             ← gas base; el control reparte entre los motores
gas 30
r 10               ← pedir 10° (máximo ±20°)
r -10
r 0
desarmar
```

Si en lugar de nivelarse **se va hacia un lado y se acelera**: `desarmar`, `mezsigno -1` y repetir.

Afinar (con el brazo funcionando, se puede en vivo):

| Si… | Prueben |
|---|---|
| Oscila / vibra | `zeta roll 1.2` o bajar `wn roll 6` |
| Responde lento | subir `wn roll 10` (de 1 en 1) |
| Se queda un poco chueco | subir `kifac roll 0.15` |

---

## Nivel 6 — Altura y roll juntos (modo auto, carrito suelto)

```
modo auto
pid alt auto
zeta alt 1
wn alt 3
armar              ← motores en ralentí, en el piso
z 10               ← despega solo (aprende el hover) y sube a 10 cm
z 15
z 20
r 5                ← inclinarse mientras flota
r 0
z 0                ← baja y se queda en el piso en ralentí
desarmar
```

Al despegar sale `DESPEGUE: hover aprendido ... gas X %`. Si X está lejos de su u₀, pongan `u0 X` y `guardar`.

| Si… | Prueben |
|---|---|
| Sube y baja (rebota) | `zeta alt 1.3` o `wn alt 2.5` |
| Lento para llegar | `wn alt 3.5` |
| Se queda abajo del objetivo | `kifac alt 0.15` |
| Sube demasiado rápido | `zvel 5` (cm/s del objetivo) |

---

## Nivel 7 — Ganancias manuales

```
copiar roll        ← copia las del modelo a las manuales (punto de partida)
pid roll manual    ← ahora usa las manuales
kp roll 0.06
kd roll 0.015
ki roll 0.04
ver
pid roll auto      ← regresar al modelo
```
Igual con `alt`. Unidades: roll en N/rad, N/(rad·s), N·s/rad; altura en N/m, N/(m·s), N·s/m.

---

## Nivel 8 — Seguridad y ajustes finos

| Comando | Qué hace | Default |
|---|---|---|
| `limang G` | Ángulo máximo que se puede pedir | 20° |
| `corte G` | Si el roll pasa de esto → aterrizaje suave | 35° |
| `vmin V` | Batería baja → aterrizaje suave | 7.0 V |
| `tater S` | Duración del aterrizaje suave | 1.5 s |
| `gasmin P` | Gas mínimo con control (que no se paren) | 4 % |
| `zvel CM` | Rapidez con que cambia la altura pedida | 10 cm/s |
| `vdiv F` | Factor del divisor de batería | 4.704 |
| `acc off` / `acc on` | No usar / usar el acelerómetro para la altura | on |
| `fabrica` | Regresa todo a valores de fábrica (luego `guardar`) | |

Cambiar de modo estando armado es "sin brincos": por ejemplo, flotar en `roll` con gas fijo y pasar a `auto`.

---

## Receta rápida para el lunes

```
ver                                     ← revisar masa, J, L, u0
modo roll  →  armar  →  gas 25          ← carrito amarrado: ¿se nivela?
r 10 / r 0                              ← ¿sigue el ángulo?
desarmar
modo manual → armar → gas … → u0set     ← carrito amarrado horizontal, con batería
desarmar  →  guardar
modo auto  →  armar  →  z 10            ← ¡a volar! (Grabar CSV en la página)
z 0  →  desarmar
```
