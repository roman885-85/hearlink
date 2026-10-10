// Защита набора (с версии 1.8, протокол 4): чужой приёмник не услышит звук, чужой передатчик не подсунет свой.
//
// У набора есть ключ — 32 случайных байта. Он рождается в передатчике и хранится в платах; в эфир и в порт не выходит.
// Всё, что идёт в эфир (звук, команды приёмникам, сведения приёмников), зашифровано и подписано ключами, выведенными
// из ключа набора. Считает АППАРАТНЫЙ AES микросхемы ESP32-S3 (требование владельца 07.10: «защита не должна
// нагружать процессор, а использовать аппаратные средства устройства»; «aes-128 вполне устроит»):
//   шифр    — AES-128 в режиме CTR (счётчик блоков);
//   подпись — AES-128 CBC-MAC по блоку «одноразовое число + длины», открытой части пакета и шифртексту; в пакет идут
//             8 байт. Сначала шифруем, потом подписываем; на приёме сначала проверяем подпись.
// Заголовок пакета идёт открыто (по нему приёмник узнаёт свой набор и канал), но тоже подписан — подменить его нельзя.
//
// К устройству AES обращаемся напрямую, поблочно (записали 16 байт в его регистры — запустили — прочли 16 байт),
// взяв на время пакета общий замок шифровального устройства. Так пакет стоит десятки микросекунд, без выделения
// памяти и без канала DMA. Как НЕ надо (замеры 07.10 на плате, пакет 212 байт):
//   — готовый AES-CCM из mbedTLS: 1,2 мс на пакет (библиотека на каждый блок заново захватывает устройство и гонит
//     его через DMA) — 60 % процессора при 500 пакетах в секунду, передатчик замолчал через несколько секунд;
//   — AES-CTR и AES-CBC из mbedTLS по одному обращению на пакет: 94 мкс.
// Программный запасной шифр (ChaCha20 + SipHash, 80 мкс) в прошивку не входит — владельцу он не нужен; его
// проверка лежит в tools/host.
//
// Повторы. У каждого пакета своё одноразовое число: вид пакета + номер включения передатчика («эпоха») + номер пакета.
// Эпоха хранится в передатчике и растёт с каждым его включением; приёмник помнит наибольшую и не принимает
// меньшую, а внутри эпохи — пакеты с номером не больше уже принятого. Вчерашнюю запись эфира ему не подсунуть.
//
// Подключение приёмника («Приймачі» → «Додати приймач»). Приёмник без ключа просит доступ: шлёт свой открытый ключ
// (X25519). Передатчик слушает такие просьбы, только пока открыто окно добавления; оператор видит имя приёмника и
// код из четырёх цифр (он же — на экране приёмника) и нажимает «Дозволити». Передатчик отвечает своим открытым
// ключом и ключом набора, зашифрованным общим секретом. Подслушавший этот обмен ключа набора не узнает.
// Приёмник, у которого ключ уже есть, новых ключей не просит и не принимает: иначе чужой передатчик мог бы
// «переманить» его. Сменить набор — только действием на самом приёмнике (порт: K0).
//
// Удаление приёмника («Приймачі» → приёмник → «Видалити з набору»). У каждого приёмника, кроме ключа набора, есть
// ЛИЧНЫЙ ключ — он выводится из того же обмена при подключении и хранится у приёмника и у передатчика (в списке
// приёмников). Удаляя приёмник, передатчик: велит ему стереть ключи (если он на связи), заводит новый ключ набора
// (поколение ключа растёт) и шлёт его каждому оставшемуся приёмнику под его личным ключом. Удалённый приёмник новый
// ключ получить не может, даже если ключи не стёр (утерян, выключен). Приёмник, который в это время был выключен,
// потом сам попросит новый ключ (подписав просьбу личным ключом) — оператору делать ничего не надо.
//
// Чего защита не даёт: заглушить эфир помехой можно по-прежнему.
#pragma once
#include <Preferences.h>
#include "aes/esp_aes.h"      // esp_aes_acquire_hardware / esp_aes_release_hardware — замок и питание устройства AES
#include "hal/aes_hal.h"
#include "hal/aes_ll.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"
#include "proto.h"   // SEC_TAG — длина подписи

