# -*- coding: utf-8 -*-
"""Схема цифрового входу AES/EBU для мосту (Pi Pico): docs/img/skhema-aes-vkhid.png.
   python3 bridge/tools/draw_aes_input.py
Підписи — лише ті, що є на деталях і платах: контакти XLR 1/2/3, ніжки 74HC04 1…14, на Pico — 15, 3V3, GND."""
from PIL import Image, ImageDraw, ImageFont
import os

W, H = 1800, 1180
im = Image.new('RGB', (W, H), (255, 255, 255))
d = ImageDraw.Draw(im)
SUP = '/System/Library/Fonts/Supplemental/'
f = ImageFont.truetype(SUP + 'Arial.ttf', 24)
fs = ImageFont.truetype(SUP + 'Arial.ttf', 20)
fb = ImageFont.truetype(SUP + 'Arial Bold.ttf', 26)
ft = ImageFont.truetype(SUP + 'Arial Bold.ttf', 34)
INK, RED, BLUE, GRN, GREY = (20, 20, 20), (200, 30, 30), (20, 80, 200), (0, 130, 60), (110, 110, 110)
LW = 4


def wire(*p, c=INK):
    d.line(p, fill=c, width=LW, joint='curve')


def dot(x, y, c=INK):
    d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=c)


def text(x, y, s, font=f, c=INK, anchor='la'):
    d.text((x, y), s, font=font, fill=c, anchor=anchor)


def res_h(x1, y, x2, label):
    m = (x1 + x2) // 2
    wire((x1, y), (m - 40, y))
    d.rectangle((m - 40, y - 14, m + 40, y + 14), outline=INK, width=LW, fill=(255, 255, 255))
    wire((m + 40, y), (x2, y))
    text(m, y - 22, label, fb, anchor='md')


def res_v(x, y1, y2, label):
    m = (y1 + y2) // 2
    wire((x, y1), (x, m - 40))
    d.rectangle((x - 14, m - 40, x + 14, m + 40), outline=INK, width=LW, fill=(255, 255, 255))
    wire((x, m + 40), (x, y2))
    text(x + 24, m, label, fb, anchor='lm')


def cap_h(x1, y, x2, label):
    m = (x1 + x2) // 2
    wire((x1, y), (m - 6, y))
    d.line((m - 6, y - 22, m - 6, y + 22), fill=INK, width=LW + 1)
    d.line((m + 6, y - 22, m + 6, y + 22), fill=INK, width=LW + 1)
    wire((m + 6, y), (x2, y))
    text(m, y - 30, label, fb, anchor='md')


def cap_v(x, y1, y2, label):
    m = (y1 + y2) // 2
    wire((x, y1), (x, m - 6))
    d.line((x - 22, m - 6, x + 22, m - 6), fill=INK, width=LW + 1)
    d.line((x - 22, m + 6, x + 22, m + 6), fill=INK, width=LW + 1)
    wire((x, m + 6), (x, y2))
    text(x + 30, m, label, fb, anchor='lm')


def gnd(x, y):
    wire((x, y), (x, y + 16))
    for i, w in enumerate((24, 15, 6)):
        d.line((x - w, y + 16 + i * 7, x + w, y + 16 + i * 7), fill=INK, width=3)
    text(x + 30, y + 22, 'GND', fs, GREY, 'lm')


