// Проверка программного запасного шифра (tools/host/seccore.h; в прошивку не входит — владелец выбрал аппаратный AES):
//  — ChaCha20: блок из RFC 8439 (п. 2.3.2) и сверка длинного сообщения с системным openssl (LibreSSL, «-chacha»);
//  — SipHash-2-4: эталоны авторов (ключ 00…0f; пустое сообщение, один байт, 15 байт) и подача кусками;
//  — пакет целиком: «закрыть — открыть», и что подмена любого бита (данных, открытой части, подписи, числа) замечается.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include "seccore.h"

static int fails = 0;
static void check(bool ok, const char *what) {
  printf("  %s: %s\n", what, ok ? "да" : "НЕТ");
  if (!ok) fails++;
}
static std::string hex(const uint8_t *p, size_t n) {
  std::string s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02x", p[i]);
    s += b;
  }
  return s;
}

int main() {
  printf("шифр и подпись пакетов:\n");
  {   // RFC 8439, 2.3.2
    uint8_t key[32], nonce[12] = { 0, 0, 0, 9, 0, 0, 0, 0x4a, 0, 0, 0, 0 }, zero[64] = {}, out[64];
    for (int i = 0; i < 32; i++) key[i] = i;
    secc::chachaXor(key, nonce, 1, zero, out, 64);
    static const uint8_t want[16] = { 0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4 };
    check(!memcmp(out, want, 16), "ChaCha20: блок из RFC 8439");
  }
  {   // сверка с системным openssl: счётчик (4 байта) и число (12 байт) вместе — его 16-байтный «iv»
    uint8_t key[32], nonce[12], msg[333], mine[333];
    srand(20261007);
    for (auto &b : key) b = rand();
    for (auto &b : nonce) b = rand();
    for (auto &b : msg) b = rand();
    secc::chachaXor(key, nonce, 0, msg, mine, sizeof(msg));
    FILE *f = fopen("build/host/sec_msg.bin", "wb");
    fwrite(msg, 1, sizeof(msg), f);
    fclose(f);
    std::string cmd = "openssl enc -chacha -K " + hex(key, 32) + " -iv 00000000" + hex(nonce, 12) + " -in build/host/sec_msg.bin -out build/host/sec_ref.bin 2>/dev/null";
    uint8_t ref[333];
    bool ran = system(cmd.c_str()) == 0;
    f = ran ? fopen("build/host/sec_ref.bin", "rb") : NULL;
    bool got = f && fread(ref, 1, sizeof(ref), f) == sizeof(ref);
    if (f) fclose(f);
    if (!got) printf("  (openssl с «-chacha» не нашёлся — сверка пропущена)\n");
    else check(!memcmp(ref, mine, sizeof(ref)), "ChaCha20: 333 байта совпали с системным openssl");
  }
  {   // SipHash-2-4, эталоны авторов
    uint8_t key[16], msg[64];
    for (int i = 0; i < 16; i++) key[i] = i;
    for (int i = 0; i < 64; i++) msg[i] = i;
    auto h = [&](size_t n) {
      secc::Sip s;
      secc::sipInit(s, key);
      secc::sipUpdate(s, msg, n);
      return secc::sipFinal(s);
    };
    check(h(0) == 0x726fdb47dd0e0e31ULL, "SipHash: пустое сообщение");
    check(h(1) == 0x74f839c593dc67fdULL, "SipHash: один байт");
    check(h(15) == 0xa129ca6149be45e5ULL, "SipHash: 15 байт");
    bool same = true;
    for (size_t n = 0; n <= 64; n++) {   // кусками по 1, 3, 5… байт — то же самое
      secc::Sip s;
      secc::sipInit(s, key);
      size_t pos = 0, step = 1;
      while (pos < n) {
        size_t m = step < n - pos ? step : n - pos;
        secc::sipUpdate(s, msg + pos, m);
        pos += m;
        step += 2;
      }
      same = same && secc::sipFinal(s) == h(n);
    }
    check(same, "SipHash: подача кусками даёт то же");
  }
  {   // пакет целиком
    secc::Keys k;
    for (int i = 0; i < 32; i++) k.enc[i] = 100 + i;
    for (int i = 0; i < 16; i++) k.mac[i] = 200 + i;
    uint8_t nonce[12] = { 1, 5, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0 }, aad[14], plain[212], pkt[212], out[212], tag[8];
    for (int i = 0; i < 14; i++) aad[i] = i * 3;
    for (int i = 0; i < 212; i++) plain[i] = i * 7 + 1;
    memcpy(pkt, plain, sizeof(pkt));
    secc::seal(k, nonce, aad, sizeof(aad), pkt, sizeof(pkt), tag);
    check(memcmp(pkt, plain, sizeof(pkt)) != 0, "пакет: зашифрованное не похоже на исходное");
    check(secc::open(k, nonce, aad, sizeof(aad), pkt, sizeof(pkt), tag, out) && !memcmp(out, plain, sizeof(out)), "пакет: открывается и совпадает");
    int missed = 0, total = 0;
    auto bad = [&]() {
      uint8_t o[212];
      total++;
      if (secc::open(k, nonce, aad, sizeof(aad), pkt, sizeof(pkt), tag, o)) missed++;
    };
    for (size_t i = 0; i < sizeof(pkt) * 8; i++) {   // каждый бит данных
      pkt[i / 8] ^= 1 << (i % 8);
      bad();
      pkt[i / 8] ^= 1 << (i % 8);
    }
    for (size_t i = 0; i < sizeof(aad) * 8; i++) {   // каждый бит открытой части
      aad[i / 8] ^= 1 << (i % 8);
      bad();
      aad[i / 8] ^= 1 << (i % 8);
    }
    for (size_t i = 0; i < 64; i++) {                // каждый бит подписи
      tag[i / 8] ^= 1 << (i % 8);
      bad();
      tag[i / 8] ^= 1 << (i % 8);
    }
    for (size_t i = 0; i < 96; i++) {                // каждый бит одноразового числа (другой номер пакета, другая эпоха)
      nonce[i / 8] ^= 1 << (i % 8);
      bad();
      nonce[i / 8] ^= 1 << (i % 8);
    }
    secc::Keys k2 = k;
    k2.mac[3] ^= 1;
    uint8_t o[212];
    total++;
    if (secc::open(k2, nonce, aad, sizeof(aad), pkt, sizeof(pkt), tag, o)) missed++;   // чужой ключ
    printf("  пакет: подмен испробовано %d, не замечено %d\n", total, missed);
    if (missed) fails++;
    check(secc::open(k, nonce, aad, sizeof(aad), pkt, sizeof(pkt), tag, out), "пакет: после всех проб исходный по-прежнему открывается");
  }
  printf(fails ? "ШИФР: ЕСТЬ ОШИБКИ (%d)\n" : "шифр: всё сошлось\n", fails);
  return fails ? 1 : 0;
}