#define SEC_KEY 32
#define SEC_PUB 32
enum { SEC_AUDIO = 1, SEC_COMMAND = 2, SEC_STATUS = 3, SEC_KEYMSG = 4, SEC_KEYREQ = 5, SEC_DEBUG = 6, SEC_DBGCMD = 7,
       SEC_RXINFO = 10, SEC_SEEN = 11 };   // 8 и 9 — обновление по радио (ota.h). У сообщения RxInfo свой вид: оно идёт с тем же номером
                            // услышанного пакета, что и сведения приёмника, и с общим видом повторилось бы одноразовое число шифра

// Ключи одной стороны обмена: шифр и подпись, оба AES-128.
struct SecKeys {
  uint8_t enc[16], mac[16];
  bool ok = false;
};

static uint8_t secKey[SEC_KEY];
static volatile bool secHave;             // ключ набора есть
static volatile uint32_t secEpoch;        // передатчик: номер нынешнего включения; приёмник: наибольший принятый
static volatile bool secEpochDirty;       // приёмник: эпоха выросла — записать в память
static volatile uint32_t secBadTag, secReplay;   // не сошлась подпись / пакет из прошлого (с запуска)
static SecKeys secKeys;                   // ключи шифра и подписи, выведенные из ключа набора
static volatile uint32_t secGen;          // поколение ключа набора: растёт при каждой смене (удалили приёмник)
static SecKeys secDev;                    // приёмник: личный ключ (для получения нового ключа набора)
static Preferences secPrefs;

// ---- аппаратный AES, поблочно. Вызывать между secHwOn() и secHwOff().
static inline void secHwOn() {
  esp_aes_acquire_hardware();   // общий замок шифровального устройства; включает его тактирование
  aes_ll_dma_enable(false);     // обычный режим: блок через регистры, без DMA
}
static inline void secHwOff() {
  esp_aes_release_hardware();
}

// Шифр: AES-CTR, счётчик блоков — последние четыре байта, с нуля. in и out могут совпадать.
static void secHwCtr(const uint8_t key[16], const uint8_t n[12], const uint8_t *in, uint8_t *out, size_t len) {
  uint8_t c[16], ks[16];
  uint32_t ctr = 0;
  memcpy(c, n, 12);
  aes_hal_setkey(key, 16, ESP_AES_ENCRYPT);
  while (len) {
    c[12] = (uint8_t)(ctr >> 24);
    c[13] = (uint8_t)(ctr >> 16);
    c[14] = (uint8_t)(ctr >> 8);
    c[15] = (uint8_t)ctr;
    ctr++;
    aes_hal_transform_block(c, ks);
    size_t m = len < 16 ? len : 16;
    for (size_t i = 0; i < m; i++) out[i] = in[i] ^ ks[i];
    in += m;
    out += m;
    len -= m;
  }
}

// Подпись: AES-CBC-MAC по сообщению «первый блок | открытая часть | шифртекст» (каждая часть добита нулями до 16 байт).
// В первом блоке — одноразовое число и обе длины, поэтому сообщение разбирается однозначно.
static void secHwTag(const uint8_t key[16], const uint8_t n[12], const uint8_t *aad, size_t aadLen, const uint8_t *ct, size_t len, uint8_t tag[SEC_TAG]) {
  uint8_t y[16] = {};
  aes_hal_setkey(key, 16, ESP_AES_ENCRYPT);
  memcpy(y, n, 12);
  y[12] = (uint8_t)aadLen;
  y[13] = (uint8_t)len;
  y[14] = (uint8_t)(len >> 8);
  y[15] = 0x48;
  aes_hal_transform_block(y, y);
  if (aadLen) {
    for (size_t i = 0; i < aadLen; i++) y[i] ^= aad[i];
    aes_hal_transform_block(y, y);
  }
  while (len) {
    size_t m = len < 16 ? len : 16;
    for (size_t i = 0; i < m; i++) y[i] ^= ct[i];
    aes_hal_transform_block(y, y);
    ct += m;
    len -= m;
  }
  memcpy(tag, y, SEC_TAG);
}