text(W // 2, 30, 'Цифровий вхід AES/EBU → міст (Pi Pico)', ft, anchor='ma')
text(W // 2, 76, 'для перевірки з прошивкою 4.00 · усі «GND» — це GND Pico (вона ж з\'єднана з GND передавача)', f, GREY, 'ma')

# ---- гніздо XLR
d.rounded_rectangle((60, 250, 270, 640), 18, outline=INK, width=LW)
text(165, 262, 'Гніздо XLR', fb, anchor='ma')
text(165, 296, 'кабель від', fs, GREY, 'ma')
text(165, 320, 'AES/EBU OUT', fs, GREY, 'ma')
text(165, 344, 'пульта WING', fs, GREY, 'ma')
for n, y in (('2', 420), ('3', 500), ('1', 590)):
    d.ellipse((240 - 18, y - 18, 240 + 18, y + 18), outline=INK, width=3, fill=(255, 255, 255))
    text(240, y, n, fb, anchor='mm')

Y = 420   # рядок сигналу
# XLR 2 → R1 → точка А
res_h(258, Y, 470, 'R1  150 Ом')
A = 520
wire((470, Y), (A, Y))
dot(A, Y)
text(A, Y + 26, 'А', fb, BLUE, 'ma')
# XLR 3 → R2 → GND
res_h(258, 500, 440, 'R2  150 Ом')
wire((440, 500), (440, 520))
gnd(440, 520)
# XLR 1 → GND
wire((258, 590), (330, 590), (330, 610))
gnd(330, 610)
# А → R3 75 → GND
res_v(A, Y, 640, 'R3  75 Ом')
gnd(A, 640)
# А → C1 → R4 → точка Б
cap_h(A, Y, 690, 'C1  0,01 мкФ')
res_h(690, Y, 860, 'R4  100 Ом')
B = 900
wire((860, Y), (B, Y))
dot(B, Y)
text(B - 14, Y + 26, 'Б', fb, BLUE, 'ra')
res_v(B, Y, 640, 'R5  100 кОм')
gnd(B, 640)

# ---- 74HC04 (вид зверху, ключ угорі)
CX1, CX2, CY1 = 1080, 1260, 330
P = 70
pinY = [CY1 + 40 + i * P for i in range(7)]          # ніжки 1…7 зліва, згори вниз
CY2 = pinY[-1] + 40
d.rectangle((CX1, CY1, CX2, CY2), outline=INK, width=LW, fill=(245, 245, 245))
d.chord((CX1 + (CX2 - CX1) // 2 - 22, CY1 - 22, CX1 + (CX2 - CX1) // 2 + 22, CY1 + 22), 0, 180, outline=INK, width=3, fill=(255, 255, 255))
text((CX1 + CX2) // 2, (CY1 + CY2) // 2 - 20, '74HC04', fb, anchor='mm')
text((CX1 + CX2) // 2, (CY1 + CY2) // 2 + 16, 'ключ (виїмка)', fs, GREY, 'mm')
text((CX1 + CX2) // 2, (CY1 + CY2) // 2 + 40, 'угорі', fs, GREY, 'mm')
for i, y in enumerate(pinY):
    n = i + 1
    wire((CX1 - 40, y), (CX1, y))
    text(CX1 + 10, y, str(n), fb, anchor='lm')
    m = 14 - i
    wire((CX2, y), (CX2 + 40, y))
    text(CX2 - 10, y, str(m), fb, anchor='rm')
p1, p2, p3, p4, p5, p6, p7 = pinY
# Б → ніжка 1
wire((B, Y), (CX1 - 40, Y))
# R6 10 кОм між ніжками 1 і 2 (зліва)
RX = 990
dot(RX, p1)
res_v(RX, p1, p2, '')
text(RX - 24, (p1 + p2) // 2, 'R6  10 кОм', fb, anchor='rm')
wire((RX, p2), (CX1 - 40, p2))
dot(RX, p2)
# ніжка 2 → ніжка 3
JX = 1025
dot(JX, p2)
wire((JX, p2), (JX, p3), (CX1 - 40, p3))
# ніжка 4 → Pico 15 (знизу)
OUTY = CY2 + 70
wire((CX1 - 40, p4), (960, p4), (960, OUTY), (1500, OUTY), c=GRN)
text(1180, OUTY - 12, 'сигнал → Pico «15»', fb, GRN, 'md')
# ніжки 5, 7 → GND (зліва)
for y in (p5, p7):
    wire((CX1 - 40, y), (1010, y), (1010, y + 6))
    gnd(1010, y + 6)
# ніжки 6 (вихід 3, вільна) — нічого
text(CX1 - 46, p6, 'вільна', fs, GREY, 'rm')
# справа: 14 → 3V3; 13, 11, 9 → GND; 12, 10, 8 — вільні
q = {14: pinY[0], 13: pinY[1], 12: pinY[2], 11: pinY[3], 10: pinY[4], 9: pinY[5], 8: pinY[6]}
wire((CX2 + 40, q[14]), (1500, q[14]), c=RED)
text(1390, q[14] - 12, '+3,3 В', fb, RED, 'md')
CAPX = 1340
dot(CAPX, q[14], RED)
cap_v(CAPX, q[14], q[14] + 120, 'C2  0,1 мкФ')
gnd(CAPX, q[14] + 120)
for n in (13, 11, 9):
    y = q[n]
    wire((CX2 + 40, y), (1300, y), (1300, y + 6))
    gnd(1300, y + 6)
for n in (12, 10, 8):
    text(CX2 + 46, q[n], 'вільна', fs, GREY, 'lm')

# ---- Pi Pico
PX1, PX2 = 1500, 1740
d.rounded_rectangle((PX1, 250, PX2, OUTY + 60), 18, outline=INK, width=LW)
text((PX1 + PX2) // 2, 262, 'Pi Pico (міст)', fb, anchor='ma')
for lab, y, c in (('3V3', q[14], RED), ('GND', q[14] + 230, INK), ('15', OUTY, GRN)):
    d.ellipse((PX1 - 18, y - 18, PX1 + 18, y + 18), outline=c, width=3, fill=(255, 255, 255))
    text(PX1 + 30, y, lab, fb, c, 'lm')
wire((PX1 - 18, q[14] + 230), (PX1 - 60, q[14] + 230), (PX1 - 60, q[14] + 246))
gnd(PX1 - 60, q[14] + 246)

# ---- деталі й замінник
by = OUTY + 110
text(60, by, 'Деталі:', fb)
text(60, by + 36, 'R1, R2 — 150 Ом · R3 — 75 Ом · R4 — 100 Ом · R5 — 100 кОм · R6 — 10 кОм · C1 — 0,01 мкФ (10 нФ) · '
     'C2 — 0,1 мкФ · мікросхема 74HC04 · гніздо XLR', f)
text(60, by + 76, 'Замість R1 і R2 можна трансформатор 1:1 (розв\'язує землю пульта): XLR 2 і 3 — на первинну обмотку, '
     'паралельно їй 110 Ом;', f, GREY)
text(60, by + 108, 'вторинна: один кінець — GND, другий — через 330 Ом у точку А; тоді XLR 1 до GND не під\'єднувати.', f, GREY)
text(60, by + 148, 'Невикористані входи 74HC04 (ніжки 5, 9, 11, 13) — обов\'язково на GND. C2 — якомога ближче до ніжок 14 і 7.',
     f, GREY)
out = os.path.join(os.path.dirname(__file__), '..', '..', 'docs', 'img', 'skhema-aes-vkhid.png')
im.save(out)
print(os.path.abspath(out))
