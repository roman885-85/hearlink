# -*- coding: utf-8 -*-
"""Схеми цифрового входу моста (вивід GP15 Pi Pico), українською й англійською — docs/img/skhema-dig-<варіант>-<мова>.png:
  aes-direct — AES/EBU (XLR) без трансформатора: зібрано й перевірено в наборі власника (21.10);
  aes-tr     — AES/EBU через імпульсний трансформатор 1:1 (розв'язка землі пульта);
  aes-hc04   — AES/EBU через 74HC04 (схема бібліотеки pico_spdif_rx + дільник 150/150/75 Ом);
  coax       — S/PDIF коаксіальний (RCA, 0,5 В): та сама схема 74HC04 без дільника;
  optical    — S/PDIF оптичний (TOSLINK): готовий модуль-приймач, логічний вихід — прямо на GP15.
Вхід Pico — логіка з тригером Шмітта; AES (2–7 В) перемикає його сам, S/PDIF коаксіал (0,5 В) — лише через підсилювач
(інвертор 74HC04 з резистором зворотного зв'язку). Міст бере 44,1 і 48 кГц.
Запуск: python3 tools/gen_schema_digital.py"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_schema as g
from gen_schema import RED, BLK, BLU, GRN, VIO, ORG, AUD, GRY, line, text, block, pin, dot, gnd, res_h, res_v, cap_h, cap_v
from gen_schema4 import save

S = AUD   # колір ланцюга сигналу

L = {
    'pico': ('Міст (Pi Pico)', 'Bridge (Pi Pico)'),
    'xlr': ('Гніздо XLR', 'XLR socket'), 'xlr_sub': ('AES/EBU OUT пульта', 'mixer AES/EBU OUT'),
    'rca': ('Гніздо RCA', 'RCA socket'), 'rca_sub': ('S/PDIF коаксіальний', 'coaxial S/PDIF'),
    'centre': ('центр', 'centre'), 'shield': ('екран', 'shield'),
    'nc1': ('контакт 1 — нікуди', 'pin 1 — not connected'),
    'bias': ('Б (≈1,3 В)', 'B (≈1.3 V)'), 'n': ('Н', 'N'), 'a': ('А', 'A'), 'b': ('Б', 'B'),
    'tr': ('трансформатор 1:1', '1:1 transformer'), 'tr_sub': ('імпульсний, широкосмуговий', 'pulse, wideband'),
    'tos': ('Приймач TOSLINK', 'TOSLINK receiver'), 'tos_sub': ('модуль на 3,3 В', '3.3 V module'),
    'verified': ('ПЕРЕВІРЕНО в наборі власника', 'VERIFIED in the owner\'s kit'),
    'notver': ('на приладі ще не перевірено', 'not yet tested on hardware'),
}
TITLE = {
    'aes-direct': ('Цифровий вхід AES/EBU → міст: без трансформатора', 'AES/EBU digital input → bridge: no transformer'),
    'aes-tr': ('Цифровий вхід AES/EBU → міст: через трансформатор 1:1', 'AES/EBU digital input → bridge: 1:1 transformer'),
    'aes-hc04': ('Цифровий вхід AES/EBU → міст: через 74HC04', 'AES/EBU digital input → bridge: with a 74HC04'),
    'coax': ('Цифровий вхід S/PDIF коаксіальний (RCA) → міст', 'Coaxial S/PDIF (RCA) digital input → bridge'),
    'optical': ('Цифровий вхід S/PDIF оптичний (TOSLINK) → міст', 'Optical S/PDIF (TOSLINK) digital input → bridge'),
}
NOTES = {
    'aes-direct': ((
        'Деталі: R1 110 Ом · R2 1 кОм · R3 10 кОм · R4 6,8 кОм · R5 100 Ом · R6 10 кОм · C1, C2 0,1 мкФ.',
        'Просто й надійно: розмах AES (2–7 В) сам перемикає вхід Pico; R2 обмежує струм. Землю пульта не розв\'язує —',
        'якщо з\'явиться гул на аналоговому вході, краще варіант із трансформатором.'), (
        'Parts: R1 110 Ω · R2 1 kΩ · R3 10 kΩ · R4 6.8 kΩ · R5 100 Ω · R6 10 kΩ · C1, C2 0.1 µF.',
        'Simple and reliable: the AES swing (2–7 V) switches the Pico input by itself; R2 limits the current. The mixer',
        'ground is not isolated — if hum appears on the analog input, prefer the transformer variant.')),
    'aes-tr': ((
        'Деталі: R1 110 Ом (100–120) · R2 1 кОм · R3 10 кОм · R4 6,8 кОм · C1 0,1 мкФ · трансформатор 1:1.',
        'Трансформатор — для цифрового сигналу (смуга ≥ 12 МГц, як у мережевих картах і переходниках AES/S/PDIF).',
        'Звуковий трансформатор 600 Ом (напр. модуль XH-M372) НЕ годиться — сигнал AES він не пропускає.'), (
        'Parts: R1 110 Ω (100–120) · R2 1 kΩ · R3 10 kΩ · R4 6.8 kΩ · C1 0.1 µF · 1:1 transformer.',
        'The transformer must be for digital signals (bandwidth ≥ 12 MHz, as in network cards and AES/S/PDIF adapters).',
        'A 600 Ω audio transformer (e.g. the XH-M372 module) will NOT do — it does not pass the AES signal.')),
    'aes-hc04': ((
        'Деталі: R1, R2 150 Ом · R3 75 Ом · R4 100 Ом · R5 100 кОм · R6 10 кОм · C1 0,01 мкФ · C2 0,1 мкФ · 74HC04.',
        'Дільник R1–R3 зводить AES до рівня S/PDIF; далі — схема бібліотеки pico_spdif_rx: інвертор із R6 підсилює,',
        'другий формує фронти. Невикористані входи 74HC04 (5, 9, 11, 13) — на GND; C2 — біля ніжок 14 і 7.'), (
        'Parts: R1, R2 150 Ω · R3 75 Ω · R4 100 Ω · R5 100 kΩ · R6 10 kΩ · C1 0.01 µF · C2 0.1 µF · 74HC04.',
        'The R1–R3 divider brings AES down to S/PDIF level; then the pico_spdif_rx library circuit: the inverter with R6',
        'amplifies, the second one squares the edges. Unused 74HC04 inputs (5, 9, 11, 13) to GND; C2 close to pins 14 and 7.')),
    'coax': ((
        'Деталі: R3 75 Ом · R4 100 Ом · R5 100 кОм · R6 10 кОм · C1 0,01 мкФ · C2 0,1 мкФ · 74HC04 · гніздо RCA.',
        'Коаксіальний S/PDIF — лише 0,5 В: прямо на Pico (як AES без трансформатора) його подавати не можна, потрібен',
        'підсилювач на 74HC04. R3 75 Ом — навантаження кабелю. Невикористані входи 74HC04 — на GND.'), (
        'Parts: R3 75 Ω · R4 100 Ω · R5 100 kΩ · R6 10 kΩ · C1 0.01 µF · C2 0.1 µF · 74HC04 · RCA socket.',
        'Coaxial S/PDIF is only 0.5 V: it cannot go straight to the Pico (as AES without a transformer does) — it needs',
        'the 74HC04 amplifier. R3 75 Ω terminates the cable. Unused 74HC04 inputs to GND.')),
    'optical': ((
        'Модуль-приймач TOSLINK на 3,3 В (напр. PLR135/T10, DLR1160, TORX179) дає готову логіку: OUT → R1 220 Ом → GP15.',
        'Модуль на 5 В (PLR135/T5, DLR1150, TORX173): VCC — від VSYS (39), а OUT — через дільник: 1 кОм до GP15,',
        'від GP15 2,2 кОм на GND (5 В на вхід Pico подавати не можна). C1 0,1 мкФ — прямо біля ніжок живлення модуля.'), (
        'A 3.3 V TOSLINK receiver module (e.g. PLR135/T10, DLR1160, TORX179) gives ready logic: OUT → R1 220 Ω → GP15.',
        'A 5 V module (PLR135/T5, DLR1150, TORX173): VCC from VSYS (39), and OUT through a divider: 1 kΩ to GP15,',
        '2.2 kΩ from GP15 to GND (never feed 5 V into a Pico input). C1 0.1 µF right at the module\'s supply pins.')),
}
COMMON = ('Спільне: «3V3», «GND», «15» — виводи Pico (3V3 OUT — ніжка 36, GP15 — ніжка 20). Частота пульта — 44,1 або 48 кГц.',
          'Common: “3V3”, “GND”, “15” are Pico pins (3V3 OUT is pin 36, GP15 is pin 20). Mixer sample rate: 44.1 or 48 kHz.')


def t(k, lang):
    return L[k][0 if lang == 'uk' else 1]


def coil(x, y0, n, right=True):
    d = 'M %g %g' % (x, y0)
    for _ in range(n):
        d += ' a 10 10 0 0 %d 0 20' % (1 if right else 0)
    g.o.append('<path d="%s" fill="none" stroke="%s" stroke-width="2.5"/>' % (d, BLK))


def pico(lang, y15=140):
    block(1080, 100, 190, 420, t('pico', lang))
    pin(1080, y15, '15', 'l')
    pin(1080, 330, '3V3', 'l')
    pin(1080, 460, 'GND', 'l')
    line([(1080, 460), (1040, 460)])
    gnd(1040, 460)


def bias(lang, x=600, ytop=270):
    """Н → R6 → Б; Б: R3 до 3V3, R4 і C1 на землю (варіанти без мікросхеми)."""
    line([(x, ytop), (x, 400)], S, 2.5)
    dot(x, 400)
    text(x - 12, 425, t('bias', lang), 13, 'end', BLU, True)
    line([(x, 400), (900, 400)], S, 2.5)
    dot(760, 400)
    line([(760, 400), (760, 426)], S, 2.5)
    cap_v(760, 430, 'C1', '0,1 µF' if lang == 'en' else '0,1 мкФ')
    line([(760, 434), (760, 455)], S, 2.5)
    gnd(760, 455)
    line([(1080, 330), (900, 330), (900, 337)], RED, 2.5)
    res_v(900, 337, 'R3', '10 kΩ' if lang == 'en' else '10 кОм', side='r')
    line([(900, 393), (900, 400)], S, 2.5)
    dot(900, 400)
    line([(900, 400), (900, 414)], S, 2.5)
    res_v(900, 414, 'R4', '6,8 kΩ'.replace(',', '.') if lang == 'en' else '6,8 кОм', side='r')
    gnd(900, 470)


def r(lang, uk, en):
    return uk if lang == 'uk' else en


def xlr(lang, show1=True):
    block(40, 100, 180, 380, t('xlr', lang), t('xlr_sub', lang))
    for y, n in ((200, '2'), (300, '3'), (400, '1')):
        pin(220, y, n, 'r')


def aes_direct(lang):
    xlr(lang)
    line([(220, 200), (600, 200)], S, 2.5)
    line([(220, 300), (380, 300)], S, 2.5)
    dot(300, 200); dot(300, 300)
    line([(300, 200), (300, 222)], S, 2.5); res_v(300, 222, 'R1', r(lang, '110 Ом', '110 Ω'), side='r'); line([(300, 278), (300, 300)], S, 2.5)
    line([(380, 300), (380, 314)], S, 2.5); res_v(380, 314, 'R5', r(lang, '100 Ом', '100 Ω'), side='r'); gnd(380, 370)
    line([(220, 400), (260, 400)]); gnd(260, 400)
    cap_h(480, 200, 'C2 ' + r(lang, '0,1 мкФ', '0.1 µF'))
    dot(600, 200)
    text(612, 222, t('n', lang), 13, 'start', BLU, True)
    line([(600, 200), (600, 140), (720, 140)], S, 2.5)
    res_h(720, 140, 'R2 ' + r(lang, '1 кОм', '1 kΩ'))
    line([(776, 140), (1080, 140)], GRN, 3)
    line([(600, 200), (600, 214)], S, 2.5)
    res_v(600, 214, 'R6', r(lang, '10 кОм', '10 kΩ'), side='r')
    bias(lang, 600, 270)


def aes_tr(lang):
    xlr(lang)
    text(130, 505, t('nc1', lang), 12, 'middle', '#555')
    line([(220, 200), (440, 200), (440, 210)], S, 2.5)
    line([(220, 300), (440, 300), (440, 290)], S, 2.5)
    dot(300, 200); dot(300, 300)
    line([(300, 200), (300, 222)], S, 2.5); res_v(300, 222, 'R1', r(lang, '110 Ом', '110 Ω'), side='r'); line([(300, 278), (300, 300)], S, 2.5)
    coil(440, 210, 4, True)
    coil(500, 210, 4, False)
    line([(462, 200), (462, 300)], BLK, 2.5); line([(478, 200), (478, 300)], BLK, 2.5)
    text(470, 330, t('tr', lang), 13, 'middle', bold=True)
    text(470, 348, t('tr_sub', lang), 11.5, 'middle', '#555')
    line([(500, 210), (500, 200), (600, 200), (600, 140), (720, 140)], S, 2.5)
    res_h(720, 140, 'R2 ' + r(lang, '1 кОм', '1 kΩ'))
    line([(776, 140), (1080, 140)], GRN, 3)
    line([(500, 290), (500, 300), (600, 300)], S, 2.5)
    bias(lang, 600, 300)


def hc04(lang, x0=650):
    """Від точки Б (x0, 200): R5 на землю, інвертор U1a з R6, U1b, вихід на GP15."""
    dot(x0, 200)
    text(x0, 186, t('b', lang), 13, 'middle', BLU, True)
    line([(x0, 200), (x0, 214)], S, 2.5)
    res_v(x0, 214, 'R5', r(lang, '100 кОм', '100 kΩ'), side='l')
    gnd(x0, 270)
    block(760, 150, 150, 330, '74HC04', 'U1')
    line([(x0, 200), (760, 200)], S, 2.5)
    pin(760, 200, '1', 'l')
    pin(760, 270, '2', 'l')
    pin(760, 330, '3', 'l')
    pin(910, 200, '4', 'r')
    pin(910, 330, '14', 'r')
    pin(910, 440, '7', 'r')
    dot(720, 200)
    line([(720, 200), (720, 207)], S, 2.5)
    res_v(720, 207, 'R6', r(lang, '10 кОм', '10 kΩ'), side='l')
    line([(720, 263), (720, 270), (760, 270)], S, 2.5)
    dot(740, 270)
    line([(740, 270), (740, 330), (760, 330)], S, 2.5)
    line([(910, 200), (990, 200), (990, 140), (1080, 140)], GRN, 3)
    line([(910, 330), (1080, 330)], RED, 2.5)
    dot(1010, 330)
    line([(1010, 330), (1010, 356)], RED, 2.5)
    cap_v(1010, 360, 'C2', r(lang, '0,1 мкФ', '0.1 µF'))
    line([(1010, 364), (1010, 380)], BLK, 2.5)
    gnd(1010, 380)
    line([(910, 440), (950, 440)])
    gnd(950, 440)
    text(835, 500, r(lang, '5, 9, 11, 13 → GND', '5, 9, 11, 13 → GND'), 12, 'middle', '#555')


def aes_hc04(lang):
    xlr(lang)
    line([(220, 200), (240, 200)], S, 2.5)
    res_h(240, 200, 'R1 ' + r(lang, '150 Ом', '150 Ω'))
    line([(296, 200), (430, 200)], S, 2.5)
    line([(220, 300), (240, 300)], S, 2.5)
    res_h(240, 300, 'R2 ' + r(lang, '150 Ом', '150 Ω'))
    line([(296, 300), (330, 300)], S, 2.5)
    gnd(330, 300)
    line([(220, 400), (260, 400)]); gnd(260, 400)
    dot(370, 200)
    text(378, 226, t('a', lang), 13, 'start', BLU, True)
    line([(370, 200), (370, 236)], S, 2.5)
    res_v(370, 236, 'R3', r(lang, '75 Ом', '75 Ω'), side='l')
    gnd(370, 292)
    cap_h(460, 200, 'C1 ' + r(lang, '0,01 мкФ', '0.01 µF'))
    line([(464, 200), (530, 200)], S, 2.5)
    res_h(530, 200, 'R4 ' + r(lang, '100 Ом', '100 Ω'))
    line([(586, 200), (650, 200)], S, 2.5)
    hc04(lang)


def coax(lang):
    block(40, 100, 180, 300, t('rca', lang), t('rca_sub', lang))
    pin(220, 200, t('centre', lang), 'r')
    pin(220, 300, t('shield', lang), 'r')
    line([(220, 300), (260, 300)]); gnd(260, 300)
    line([(220, 200), (430, 200)], S, 2.5)
    dot(370, 200)
    text(378, 226, t('a', lang), 13, 'start', BLU, True)
    line([(370, 200), (370, 236)], S, 2.5)
    res_v(370, 236, 'R3', r(lang, '75 Ом', '75 Ω'), side='l')
    gnd(370, 292)
    cap_h(460, 200, 'C1 ' + r(lang, '0,01 мкФ', '0.01 µF'))
    line([(464, 200), (530, 200)], S, 2.5)
    res_h(530, 200, 'R4 ' + r(lang, '100 Ом', '100 Ω'))
    line([(586, 200), (650, 200)], S, 2.5)
    hc04(lang)


def optical(lang):
    block(380, 100, 240, 400, t('tos', lang), t('tos_sub', lang))
    g.o.append('<rect x="350" y="250" width="30" height="40" rx="4" fill="#ddd" stroke="#333" stroke-width="2"/>')
    text(300, 245, r(lang, 'світловод', 'optical'), 12, 'middle', '#555')
    text(300, 262, r(lang, 'від пульта', 'cable in'), 12, 'middle', '#555')
    line([(250, 270), (348, 270)], RED, 4)
    pin(620, 140, 'OUT', 'r')
    pin(620, 330, 'VCC', 'r')
    pin(620, 460, 'GND', 'r')
    line([(620, 140), (760, 140)], S, 2.5)
    res_h(760, 140, 'R1 ' + r(lang, '220 Ом', '220 Ω'))
    line([(816, 140), (1080, 140)], GRN, 3)
    line([(620, 330), (1080, 330)], RED, 2.5)
    dot(720, 330)
    line([(720, 330), (720, 356)], RED, 2.5)
    cap_v(720, 360, 'C1', r(lang, '0,1 мкФ', '0.1 µF'))
    line([(720, 364), (720, 380)], BLK, 2.5)
    gnd(720, 380)
    line([(620, 460), (1080, 460)])


def draw(name, fn):
    for i, lang in enumerate(('uk', 'en')):
        W, H = 1320, 720
        text(W / 2, 36, TITLE[name][i], 20, 'middle', bold=True)
        ok = name == 'aes-direct'
        text(W / 2, 62, t('verified' if ok else 'notver', lang), 13.5, 'middle', GRN if ok else '#a15c00', True)
        pico(lang)
        fn(lang)
        for k, s in enumerate(NOTES[name][i]):
            text(40, 590 + k * 24, s, 14, c='#222')
        text(40, 590 + 3 * 24 + 8, COMMON[i], 13, c='#555')
        save('skhema-dig-%s-%s' % (name, lang), W, H)


if __name__ == '__main__':
    draw('aes-direct', aes_direct)
    draw('aes-tr', aes_tr)
    draw('aes-hc04', aes_hc04)
    draw('coax', coax)
    draw('optical', optical)