// ---- пакет: закрыть и открыть ключами k
static bool secSealWith(const SecKeys &k, const uint8_t n[12], const uint8_t *aad, size_t aadLen, uint8_t *data, size_t len, uint8_t *tag) {
  if (!k.ok || aadLen > 16 || len > 0xFFFF) return false;
  secHwOn();
  secHwCtr(k.enc, n, data, data, len);
  secHwTag(k.mac, n, aad, aadLen, data, len, tag);
  secHwOff();
  return true;
}

static bool secOpenWith(const SecKeys &k, const uint8_t n[12], const uint8_t *aad, size_t aadLen, const uint8_t *in, size_t len, const uint8_t *tag, uint8_t *out) {
  if (!k.ok || aadLen > 16 || len > 0xFFFF) return false;
  uint8_t t[SEC_TAG], d = 0;
  secHwOn();
  secHwTag(k.mac, n, aad, aadLen, in, len, t);
  for (int i = 0; i < SEC_TAG; i++) d |= t[i] ^ tag[i];   // сравнение без раннего выхода
  if (!d) secHwCtr(k.enc, n, in, out, len);
  secHwOff();
  return !d;
}

static inline void secNonce(uint8_t n[12], uint8_t type, uint32_t a, uint32_t b, const uint8_t *id) {
  n[0] = type;
  memcpy(n + 1, &a, 4);
  memcpy(n + 5, &b, 4);
  n[9] = id ? id[0] : 0;
  n[10] = id ? id[1] : 0;
  n[11] = id ? id[2] : 0;
}

// Зашифровать на месте и подписать. aad — открытая часть пакета (подписывается, не шифруется).
static bool secSeal(uint8_t type, uint32_t a, uint32_t b, const uint8_t *id, const uint8_t *aad, size_t aadLen, uint8_t *data, size_t len, uint8_t *tag) {
  if (!secHave) return false;
  uint8_t n[12];
  secNonce(n, type, a, b, id);
  return secSealWith(secKeys, n, aad, aadLen, data, len, tag);
}

// Проверить подпись и расшифровать в out. Не сошлось — false, out не тронут.
static bool secOpen(uint8_t type, uint32_t a, uint32_t b, const uint8_t *id, const uint8_t *aad, size_t aadLen, const uint8_t *in, size_t len, const uint8_t *tag,
                    uint8_t *out) {
  if (!secHave) return false;
  uint8_t n[12];
  secNonce(n, type, a, b, id);
  return secOpenWith(secKeys, n, aad, aadLen, in, len, tag, out);
}

// Из секрета (ключа набора или общего секрета обмена) — ключ шифра и ключ подписи.
static bool secDerive(const uint8_t *secret, size_t n, const char *label, SecKeys &k) {
  uint8_t okm[32];
  k.ok = mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), NULL, 0, secret, n, (const uint8_t *)label, strlen(label), okm, sizeof(okm)) == 0;
  if (k.ok) {
    memcpy(k.enc, okm, 16);
    memcpy(k.mac, okm + 16, 16);
  }
  memset(okm, 0, sizeof(okm));
  return k.ok;
}

static void secApplyKey() {
  secHave = false;
  delay(30);   // идущие в эту минуту шифрование и расшифровка закончатся
  secHave = secDerive(secKey, SEC_KEY, "hearlink kit keys v3", secKeys);
}

