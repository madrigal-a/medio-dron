"""Banco de empuje para motor 1103 (Happymodel EX1103) + báscula.
Pieza 1: torre (base + columna + asiento). Pieza 2: disco portamotor intercambiable.
Unidades: mm. El motor empuja HACIA ABAJO (hélice soplando hacia arriba)."""
import math
import numpy as np
import trimesh
from manifold3d import Manifold, CrossSection, set_circular_segments

set_circular_segments(96)

# ---------------- parámetros ----------------
BASE_W, BASE_T, BASE_R = 70.0, 4.0, 6.0      # placa base sobre la báscula
COL_D = 16.0                                  # diámetro de la columna
TOWER_H = 82.0                                # altura total de la torre (sin disco)
FLARE_H = 12.0                                # transición cónica columna -> asiento
SEAT_D, SEAT_T = 30.0, 8.0                    # asiento superior
PUCK_D, PUCK_T = 30.0, 3.0                    # disco portamotor
PUCK_MOUNT_T = 1.8                            # espesor donde van los tornillos del motor
PUCK_RECESS_D = 15.0                          # rebaje inferior para cabezas de tornillo
SLOT_R0, SLOT_R1, SLOT_W = 3.2, 3.9, 1.6      # 3 hoyos a 120° en círculo de 7 mm (M1.2–M1.4), con holgura
CENTER_HOLE_D = 3.6                           # paso para eje / clip inferior
M3_R = 11.0                                   # radio de los 2 tornillos M3 que fijan el disco
M3_CLEAR, M3_PILOT, M3_DEPTH = 3.3, 2.6, 12.0
SEAT_POCKET_D, SEAT_POCKET_T = 9.0, 2.0       # hueco para el eje/rodamiento del motor
GUSSET_H, GUSSET_L, GUSSET_T = 28.0, 26.0, 4.0


def rounded_square(w, r):
    sq = CrossSection.square([w - 2 * r, w - 2 * r], center=True)
    return sq.offset(r, CrossSection.JoinType.Round if hasattr(CrossSection, "JoinType") else 1)


def cyl(h, d, d2=None, z=0.0):
    return Manifold.cylinder(h, d / 2, (d2 if d2 else d) / 2).translate([0, 0, z])


def slot(r0, r1, w, h, ang, z=0.0):
    a = math.radians(ang)
    pts = []
    for r in (r0, r1):
        pts.append(cyl(h, w).translate([r * math.cos(a), r * math.sin(a), z]))
    return Manifold.hull(pts[0] + pts[1])


# ---------------- torre ----------------
try:
    base2d = CrossSection.square([BASE_W - 2 * BASE_R] * 2, center=True).offset(
        BASE_R, CrossSection.JoinType.Round if hasattr(CrossSection, "JoinType") else "Round")
except Exception:
    from manifold3d import JoinType
    base2d = CrossSection.square([BASE_W - 2 * BASE_R] * 2, center=True).offset(BASE_R, JoinType.Round)
base = Manifold.extrude(base2d, BASE_T)

col_top = TOWER_H - SEAT_T - FLARE_H
column = cyl(col_top - BASE_T + 0.01, COL_D, z=BASE_T)
flare = cyl(FLARE_H + 0.01, COL_D, SEAT_D, z=col_top)
seat = cyl(SEAT_T, SEAT_D, z=TOWER_H - SEAT_T)

# refuerzos triangulares (4) en la base de la columna
tri = CrossSection([[(0, 0), (GUSSET_L, 0), (0, GUSSET_H)]])
g = Manifold.extrude(tri, GUSSET_T).translate([0, 0, -GUSSET_T / 2])  # en plano XY, espesor en Z
g = g.rotate([90, 0, 0])            # triángulo pasa al plano XZ
gussets = Manifold()
for k in range(4):
    gussets += g.translate([COL_D / 2 - 1.0, 0, BASE_T - 0.01]).rotate([0, 0, 45 + 90 * k])

tower = base + column + flare + seat + gussets

# hueco central del asiento y agujeros guía M3
tower -= cyl(SEAT_POCKET_T + 0.01, SEAT_POCKET_D, z=TOWER_H - SEAT_POCKET_T)
for ang in (45, 225):
    a = math.radians(ang)
    tower -= cyl(M3_DEPTH + 0.01, M3_PILOT, z=TOWER_H - M3_DEPTH).translate(
        [M3_R * math.cos(a), M3_R * math.sin(a), 0])

# aligerado de la base (ventanas) — menos plástico, sigue plana abajo
for k in range(4):
    a = math.radians(90 * k)
    win = Manifold.extrude(CrossSection.circle(7.0), BASE_T + 0.02).translate([0, 0, -0.01])
    tower -= win.translate([23 * math.cos(a), 23 * math.sin(a), 0])

# ---------------- disco portamotor ----------------
puck = cyl(PUCK_T, PUCK_D)
puck -= cyl(PUCK_T - PUCK_MOUNT_T + 0.01, PUCK_RECESS_D, z=-0.01)   # rebaje abajo
puck -= cyl(PUCK_T + 0.02, CENTER_HOLE_D, z=-0.01)
for ang in (90, 210, 330):          # 3 hoyos en triángulo, círculo de 7 mm
    puck -= slot(SLOT_R0, SLOT_R1, SLOT_W, PUCK_T + 0.02, ang, z=-0.01)
# canal pasante para la salida de cables del motor (entre los 2 tornillos de abajo)
puck -= Manifold.cube([3.2, PUCK_D, PUCK_T + 0.02]).translate([-1.6, -PUCK_D - 5.0, -0.01])
for ang in (45, 225):
    a = math.radians(ang)
    puck -= cyl(PUCK_T + 0.02, M3_CLEAR, z=-0.01).translate([M3_R * math.cos(a), M3_R * math.sin(a), 0])
# marca de orientación (muesca en el borde) para alinear con la torre
puck -= Manifold.cube([2.0, 2.0, PUCK_T + 0.02]).translate([PUCK_D / 2 - 1.2, -1.0, -0.01])


def to_mesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3],
                           faces=np.asarray(mesh.tri_verts), process=True)


t = to_mesh(tower)
p = to_mesh(puck)
for name, m in (("torre", t), ("disco", p)):
    print(name, "watertight:", m.is_watertight, "vol cm3: %.1f" % (m.volume / 1000), "bbox:", np.round(m.extents, 1))

t.export("banco_1103_torre.stl")
p.export("banco_1103_disco_motor.stl")

# ensamblado para vista previa
asm = trimesh.util.concatenate([t, p.copy().apply_translation([0, 0, TOWER_H])])
asm.export("preview_asm.stl")
