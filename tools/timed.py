#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Команды плате в заданные моменты (а не через 1,5 с, как у port.py) и печать порта.
   python3 tools/timed.py <порт> <секунд> <t1>:<команда> <t2>:<команда> …
Например: tools/timed.py /dev/cu.usbserial-1460 14 2:g0 6.5:e19=1500 10:g5 13:e19"""
import os, sys, time, termios, select, fcntl, struct

port, secs = sys.argv[1], float(sys.argv[2])
plan = sorted((float(x.split(':', 1)[0]), x.split(':', 1)[1]) for x in sys.argv[3:])
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))   # DTR и RTS отпустить — иначе автосброс держит плату
t0, buf = time.time(), b''
while time.time() - t0 < secs:
    if plan and time.time() - t0 >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(fd, (c + '\n').encode())
        print('%5.1f  >>> %s' % (time.time() - t0, c), flush=True)
    if select.select([fd], [], [], 0.1)[0]:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            print('%5.1f  %s' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip()), flush=True)
os.close(fd)