static void secBegin() {
  uint8_t d[32];
  secPrefs.begin("hl-sec", false);
  secEpoch = secPrefs.getUInt("epoch", 0);
  secGen = secPrefs.getUInt("gen", 0);
  if (secPrefs.getBytesLength("dev") == sizeof(d) && secPrefs.getBytes("dev", d, sizeof(d)) == sizeof(d)) {
    memcpy(secDev.enc, d, 16);
    memcpy(secDev.mac, d + 16, 16);
    secDev.ok = true;
  }
  // ключ без поколения — от сборок до личных ключей: считаем, что ключа нет (приёмник попросит доступ заново)
  if (secGen && secPrefs.getBytesLength("key") == SEC_KEY && secPrefs.getBytes("key", secKey, SEC_KEY) == SEC_KEY) secApplyKey();
  memset(d, 0, sizeof(d));
}

static void secSaveEpoch() {
  secPrefs.putUInt("epoch", secEpoch);
  secEpochDirty = false;
}

static void secStore(const uint8_t *key, uint32_t epoch, uint32_t gen) {
  memcpy(secKey, key, SEC_KEY);
  secPrefs.putBytes("key", secKey, SEC_KEY);
  secGen = gen;
  secPrefs.putUInt("gen", gen);
  secEpoch = epoch;
  secSaveEpoch();
  secApplyKey();
}

// Приёмник: запомнить личный ключ.
static void secStoreDev(const SecKeys &k) {
  uint8_t d[32];
  memcpy(d, k.enc, 16);
  memcpy(d + 16, k.mac, 16);
  secPrefs.putBytes("dev", d, sizeof(d));
  secDev = k;
  secDev.ok = true;
  memset(d, 0, sizeof(d));
}

// Стереть ключи (приёмник: его удалили из набора или владелец выбрал «Забути набір»).
static void secForget() {
  secHave = false;
  delay(30);
  memset(secKey, 0, SEC_KEY);
  memset(&secDev, 0, sizeof(secDev));
  secPrefs.remove("key");
  secPrefs.remove("dev");
}

// Новый ключ набора (передатчик). Радио должно быть включено: от него микросхема берёт настоящую случайность.
static void secNewKey() {
  uint8_t k[SEC_KEY];
  esp_fill_random(k, SEC_KEY);
  secStore(k, secEpoch, secGen + 1);
  memset(k, 0, sizeof(k));
}

// ---- подключение приёмника: обмен ключами X25519
struct SecPair {
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  mbedtls_ecp_point Q;
  uint8_t pub[SEC_PUB];
  bool ready = false;
};

static int secRng(void *, unsigned char *buf, size_t n) {
  esp_fill_random(buf, n);
  return 0;
}

static void secPairFree(SecPair &p) {
  if (!p.ready) return;
  mbedtls_ecp_point_free(&p.Q);
  mbedtls_mpi_free(&p.d);
  mbedtls_ecp_group_free(&p.grp);
  p.ready = false;
}

static bool secPairGen(SecPair &p) {
  secPairFree(p);
  mbedtls_ecp_group_init(&p.grp);
  mbedtls_mpi_init(&p.d);
  mbedtls_ecp_point_init(&p.Q);
  p.ready = true;
  size_t n = 0;
  bool ok = mbedtls_ecp_group_load(&p.grp, MBEDTLS_ECP_DP_CURVE25519) == 0 && mbedtls_ecdh_gen_public(&p.grp, &p.d, &p.Q, secRng, NULL) == 0 &&
            mbedtls_ecp_point_write_binary(&p.grp, &p.Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &n, p.pub, SEC_PUB) == 0 && n == SEC_PUB;
  if (!ok) secPairFree(p);
  return ok;
}

