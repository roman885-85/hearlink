#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Опыт «чем оживить вставшее радио передатчика» (итог 09.10: лёгкие толчки оживляют, но буфера драйвера теряются).
    (прошивка 2.50 и новее, порт передатчика).
   python3 tools/kicktest.py <порт> <минут>
Держит самооживление выключенным (Q0 раз в 45 с), ждёт, пока драйвер встанет (все отправки отклоняются две секунды
подряд), и пробует «толчки» по одному — от самого лёгкого: тот же канал заново, наблюдение за эфиром вкл/выкл,
другой канал и обратно, первая ступень оживления, вторая. Печатает, после какого толчка эфир вернулся.
В конце возвращает самооживление (Q3)."""
import os, re, sys, time, termios, select, fcntl, struct

port, minutes = sys.argv[1], float(sys.argv[2])
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))
t0 = time.time()
buf = b''
stat = re.compile(r'ПЕРЕДАВАЧ к=(\d+) .*?пакетів (\d+), відмов (\d+)')

def send(c):
    os.write(fd, (c + '\n').encode())
    print('%6.1f  >>> %s' % (time.time() - t0, c), flush=True)

def lines(wait):
    """читать wait секунд, вернуть разобранные строки состояния [(канал, пакетов, отказов)]"""
    global buf
    out, end = [], time.time() + wait
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        try:
            d = os.read(fd, 65536)
        except OSError:
            d = b''
        buf += d
        while b'\n' in buf:
            ln, buf = buf.split(b'\n', 1)
            s = ln.decode('utf-8', 'replace')
            m = stat.search(s)
            if m:
                out.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
            elif 'РАДІО СТАЛО' in s or 'rst:' in s or 'task_wdt' in s:
                print('%6.1f  %s' % (time.time() - t0, s.strip()[:150]), flush=True)
    return out

send('?')
lastQ0, dead, ch, stalls, cured = -100, 0, 0, 0, {}
while time.time() - t0 < minutes * 60:
    if time.time() - lastQ0 > 45:
        send('Q0')
        lastQ0 = time.time()
    for c, pk, ref in lines(1.0):
        ch = c
        dead = dead + 1 if ref >= 300 else 0
    if dead < 2:
        continue
    stalls += 1
    print('%6.1f  радио встало (канал %d) — остановка № %d' % (time.time() - t0, ch, stalls), flush=True)
    other = ch - 1 if ch > 1 else ch + 1
    winner = 'ничто'
    for name, cmds in (('тот же канал заново', ['c%d' % ch]), ('наблюдение вкл/выкл', ['A1', 'A0']),
                       ('другой канал и обратно', ['c%d' % other, 'c%d' % ch]), ('ступень 1', ['Q7']), ('ступень 2', ['Q8'])):
        for c in cmds:
            send(c)
            got = lines(1.3)
        got += lines(1.5)
        if got and got[-1][2] == 0 and got[-1][1] > 300:
            winner = name
            break
    cured[winner] = cured.get(winner, 0) + 1
    print('%6.1f  оживил: %s' % (time.time() - t0, winner), flush=True)
    dead = 0
    send('Q0')
    lastQ0 = time.time()
send('Q3')
lines(1.0)
print('итог: остановок %d; чем оживлено: %s' % (stalls, ', '.join('%s — %d' % kv for kv in cured.items()) or '—'))
