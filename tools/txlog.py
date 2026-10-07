#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Пишет всю печать платы с отметками времени и шлёт ей команды в назначенные секунды.
   python3 tools/txlog.py <порт> <секунд> [секунда:команда …]
Нужен для долгих прогонов, где важна каждая строка (например, слепок «чёрного ящика» радио): watch2.py
показывает только разобранные показатели."""
import os, sys, time, termios, select, fcntl, struct

port, secs = sys.argv[1], float(sys.argv[2])
plan = sorted((float(a.split(':', 1)[0]), a.split(':', 1)[1]) for a in sys.argv[3:])
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))   # обе линии управления отпустить (см. port.py)
t0, buf = time.time(), b''
while time.time() - t0 < secs:
    now = time.time() - t0
    while plan and plan[0][0] <= now:
        c = plan.pop(0)[1]
        os.write(fd, (c + '\n').encode())
        print('%6.1f  >>> %s' % (now, c), flush=True)
    if select.select([fd], [], [], 0.1)[0]:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            print('%6.1f  %s' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip()), flush=True)
os.close(fd)
