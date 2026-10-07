#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сценарий на двух портах сразу: команды передатчику (t:) и приёмнику (r:) по секундам, печать — существенные строки обеих плат.
   python3 tools/duo.py <порт передатчика> <порт приёмника> <секунд> 5:t:J1 8:r:K0 …"""
import os, sys, time, termios, select, fcntl, struct, re


def op(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))
    return fd


tx, rx, secs = op(sys.argv[1]), op(sys.argv[2]), float(sys.argv[3])
plan = []
for a in sys.argv[4:]:
    t, who, cmd = a.split(':', 2)
    plan.append((float(t), who, cmd))
plan.sort()
buf, last, t0 = {tx: b'', rx: b''}, {}, time.time()
alive = -99.0   # приёмник печатает отчёты, только пока с порта что-то приходит
while time.time() - t0 < secs:
    if time.time() - t0 - alive >= 20:
        alive = time.time() - t0
        os.write(rx, b'\n')
    t = time.time() - t0
    if plan and t >= plan[0][0]:
        _, who, cmd = plan.pop(0)
        os.write(tx if who == 't' else rx, (cmd + '\n').encode())
        print('%5.1f  >>> %s: %s' % (t, 'передатчику' if who == 't' else 'приёмнику', cmd), flush=True)
    for fd in select.select([tx, rx], [], [], 0.2)[0]:
        try:
            buf[fd] += os.read(fd, 4096)
        except BlockingIOError:
            continue
        while b'\n' in buf[fd]:
            line, buf[fd] = buf[fd].split(b'\n', 1)
            s = line.decode('utf-8', 'replace').rstrip()
            t = time.time() - t0
            tag = 'ПРД' if fd == tx else 'ПРМ'
            if fd == rx:
                m = re.search(r'ПРИЙМАЧ к=(\d+)(.*)', s)
                if m:
                    f = re.search(r'кадрів (\d+)', s)
                    key = 'не подключён' if 'НЕ ПІДКЛЮЧЕНО' in s else 'сигнала нет' + (' (чужая подпись)' if 'чужим підписом' in s else '') if 'сигналу немає' in s else 'звук идёт'
                    if last.get('rx') != key:
                        last['rx'] = key
                        print('%5.1f  %s: %s%s' % (t, tag, key, (' — ' + s[s.find('|') + 2:][:110]) if key != 'звук идёт' else ' (кадров %s)' % (f.group(1) if f else '?')), flush=True)
                    continue
                if s.startswith('стан:') or 'виводи виходу' in s or not s.strip():
                    continue
            else:
                if 'ПЕРЕДАВАЧ к=' in s:
                    m = re.search(r'пакетів (\d+), відмов (\d+)', s)
                    key = 'отказы' if m and int(m.group(2)) else 'передаёт'
                    if last.get('tx') != key:
                        last['tx'] = key
                        print('%5.1f  %s: %s' % (t, tag, key), flush=True)
                    continue
                m = re.search(r'приймач (\w+) «(.*?)»: (.*?), гучність', s)
                if m:
                    if m.group(1) != '884A94' and 'не на зв' in m.group(3):
                        continue
                    key = m.group(3)
                    if last.get('peer' + m.group(1)) != key:
                        last['peer' + m.group(1)] = key
                        print('%5.1f  %s: в списке «%s» — %s' % (t, tag, m.group(2), key), flush=True)
                    continue
                if not s.strip():
                    continue
            print('%5.1f  %s: %s' % (t, tag, s[:200]), flush=True)
os.close(tx); os.close(rx)
