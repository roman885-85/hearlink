# -*- coding: utf-8 -*-
"""Схеми набору 4.x («Next Edition», міст на Raspberry Pi Pico): загальна схема й підключення моста —
docs/img/skhema-4x-systema-<мова>.png, docs/img/skhema-4x-mist-<мова>.png (uk, en).
Запуск: python3 tools/gen_schema4.py
Виводи моста — з bridge/src/main.c (PIN_*), передавача — з docs/uk/hardware.md (роз'єм H1)."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_schema as g
from gen_schema import RED, BLK, BLU, GRN, VIO, ORG, AUD, GRY, line, text, block, pin, dot, gnd, res_h, res_v, cap_h, term

IMG = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'img')

T = {
    'sys_title': ('Набір 4.x: пульт → міст → передавач → приймачі', 'Kit 4.x: mixer → bridge → transmitter → receivers'),
    'mixer': ('Пульт (мікшер)', 'Mixer console'), 'mixer_sub': ('напр. Behringer WING', 'e.g. Behringer WING'),
    'aes_out': ('AES/EBU або S/PDIF', 'AES/EBU or S/PDIF'), 'ana_out': ('лінійний вихід Л / П', 'line out L / R'),
    'pc': ('Комп\'ютер', 'Computer'), 'pc_sub': ('USB: звукова карта', 'USB: sound card'),
    'aes_in': ('схема цифрового входу', 'digital input circuit'), 'aes_in_sub': ('AES/EBU, коаксіал, оптика — окремо', 'AES/EBU, coaxial, optical — separate'),
    'adc': ('АЦП PCM1808', 'PCM1808 ADC'), 'adc_sub': ('модуль, 24 біти', 'module, 24-bit'),
    'bridge': ('Міст', 'Bridge'), 'bridge_sub': ('Raspberry Pi Pico / YD-RP2040', 'Raspberry Pi Pico / YD-RP2040'),
    'bridge_l1': ('вибір входу: цифровий · аналоговий · USB', 'input: digital · analog · USB'),
    'bridge_l2': ('24 біти, перерахунок частоти → 32 / 48 кГц', '24-bit, resampling → 32 / 48 kHz'),
    'bridge_l3': ('віднімання гулу радіо (аналог)', 'radio hum removal (analog)'),
    'tx': ('Передавач', 'Transmitter'), 'tx_sub': ('модуль ESP32-4848S040, екран 4″', 'ESP32-4848S040 module, 4″ screen'),
    'tx_l1': ('обробка, шифр AES-128', 'processing, AES-128 cipher'), 'tx_l2': ('радіо 2,4 ГГц (ESP-NOW)', '2.4 GHz radio (ESP-NOW)'),
    'rx': ('Приймачі', 'Receivers'), 'rx_sub': ('ESP32-S3 + навушники', 'ESP32-S3 + headphones'),
    'i2s': ('I2S: звук + стан моста', 'I2S: audio + bridge status'), 'uart': ('команди (UART)', 'commands (UART)'),
    'air': ('ефір', 'air'),
    'w_title': ('Передавач 4.x: міст (Pi Pico), АЦП PCM1808, цифровий вхід AES', 'Transmitter 4.x: bridge (Pi Pico), PCM1808 ADC, AES digital input'),
    'w_sub': ('числа в дужках — номери ніжок Pico; YD-RP2040 має ту саму розкладку', 'numbers in brackets are Pico pin numbers; YD-RP2040 has the same layout'),
    'from_mixer': ('З пульта: лінійний вихід', 'From the mixer: line out'),
    'from_mixer2': ('за потреби — через розв\'язку землі', 'if needed — through a ground isolator'),
    'L': ('Л', 'L'), 'R': ('П', 'R'), 'G': ('земля', 'ground'),
    'r10k': ('10 кОм', '10 kΩ'), 'c1u': ('1 мкФ', '1 µF'),
    'remove': ('зняти 2 вхідні конденсатори', 'remove the 2 input capacitors'),
    'remove2': ('модуля (фото в описі)', 'of the module (photo in the docs)'),
    'tx_block': ('Модуль ESP32-4848S040', 'ESP32-4848S040 module'), 'tx_block_sub': ('роз\'єм H1 на звороті', 'connector H1 on the back'),
    'audio': ('звук + стан', 'audio + status'), 'cmds': ('команди', 'commands'), 'clk': ('такти', 'clocks'),
    'mclk': ('головний такт', 'master clock'),
    'xlr': ('Цифровий вихід пульта', 'Mixer digital out'), 'xlr_sub': ('XLR, RCA або TOSLINK', 'XLR, RCA or TOSLINK'),
    'aes_pwr': ('живлення схеми: 3V3 (36) і GND моста', 'circuit supply: bridge 3V3 (36) and GND'),
    'usb': ('USB → комп\'ютер: вхід «USB», порт, перша прошивка', 'USB → computer: “USB” input, console, first flashing'),
    'diode': ('D1 1N5819', 'D1 1N5819'), 'diode_sub': ('Шотткі', 'Schottky'),
    'radio': ('радіо → приймачі', 'radio → receivers'),
    'notes': ((
        ('Що зробити, крім проводів', True),
        ('1. PCM1808: FMY (FMT), MD0, MD1 — на GND (формат I2S, такти дає міст). Міст сам вирівнює біт і ставить Л/П на місце.', False),
        ('2. PCM1808: зняти два бежеві вхідні конденсатори біля ніжок 13–14 мікросхеми — інакше аналог глухий (зріз ~280 Гц).', False),
        ('3. Живлення моста: від передавача (5V → D1 → VSYS) або від USB. Діод не дає двом джерелам 5 В зійтися, коли Pico на USB.', False),
        ('4. Земля спільна: GND передавача, моста, PCM1808 і схеми цифрового входу. Провід звуку з пульта — екранований.', False),
        ('5. Перша прошивка Pico — файлом bridge-full.uf2 (див. «Встановлення»); далі міст оновлює передавач сам.', False),
    ), (
        ('Besides the wires', True),
        ('1. PCM1808: FMY (FMT), MD0, MD1 to GND (I2S format, the bridge drives the clocks). The bridge aligns the bit and orders L/R itself.', False),
        ('2. PCM1808: remove the two beige input capacitors next to pins 13–14 of the chip — otherwise the analog is dull (~280 Hz roll-off).', False),
        ('3. Bridge power: from the transmitter (5V → D1 → VSYS) or from USB. The diode keeps the two 5 V sources apart when the Pico is on USB.', False),
        ('4. Common ground: transmitter, bridge, PCM1808 and the digital input circuit. Use a shielded cable for audio from the mixer.', False),
        ('5. First flashing of the Pico — the bridge-full.uf2 file (see “Installation”); after that the transmitter updates the bridge itself.', False),
    )),
}


def tr(k, lang):
    v = T[k]
    return v[0] if lang == 'uk' else v[1]


def arrow(x1, y1, x2, y2, c=BLK, w=3, lab=None, lc=None):
    line([(x1, y1), (x2, y2)], c, w)
    import math
    a = math.atan2(y2 - y1, x2 - x1)
    p = [(x2, y2), (x2 - 14 * math.cos(a) + 7 * math.sin(a), y2 - 14 * math.sin(a) - 7 * math.cos(a)),
         (x2 - 14 * math.cos(a) - 7 * math.sin(a), y2 - 14 * math.sin(a) + 7 * math.cos(a))]
    g.o.append('<polygon points="%s" fill="%s"/>' % (' '.join('%g,%g' % q for q in p), c))
    if lab:
        text((x1 + x2) / 2, (y1 + y2) / 2 - 9, lab, 12.5, 'middle', lc or c)


def save(name, W, H):
    body = '<rect width="%d" height="%d" fill="#fff"/>%s' % (W, H, '\n'.join(g.o))
    head = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="Helvetica, Arial, sans-serif">'
    del g.o[:]
    big = os.path.join(IMG, name + '.x2.svg')
    open(big, 'w', encoding='utf-8').write(head % (W, H, W * 2, H * 2) + body + '</svg>')
    subprocess.call(['sips', '-s', 'format', 'png', big, '--out', os.path.join(IMG, name + '.png')],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.remove(big)
    print('ok', name + '.png')


def system(lang):
    W, H = 1340, 460
    text(W / 2, 32, tr('sys_title', lang), 19, 'middle', bold=True)
    block(30, 70, 240, 220, tr('mixer', lang), tr('mixer_sub', lang))
    text(150, 140, tr('aes_out', lang), 13, 'middle', GRN, True)
    text(150, 230, tr('ana_out', lang), 13, 'middle', AUD, True)
    block(30, 320, 240, 100, tr('pc', lang), tr('pc_sub', lang))
    block(330, 85, 220, 80, tr('aes_in', lang), tr('aes_in_sub', lang))
    block(330, 200, 220, 80, tr('adc', lang), tr('adc_sub', lang))
    arrow(270, 135, 330, 125, GRN)
    arrow(270, 225, 330, 240, AUD)
    block(610, 70, 310, 350, tr('bridge', lang), tr('bridge_sub', lang))
    for i, k in enumerate(('bridge_l1', 'bridge_l2', 'bridge_l3')):
        text(765, 160 + i * 26, tr(k, lang), 12.5, 'middle', '#333')
    text(765, 270, 'GP15 ← AES', 12, 'middle', GRN)
    text(765, 292, 'GP0–GP3 ↔ PCM1808', 12, 'middle', AUD)
    text(765, 314, 'USB ↔ ' + tr('pc', lang).lower(), 12, 'middle', BLU)
    arrow(550, 125, 610, 140, GRN)
    arrow(550, 240, 610, 240, AUD)
    arrow(270, 370, 610, 370, BLU, 3)
    block(1070, 70, 240, 200, tr('tx', lang), tr('tx_sub', lang))
    text(1190, 160, tr('tx_l1', lang), 12.5, 'middle', '#333')
    text(1190, 186, tr('tx_l2', lang), 12.5, 'middle', '#333')
    arrow(920, 130, 1070, 130, ORG, 3)
    text(995, 118, tr('i2s', lang), 11.5, 'middle', ORG)
    arrow(1070, 200, 920, 200, VIO, 3)
    text(995, 222, tr('uart', lang), 11.5, 'middle', VIO)
    block(1070, 330, 240, 100, tr('rx', lang), tr('rx_sub', lang))
    arrow(1190, 270, 1190, 330, BLU, 3)
    for k in range(3):
        g.o.append('<path d="M %d 282 q 10 18 0 36" fill="none" stroke="%s" stroke-width="2.5"/>' % (1205 + k * 12, BLU))
    text(1250, 306, tr('air', lang), 13, 'start', BLU, True)
    save('skhema-4x-systema-' + lang, W, H)


def wiring(lang):
    W, H = 1510, 1000
    text(W / 2, 34, tr('w_title', lang), 19, 'middle', bold=True)
    text(W / 2, 58, tr('w_sub', lang), 13, 'middle', '#555')
    # аналоговий вхід
    text(40, 104, tr('from_mixer', lang), 13.5, bold=True)
    text(40, 122, tr('from_mixer2', lang), 12, c='#555')
    for y, nm, rn, rs, cn, pinn in ((200, tr('L', lang), 'R1', 'R3', 'C1', 'LIN'), (300, tr('R', lang), 'R2', 'R4', 'C2', 'RIN')):
        term(60, y, nm)
        line([(68, y), (330, y)], AUD, 3)
        res_h(95, y, rn + ' ' + tr('r10k', lang))
        dot(195, y)
        line([(195, y), (195, y + 14)], AUD, 3)
        res_v(195, y + 14, rs, tr('r10k', lang), side='r')
        gnd(195, y + 70)
        cap_h(275, y, cn + ' ' + tr('c1u', lang))
        pin(330, y, pinn, 'l')
    term(60, 440, tr('G', lang))
    line([(68, 440), (330, 440)], BLK, 2.5)
    pin(330, 440, '⏚ (GND)', 'l')
    # PCM1808, міст, передавач — блоки спершу, щоб не закрили підписи виводів
    block(330, 120, 230, 400, 'PCM1808', tr('adc_sub', lang))
    block(760, 120, 290, 620, tr('bridge', lang), 'Pi Pico / YD-RP2040')
    block(1200, 120, 280, 330, tr('tx_block', lang), tr('tx_block_sub', lang))
    rows = [('SCK', 'GP0 (1)', VIO, tr('mclk', lang)), ('BCK', 'GP1 (2)', ORG, ''), ('LRC', 'GP2 (4)', ORG, ''),
            ('OUT', 'GP3 (5)', ORG, ''), ('3.3V', '3V3 OUT (36)', RED, ''), ('+5V', 'VSYS (39)', RED, ''),
            ('GND', 'GND (3)', BLK, '')]
    for i, (a, b, c, lab) in enumerate(rows):
        y = 170 + i * 44
        line([(560, y), (760, y)], c, 3 if c in (ORG, VIO) else 2.5)
        pin(560, y, a, 'r')
        pin(760, y, b, 'l')
        if lab:
            text(660, y - 8, lab, 11.5, 'middle', c)
    line([(560, 490), (600, 490)])
    pin(560, 490, 'FMY, MD1, MD0', 'r')
    gnd(600, 490)
    text(445, 548, tr('remove', lang), 13, 'middle', RED, True)
    text(445, 566, tr('remove2', lang), 12, 'middle', RED)
    # міст
    up, lo = ('H1 верх', 'H1 низ') if lang == 'uk' else ('H1 upper', 'H1 lower')
    right = [('GP4 (6)', 'IO40 · ' + up + ' L1', ORG, tr('audio', lang), '→'), ('GP5 (7)', 'TXD (IO43) · ' + lo + ' 2', VIO, tr('cmds', lang), '←'),
             ('GP6 (9)', 'IO1 · ' + up + ' L3', ORG, 'BCK', '←'), ('GP7 (10)', 'IO2 · ' + up + ' L2', ORG, 'LRCK', '←'), ('GND (8)', 'GND · ' + lo + ' 4', BLK, '', '')]
    for i, (a, b, c, lab, d) in enumerate(right):
        y = 170 + i * 44
        line([(1050, y), (1200, y)], c, 3 if c in (ORG, VIO) else 2.5)
        pin(1050, y, a, 'r')
        pin(1200, y, b, 'l')
        if lab:
            text(1125, y - 8, (lab + ' ' + d) if d == '→' else (d + ' ' + lab), 11.5, 'middle', c)
    # 5 В від передавача через діод на VSYS
    y5 = 400
    line([(1200, y5), (1150, y5)], RED, 2.5)
    g.o.append('<polygon points="1150,%d 1128,%d 1128,%d" fill="#fff" stroke="%s" stroke-width="2.5"/>' % (y5, y5 - 11, y5 + 11, RED))
    line([(1124, y5 - 12), (1124, y5 + 12)], RED, 3)
    line([(1124, y5), (1050, y5)], RED, 2.5)
    text(1137, y5 - 18, tr('diode', lang), 11.5, 'middle', RED)
    text(1137, y5 + 30, tr('diode_sub', lang), 11, 'middle', '#555')
    pin(1050, y5, 'VSYS (39)', 'r')
    pin(1200, y5, '5V · ' + lo + ' 1', 'l')
    # передавач
    text(1335, 490, tr('radio', lang), 13, 'middle', BLU, True)
    # AES
    block(40, 610, 220, 120, tr('xlr', lang), tr('xlr_sub', lang))
    block(330, 620, 230, 100, tr('aes_in', lang), tr('aes_in_sub', lang))
    line([(260, 670), (330, 670)], GRN, 3)
    line([(560, 670), (760, 670)], GRN, 3)
    pin(760, 670, 'GP15 (20)', 'l')
    text(660, 660, 'AES/EBU · S/PDIF', 12, 'middle', GRN, True)
    text(445, 740, tr('aes_pwr', lang), 11.5, 'middle', '#555')
    # USB
    text(905, 768, tr('usb', lang), 12.5, 'middle', BLU, True)
    line([(905, 740), (905, 752)], BLU, 3)
    # примітки
    for i, (t, b) in enumerate(T['notes'][0 if lang == 'uk' else 1]):
        text(40, 820 + i * 27, t, 14 if b else 13, c='#222', bold=b)
    save('skhema-4x-mist-' + lang, W, H)


if __name__ == '__main__':
    os.makedirs(IMG, exist_ok=True)
    for lang in ('uk', 'en'):
        system(lang)
        wiring(lang)
