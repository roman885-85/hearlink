#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Рисунки модулей набора для описания (docs/img/mod-*.png): плата, главные детали, подписи выводов.
Это рисунки, а не фотографии: фотографий своих модулей у проекта нет, а чужие (магазинов) выкладывать нельзя.
   python3 tools/gen_modules.py"""
import os
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'docs', 'img')
K = 2
def font(sz, bold=False):
    for p in ('/System/Library/Fonts/Supplemental/Arial Bold.ttf' if bold else '/System/Library/Fonts/Supplemental/Arial.ttf',
              '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if bold else '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        if os.path.exists(p): return ImageFont.truetype(p, sz * K)
    return ImageFont.load_default()
F, FB, FS = font(13), font(15, True), font(11)
BG, INK, MUTE = (247, 247, 244), (30, 30, 30), (110, 110, 110)
GREEN, BLUE, BLACK, PURPLE, RED = (22, 110, 70), (30, 80, 150), (35, 35, 38), (90, 50, 130), (160, 40, 40)
GOLD, SILVER, CHIP = (212, 175, 80), (190, 190, 195), (25, 25, 28)

class C:
    def __init__(s, w, h, title):
        s.im = Image.new('RGB', (w * K, h * K), BG); s.d = ImageDraw.Draw(s.im)
        s.text(14, 10, title, FB)
    def box(s, x, y, w, h, fill, r=6, outline=None): s.d.rounded_rectangle([x*K, y*K, (x+w)*K, (y+h)*K], r*K, fill=fill, outline=outline, width=K)
    def rect(s, x, y, w, h, fill): s.d.rectangle([x*K, y*K, (x+w)*K, (y+h)*K], fill=fill)
    def circ(s, x, y, r, fill, outline=None): s.d.ellipse([(x-r)*K, (y-r)*K, (x+r)*K, (y+r)*K], fill=fill, outline=outline, width=K)
    def text(s, x, y, t, f=None, fill=INK, anchor='la'): s.d.text((x*K, y*K), t, font=f or F, fill=fill, anchor=anchor)
    def line(s, pts, fill=MUTE, w=1): s.d.line([(x*K, y*K) for x, y in pts], fill=fill, width=w*K)
    def pins(s, x, y, n, step=12, vertical=True, labels=None, side='l', ink=INK):
        for i in range(n):
            px, py = (x, y + i * step) if vertical else (x + i * step, y)
            s.circ(px, py, 4, GOLD, (120, 95, 30))
            if labels and i < len(labels) and labels[i]:
                if vertical: s.text(px - 9 if side == 'l' else px + 9, py, labels[i], FS, ink, anchor='rm' if side == 'l' else 'lm')
                else: s.text(px, py + (12 if side == 'b' else -12), labels[i], FS, anchor='mm')
    def note(s, x, y, lines):
        for i, t in enumerate(lines): s.text(x, y + i * 17, t, FS, MUTE)
    def save(s, name): s.im.save(os.path.join(OUT, name)); print(name)

def tx():
    c = C(760, 430, 'ESP32-4848S040 — модуль передавача / transmitter module')
    # лицевая сторона
    c.box(30, 50, 300, 300, BLACK, 10); c.box(44, 64, 272, 272, (12, 14, 18), 4)
    c.box(60, 80, 240, 40, (40, 34, 28), 6); c.text(180, 100, 'ВІДРОДЖЕННЯ', F, (220, 190, 150), 'mm')
    for i in range(3): c.box(60 + i * 82, 135, 76, 44, (36, 32, 28) if i else (200, 150, 110), 6)
    c.box(60, 190, 240, 70, (30, 27, 24), 6)
    for i in range(24): c.rect(70 + i * 9, 215, 6, 22, (60, 170, 100) if i < 16 else (70, 70, 70))
    c.text(180, 360, 'екран 4″ 480×480, сенсор / 4″ touch screen', FS, MUTE, 'ma')
    # обратная сторона
    c.box(400, 50, 300, 300, GREEN, 10)
    c.box(430, 70, 90, 110, SILVER, 4); c.text(475, 125, 'ESP32-S3', FS, INK, 'mm'); c.text(475, 141, 'WROOM-1U', FS, INK, 'mm'); c.text(475, 157, 'N16R8', FS, INK, 'mm')
    c.circ(532, 82, 5, GOLD); c.text(545, 82, 'IPEX — антена / antenna', FS, (235, 235, 235), 'lm')
    c.box(600, 150, 70, 46, SILVER, 3); c.text(635, 173, 'microSD', FS, INK, 'mm')
    c.box(610, 300, 60, 36, SILVER, 3); c.text(640, 318, 'USB-C', FS, INK, 'mm')
    c.box(430, 306, 60, 30, CHIP, 3); c.text(460, 321, 'CH340', FS, (230, 230, 230), 'mm')
    c.box(540, 210, 44, 44, (40, 60, 140), 3); c.text(562, 232, 'реле', FS, (230, 230, 230), 'mm')
    c.text(424, 215, 'H1', FS, (235, 235, 235))
    c.pins(432, 240, 4, step=14, vertical=True, labels=['GND', 'L1', 'L2', 'L3'], side='l', ink=(240, 240, 240))
    c.pins(452, 240, 4, step=14, vertical=True, labels=['GND', 'RXD', 'TXD', '5V'], side='r', ink=(240, 240, 240))
    c.text(550, 360, 'зворотний бік / back side', FS, MUTE, 'ma')
    c.note(30, 385, ['L1–L3 = IO40, IO2, IO1 (порядок на роз\'ємі перевіряється командою H / order is found with the H command).',
                     'Вхід звуку — IO2. 3,3 В на роз\'ємах немає / no 3.3 V on the headers. Живлення 5 В ≥ 1 А.'])
    c.save('mod-esp32-4848s040.png')

def devkit():
    c = C(760, 330, 'ESP32-S3-DevKitC-1 N16R8 — плата приймача / receiver board')
    c.box(120, 70, 520, 150, BLACK, 8)
    c.box(140, 95, 130, 100, SILVER, 4); c.text(205, 135, 'ESP32-S3', FS, INK, 'mm'); c.text(205, 152, 'WROOM-1 N16R8', FS, INK, 'mm')
    c.box(590, 90, 44, 30, SILVER, 3); c.box(590, 170, 44, 30, SILVER, 3)
    c.text(650, 105, 'USB (міст / bridge)', FS, MUTE, 'lm'); c.text(650, 185, 'USB (рідний / native)', FS, MUTE, 'lm')
    c.circ(430, 145, 7, (240, 240, 240)); c.text(430, 165, 'RGB 48/38', FS, (230, 230, 230), 'ma')
    c.box(480, 100, 22, 14, SILVER, 2); c.box(480, 176, 22, 14, SILVER, 2)
    c.text(491, 92, 'BOOT', FS, (230, 230, 230), 'mb'); c.text(491, 200, 'RESET', FS, (230, 230, 230), 'ma')
    c.pins(290, 78, 22, step=13, vertical=False)   # выводы без подписей: порядок у плат разных производителей разный
    c.pins(290, 212, 22, step=13, vertical=False)
    c.note(30, 250, ['5, 6, 7 — ручка (A, B, кнопка) / knob;   8, 9 — екран SDA, SCL / display;   17, 18 — вихід звуку «+», «−» / audio out;',
                     '11, 12, 13 — PCM5102 (за бажанням / optional);   15, 16 — кнопки модуля (не використовуються / unused).',
                     'Розташування виводів на вашій платі звіряйте з написами на ній / check the silkscreen of your board.'])
    c.save('mod-esp32-s3-devkitc.png')

def m75():
    c = C(760, 300, 'M75: OLED SH1106 1,3″ 128×64 + енкодер EC11 / display with rotary encoder')
    c.box(60, 60, 420, 150, BLUE, 8)
    c.box(85, 78, 190, 114, BLACK, 4); c.box(96, 90, 168, 84, (10, 14, 20), 2)
    for i in range(14): c.rect(104 + i * 11, 160 - (8 + (i * 7) % 40), 8, 8 + (i * 7) % 40, (150, 215, 255))
    c.circ(380, 135, 38, SILVER, (120, 120, 125)); c.circ(380, 135, 24, (60, 60, 64)); c.line([(380, 135), (380, 114)], (230, 230, 230), 2)
    c.box(300, 80, 22, 14, SILVER, 2); c.box(300, 176, 22, 14, SILVER, 2); c.text(311, 72, 'CON', FS, (235, 235, 235), 'mb'); c.text(311, 198, 'BAK', FS, (235, 235, 235), 'ma')
    c.pins(500, 72, 9, step=15, vertical=True, labels=['GND', 'VCC → 3V3', 'SCL → 9', 'SDA → 8', 'PSH → 7', 'TRA → 5', 'TRB → 6', 'CON → 15', 'BAK → 16'], side='r')
    c.note(30, 232, ['VCC — тільки 3,3 В: підтяжки I2C на модулі йдуть до VCC / 3.3 V only: the I2C pull-ups go to VCC.',
                     'Порядок і назви виводів на вашому модулі можуть відрізнятися / pin order and names may differ on your module.'])
    c.save('mod-m75-oled-encoder.png')

def small(name, title, color, chip, left, right, notes, fname):
    c = C(760, 250, title)
    c.box(250, 60, 240, 120, color, 8)
    c.box(330, 95, 80, 50, CHIP, 3); c.text(370, 120, chip, FS, (235, 235, 235), 'mm')
    c.pins(262, 78, len(left), step=(96 // max(1, len(left) - 1)) if len(left) > 1 else 12, vertical=True, labels=left, side='l')
    c.pins(478, 78, len(right), step=(96 // max(1, len(right) - 1)) if len(right) > 1 else 12, vertical=True, labels=right, side='r')
    c.note(30, 200, notes)
    c.save(fname)

os.makedirs(OUT, exist_ok=True)
tx(); devkit(); m75()
small('max', 'MAX97220 — підсилювач навушників / headphone amplifier (за бажанням / optional)', RED, 'MAX97220',
      ['VCC', 'GND', 'R− ← 18', 'R+ ← 17'], ['L− ← 18', 'L+ ← 17', 'CTRL', 'R · G · L → навушники'],
      ['Різницевий вхід: «+» на L+ і R+, «−» на L− і R−; у меню приймача «Виводи: протифаза».',
       'Differential input: “+” to L+ and R+, “−” to L− and R−; receiver menu “Pins: antiphase”.'], 'mod-max97220.png')
small('pcm5102', 'PCM5102 — зовнішній ЦАП приймача / external receiver DAC (за бажанням, не перевірено / optional, untested)', PURPLE, 'PCM5102',
      ['VIN', 'GND', 'LCK ← 12', 'DIN ← 13', 'BCK ← 11', 'SCK → GND'], ['OUT L', 'GND', 'OUT R'],
      ['У меню приймача «Вихід звуку: PCM5102» (команда порту o1) / receiver menu “Audio out: PCM5102” (serial o1).'], 'mod-pcm5102.png')
small('pcm1808', 'PCM1808 — зовнішній АЦП передавача / external transmitter ADC (за бажанням, не перевірено / optional, untested)', PURPLE, 'PCM1808',
      ['LIN', 'GND', 'RIN'], ['5V / 3V3', 'GND', 'SCK ← IO43', 'BCK ← IO1', 'LRC ← IO2', 'OUT → IO40', 'FMT, MD → GND'],
      ['На передавачі: «Звук» → «Вхід: PCM1808» (команда порту a1). Поки він працює, передавач не друкує в порт.',
       'Transmitter: “Sound” → “Input: PCM1808” (serial a1). While it runs the transmitter prints nothing to the port.'], 'mod-pcm1808.png')
