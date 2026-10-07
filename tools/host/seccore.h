// Шифр и подпись пакетов — без железа и библиотек, поэтому тот же файл проверяется на компьютере по эталонам
// (tools/host/sec_test.cpp, запуск tools/host/run.sh).
//
// Шифрование — ChaCha20 (RFC 8439: ключ 32 байта, одноразовое число 12 байт, счётчик блоков с нуля).
// Подпись — SipHash-2-4 (ключ 16 байт, подпись 8 байт) по одноразовому числу, открытой части и шифртексту:
// сначала шифруем, потом подписываем; при приёме сначала проверяем подпись и только потом расшифровываем.
// Ключи шифра и подписи разные, оба выводятся из ключа набора (sec.h).
//
// Почему не AES из библиотеки: аппаратный AES ESP32-S3 через mbedTLS тратит ~35 мкс на каждый блок (захват, обмен
// с устройством), и AES-CCM выходил 1,2 мс на пакет — при 500 пакетах в секунду это 60 % процессора; передатчик
// на нём перестал успевать и замолчал (замер 07.10). Здесь на пакет уходят десятки микросекунд.
#pragma once
#include <stdint.h>
#include <string.h>

namespace secc {

static inline uint32_t rotl32(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}
static inline uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Один блок ключевого потока ChaCha20 (64 байта) для счётчика counter.
static void chachaBlock(const uint32_t key[8], uint32_t counter, const uint32_t nonce[3], uint32_t out[16]) {
  uint32_t s[16] = { 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, key[0], key[1], key[2], key[3],
                     key[4], key[5], key[6], key[7], counter, nonce[0], nonce[1], nonce[2] };
  uint32_t x[16];
  memcpy(x, s, sizeof(x));
#define SECC_QR(a, b, c, d) \
  x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16); \
  x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12); \
  x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 8); \
  x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 7);
  for (int i = 0; i < 10; i++) {
    SECC_QR(0, 4, 8, 12)
    SECC_QR(1, 5, 9, 13)
    SECC_QR(2, 6, 10, 14)
    SECC_QR(3, 7, 11, 15)
    SECC_QR(0, 5, 10, 15)
    SECC_QR(1, 6, 11, 12)
    SECC_QR(2, 7, 8, 13)
    SECC_QR(3, 4, 9, 14)
  }
#undef SECC_QR
  for (int i = 0; i < 16; i++) out[i] = x[i] + s[i];
}

// Зашифровать или расшифровать (это одно и то же). in и out могут совпадать.
static void chachaXor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t *in, uint8_t *out, size_t len) {
  uint32_t k[8], n[3], ks[16];
  for (int i = 0; i < 8; i++) k[i] = le32(key + 4 * i);
  for (int i = 0; i < 3; i++) n[i] = le32(nonce + 4 * i);
  while (len) {
    chachaBlock(k, counter++, n, ks);
    size_t m = len < 64 ? len : 64;
    const uint8_t *kb = (const uint8_t *)ks;   // процессоры ESP32 и компьютера — с младшим байтом вперёд, как и требует шифр
    for (size_t i = 0; i < m; i++) out[i] = in[i] ^ kb[i];
    in += m;
    out += m;
    len -= m;
  }
}

// SipHash-2-4: подпись 8 байт. Данные можно подавать кусками.
struct Sip {
  uint64_t v0, v1, v2, v3;
  uint8_t buf[8];
  uint32_t fill = 0;
  uint64_t total = 0;
};
static inline uint64_t rotl64(uint64_t x, int n) {
  return (x << n) | (x >> (64 - n));
}
static inline void sipRound(Sip &s) {
  s.v0 += s.v1; s.v1 = rotl64(s.v1, 13); s.v1 ^= s.v0; s.v0 = rotl64(s.v0, 32);
  s.v2 += s.v3; s.v3 = rotl64(s.v3, 16); s.v3 ^= s.v2;
  s.v0 += s.v3; s.v3 = rotl64(s.v3, 21); s.v3 ^= s.v0;
  s.v2 += s.v1; s.v1 = rotl64(s.v1, 17); s.v1 ^= s.v2; s.v2 = rotl64(s.v2, 32);
}
static inline void sipWord(Sip &s, uint64_t m) {
  s.v3 ^= m;
  sipRound(s);
  sipRound(s);
  s.v0 ^= m;
}
static void sipInit(Sip &s, const uint8_t key[16]) {
  uint64_t k0, k1;
  memcpy(&k0, key, 8);
  memcpy(&k1, key + 8, 8);
  s.v0 = k0 ^ 0x736f6d6570736575ULL;
  s.v1 = k1 ^ 0x646f72616e646f6dULL;
  s.v2 = k0 ^ 0x6c7967656e657261ULL;
  s.v3 = k1 ^ 0x7465646279746573ULL;
  s.fill = 0;
  s.total = 0;
}
static void sipUpdate(Sip &s, const uint8_t *p, size_t len) {
  s.total += len;
  while (len && s.fill) {   // добрать начатое слово
    s.buf[s.fill++] = *p++;
    len--;
    if (s.fill == 8) {
      uint64_t m;
      memcpy(&m, s.buf, 8);
      sipWord(s, m);
      s.fill = 0;
    }
  }
  while (len >= 8) {
    uint64_t m;
    memcpy(&m, p, 8);
    sipWord(s, m);
    p += 8;
    len -= 8;
  }
  while (len) {
    s.buf[s.fill++] = *p++;
    len--;
  }
}
static uint64_t sipFinal(Sip &s) {
  uint64_t b = s.total << 56;
  for (uint32_t i = 0; i < s.fill; i++) b |= (uint64_t)s.buf[i] << (8 * i);
  sipWord(s, b);
  s.v2 ^= 0xff;
  sipRound(s);
  sipRound(s);
  sipRound(s);
  sipRound(s);
  return s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
}

struct Keys {
  uint8_t enc[32];   // ключ шифра
  uint8_t mac[16];   // ключ подписи
};

// Подпись: одноразовое число, длина открытой части, открытая часть, шифртекст.
static uint64_t tagOf(const Keys &k, const uint8_t nonce[12], const uint8_t *aad, size_t aadLen, const uint8_t *ct, size_t len) {
  Sip s;
  sipInit(s, k.mac);
  sipUpdate(s, nonce, 12);
  uint8_t al = (uint8_t)aadLen;
  sipUpdate(s, &al, 1);
  if (aadLen) sipUpdate(s, aad, aadLen);
  if (len) sipUpdate(s, ct, len);
  return sipFinal(s);
}

// Зашифровать на месте и подписать (tag — 8 байт).
static void seal(const Keys &k, const uint8_t nonce[12], const uint8_t *aad, size_t aadLen, uint8_t *data, size_t len, uint8_t *tag) {
  chachaXor(k.enc, nonce, 0, data, data, len);
  uint64_t t = tagOf(k, nonce, aad, aadLen, data, len);
  memcpy(tag, &t, 8);
}

// Проверить подпись и расшифровать в out. Подпись не сошлась — false, out не тронут.
static bool open(const Keys &k, const uint8_t nonce[12], const uint8_t *aad, size_t aadLen, const uint8_t *in, size_t len, const uint8_t *tag, uint8_t *out) {
  uint64_t t = tagOf(k, nonce, aad, aadLen, in, len), got;
  memcpy(&got, tag, 8);
  uint64_t d = t ^ got;   // сравнение без раннего выхода
  if ((uint32_t)(d | (d >> 32)) != 0) return false;
  chachaXor(k.enc, nonce, 0, in, out, len);
  return true;
}

}   // namespace secc
