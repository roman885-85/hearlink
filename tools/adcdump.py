#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Выгрузка записи «сырых» 32-битных слов PCM1808 с передатчика (с 3.20; запись — команды e19 / e21).
   python3 tools/adcdump.py <порт> <файл.bin> [проходів]
Шлёт e20, собирает строки «@@ADC <номер слова> <base64>», проверяет, что пришло всё и по порядку, и пишет слова
как есть (little-endian int32, Л П Л П … в порядке буфера I2S).
Частину рядків з'їдає рядок стану передавача, що вклинюється посеред (21.10: 180 з 2000), а пропущене тут ставиться
нулями — на записі музики це клацання, що в спектрі виглядають «верхами». Тому «проходів» (за замовчуванням 1): e20
повторюється, доки не збереться все або не скінчаться проходи; запис на передавачі від вивантаження не стирається."""
import os, sys, time, termios, select, fcntl, struct, base64

port, out = sys.argv[1], sys.argv[2]
passes = int(sys.argv[3]) if len(sys.argv) > 3 else 1
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
total, parts = None, {}
for npass in range(passes):
    os.write(fd, b'e20\n')
    t0, buf, done = time.time(), b'', False
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
                elif len(f) == 2 and total is not None:
                    # рядок, у який вклинився чужий текст, має інші знаки або іншу довжину — не брати (інакше
                    # коротший шматок зсунув би все далі)
                    try:
                        k = int(f[0])
                        b = base64.b64decode(f[1], validate=True)
                    except Exception as e:
                        print('зіпсований рядок', f[0], e)
                        continue
                    if k % 48 == 0 and len(b) == min(48, total - k) * 4:
                        parts[k] = b
    if total is not None and all(k in parts for k in range(0, total, 48)):
        break
    print('прохід %d: рядків %d з %d' % (npass + 1, len(parts), (total + 47) // 48 if total else 0), flush=True)
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
