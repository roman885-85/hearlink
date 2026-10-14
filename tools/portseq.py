#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Команды плате по расписанию, вся её печать — на экран (как port.py, но время каждой команды задаётся).
   python3 tools/portseq.py <порт> <секунд всего> <секунда:команда> …
Пример: … 200 3:"E0 4C3B10" 140:? 150:"E1 4C3B10" 175:?"""
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
    t = time.time() - t0
    if plan and t >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(fd, (c + '\n').encode())
        print('%6.1f  >>> %s' % (t, c), flush=True)
    if select.select([fd], [], [], 0.2)[0]:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            print('%6.1f  %s' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip()), flush=True)
os.close(fd)