// Общий секрет двух сторон → ключи, которыми закрыт ключ набора в ответе передатчика (wrap), и личный ключ приёмника (dev).
static bool secPairKey(SecPair &p, const uint8_t *peerPub, const uint8_t *pubRx, const uint8_t *pubTx, SecKeys &out, SecKeys &dev) {
  if (!p.ready) return false;
  mbedtls_ecp_point Qp;
  mbedtls_mpi z;
  mbedtls_ecp_point_init(&Qp);
  mbedtls_mpi_init(&z);
  uint8_t ikm[32 + 2 * SEC_PUB];   // общий секрет и оба открытых ключа: ключи привязаны именно к этой паре
  bool ok = mbedtls_ecp_point_read_binary(&p.grp, &Qp, peerPub, SEC_PUB) == 0 && mbedtls_ecdh_compute_shared(&p.grp, &z, &Qp, &p.d, secRng, NULL) == 0 &&
            mbedtls_mpi_write_binary_le(&z, ikm, 32) == 0;
  memcpy(ikm + 32, pubRx, SEC_PUB);
  memcpy(ikm + 32 + SEC_PUB, pubTx, SEC_PUB);
  ok = ok && secDerive(ikm, sizeof(ikm), "hearlink pairing v3", out) && secDerive(ikm, sizeof(ikm), "hearlink device v1", dev);
  mbedtls_mpi_free(&z);
  mbedtls_ecp_point_free(&Qp);
  memset(ikm, 0, sizeof(ikm));
  return ok;
}

// Код из четырёх цифр по открытому ключу приёмника: его показывают и приёмник, и передатчик.
static uint16_t secPairCode(const uint8_t *pubRx) {
  uint8_t h[32];
  mbedtls_sha256(pubRx, SEC_PUB, h, 0);
  return (uint16_t)(((h[0] << 8) | h[1]) % 10000);
}

// Закрыть / открыть ключ набора ключами обмена. Они одноразовые, поэтому одноразовое число постоянное.
static bool secBox(bool seal, const SecKeys &k, uint8_t *data, size_t len, uint8_t *tag) {
  static const uint8_t n[12] = { 'p', 'a', 'i', 'r' };
  if (seal) return secSealWith(k, n, NULL, 0, data, len, tag);
  uint8_t out[64];
  if (len > sizeof(out) || !secOpenWith(k, n, NULL, 0, data, len, tag, out)) return false;
  memcpy(data, out, len);
  return true;
}

// Отладка (порт: x<число>): гонять шифр вдобавок к рабочему, ничего не меняя в пакетах. Бит 0 — в задаче передачи
// звука (на каждый отправленный пакет), бит 1 — в задаче Wi-Fi (на каждый принятый пакет звука или сведений).
static volatile uint8_t secShadow;
static volatile uint32_t secHwRuns, secHwFails;
static void secShadowRun(const uint8_t *data, size_t len) {
  static const uint8_t n[12] = { 9 };
  uint8_t tmp[256], tag[SEC_TAG];
  if (len > sizeof(tmp) || !secKeys.ok) return;
  memcpy(tmp, data, len);
  bool ok = secSealWith(secKeys, n, NULL, 0, tmp, len, tag);
  secHwRuns = secHwRuns + 1;
  if (!ok) secHwFails = secHwFails + 1;
}

