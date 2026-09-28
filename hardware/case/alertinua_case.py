# -*- coding: utf-8 -*-
"""
Параметричний корпус alertinua для 3D-друку (FreeCAD, запуск: freecadcmd alertinua_case.py).

Компактна версія: акумулятор лежить ПІД платою (між напрямними), праворуч
від плати - лише вузька смуга під зумер, світлодіоди, фоторезистор і вимикач.

Дві деталі:
  * base - основа: напрямні для LILYGO T-Display, акумулятор 602030 під платою,
           отвір під USB-C, проріз під вимикач SS-12D00, 2 стійки під гвинти M2;
  * lid  - кришка: вікно дисплея, доступ до кнопки BOOT, 2 світлодіоди 5 мм,
           фоторезистор 5 мм, решітка й тримач зумера ⌀12, посадковий бортик.

Система координат: (0,0,0) - внутрішній кут основи на рівні верху дна.
X - уздовж довгої сторони (USB-C зліва, x=0), Y - поперек, Z - вгору.
Усі розміри в мм. Значення з позначкою "ВИМІРЯТИ" - оцінки, їх варто
звірити штангенциркулем з реальними деталями перед друком.
"""
import os
import FreeCAD as App
import Part
import Mesh
from FreeCAD import Vector as V

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

# ---------------------------------------------------------------- корпус
WALL = 2.0            # товщина стінок
FLOOR = 2.0           # товщина дна
LID_T = 2.0           # товщина кришки
IN_L, IN_W, IN_H = 72.0, 32.0, 16.0    # внутрішній об'єм (до низу кришки)
R_OUT = 3.0           # радіус заокруглення вертикальних ребер зовні
LIP_T, LIP_H, LIP_GAP = 1.2, 3.0, 0.25  # посадковий бортик кришки і зазор

# ---------------------------------------------------------------- гвинти M2
POST_D = 6.0          # діаметр стійки
POST_HOLE = 1.8       # отвір під саморіз M2 у стійці
POST_HOLE_DEPTH = 10.0
LID_SCREW_D = 2.4     # прохідний отвір у кришці
LID_SCREW_HEAD_D = 4.2  # потай під голівку
# Стійки лише в правих кутах (у лівих їм заважали б кути плати);
# ліву частину кришки тримає посадковий бортик.
POSTS = [(IN_L - 3.0, 3.0), (IN_L - 3.0, IN_W - 3.0)]

# ---------------------------------------------------------------- T-Display
BOARD_L, BOARD_W, BOARD_H = 51.52, 25.04, 8.54   # з урахуванням компонентів
BOARD_X0 = 1.6                                   # зазор до лівої стінки (USB)
BOARD_Y0 = (IN_W - BOARD_W) / 2.0
BOARD_TOP_GAP = 0.4                              # щоб скло не тиснуло в кришку
RAIL_H = IN_H - BOARD_TOP_GAP - BOARD_H          # висота напрямних під платою
RAIL_T = 1.5

# Вікно дисплея: активна область 1.14" 240×135 ≈ 24.8 × 14.0 мм.
DISP_W, DISP_H = 25.4, 14.6                      # з запасом 0.3 мм на бік
DISP_CX = BOARD_X0 + BOARD_L / 2.0 + 2.5         # ВИМІРЯТИ: центр екрана по X
DISP_CY = IN_W / 2.0                             # ВИМІРЯТИ: центр екрана по Y
DISP_CHAMFER = 1.2                               # фаска вікна зовні

# Кнопка BOOT (GPIO0) - отвір під шпильку/зубочистку.
BOOT_HOLE_D = 3.0
BOOT_X, BOOT_Y = BOARD_X0 + 4.5, BOARD_Y0 + 4.0  # ВИМІРЯТИ

# USB-C у лівій стінці (з великим запасом під корпус штекера).
USB_W, USB_H = 13.0, 8.0
USB_CY = BOARD_Y0 + BOARD_W / 2.0
USB_CZ = RAIL_H + 2.5                            # ВИМІРЯТИ

# ---------------------------------------------------------------- акумулятор 602030
BATT_L, BATT_W, BATT_H = 34.0, 20.0, 6.0         # 30 мм + плата захисту
BATT_X0 = 4.0                                    # під платою, між напрямними
BATT_Y0 = (IN_W - BATT_W) / 2.0
BATT_STOP_T, BATT_STOP_H = 1.2, 4.0              # упор, щоб не з'їжджав до зумера

# ---------------------------------------------------------------- кришка: виводи
LED_D = 5.2
LEDS = [(57.5, 4.5), (63.0, 4.5)]                # LED1 тривога, LED2 Wi-Fi
LDR_D = 5.3
LDR = (58.5, 27.5)
BUZ_X, BUZ_Y = 62.0, 16.0
BUZ_D = 12.0                                     # ВИМІРЯТИ: діаметр зумера
BUZ_RING_T, BUZ_RING_H = 1.2, 4.0
BUZ_GRILLE_HOLE = 1.6

