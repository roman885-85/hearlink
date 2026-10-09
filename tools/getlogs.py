# -*- coding: utf-8 -*-
# Снять с карты передатчика журналы /LOG/log-NNNN.txt через порт: getlogs.py <порт> <первый> <последний> <папка>
import os, sys, time, termios, select, fcntl, struct
port, a, b, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
t = termios.tcgetattr(fd)
t[0] = 0; t[1] = 0; t[3] = 0
t[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
t[4] = t[5] = termios.B115200
t[6][termios.VMIN] = 0; t[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, t)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))
def read_quiet(quiet, limit):
    buf, last, t0 = b'', time.time(), time.time()
    while time.time() - last < quiet and time.time() - t0 < limit:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                d = os.read(fd, 65536)
            except OSError:
                d = b''
            if d:
                buf += d; last = time.time()
    return buf
read_quiet(0.5, 2)
for n in range(a, b + 1):
    os.write(fd, ('Ft/LOG/log-%04d.txt\n' % n).encode())
    data = read_quiet(0.45, 40)
    open(os.path.join(out, 'log-%04d.txt' % n), 'wb').write(data)
os.close(fd)
