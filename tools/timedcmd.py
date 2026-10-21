# -*- coding: utf-8 -*-
"""Команди платі в задані моменти й увесь її друк у файл (з мітками часу).
   python3 tools/timedcmd.py <порт> <секунд> <файл> [t:команда …]   (t — секунди від відкриття порту)
Наприклад: timedcmd.py /dev/cu.usbserial-1460 60 log.txt 0:P 2:h2 20:P 45:"E1 4C3B10" 60:P"""
import fcntl, os, select, struct, sys, termios, time

port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
plan = sorted((float(a.split(':', 1)[0]), a.split(':', 1)[1]) for a in sys.argv[4:])
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))   # DTR і RTS відпустити (автоскидання ESP32)
log = open(out, 'w', encoding='utf-8')
t0, buf = time.time(), b''
while time.time() - t0 < secs:
    now = time.time() - t0
    while plan and now >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(fd, (c + '\n').encode())
        log.write('%6.1f  >>> %s\n' % (now, c))
    if select.select([fd], [], [], 0.1)[0]:
        try:
            buf += os.read(fd, 65536)
        except BlockingIOError:
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            log.write('%6.1f  %s\n' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip()))
    log.flush()
os.close(fd)
