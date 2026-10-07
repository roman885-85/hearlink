#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Наблюдение за передатчиком при отладке защиты: раз в N секунд команда K (память, счётчики шифра), печать её ответа
и секунд с отказами драйвера.   python3 tools/kwatch.py <порт> <секунд> <шаг, с> [время:команда …]"""
import os, sys, time, termios, select, fcntl, struct, re
port, secs, step = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
plan = sorted((float(a.split(':', 1)[0]), a.split(':', 1)[1]) for a in sys.argv[4:])
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))
t0, buf, nextK, refused, secsSeen, airmax = time.time(), b'', 2.0, 0, 0, 0
while time.time() - t0 < secs:
    t = time.time() - t0
    if plan and t >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(fd, (c + '\n').encode())
        print('%5.0f  >>> %s' % (t, c), flush=True)
    if t >= nextK:
        os.write(fd, b'K\n')
        nextK += step
    if select.select([fd], [], [], 0.2)[0]:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            continue
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            s = line.decode('utf-8', 'replace').rstrip()
            t = time.time() - t0
            m = re.search(r'пакетів (\d+), відмов (\d+), не пішло в ефір (\d+), перезапусків радіо (\d+), до ефіру сер\. (\d+) макс\. (\d+)', s)
            if m:
                secsSeen += 1
                airmax = max(airmax, int(m.group(6)))
                if int(m.group(2)):
                    refused += 1
                    e = re.search(r'відмова драйвера: (\S+)', s)
                    print('%5.0f  ОТКАЗЫ: пакетов %s, отказов %s, перезапусков радио %s%s' % (t, m.group(1), m.group(2), m.group(4), ', код ' + e.group(1) if e else ''), flush=True)
            elif "пам'ять" in s:
                print('%5.0f  %s | секунд с отказами %d из %d, наиб. ожидание эфира %.0f мс' % (t, s.strip()[:230], refused, secsSeen, airmax / 1000.0), flush=True)
                airmax = 0
            elif 'додатково:' in s or 'запуск через' in s:
                print('%5.0f  %s' % (t, s.strip()[:200]), flush=True)
os.close(fd)
