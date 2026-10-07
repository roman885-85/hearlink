#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Читает печать платы и по желанию шлёт ей команды.
   python3 tools/port.py <порт> [секунд] [команда …]
Команды уходят по одной через 2,5 с после открытия порта (плата успевает запуститься), с паузой 1,5 с."""
import os, sys, time, termios, select, fcntl, struct

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 12
cmds = sys.argv[3:]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
# обе линии управления отпустить: иначе схема автосброса ESP32 может держать плату в сбросе или в загрузчике
TIOCMBIC, TIOCM_DTR, TIOCM_RTS = 0x8004746b, 0x002, 0x004
fcntl.ioctl(fd, TIOCMBIC, struct.pack('I', TIOCM_DTR | TIOCM_RTS))
t0, buf, nxt = time.time(), b'', 2.5
while time.time() - t0 < secs:
    if cmds and time.time() - t0 >= nxt:
        c = cmds.pop(0)
        os.write(fd, (c + '\n').encode())
        print('%5.1f  >>> %s' % (time.time() - t0, c), flush=True)
        nxt = time.time() - t0 + 1.5
    if select.select([fd], [], [], 0.2)[0]:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            print('%5.1f  %s' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip()), flush=True)
os.close(fd)
