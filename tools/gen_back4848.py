# -*- coding: utf-8 -*-
"""Задня сторона модуля передавача ESP32-4848S040 — що де стоїть і куди що підключати (набір 4.x):
docs/img/peredavach-4848-zvorot-<мова>.png (uk, en). Основа — фото docs/photos/esp32-4848s040-back.jpg з номерами
на деталях; праворуч — список і роз'єм H1 крупно. H1 — 2 × 4: нижній ряд (до краю плати) підписано на платі
«5V TXD RXD GND»; верхній — лише праве «GND», три інші без підпису (L1–L3 = IO40, IO2, IO1 за таблицею виробника,
від GND ліворуч — L1, L2, L3: власник 24.10).
Запуск: python3 tools/gen_back4848.py"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SUP = '/System/Library/Fonts/Supplemental/'
F = lambda n, b=False: ImageFont.truetype(SUP + ('Arial Bold.ttf' if b else 'Arial.ttf'), n)
INK, RED, BLU, VIO, ORG, GRY, GRN = (20, 20, 20), (200, 30, 30), (20, 90, 190), (120, 60, 170), (205, 95, 20), (120, 120, 120), (0, 130, 60)

# деталі на фото (координати фото 1200 × 900)
PARTS = [
    ((350, 672), 'H1', RED),
    ((440, 88), 'USB-C', BLU),
    ((730, 130), 'microSD', INK),
    ((578, 392), 'IPEX', BLU),
    ((858, 340), 'BOOT', INK),
    ((858, 602), 'RST', INK),
    ((670, 818), 'P1', GRY),
    ((330, 818), 'BAT', GRY),
    ((605, 78), 'speak', GRY),
]
TXT = {
    'title': ('Модуль передавача ESP32-4848S040 — зворотний бік: куди що підключати',
              'Transmitter module ESP32-4848S040 — back side: what goes where'),
    'sub': ('кришку знято; набір 4.x — з мостом Pi Pico; у дужках — номери ніжок Pico',
            'cover removed; kit 4.x with the Pi Pico bridge; numbers in brackets are Pico pin numbers'),
    'items': ([
        ('H1', 'роз\'єм до моста — див. праворуч унизу'),
        ('USB-C', 'живлення 5 В, ≥ 1 А (не від порту ноутбука); кабель для прошивки'),
        ('microSD', 'картка FAT32: тека UPDATE (оновлення), журнали, записи ефіру'),
        ('IPEX', 'антена — обов\'язково вставлена'),
        ('BOOT', 'утримувати при першому записі кабелем, якщо порт не відповідає'),
        ('RST', 'перезапуск'),
        ('P1', 'той самий порт (GND RX TX 5V) — не потрібен'),
        ('BAT', 'акумулятор — за бажанням'),
        ('speak', 'динамік — не використовується'),
    ], [
        ('H1', 'connector to the bridge — see lower right'),
        ('USB-C', '5 V power, ≥ 1 A (not from a laptop port); flashing cable'),
        ('microSD', 'FAT32 card: UPDATE folder (updates), logs, air recordings'),
        ('IPEX', 'antenna — must be plugged in'),
        ('BOOT', 'hold for the first cable flashing if the port does not respond'),
        ('RST', 'restart'),
        ('P1', 'the same port (GND RX TX 5V) — not needed'),
        ('BAT', 'battery — optional'),
        ('speak', 'speaker — not used'),
    ]),
    'h1': ('Роз\'єм H1 крупно (як на фото)', 'Connector H1 enlarged (as in the photo)'),
    'upper': ('верхній ряд', 'upper row'), 'lower': ('нижній ряд — до краю плати', 'lower row — board edge side'),
    'lpins': ('IO1 (BCK) → GP6 (9) · IO2 (LRCK) → GP7 (10) · IO40 (DATA) ← GP4 (6)',
              'IO1 (BCK) → GP6 (9) · IO2 (LRCK) → GP7 (10) · IO40 (DATA) ← GP4 (6)'),
    'lnote': ('верхній ряд на платі не підписано: від GND ліворуч — L1, L2, L3 (перевірено в наборі власника)',
              'the upper row is unlabelled on the board: from GND to the left — L1, L2, L3 (verified in the owner\'s kit)'),
    'hnote': ('BCK і LRCK — такти від передавача, DATA — звук від моста',
              'BCK and LRCK are clocks from the transmitter, DATA is audio from the bridge'),
    'cells_low': ([('5V', '→ D1 → VSYS (39)', RED), ('TXD', '→ GP5 (7)', VIO), ('RXD', 'вільний', GRY), ('GND', '→ GND (8)', INK)],
                  [('5V', '→ D1 → VSYS (39)', RED), ('TXD', '→ GP5 (7)', VIO), ('RXD', 'free', GRY), ('GND', '→ GND (8)', INK)]),
    'cells_up': ([('L3 · IO1', '→ GP6 (9)', ORG), ('L2 · IO2', '→ GP7 (10)', ORG), ('L1 · IO40', '← GP4 (6)', ORG), ('GND', 'запасна', INK)],
                 [('L3 · IO1', '→ GP6 (9)', ORG), ('L2 · IO2', '→ GP7 (10)', ORG), ('L1 · IO40', '← GP4 (6)', ORG), ('GND', 'spare', INK)]),
}


def draw(lang):
    i = 0 if lang == 'uk' else 1
    W, H = 2000, 1040
    im = Image.new('RGB', (W, H), (255, 255, 255))
    d = ImageDraw.Draw(im)
    d.text((W // 2, 40), TXT['title'][i], font=F(36, True), fill=INK, anchor='mm')
    d.text((W // 2, 84), TXT['sub'][i], font=F(24), fill=(90, 90, 90), anchor='mm')
    ph = Image.open(os.path.join(ROOT, 'docs/photos/esp32-4848s040-back.jpg')).convert('RGB')
    k = 0.95
    ph = ph.resize((int(ph.width * k), int(ph.height * k)), Image.LANCZOS)
    ox, oy = 30, 120
    im.paste(ph, (ox, oy))
    for n, ((x, y), name, c) in enumerate(PARTS, 1):
        cx, cy = ox + x * k, oy + y * k
        r = 22
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=c, width=5)
        d.text((cx, cy), str(n), font=F(24, True), fill=c, anchor='mm')
    # список
    lx, ly = 1200, 140
    for n, (name, s) in enumerate(TXT['items'][i], 1):
        y = ly + (n - 1) * 46
        c = PARTS[n - 1][2]
        d.ellipse((lx, y, lx + 34, y + 34), outline=c, width=4)
        d.text((lx + 17, y + 17), str(n), font=F(20, True), fill=c, anchor='mm')
        d.text((lx + 48, y + 17), name, font=F(23, True), fill=c, anchor='lm')
        d.text((lx + 170, y + 17), s, font=F(19), fill=INK, anchor='lm')
    # H1 крупно
    hx, hy, cw, ch = 1200, 640, 180, 110
    d.text((hx, hy - 50), TXT['h1'][i], font=F(26, True), fill=RED)
    for row, key in ((0, 'cells_up'), (1, 'cells_low')):
        for col, (nm, dest, c) in enumerate(TXT[key][i]):
            x0, y0 = hx + col * cw, hy + row * ch
            d.rectangle((x0, y0, x0 + cw - 8, y0 + ch - 8), fill=(250, 250, 250), outline=(30, 30, 30), width=3)
            d.rectangle((x0 + cw / 2 - 18, y0 + 12, x0 + cw / 2 + 10, y0 + 40), fill=(20, 20, 20))
            d.text((x0 + (cw - 8) / 2, y0 + 62), nm, font=F(24, True), fill=c, anchor='mm')
            d.text((x0 + (cw - 8) / 2, y0 + 88), dest, font=F(17), fill=c, anchor='mm')
    d.line((hx, hy + 2 * ch + 8, hx + 4 * cw - 8, hy + 2 * ch + 8), fill=(122, 90, 16), width=6)
    d.text((hx + 2 * cw, hy + 2 * ch + 30), TXT['lower'][i], font=F(18), fill=(90, 90, 90), anchor='mm')
    d.text((hx, hy + 2 * ch + 70), TXT['lpins'][i], font=F(20, True), fill=ORG)
    d.text((hx, hy + 2 * ch + 104), TXT['lnote'][i], font=F(19), fill=INK)
    d.text((hx, hy + 2 * ch + 132), TXT['hnote'][i], font=F(18), fill=(90, 90, 90))
    out = os.path.join(ROOT, 'docs/img/peredavach-4848-zvorot-%s.png' % lang)
    im.save(out)
    print('ok', os.path.basename(out))


if __name__ == '__main__':
    draw('uk')
    draw('en')
