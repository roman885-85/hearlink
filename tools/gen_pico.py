# -*- coding: utf-8 -*-
"""Плата моста (Raspberry Pi Pico / YD-RP2040) — усі 40 виводів, занятi — кольором і куди йдуть:
docs/img/mist-pico-vyvody-<мова>.png (uk, en). Розкладка — стандартна Pico (YD-RP2040 така сама); призначення —
bridge/src/main.c (PIN_*). Запуск: python3 tools/gen_pico.py"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SUP = '/System/Library/Fonts/Supplemental/'
F = lambda n, b=False: ImageFont.truetype(SUP + ('Arial Bold.ttf' if b else 'Arial.ttf'), n)
INK, RED, GRN, VIO, ORG, BLU, AUD, GRY = (20, 20, 20), (200, 30, 30), (0, 130, 60), (120, 60, 170), (205, 95, 20), (20, 90, 190), (0, 121, 107), (150, 150, 150)

LEFT = ['GP0', 'GP1', 'GND', 'GP2', 'GP3', 'GP4', 'GP5', 'GND', 'GP6', 'GP7', 'GP8', 'GP9', 'GND', 'GP10', 'GP11',
        'GP12', 'GP13', 'GND', 'GP14', 'GP15']                       # ніжки 1…20 згори вниз
RIGHT = ['VBUS · Vout', 'VSYS · Vin', 'GND', '3V3_EN · 23', '3V3(OUT)', 'ADC_VREF · 29', 'GP28', 'AGND', 'GP27', 'GP26', 'RUN', 'GP22', 'GND',
         'GP21', 'GP20', 'GP19', 'GP18', 'GND', 'GP17', 'GP16']      # ніжки 40…21 згори вниз
USE = {   # ніжка: (колір, uk, en)
    1: (VIO, 'PCM1808 SCK — головний такт 12,5 МГц', 'PCM1808 SCK — 12.5 MHz master clock'),
    2: (ORG, 'PCM1808 BCK', 'PCM1808 BCK'),
    3: (INK, 'PCM1808 GND', 'PCM1808 GND'),
    4: (ORG, 'PCM1808 LRC', 'PCM1808 LRC'),
    5: (ORG, 'PCM1808 OUT (звук АЦП)', 'PCM1808 OUT (ADC audio)'),
    6: (ORG, '→ передавач IO40 (L1): звук + стан', '→ transmitter IO40 (L1): audio + status'),
    7: (VIO, '← передавач TXD (IO43): команди', '← transmitter TXD (IO43): commands'),
    8: (INK, 'GND передавача (H1)', 'transmitter GND (H1)'),
    9: (ORG, '← передавач IO1 (L3): BCK', '← transmitter IO1 (L3): BCK'),
    10: (ORG, '← передавач IO2 (L2): LRCK', '← transmitter IO2 (L2): LRCK'),
    18: (INK, 'GND схеми цифрового входу', 'digital input circuit GND'),
    20: (GRN, '← цифровий вхід AES / S/PDIF', '← digital input AES / S/PDIF'),
    39: (RED, 'VSYS (на YD — Vin) ← 5V передавача через D1; → PCM1808 +5V', 'VSYS (YD: Vin) ← transmitter 5V via D1; → PCM1808 +5V'),
    36: (RED, '3V3 → PCM1808 3.3V, схема цифрового входу', '3V3 → PCM1808 3.3V, digital input circuit'),
}


def draw(lang):
    i = 1 if lang == 'uk' else 2
    W, H = 1900, 1250
    im = Image.new('RGB', (W, H), (255, 255, 255))
    d = ImageDraw.Draw(im)
    d.text((W // 2, 40), ('Міст: Raspberry Pi Pico / YD-RP2040 — виводи' if lang == 'uk' else
                          'Bridge: Raspberry Pi Pico / YD-RP2040 — pins'), font=F(36, True), fill=INK, anchor='mm')
    d.text((W // 2, 84), ('вигляд згори, USB угорі; у кружечках — номери ніжок; кольором — зайняті мостом; після «·» — напис на YD-RP2040' if lang == 'uk' else
                          'top view, USB at the top; numbers in circles are pin numbers; coloured — used by the bridge; after “·” — the YD-RP2040 label'),
           font=F(23), fill=(90, 90, 90), anchor='mm')
    bx0, bx1, by0, by1 = 690, 1210, 150, 1170
    d.rounded_rectangle((bx0, by0, bx1, by1), 24, fill=(30, 110, 60), outline=(10, 60, 30), width=4)
    d.rounded_rectangle((900, 120, 1000, 190), 10, fill=(200, 200, 200), outline=(80, 80, 80), width=3)   # USB
    d.text((950, 160), 'USB', font=F(22, True), fill=INK, anchor='mm')
    d.rectangle((890, 560, 1010, 680), fill=(40, 40, 40))
    d.text((950, 620), 'RP2040', font=F(22, True), fill=(230, 230, 230), anchor='mm')
    # кнопки й світлодіоди
    d.rounded_rectangle((870, 250, 920, 290), 6, fill=(240, 240, 240), outline=INK, width=2)
    d.text((895, 305), 'BOOT', font=F(18, True), fill=(255, 255, 255), anchor='mm')
    d.ellipse((990, 245, 1014, 269), fill=(120, 170, 255), outline=INK)
    d.text((1002, 285), 'LED', font=F(16, True), fill=(255, 255, 255), anchor='mm')
    d.text((1002, 305), 'GP25', font=F(14), fill=(220, 220, 220), anchor='mm')
    d.rounded_rectangle((870, 900, 920, 940), 6, fill=(240, 240, 240), outline=INK, width=2)
    d.text((895, 955), 'USR*', font=F(16, True), fill=(255, 255, 255), anchor='mm')
    d.text((895, 975), 'GP24', font=F(14), fill=(220, 220, 220), anchor='mm')
    d.ellipse((990, 905, 1020, 935), fill=(255, 255, 255), outline=INK)
    d.text((1005, 955), 'RGB*', font=F(16, True), fill=(255, 255, 255), anchor='mm')
    d.text((1005, 975), 'GP23', font=F(14), fill=(220, 220, 220), anchor='mm')
    d.rounded_rectangle((980, 330, 1030, 370), 6, fill=(240, 240, 240), outline=INK, width=2)
    d.text((1005, 385), 'RST*', font=F(16, True), fill=(255, 255, 255), anchor='mm')
    step = 49
    for k in range(20):
        y = 220 + k * step
        for side, names, pinno in (('l', LEFT, k + 1), ('r', RIGHT, 40 - k)):
            x = bx0 + 24 if side == 'l' else bx1 - 24
            name = names[k]
            use = USE.get(pinno)
            c = use[0] if use else ((90, 90, 90) if 'GND' in name else GRY)
            d.ellipse((x - 14, y - 14, x + 14, y + 14), fill=(255, 255, 255), outline=c, width=4 if use else 2)
            d.text((x, y), str(pinno), font=F(15, True), fill=c if use else (110, 110, 110), anchor='mm')
            nx = x + 26 if side == 'l' else x - 26
            d.text((nx, y), name, font=F(17, True if use else False), fill=(255, 255, 255) if use else (200, 220, 205),
                   anchor='lm' if side == 'l' else 'rm')
            if use:
                ex = bx0 - 30 if side == 'l' else bx1 + 30
                d.line((x - 14 if side == 'l' else x + 14, y, ex, y), fill=c, width=3)
                d.text((ex - 8 if side == 'l' else ex + 8, y), use[i], font=F(19, True), fill=c,
                       anchor='rm' if side == 'l' else 'lm')
    foot = ('* лише на YD-RP2040: кнопка USR — перемкнути вхід (аналоговий ↔ цифровий), RGB — що звучить і рівень, RST — перезапуск. '
            'На Raspberry Pi Pico їх немає — міст працює й без них.' if lang == 'uk' else
            '* YD-RP2040 only: the USR button switches the input (analog ↔ digital), the RGB LED shows what plays and the level, RST restarts. '
            'A Raspberry Pi Pico has none of them — the bridge works without them.')
    d.text((40, 1200), foot, font=F(19), fill=INK)
    out = os.path.join(ROOT, 'docs/img/mist-pico-vyvody-%s.png' % lang)
    im.save(out)
    print('ok', os.path.basename(out))


if __name__ == '__main__':
    draw('uk')
    draw('en')
