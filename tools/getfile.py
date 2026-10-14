#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Снять с карты передатчика кусок любого файла (например, записи звука) через порт.
   python3 tools/getfile.py <порт> <путь на карте> <файл на компьютере> [смещение] [длина]
Плата отвечает на команду Fx строками «=<48 байт в base64>*<сумма байтов>»; строки с неверной суммой запрашиваются
заново. Скорость — около 8 КБ в секунду (секунда записи 32 кГц — за 8 секунд)."""
import os, sys, time, termios, select, fcntl, struct, base64

port, remote, local = sys.argv[1], sys.argv[2], sys.argv[3]
off = int(sys.argv[4]) if len(sys.argv) > 4 else 0
length = int(sys.argv[5]) if len(sys.argv) > 5 else 1 << 30
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))

def ask(o, n):
    """Запросить n байт со смещения o; вернуть {смещение: 48 байт} для строк с верной суммой, размер файла и сколько отдано."""
    os.write(fd, ('Fx%s|%d|%d\n' % (remote, o, n)).encode())
    got, pos, buf, size, sent, last = {}, o, b'', None, None, time.time()
    while time.time() - last < 6:
        if not select.select([fd], [], [], 0.2)[0]:
            continue
        try:
            d = os.read(fd, 65536)
        except OSError:
            d = b''
        if not d:
            continue
        last = time.time()
        buf += d
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            line = line.strip()
            if line.startswith(b'=') and b'*' in line:
                body, _, cs = line[1:].rpartition(b'*')
                try:
                    raw = base64.b64decode(body, validate=True)
                    ok = len(cs) == 4 and (sum(raw) & 0xFFFF) == int(cs, 16)
                except Exception:
                    raw, ok = b'', False
                if ok:
                    got[pos] = raw
                pos += 48          # строка, верная или нет, занимает своё место
            elif b'\xd0\xba\xd1\x96\xd0\xbd\xd0\xb5\xd1\x86\xd1\x8c \xd1\x88\xd0\xbc\xd0\xb0\xd1\x82\xd0\xba\xd0\xb0' in line:   # «кінець шматка»
                t = line.decode('utf-8', 'replace')
                nums = [int(x) for x in ''.join(ch if ch.isdigit() else ' ' for ch in t).split()]
                if len(nums) >= 3:
                    sent, size = nums[0], nums[2]
                return got, size, sent
            elif 'не відкрився'.encode() in line:
                return got, -1, 0
    return got, size, sent

time.sleep(1.0)
try:
    os.read(fd, 65536)
except OSError:
    pass
data, t0 = {}, time.time()
got, size, sent = ask(off, length)
if size == -1:
    print('файл на карте не открылся:', remote); sys.exit(1)
if size is None:
    print('плата не ответила на Fx (прошивка до 2.68?)'); sys.exit(1)
length = min(length, max(0, size - off))
data.update(got)
want = list(range(off, off + length, 48))
for attempt in range(6):
    bad = [o for o in want if o not in data or (len(data[o]) != 48 and o + len(data[o]) != off + length)]
    if not bad:
        break
    for o in bad:
        g, _, _ = ask(o, min(48, off + length - o))
        data.update(g)
bad = [o for o in want if o not in data]
with open(local, 'wb') as f:
    for o in want:
        f.write(data.get(o, b'\0' * 48))
os.close(fd)
print('%s: %d байт со смещения %d (файл на карте %d байт), %.0f с%s' % (local, length, off, size, time.time() - t0, '; НЕ СНЯТО кусков: %d' % len(bad) if bad else ''))
sys.exit(1 if bad else 0)
