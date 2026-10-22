# -*- coding: utf-8 -*-
"""Схема цифрового входу AES/EBU без мікросхеми — через трансформатор 1:1: docs/img/skhema-aes-vkhid.png.
   python3 bridge/tools/draw_aes_input_tr.py
Вхід Pico — логіка з тригером Шмітта (пороги ≈0,8 і 2,0 В): повний розмах AES (2–7 В) з трансформатора, зміщений до
≈1,3 В, перемикає його сам; резистор 1 кОм обмежує струм, коли розмах виходить за 0…3,3 В. Ділити до рівня S/PDIF
(0,5 В), як у переходниках Rane note 149, тут не треба — там далі стоїть мікросхема-приймач із підсилювачем."""
from PIL import Image, ImageDraw, ImageFont
import os

W, H = 1700, 1150
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


def res_h(x1, y, x2, label, c=INK):
    m = (x1 + x2) // 2
    wire((x1, y), (m - 40, y), c=c)
    d.rectangle((m - 40, y - 14, m + 40, y + 14), outline=INK, width=LW, fill=(255, 255, 255))
    wire((m + 40, y), (x2, y), c=c)
    text(m, y - 22, label, fb, anchor='md')


def res_v(x, y1, y2, label, c=INK, left=False):
    m = (y1 + y2) // 2
    wire((x, y1), (x, m - 40), c=c)
    d.rectangle((x - 14, m - 40, x + 14, m + 40), outline=INK, width=LW, fill=(255, 255, 255))
    wire((x, m + 40), (x, y2), c=c)
    if left:
        text(x - 24, m, label, fb, anchor='rm')
    else:
        text(x + 24, m, label, fb, anchor='lm')


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


def coil(x, y1, y2, right):
    # обмотка: півкола вздовж вертикалі
    n = 5
    h = (y2 - y1) / n
    for i in range(n):
        top = y1 + i * h
        box = (x - h / 2, top, x + h / 2, top + h)
        d.arc(box, 270, 90, fill=INK, width=LW) if right else d.arc(box, 90, 270, fill=INK, width=LW)



def cap_h(x1, y, x2, label, c=INK):
    m = (x1 + x2) // 2
    wire((x1, y), (m - 6, y), c=c)
    d.line((m - 6, y - 22, m - 6, y + 22), fill=INK, width=LW + 1)
    d.line((m + 6, y - 22, m + 6, y + 22), fill=INK, width=LW + 1)
    wire((m + 6, y), (x2, y), c=c)
    text(m, y - 30, label, fb, anchor='md')


text(W // 2, 30, 'Цифровий вхід AES/EBU → міст (Pi Pico) — без трансформатора (для перевірки)', ft, anchor='ma')
text(W // 2, 76, 'XH-M372 — звуковий модуль, сигнал AES (мегагерци) не пропускає · «3V3», «GND», «15» — надписи на Pico', f, GREY, 'ma')

# гніздо XLR
d.rounded_rectangle((60, 220, 280, 640), 18, outline=INK, width=LW)
text(170, 232, 'Гніздо XLR', fb, anchor='ma')
text(170, 266, 'кабель від', fs, GREY, 'ma')
text(170, 290, 'AES/EBU OUT', fs, GREY, 'ma')
text(170, 314, 'пульта WING', fs, GREY, 'ma')
for n, y in (('2', 400), ('3', 520), ('1', 600)):
    d.ellipse((250 - 18, y - 18, 250 + 18, y + 18), outline=INK, width=3, fill=(255, 255, 255))
    text(250, y, n, fb, anchor='mm')

# R1 110 поперек 2–3
wire((268, 400), (560, 400))
wire((268, 520), (420, 520))
dot(420, 400)
dot(420, 520)
res_v(420, 400, 520, 'R1  110 Ом')
# 3 → R5 100 → GND;  1 → GND
wire((420, 520), (470, 520), (470, 600))
res_h(470, 600, 650, 'R5  100 Ом')
wire((650, 600), (690, 600), (690, 620))
gnd(690, 620)
wire((268, 600), (330, 600), (330, 640))
gnd(330, 640)
# 2 → C2 0,1 → точка Н
cap_h(560, 400, 760, 'C2  0,1 мкФ')
N = 820
wire((760, 400), (N, 400))
dot(N, 400)
text(N, 426, 'Н', fb, BLUE, 'ma')
# Н → R2 1 кОм → Pico 15
SY = 300
wire((N, 400), (N, SY), (900, SY), c=GRN)
res_h(900, SY, 1180, 'R2  1 кОм', c=GRN)
wire((1180, SY), (1420, SY), c=GRN)
# Н → R6 10 кОм → точка Б (зміщення)
BY = 680
res_v(N, 400, BY, 'R6  10 кОм')
wire((N, BY), (1240, BY))
dot(1000, BY)
dot(1240, BY)
text(900, BY + 12, 'Б (≈1,3 В)', fb, BLUE, 'ma')
VY = 520
res_v(1240, VY, BY, 'R3  10 кОм', c=RED)
wire((1240, VY), (1420, VY), c=RED)
res_v(1240, BY, 860, 'R4  6,8 кОм')
gnd(1240, 860)
cap_v(1000, BY, 860, 'C1  0,1 мкФ')
gnd(1000, 860)

# Pico
PX1, PX2 = 1420, 1640
d.rounded_rectangle((PX1, 200, PX2, 940), 18, outline=INK, width=LW)
text((PX1 + PX2) // 2, 212, 'Pi Pico (міст)', fb, anchor='ma')
for lab, y, c in (('15', SY, GRN), ('3V3', VY, RED), ('GND', 880, INK)):
    d.ellipse((PX1 - 18, y - 18, PX1 + 18, y + 18), outline=c, width=3, fill=(255, 255, 255))
    text(PX1 + 30, y, lab, fb, c, 'lm')
wire((PX1 - 18, 880), (1340, 880), (1340, 890))
gnd(1340, 890)

by = 970
text(60, by, 'Нові деталі проти схеми з трансформатором: C2 — 0,1 мкФ, R5 — 100 Ом, R6 — 10 кОм. Решта та сама: R1 110 Ом,'
     ' R2 1 кОм, R3 10 кОм,', f)
text(60, by + 32, 'R4 6,8 кОм, C1 0,1 мкФ. Трансформатор XH-M372 прибрати.', f)
text(60, by + 72, 'Тут немає розв\'язки землі (контакт 1 — на GND): для перевірки годиться.', f, GREY)
text(60, by + 104, 'Для постійної роботи — імпульсний трансформатор (наприклад, зі старої мережевої карти).', f, GREY)
out = os.path.join(os.path.dirname(__file__), '..', '..', 'docs', 'img', 'skhema-aes-vkhid-bez-tr.png')
im.save(out)
print(os.path.abspath(out))