// Самопроверка и замер на плате (команда порта B). Свои ключи — рабочие не трогает и работе не мешает.
//  — шифр и подпись сверяются с эталоном, посчитанным на компьютере системным openssl (tools/host/sec_kat.py);
//  — круг «закрыть — открыть», подмена байта данных, заголовка, номера пакета;
//  — время на пакет, память до и после 2000 пакетов; обмен ключами двух сторон.
static void secSelfTest() {
  static uint8_t buf[256], out[256];
  static SecKeys k;
  uint8_t tag[SEC_TAG], aad[14], n[12] = { 1, 5, 0, 0, 0, 9 };
  for (int i = 0; i < 16; i++) {
    k.enc[i] = i;
    k.mac[i] = 32 + i;
  }
  k.ok = true;
  for (int i = 0; i < 14; i++) aad[i] = i * 3;
  for (int i = 0; i < 212; i++) buf[i] = i * 7 + 1;
  {
    static const uint8_t wantCt[8] = { 0x32, 0x14, 0xac, 0xdb, 0x1c, 0xba, 0x48, 0x34 }, wantTag[8] = { 0xda, 0x48, 0x87, 0x43, 0xb8, 0x37, 0xef, 0x85 };
    secSealWith(k, n, aad, sizeof(aad), buf, 212, tag);
    Serial.printf("апаратний AES-128: шифр з еталоном %s, підпис з еталоном %s\n", !memcmp(buf, wantCt, 8) ? "збігся" : "НЕ ЗБІГСЯ",
                  !memcmp(tag, wantTag, 8) ? "збігся" : "НЕ ЗБІГСЯ");
  }
  uint32_t h0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), t0 = micros();
  for (int i = 0; i < 2000; i++) {
    n[6] = i;
    n[7] = i >> 8;
    secSealWith(k, n, aad, sizeof(aad), buf, 212, tag);
  }
  uint32_t t1 = micros();
  bool okOpen = secOpenWith(k, n, aad, sizeof(aad), buf, 212, tag, out);
  uint32_t t2 = micros();
  for (int i = 0; i < 2000; i++) secOpenWith(k, n, aad, sizeof(aad), buf, 212, tag, out);
  uint32_t t3 = micros(), h1 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  buf[40] ^= 1;
  bool badData = !secOpenWith(k, n, aad, sizeof(aad), buf, 212, tag, out);
  buf[40] ^= 1;
  aad[5] ^= 1;
  bool badAad = !secOpenWith(k, n, aad, sizeof(aad), buf, 212, tag, out);
  aad[5] ^= 1;
  n[5] ^= 1;
  bool badSeq = !secOpenWith(k, n, aad, sizeof(aad), buf, 212, tag, out);
  n[5] ^= 1;
  Serial.printf("апаратний AES-128: закрити пакет 212 байт — %u мкс, відкрити — %u мкс; відкривається: %s; підміна даних помічена: %s, заголовка: %s, номера: %s; "
                "пам'ять за 4000 пакетів: різниця %d байт\n",
                (unsigned)((t1 - t0) / 2000), (unsigned)((t3 - t2) / 2000), okOpen ? "так" : "НІ", badData ? "так" : "НІ", badAad ? "так" : "НІ", badSeq ? "так" : "НІ",
                (int)(h1 - h0));
  static SecPair a, b;   // не на стеке: в них большие числа
  uint32_t t4 = micros();
  bool g = secPairGen(a) && secPairGen(b);
  uint32_t t5 = micros();
  static SecKeys ka, kb, da, db;
  uint8_t box[41] = { 9, 8, 7 }, btag[SEC_TAG];
  bool kk = g && secPairKey(a, b.pub, a.pub, b.pub, ka, da) && secPairKey(b, a.pub, a.pub, b.pub, kb, db);
  uint32_t t6 = micros();
  bool same = kk && !memcmp(ka.enc, kb.enc, 16) && !memcmp(ka.mac, kb.mac, 16) && !memcmp(da.enc, db.enc, 16) && !memcmp(da.mac, db.mac, 16) &&
              memcmp(da.enc, ka.enc, 16) != 0;
  bool boxOk = same && secBox(true, kb, box, sizeof(box), btag) && box[0] != 9 && secBox(false, ka, box, sizeof(box), btag) && box[0] == 9 && box[2] == 7;
  Serial.printf("обмін ключами: пара ключів — %u мс, спільний секрет — %u мс; секрети збіглися: %s; ключ набору передається: %s; код %04u\n",
                (unsigned)((t5 - t4) / 2000), (unsigned)((t6 - t5) / 2000), same ? "так" : "НІ", boxOk ? "так" : "НІ", g ? secPairCode(a.pub) : 0);
  secPairFree(a);
  secPairFree(b);
}
