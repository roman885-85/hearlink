# -*- coding: utf-8 -*-
"""Порт USB моста (Pico) на столі: показати рядки стану й послати команди-літери в задані моменти.
   python3 bridge/tools/usbmon.py [порт] [секунд] [t:літера …]   (порт — /dev/cu.usbmodem*, за замовчуванням перший)
Наприклад: usbmon.py /dev/cu.usbmodem14601 8 2:t 5:t   — увімкнути й вимкнути тон.
Pico шле лише тоді, коли порт відкрито з DTR — тут DTR піднімається."""
import fcntl, glob, os, select, struct, sys, termios, time

args = sys.argv[1:]
port = args.pop(0) if args and args[0].startswith('/dev/') else (sorted(glob.glob('/dev/cu.usbmodem*')) or [None])[0]
if not port:
    sys.exit('порту моста немає (/dev/cu.usbmodem*)')
secs = float(args.pop(0)) if args and not ':' in args[0] else 6.0
plan = sorted((float(a.split(':', 1)[0]), a.split(':', 1)[1]) for a in args)
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746c, struct.pack('I', 0x002))   # TIOCMBIS: підняти DTR
t0, buf = time.time(), b''
while time.time() - t0 < secs:
    if plan and time.time() - t0 >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(fd, c.encode())
        print('%5.1f  >>> %s' % (time.time() - t0, c), flush=True)
    if select.select([fd], [], [], 0.1)[0]:
        try:
            buf += os.read(fd, 4096)
        except (BlockingIOError, OSError):
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            print('%5.1f  %s' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip()), flush=True)
os.close(fd)
