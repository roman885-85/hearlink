#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Положить файл с компьютера на карту памяти передатчика через порт (прошивка 2.12 и новее).
   python3 tools/sendfile.py <порт передатчика> <файл на компьютере> [путь на карте] [скорость, по умолчанию 230400]
Путь на карте — с косой черты; без него файл ляжет в корень под своим именем. 5 МБ на 230400 идут около пяти минут.
На время передачи отчёты передатчика в порт, журнал на карту и уход с занятого канала не работают."""
import os, sys, time, termios, select, fcntl, struct

port, src = sys.argv[1], sys.argv[2]
dst = sys.argv[3] if len(sys.argv) > 3 else '/' + os.path.basename(src)
baud = int(sys.argv[4]) if len(sys.argv) > 4 else 230400
data = open(src, 'rb').read()
BAUDS = {115200: termios.B115200, 230400: termios.B230400}


def setbaud(fd, b):
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = BAUDS[b]
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSADRAIN, a)


def crc16(d):
    c = 0xFFFF
    for x in d:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def rd(fd, secs):
    if select.select([fd], [], [], secs)[0]:
        try:
            return os.read(fd, 4096)
        except BlockingIOError:
            return b''
    return b''


fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
setbaud(fd, 115200)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))   # отпустить обе линии управления
time.sleep(2.5)
while rd(fd, 0.3):
    pass
os.write(fd, ('Fu%s|%d|%d\n' % (dst, len(data), baud)).encode())
buf, t0 = b'', time.time()
while b'UPLOAD READY' not in buf and b'UPLOAD FAIL' not in buf and time.time() - t0 < 8:
    buf += rd(fd, 0.3)
if b'UPLOAD READY' not in buf:
    print('передатчик не готов принять файл:', buf[-200:].decode('utf-8', 'replace'))
    sys.exit(1)
blk = int(buf.split(b'UPLOAD READY')[1].split()[0])
time.sleep(0.15)
setbaud(fd, baud)
time.sleep(0.2)
while rd(fd, 0.05):
    pass
sent, retries, t0, shown = 0, 0, time.time(), -1
while sent < len(data):
    part = data[sent:sent + blk]
    pkt = b'\xa5\x5a' + struct.pack('<H', len(part)) + part + struct.pack('>H', crc16(part))
    off = 0
    while off < len(pkt):   # порт неблокирующий: пишем, сколько берёт
        try:
            off += os.write(fd, pkt[off:off + 1024])
        except BlockingIOError:
            time.sleep(0.005)
    ans, t1 = b'', time.time()
    while time.time() - t1 < 12 and not any(x in ans for x in (b'\x06K', b'\x15N', b'\x18E')):
        ans += rd(fd, 0.5)
    if b'\x06K' in ans:
        sent += len(part)
    elif b'\x18E' in ans:
        print('\nпередатчик прервал приём: карта не пишет')
        break
    else:
        retries += 1
        if retries > 30:
            print('\nслишком много повторов — прерываю')
            break
        time.sleep(0.3)
        while rd(fd, 0.05):
            pass
    pc = sent * 100 // len(data)
    if pc // 5 != shown:
        shown = pc // 5
        print('  %3d %%  %.0f с  %.1f КБ/с  повторов %d' % (pc, time.time() - t0, sent / 1024 / max(time.time() - t0, 0.1), retries), flush=True)
time.sleep(0.8)
setbaud(fd, 115200)
buf, t1 = b'', time.time()
while b'UPLOAD DONE' not in buf and b'UPLOAD FAIL' not in buf and time.time() - t1 < 15:
    buf += rd(fd, 0.5)
tail = buf[buf.rfind(b'UPLOAD'):].split(b'\n')[0].decode('utf-8', 'replace') if b'UPLOAD' in buf else '(ответа нет)'
print('итог:', tail, '| отправлено %d из %d байт за %.0f с' % (sent, len(data), time.time() - t0))
os.close(fd)
sys.exit(0 if b'UPLOAD DONE' in buf else 1)