# ---------------------------------------------------------------- вимикач SS-12D00
SW_SLOT_W, SW_SLOT_H = 6.5, 3.2                  # проріз під важіль (Y × Z)
SW_CY, SW_CZ = 16.0, 4.0                         # низько: над ним висить зумер


def box(x, y, z, lx, ly, lz):
    return Part.makeBox(lx, ly, lz, V(x, y, z))


def rounded_box(x, y, z, lx, ly, lz, r):
    b = box(x, y, z, lx, ly, lz)
    if r <= 0:
        return b
    edges = [e for e in b.Edges
             if abs(e.Vertexes[0].Point.x - e.Vertexes[1].Point.x) < 1e-6
             and abs(e.Vertexes[0].Point.y - e.Vertexes[1].Point.y) < 1e-6]
    return b.makeFillet(r, edges)


def cyl(x, y, z, d, h):
    return Part.makeCylinder(d / 2.0, h, V(x, y, z))


# ================================================================= ОСНОВА
out_l, out_w = IN_L + 2 * WALL, IN_W + 2 * WALL
base_h = FLOOR + IN_H
base = rounded_box(-WALL, -WALL, -FLOOR, out_l, out_w, base_h, R_OUT)
base = base.cut(rounded_box(0, 0, 0, IN_L, IN_W, IN_H + 1, max(R_OUT - WALL, 0.5)))

# стійки під гвинти
for (px, py) in POSTS:
    base = base.fuse(cyl(px, py, 0, POST_D, IN_H))
    base = base.cut(cyl(px, py, IN_H - POST_HOLE_DEPTH, POST_HOLE, POST_HOLE_DEPTH + 1))

# напрямні під довгі краї плати + упор праворуч
rail_len = BOARD_L - 4.0
base = base.fuse(box(BOARD_X0 + 2.0, BOARD_Y0 - 0.2, 0, rail_len, RAIL_T, RAIL_H))
base = base.fuse(box(BOARD_X0 + 2.0, BOARD_Y0 + BOARD_W - RAIL_T + 0.2, 0, rail_len, RAIL_T, RAIL_H))
base = base.fuse(box(BOARD_X0 + BOARD_L + 0.3, BOARD_Y0 + 3.0, 0, 1.5, BOARD_W - 6.0, RAIL_H + 3.0))

# упор акумулятора з правого боку (з боків його тримають напрямні плати)
base = base.fuse(box(BATT_X0 + BATT_L + 0.5, BATT_Y0 + 3.0, 0, BATT_STOP_T, BATT_W - 6.0, BATT_STOP_H))

# отвір USB-C у лівій стінці
base = base.cut(box(-WALL - 1, USB_CY - USB_W / 2, USB_CZ - USB_H / 2, WALL + 2, USB_W, USB_H))
# проріз вимикача у правій стінці
base = base.cut(box(IN_L - 1, SW_CY - SW_SLOT_W / 2, SW_CZ - SW_SLOT_H / 2, WALL + 2, SW_SLOT_W, SW_SLOT_H))

base = base.removeSplitter()

# ================================================================= КРИШКА
# Кришка лежить на стінках: її низ на z = IN_H. Бортик заходить усередину.
lid = rounded_box(-WALL, -WALL, IN_H, out_l, out_w, LID_T, R_OUT)
lip_o = rounded_box(LIP_GAP, LIP_GAP, IN_H - LIP_H, IN_L - 2 * LIP_GAP, IN_W - 2 * LIP_GAP, LIP_H,
                    max(R_OUT - WALL - LIP_GAP, 0.5))
lip_i = box(LIP_GAP + LIP_T, LIP_GAP + LIP_T, IN_H - LIP_H - 1,
            IN_L - 2 * (LIP_GAP + LIP_T), IN_W - 2 * (LIP_GAP + LIP_T), LIP_H + 2)
lip = lip_o.cut(lip_i)
for (px, py) in POSTS:  # бортик обходить стійки
    lip = lip.cut(cyl(px, py, IN_H - LIP_H - 1, POST_D + 1.0, LIP_H + 2))
lid = lid.fuse(lip)

# тримач зумера на внутрішній поверхні
ring = cyl(BUZ_X, BUZ_Y, IN_H - BUZ_RING_H, BUZ_D + 0.4 + 2 * BUZ_RING_T, BUZ_RING_H)
ring = ring.cut(cyl(BUZ_X, BUZ_Y, IN_H - BUZ_RING_H - 1, BUZ_D + 0.4, BUZ_RING_H + 2))
lid = lid.fuse(ring)

