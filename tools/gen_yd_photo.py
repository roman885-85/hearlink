# -*- coding: utf-8 -*-
"""Фото YD-RP2040 (плата моста власника, прямий знімок docs/photos/yd-rp2040-flat.jpg) з позначеними виводами, які потрібні мосту:
docs/img/mist-yd-rp2040-foto-<мова>.png (uk, en). Нижній ряд на фото — ніжки 1–20 (GP0…GP15, USB ліворуч), верхній —
40…21: на YD-RP2040 ніжку 39 (VSYS у Pico) підписано «Vin», 40 (VBUS) — «Vout», 37 — «23», 35 — «29».
Запуск: python3 tools/gen_yd_photo.py"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SUP = '/System/Library/Fonts/Supplemental/'
F = lambda n, b=False: ImageFont.truetype(SUP + ('Arial Bold.ttf' if b else 'Arial.ttf'), n)
INK, RED, GRN, VIO, ORG = (20, 20, 20), (200, 30, 30), (0, 130, 60), (120, 60, 170), (205, 95, 20)

BOT = {1: VIO, 2: ORG, 3: INK, 4: ORG, 5: ORG, 6: ORG, 7: VIO, 8: INK, 9: ORG, 10: ORG, 18: INK, 20: GRN}
TOP = {36: RED, 39: RED}
LEG = {
    'uk': [(1, 'GP0 → PCM1808 SCK'), (2, 'GP1 → PCM1808 BCK'), (3, 'GND → PCM1808 GND'), (4, 'GP2 → PCM1808 LRC'),
           (5, 'GP3 ← PCM1808 OUT'), (6, 'GP4 → передавач IO40 (L1)'), (7, 'GP5 ← передавач TXD'),
           (8, 'GND → передавач GND'), (9, 'GP6 ← передавач IO1 (L3)'), (10, 'GP7 ← передавач IO2 (L2)'),
           (18, 'GND → схема цифрового входу'), (20, 'GP15 ← цифровий вхід'), (36, '3V3 → PCM1808, схема входу'),
           (39, 'Vin (= VSYS) ← 5V через діод; → PCM1808 +5V')],
    'en': [(1, 'GP0 → PCM1808 SCK'), (2, 'GP1 → PCM1808 BCK'), (3, 'GND → PCM1808 GND'), (4, 'GP2 → PCM1808 LRC'),
           (5, 'GP3 ← PCM1808 OUT'), (6, 'GP4 → transmitter IO40 (L1)'), (7, 'GP5 ← transmitter TXD'),
           (8, 'GND → transmitter GND'), (9, 'GP6 ← transmitter IO1 (L3)'), (10, 'GP7 ← transmitter IO2 (L2)'),
           (18, 'GND → digital input circuit'), (20, 'GP15 ← digital input'), (36, '3V3 → PCM1808, input circuit'),
           (39, 'Vin (= VSYS) ← 5V via diode; → PCM1808 +5V')],
}
COL = dict(list(BOT.items()) + list(TOP.items()))


def draw(lang):
    src = Image.open(os.path.join(ROOT, 'docs/photos/yd-rp2040-flat.jpg')).convert('RGB')
    x0, y0, x1, y1, k = 40, 55, 460, 265, 3
    ph = src.crop((x0, y0, x1, y1)).resize(((x1 - x0) * k, (y1 - y0) * k), Image.LANCZOS)
    W, H = 1900, 920
    im = Image.new('RGB', (W, H), (255, 255, 255))
    d = ImageDraw.Draw(im)
    d.text((W // 2, 38), ('Плата моста YD-RP2040: які виводи під\'єднати' if lang == 'uk' else
                          'YD-RP2040 bridge board: which pins to connect'), font=F(34, True), fill=INK, anchor='mm')
    d.text((W // 2, 78), ('у кружечках — номер ніжки (як у Raspberry Pi Pico); написи на платі — номери GP' if lang == 'uk' else
                          'circles show the pin number (as on a Raspberry Pi Pico); the board prints GP numbers'),
           font=F(22), fill=(90, 90, 90), anchor='mm')
    ox, oy = 40, 150
    im.paste(ph, (ox, oy))
    P = lambda x, y: (ox + (x - x0) * k, oy + (y - y0) * k)
    for n, c in BOT.items():
        x, y = P(65.7 + (n - 1) * 19.23, 235)
        d.ellipse((x - 21, y - 21, x + 21, y + 21), outline=c, width=5)
        d.ellipse((x - 16, y + 96, x + 16, y + 128), fill=c)
        d.text((x, y + 112), str(n), font=F(17, True), fill=(255, 255, 255), anchor='mm')
        d.line((x, y + 21, x, y + 96), fill=c, width=3)
    for n, c in TOP.items():
        x, y = P(66 + (40 - n) * 19.23, 100.7)
        d.ellipse((x - 21, y - 21, x + 21, y + 21), outline=c, width=5)
        d.ellipse((x - 16, y - 118, x + 16, y - 86), fill=c)
        d.text((x, y - 102), str(n), font=F(17, True), fill=(255, 255, 255), anchor='mm')
        d.line((x, y - 21, x, y - 86), fill=c, width=3)
    lx, ly = 1340, 140
    for j, (n, t) in enumerate(LEG[lang]):
        y = ly + j * 47
        c = COL[n]
        d.ellipse((lx, y, lx + 32, y + 32), fill=c)
        d.text((lx + 16, y + 16), str(n), font=F(16, True), fill=(255, 255, 255), anchor='mm')
        d.text((lx + 44, y + 16), t, font=F(18, True if n in (39, 20) else False), fill=c if c != INK else INK, anchor='lm')
    note = ('Ніжку 39 на YD-RP2040 підписано «Vin» (у Pico — VSYS), 40 — «Vout» (VBUS, 5 В від USB). Кнопки: BOOT — запис, '
            'USR — вхід аналоговий ↔ цифровий, RST — перезапуск.' if lang == 'uk' else
            'Pin 39 is labelled “Vin” on the YD-RP2040 (VSYS on a Pico), pin 40 “Vout” (VBUS, 5 V from USB). Buttons: BOOT — '
            'writing, USR — input analog ↔ digital, RST — restart.')
    d.text((40, 870), note, font=F(19), fill=INK)
    out = os.path.join(ROOT, 'docs/img/mist-yd-rp2040-foto-%s.png' % lang)
    im.save(out)
    print('ok', os.path.basename(out))


if __name__ == '__main__':
    draw('uk')
    draw('en')
