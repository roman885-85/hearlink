#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Эталон для аппаратного шифра набора (hearlink/sec.h): то же построение — AES-128-CTR и подпись AES-128-CBC-MAC —
считается на компьютере системным openssl. Числа из вывода вписаны в самопроверку прошивки (команда порта B):
плата сверяет с ними то, что выдал её аппаратный AES.
Пакет: ключ шифра 00…0f, ключ подписи 20…2f, одноразовое число 01 05000000 09000000 000000, открытая часть — 14 байт
(i*3), данные — 212 байт ((i*7+1) mod 256)."""
import subprocess, os, binascii
d = os.path.join(os.path.dirname(__file__), '../../build/host')
os.makedirs(d, exist_ok=True)
enc = bytes(range(16)); mac = bytes(range(32, 48))
nonce = bytes([1, 5, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0])
aad = bytes((i * 3) & 255 for i in range(14))
pt = bytes((i * 7 + 1) & 255 for i in range(212))


def ossl(mode, key, iv, data, extra=()):
    a, b = os.path.join(d, 'kat_in.bin'), os.path.join(d, 'kat_out.bin')
    open(a, 'wb').write(data)
    subprocess.check_call(['openssl', 'enc', mode, '-K', binascii.hexlify(key).decode(), '-iv', binascii.hexlify(iv).decode(), '-in', a, '-out', b] + list(extra))
    return open(b, 'rb').read()


ct = ossl('-aes-128-ctr', enc, nonce + bytes(4), pt)
pad = lambda x: x + bytes((-len(x)) % 16)
m = nonce + bytes([len(aad), len(pt) & 255, len(pt) >> 8, 0x48]) + pad(aad) + pad(ct)
c = ossl('-aes-128-cbc', mac, bytes(16), m, ['-nopad'])
tag = c[-16:-8]
print('начало шифртекста:', ', '.join('0x%02x' % b for b in ct[:8]))
print('подпись:          ', ', '.join('0x%02x' % b for b in tag))
