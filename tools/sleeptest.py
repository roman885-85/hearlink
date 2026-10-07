#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка сна и пробуждения приёмников через порт передатчика (прошивка 1.6 и новее).
   python3 tools/sleeptest.py <порт передатчика> <время:команда> … [конец:<секунд>]
Раз в секунду спрашивает у передатчика список приёмников (P) и печатает их строки коротко; в назначенные секунды
шлёт команды. Пример: … 6:E0 20:E1 34:z1 50:z0 конец:64   (E0/E1 — выключить/включить приёмник, z1/z0 — эфир молчит/идёт)."""
import os, sys, time, termios, select, fcntl, struct, re

port = sys.argv[1]
plan, end = [], 30.0
for a in sys.argv[2:]:
    k, v = a.split(':', 1)
    if k in ('конец', 'end'):
        end = float(v)
    else:
        plan.append((float(k), v))
plan.sort()
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))
t0, buf, nextP, last = time.time(), b'', 1.0, {}
while time.time() - t0 < end:
    t = time.time() - t0
    if plan and t >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(fd, (c + '\n').encode())
        print('%5.1f  >>> %s' % (t, c), flush=True)
    if t >= nextP:
        os.write(fd, b'P\n')
        nextP += 1.0
    if select.select([fd], [], [], 0.1)[0]:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            s = line.decode('utf-8', 'replace').rstrip()
            t = time.time() - t0
            m = re.search(r'приймач (\w+) «(.*?)»: (.*?), гучність (\d+), сигнал (-?\d+) дБм.*запас (\d+) мс, втрати ([\d.]+) %, пропусків (\d+), провалів (\d+) мс, '
                          r'версія ([\d.]+), вихід (.*)$', s)
            if m:
                if 'не на зв' in m.group(3) and m.group(10) == '0.0':
                    continue   # запомненный, но выключенный приёмник
                key = (m.group(3), m.group(11))
                txt = '%s: %s | вихід %s | запас %s мс, втрати %s %%, пропусків %s, провалів %s мс, версія %s' % (
                    m.group(2), m.group(3), m.group(11), m.group(6), m.group(7), m.group(8), m.group(9), m.group(10))
                if last.get(m.group(1)) != key:      # печатать только перемены состояния
                    last[m.group(1)] = key
                    print('%5.1f  %s' % (t, txt), flush=True)
            elif 'ПЕРЕДАВАЧ к=' in s:
                if 'ПЕРЕДАЧА СТОЇТЬ' in s and not last.get('hold'):
                    last['hold'] = True
                    print('%5.1f  передавач: передача стоїть' % t, flush=True)
                elif 'ПЕРЕДАЧА СТОЇТЬ' not in s and last.get('hold'):
                    last['hold'] = False
                    print('%5.1f  передавач: передача йде' % t, flush=True)
            elif s.strip() and not s.startswith('  приймач') and 'приймачів у списку' not in s:
                print('%5.1f  %s' % (t, s[:170]), flush=True)
os.close(fd)