top = IN_H + LID_T
# вікно дисплея з фаскою зовні
win = box(DISP_CX - DISP_W / 2, DISP_CY - DISP_H / 2, IN_H - 1, DISP_W, DISP_H, LID_T + 2)
lid = lid.cut(win)
ch = Part.makeLoft([
    Part.makePolygon([V(DISP_CX - DISP_W / 2, DISP_CY - DISP_H / 2, top - DISP_CHAMFER),
                      V(DISP_CX + DISP_W / 2, DISP_CY - DISP_H / 2, top - DISP_CHAMFER),
                      V(DISP_CX + DISP_W / 2, DISP_CY + DISP_H / 2, top - DISP_CHAMFER),
                      V(DISP_CX - DISP_W / 2, DISP_CY + DISP_H / 2, top - DISP_CHAMFER),
                      V(DISP_CX - DISP_W / 2, DISP_CY - DISP_H / 2, top - DISP_CHAMFER)]),
    Part.makePolygon([V(DISP_CX - DISP_W / 2 - DISP_CHAMFER, DISP_CY - DISP_H / 2 - DISP_CHAMFER, top + 0.01),
                      V(DISP_CX + DISP_W / 2 + DISP_CHAMFER, DISP_CY - DISP_H / 2 - DISP_CHAMFER, top + 0.01),
                      V(DISP_CX + DISP_W / 2 + DISP_CHAMFER, DISP_CY + DISP_H / 2 + DISP_CHAMFER, top + 0.01),
                      V(DISP_CX - DISP_W / 2 - DISP_CHAMFER, DISP_CY + DISP_H / 2 + DISP_CHAMFER, top + 0.01),
                      V(DISP_CX - DISP_W / 2 - DISP_CHAMFER, DISP_CY - DISP_H / 2 - DISP_CHAMFER, top + 0.01)]),
], True)
lid = lid.cut(ch)

# кнопка BOOT, світлодіоди, фоторезистор
lid = lid.cut(cyl(BOOT_X, BOOT_Y, IN_H - 1, BOOT_HOLE_D, LID_T + 2))
for (lx, ly) in LEDS:
    lid = lid.cut(cyl(lx, ly, IN_H - 1, LED_D, LID_T + 2))
lid = lid.cut(cyl(LDR[0], LDR[1], IN_H - 1, LDR_D, LID_T + 2))

# решітка зумера: центральний отвір + кільце з 8
lid = lid.cut(cyl(BUZ_X, BUZ_Y, IN_H - 1, BUZ_GRILLE_HOLE, LID_T + 2))
import math
for i in range(8):
    a = 2 * math.pi * i / 8
    lid = lid.cut(cyl(BUZ_X + 3.2 * math.cos(a), BUZ_Y + 3.2 * math.sin(a), IN_H - 1,
                      BUZ_GRILLE_HOLE, LID_T + 2))

# отвори під гвинти з потаєм
for (px, py) in POSTS:
    lid = lid.cut(cyl(px, py, IN_H - LIP_H - 1, LID_SCREW_D, LID_T + LIP_H + 2))
    lid = lid.cut(Part.makeCone(LID_SCREW_D / 2, LID_SCREW_HEAD_D / 2, 1.0,
                                V(px, py, top - 1.0 + 0.001)))

lid = lid.removeSplitter()

# ================================================================= перевірки
assert base.isValid() and lid.isValid(), "невалідна геометрія"
inter = base.common(lid)
print("base volume  %.1f мм³" % base.Volume)
print("lid volume   %.1f мм³" % lid.Volume)
print("перетин base∩lid %.3f мм³ (має бути 0)" % inter.Volume)
bb = base.fuse(lid).BoundBox
print("габарити: %.1f × %.1f × %.1f мм" % (bb.XLength, bb.YLength, bb.ZLength))
print("висота напрямних під платою: %.2f мм" % RAIL_H)

# ================================================================= експорт
doc = App.newDocument("alertinua_case")
doc.addObject("Part::Feature", "Base").Shape = base
doc.addObject("Part::Feature", "Lid").Shape = lid
doc.recompute()
doc.saveAs(os.path.join(OUT, "alertinua_case.FCStd"))

base.exportStep(os.path.join(OUT, "alertinua_base.step"))
lid.exportStep(os.path.join(OUT, "alertinua_lid.step"))

# STL у положенні для друку: основа дном на столі, кришка - верхом на столі.
def export_stl(shape, name):
    m = Mesh.Mesh()
    m.addFacets(shape.tessellate(0.05))
    m.write(os.path.join(OUT, name))

b_print = base.copy()
b_print.translate(V(WALL, WALL, FLOOR))
l_print = lid.copy()
l_print.rotate(V(0, 0, 0), V(1, 0, 0), 180)
l_print.translate(V(WALL, IN_W + WALL, top))
export_stl(b_print, "alertinua_base.stl")
export_stl(l_print, "alertinua_lid.stl")

# сировина для прев'ю (у складеному положенні)
export_stl(base, "_preview_base.stl")
export_stl(lid, "_preview_lid.stl")
print("OK ->", OUT)
