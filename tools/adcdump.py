#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Выгрузка записи «сырых» 32-битных слов PCM1808 с передатчика (с 3.20; запись — команды e19 / e21).
   python3 tools/adcdump.py <порт> <файл.bin>
Шлёт e20, собирает строки «@@ADC <номер слова> <base64>», проверяет, что пришло всё и по порядку, и пишет слова
как есть (little-endian int32, Л П Л П … в порядке буфера I2S)."""
import os, sys, time, termios, select, fcntl, struct, base64

port, out = sys.argv[1], sys.argv[2]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
TIOCMBIC, TIOCM_DTR, TIOCM_RTS = 0x8004746b, 0x002, 0x004
fcntl.ioctl(fd, TIOCMBIC, struct.pack('I', TIOCM_DTR | TIOCM_RTS))
time.sleep(1.0)
os.write(fd, b'e20\n')
t0, buf, total, parts, done = time.time(), b'', None, {}, False
while time.time() - t0 < 120 and not done:
    if select.select([fd], [], [], 0.5)[0]:
        try:
            buf += os.read(fd, 65536)
        except BlockingIOError:
            pass
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            s = line.decode('utf-8', 'replace').strip()
            i = s.find('@@ADC ')
            if i < 0:
                continue
            f = s[i + 6:].split(' ')
            if f[0] == 'begin':
                total = int(f[1])
            elif f[0] == 'end':
                done = True
            elif len(f) == 2:
                try:
                    parts[int(f[0])] = base64.b64decode(f[1])
                except Exception as e:
                    print('испорчена строка', f[0], e)
os.close(fd)
if total is None:
    sys.exit('нет записи: передатчик не ответил «@@ADC begin» (e19 — что с записью)')
data, at, bad = b'', 0, 0
while at < total:
    p = parts.get(at)
    if p is None:
        bad += 1
        p = b'\0' * (min(48, total - at) * 4)
    data += p
    at += len(p) // 4
open(out, 'wb').write(data[:total * 4])
print('слов %d, строк %d, пропало строк %d, %s' % (total, len(parts), bad, out))
