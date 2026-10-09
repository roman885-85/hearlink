// Обновление прошивки приёмников по радио от передатчика.
//
// Владелец 07.10: «добавить функцию обновления прошивки приемников по сети от передатчика»; «во время обновлений
// приемников по воздуху, на экране соответствующее сообщение с ходом выполнения обновления»; «дальнейшие обновления
// и отладку приемников делать по воздуху от передатчика».
//
// Прошивка у набора одна на все платы, поэтому передатчик раздаёт ту, на которой работает сам: новую версию ставят
// по USB в передатчик, а приёмникам она уходит по радио.
//
// Ход:
//  1. Передатчик останавливает звук в эфире и 2,5 с объявляет: размер, число блоков, отпечаток (SHA-256) прошивки.
//  2. Приёмник сверяет отпечаток со своей прошивкой. Та же — отвечает «у меня такая же» и ничего не делает. Другая —
//     берёт память под весь образ (внешняя память, ~2,2 МБ) и отвечает «принимаю».
//  3. Передатчик шлёт блоки по 200 байт подряд. Приёмник раз в 0,3 с сообщает, сколько принял и каких блоков не
//     хватает; передатчик досылает недостающее, пока у всех не соберётся целиком.
//  4. Приёмник сверяет отпечаток всего образа, пишет его во второй раздел прошивки (разметка — partitions.csv),
//     назначает этот раздел загрузочным и перезапускается. Передатчик к этому времени уже вернул звук в эфир.
// Спящий приёмник объявления не слышит и не обновляется — его обновляют в другой раз.
//
// Защита: все пакеты закрыты ключом набора (чужой не подсунет свою прошивку). В объявлении — номер включения
// передатчика: записанное когда-то обновление, пущенное в эфир повторно, приёмник не примет (как и старый звук).
// Образ принимается целиком в память и пишется во флеш только после сверки отпечатка; пока запись не закончена и не
// проверена, загрузочным остаётся прежний раздел — оборванное обновление приёмник не портит.
#pragma once
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_app_format.h>
#include <mbedtls/sha256.h>

#define MAGIC_OTA 0x4F48   // «HO»
#define OTA_BLOCK 200      // байт прошивки в пакете
#define OTA_RANGES 40      // столько промежутков «не хватает» помещается в одно сообщение приёмника
enum { OTA_ANNOUNCE = 1, OTA_DATA = 2, OTA_POLL = 3, OTA_STATE = 4, OTA_CANCEL = 5, OTA_ANNOUNCE_DEMO = 6 };
// OTA_ANNOUNCE_DEMO — пробное обновление (владелец 07.10: «выполни фиктивное обновление приемников по воздуху, чтобы я
// видел, как это происходит»): всё идёт по-настоящему — приём, сверка отпечатка, экраны, светодиод, — но приёмник
// прошивку не записывает и не перезапускается, а «запись» изображает пять секунд. Приёмник принимает такое
// объявление, даже если прошивка у него та же. Приёмники до 2.21 этого вида объявления не знают и не откликаются.
// ВАЖНО: вид пакетов обновления менять нельзя — по ним обновляются приёмники со старой прошивкой. Новое — только
// новыми видами пакетов (как здесь) или в хвосте объявления, который старые не читают.
// что делает приёмник
enum { OS_IDLE = 0, OS_SAME, OS_RECV, OS_CHECK, OS_WRITE, OS_DONE, OS_FAIL };
// почему не вышло
enum { OE_NONE = 0, OE_NOSLOT, OE_NOMEM, OE_HASH, OE_FLASH, OE_LOST, OE_BIG, OE_CANCEL };
enum { SEC_OTA = 8, SEC_OTAST = 9 };   // виды сообщений для шифра (продолжение списка в sec.h)

struct __attribute__((packed)) OtaHdr {
  uint16_t magic;
  uint8_t ver, kit;
  uint8_t kind;
  uint8_t from[3];     // от кого: 00 00 00 — передатчик
  uint32_t session;    // номер этого обновления (случайный)
  // ---- досюда (12 байт) — открытая часть под подписью
  uint32_t n;          // блок прошивки — его номер; остальное — счётчик (вместе с session — одноразовое число шифра)
};
#define OTA_AAD 12
struct __attribute__((packed)) OtaAnn {
  uint32_t epoch;      // номер включения передатчика (свежесть)
  uint32_t size;       // байт в образе
  uint16_t blocks;
  uint8_t sha[16];     // первые 16 байт SHA-256 образа
  char version[8];
};
struct __attribute__((packed)) OtaSt {
  uint8_t phase, percent, err, nr;
  uint16_t got;        // блоков принято
  struct __attribute__((packed)) {
    uint16_t a, n;     // с какого блока и сколько не хватает
  } r[OTA_RANGES];
};
#define OTA_PKT 250

// Длина образа прошивки в разделе: заголовок, куски, контрольный байт с добивкой до 16, отпечаток (если есть).
static uint32_t otaImageLen(const uint8_t *m, uint32_t cap) {
  const esp_image_header_t *h = (const esp_image_header_t *)m;
  if (cap < sizeof(esp_image_header_t) || h->magic != ESP_IMAGE_HEADER_MAGIC || !h->segment_count || h->segment_count > 16) return 0;
  uint32_t off = sizeof(esp_image_header_t);
  for (int i = 0; i < h->segment_count; i++) {
    if (off + sizeof(esp_image_segment_header_t) > cap) return 0;
    esp_image_segment_header_t sh;
    memcpy(&sh, m + off, sizeof(sh));
    off += sizeof(sh) + sh.data_len;
    if (off > cap) return 0;
  }
  off = (off + 1 + 15) & ~15u;
  if (h->hash_appended) off += 32;
  return off <= cap ? off : 0;
}

// Отпечаток SHA-256. Считает аппаратный блок; данные из отображённого флеша подаём ему через свою память.
// Память под это — на каждый вызов своя: функцию зовут сразу две задачи (задача карты проверяет файл, задача
// раздачи считает отпечаток прошивки). Когда память была одна на всех, два одновременных подсчёта портили друг
// другу данные: файл на карте «не годился», а приёмники отвергали принятую прошивку (07.10, пробное обновление).
// gentle — считать не спеша (с паузами): подсчёт по двум мегабайтам сильно нагружает общую шину памяти, и на
// передатчике экран с разбором мелодии в это время подтормаживают (07.10: кадр меню 1,6 с вскоре после включения).
static void otaSha(const uint8_t *data, uint32_t len, uint8_t out[32], bool gentle = false) {
  uint8_t *tmp = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  for (uint32_t o = 0; o < len;) {
    uint32_t n = len - o < 4096 ? len - o : 4096;
    if (tmp) {
      memcpy(tmp, data + o, n);
      mbedtls_sha256_update(&c, tmp, n);
    } else mbedtls_sha256_update(&c, data + o, n);
    o += n;
    if (gentle && (o & 0x3FFF) == 0) vTaskDelay(pdMS_TO_TICKS(12));   // не спеша: после каждых 16 КБ — пауза
    else if ((o & 0xFFFF) == 0) vTaskDelay(1);   // каждые 64 КБ уступить ядро
  }
  mbedtls_sha256_finish(&c, out);
  mbedtls_sha256_free(&c);
  if (tmp) free(tmp);
}

// Прошивка, на которой работает эта плата. Сначала пробуем отобразить раздел в память (чтение идёт через кэш, флеш
// не «захватывается», экран передатчика не вздрагивает); не вышло — читаем раздел целиком во внешнюю память.
struct OtaImg {
  const uint8_t *p = nullptr;
  uint32_t len = 0;
  esp_partition_mmap_handle_t mh = 0;
  uint8_t *copy = nullptr;
};
static void otaImgClose(OtaImg &im) {
  if (im.copy) free(im.copy);
  else if (im.p) esp_partition_munmap(im.mh);
  im = OtaImg();
}
static bool otaImgOpen(OtaImg &im) {
  const esp_partition_t *part = esp_ota_get_running_partition();
  const void *ptr = nullptr;
  if (!part) return false;
  if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &ptr, &im.mh) == ESP_OK) im.p = (const uint8_t *)ptr;
  else {
    im.copy = (uint8_t *)heap_caps_malloc(part->size, MALLOC_CAP_SPIRAM);
    if (!im.copy) return false;
    for (uint32_t o = 0; o < part->size; o += 65536) {
      if (esp_partition_read(part, o, im.copy + o, part->size - o < 65536 ? part->size - o : 65536) != ESP_OK) {
        otaImgClose(im);
        return false;
      }
      vTaskDelay(1);
    }
    im.p = im.copy;
  }
  im.len = otaImageLen(im.p, part->size);
  if (!im.len) {
    otaImgClose(im);
    return false;
  }
  return true;
}

// ============================================================================================ приёмник
struct OtaRx {
  volatile uint8_t phase = OS_IDLE, percent = 0, err = OE_NONE;
  volatile uint32_t session = 0;
  uint32_t size = 0, blocks = 0;
  uint8_t sha[16];
  char version[9] = "";
  uint8_t *buf = nullptr;
  uint32_t *have = nullptr;
  volatile uint32_t got = 0, hi = 0;
  volatile uint32_t lastMs = 0;         // когда слышали пакет обновления
  volatile bool annNew = false, pollNow = false, cancel = false;
  volatile bool annDemo = false;        // пришедшее объявление — пробное
  bool demo = false;                    // идущее обновление — пробное: не записывать
  OtaHdr annHdr;
  OtaAnn ann;
  uint32_t ctr = 0;
  uint32_t doneMs = 0;
};
static OtaRx otaRx;
static uint8_t otaRxId[3];
static void (*otaRxLive)();   // приёмник: «передатчик слышен» (обновляет отметку времени связи)

// приёмник, задача Wi-Fi
static void otaRxOnPacket(const uint8_t *data, int len) {
  OtaRx &o = otaRx;
  const OtaHdr *h = (const OtaHdr *)data;
  int bl = len - (int)sizeof(OtaHdr) - SEC_TAG;
  if (bl < 0 || h->from[0] || h->from[1] || h->from[2] || !secHave) return;
  const uint8_t *body = data + sizeof(OtaHdr);
  if (h->kind == OTA_ANNOUNCE || h->kind == OTA_ANNOUNCE_DEMO) {
    OtaAnn a;
    if (bl != (int)sizeof(OtaAnn) || !secOpen(SEC_OTA, h->session, h->n, NULL, data, OTA_AAD, body, bl, body + bl, (uint8_t *)&a)) return;
    if (a.epoch < secEpoch) return;   // запись прежнего включения передатчика
    o.lastMs = millis();
    if (otaRxLive) otaRxLive();
    if (h->session != o.session && !o.annNew && o.phase != OS_RECV && o.phase != OS_CHECK && o.phase != OS_WRITE) {
      o.annHdr = *h;
      o.ann = a;
      o.annDemo = h->kind == OTA_ANNOUNCE_DEMO;
      o.annNew = true;
    }
    return;
  }
  if (h->session != o.session || !o.session) return;
  if (h->kind == OTA_DATA) {
    uint8_t *buf = o.buf;
    uint32_t *have = o.have;
    if (o.phase != OS_RECV || h->n >= o.blocks || !buf || !have) return;
    uint32_t want = h->n == o.blocks - 1 ? o.size - h->n * OTA_BLOCK : OTA_BLOCK;
    if ((uint32_t)bl != want) return;
    if (!secOpen(SEC_OTA, h->session, h->n, NULL, data, OTA_AAD, body, bl, body + bl, buf + h->n * OTA_BLOCK)) return;
    o.lastMs = millis();
    if (otaRxLive) otaRxLive();
    uint32_t bit = 1u << (h->n & 31);
    if (!(have[h->n >> 5] & bit)) {
      have[h->n >> 5] |= bit;
      o.got = o.got + 1;
    }
    if (h->n + 1 > o.hi) o.hi = h->n + 1;
    return;
  }
  uint8_t one;
  if (bl != 1 || !secOpen(SEC_OTA, h->session, h->n, NULL, data, OTA_AAD, body, 1, body + 1, &one)) return;
  o.lastMs = millis();
  if (otaRxLive) otaRxLive();
  if (h->kind == OTA_POLL) {
    if (o.phase == OS_RECV) o.hi = o.blocks;   // передатчик прошёл весь образ: всё, чего нет, — недостающее
    o.pollNow = true;
  } else if (h->kind == OTA_CANCEL) o.cancel = true;
}

// Память под образ отдаём так: сначала состояние уже не «принимаю» (это делает вызывающий), потом пауза — задача
// Wi-Fi, которая в этот миг могла расшифровывать блок прямо в эту память, успевает закончить, — и только тогда free.
static void otaRxFree() {
  OtaRx &o = otaRx;
  uint8_t *b = o.buf;
  uint32_t *hv = o.have;
  if (!b && !hv) return;
  o.buf = nullptr;
  delay(30);
  o.have = nullptr;
  if (b) free(b);
  if (hv) free(hv);
}

static void otaRxSend() {
  OtaRx &o = otaRx;
  static uint8_t pkt[OTA_PKT];
  OtaHdr *h = (OtaHdr *)pkt;
  OtaSt *s = (OtaSt *)(pkt + sizeof(OtaHdr));
  h->magic = MAGIC_OTA;
  h->ver = PROTO_VER;
  h->kit = cfg.kit;
  h->kind = OTA_STATE;
  memcpy(h->from, otaRxId, 3);
  h->session = o.session;
  h->n = ++o.ctr;
  s->phase = o.phase;
  s->percent = o.percent;
  s->err = o.err;
  s->got = (uint16_t)o.got;
  int nr = 0;
  if (o.phase == OS_RECV && o.have) {   // каких блоков не хватает (до самого дальнего из принятых)
    uint32_t hi = o.hi, i = 0;
    while (i < hi && nr < OTA_RANGES) {
      if ((i & 31) == 0 && o.have[i >> 5] == 0xFFFFFFFFu) {
        i += 32;
        continue;
      }
      if (o.have[i >> 5] & (1u << (i & 31))) {
        i++;
        continue;
      }
      uint32_t a = i;
      while (i < hi && !(o.have[i >> 5] & (1u << (i & 31))) && i - a < 60000) i++;
      s->r[nr].a = (uint16_t)a;
      s->r[nr].n = (uint16_t)(i - a);
      nr++;
    }
  }
  s->nr = nr;
  size_t bl = offsetof(OtaSt, r) + nr * 4;
  uint8_t *body = (uint8_t *)s;
  if (!secSeal(SEC_OTAST, h->session, h->n, h->from, pkt, OTA_AAD, body, bl, body + bl)) return;
  esp_now_send(BCAST, pkt, sizeof(OtaHdr) + bl + SEC_TAG);
}

// Отпечаток, приписанный в хвост образа собственной прошивки (последние 32 байта), и длина образа: по ним файл на
// карте сверяется с работающей прошивкой без чтения и подсчёта двух мегабайт.
static bool otaOwnTail(uint8_t out[32], uint32_t *len) {
  static uint8_t tail[32];
  static uint32_t ownLen;
  if (!ownLen) {
    OtaImg im;
    if (!otaImgOpen(im)) return false;
    memcpy(tail, im.p + im.len - 32, 32);
    ownLen = im.len;
    otaImgClose(im);
  }
  memcpy(out, tail, 32);
  *len = ownLen;
  return true;
}

// отпечаток собственной прошивки (первые 16 байт) — считается один раз
static bool otaOwnSha(uint8_t out[16]) {
  static uint8_t own[32];
  static volatile bool have, busy;
  while (busy) vTaskDelay(5);   // его уже считает другая задача — дождаться готового
  if (!have) {
    busy = true;
    OtaImg im;
    if (!otaImgOpen(im)) {
      busy = false;
      return false;
    }
    otaSha(im.p, im.len, own, cfg.isTx);
    otaImgClose(im);
    have = true;
    busy = false;
  }
  memcpy(out, own, 16);
  return true;
}

// приёмник, главный цикл
static void otaRxTick() {
  OtaRx &o = otaRx;
  static uint32_t lastSend, failAt;
  uint32_t now = millis();
  if (o.annNew) {   // новое объявление
    OtaAnn a = o.ann;
    uint32_t session = o.annHdr.session;
    otaRxFree();
    o.got = 0;
    o.hi = 0;
    o.percent = 0;
    o.err = OE_NONE;
    o.cancel = false;
    o.size = a.size;
    o.blocks = a.blocks;
    memcpy(o.sha, a.sha, 16);
    memcpy(o.version, a.version, 8);
    o.version[8] = 0;
    uint8_t own[16];
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    o.demo = o.annDemo;
    if (!o.demo && otaOwnSha(own) && !memcmp(own, a.sha, 16)) o.phase = OS_SAME;
    else if (!next && !o.demo) {   // разметка памяти без второго раздела прошивки — нужна одна заливка по USB
      o.phase = OS_FAIL;
      o.err = OE_NOSLOT;
    } else if (a.size > (next ? next->size : 0x300000u) || a.blocks != (a.size + OTA_BLOCK - 1) / OTA_BLOCK || !a.size) {
      o.phase = OS_FAIL;
      o.err = OE_BIG;
    } else {
      uint32_t words = (a.blocks + 31) / 32;
      o.buf = (uint8_t *)heap_caps_malloc(a.blocks * OTA_BLOCK, MALLOC_CAP_SPIRAM);
      o.have = (uint32_t *)heap_caps_calloc(words, 4, MALLOC_CAP_SPIRAM);
      if (!o.buf || !o.have) {
        otaRxFree();
        o.phase = OS_FAIL;
        o.err = OE_NOMEM;
      } else o.phase = OS_RECV;
    }
    o.session = session;
    o.annNew = false;
    failAt = o.phase == OS_FAIL ? now : 0;
    Serial.printf("оновлення: оголошено прошивку %s, %u байт — %s\n", o.version, (unsigned)o.size,
                  o.phase == OS_SAME ? "у мене така сама" : o.phase == OS_RECV ? "приймаю" : "не можу прийняти");
  }
  if (o.phase == OS_IDLE) return;
  if (o.cancel && (o.phase == OS_RECV || o.phase == OS_SAME)) {
    o.phase = OS_FAIL;
    o.err = OE_CANCEL;
    otaRxFree();
    failAt = now;
  }
  o.cancel = false;
  if (o.phase == OS_RECV) {
    o.percent = o.blocks ? (uint8_t)(o.got * 100 / o.blocks) : 0;
    if (msSince(o.lastMs, now) > 10000) {   // передатчик замолчал
      o.phase = OS_FAIL;
      o.err = OE_LOST;
      otaRxFree();
      failAt = now;
    } else if (o.got >= o.blocks) {
      o.phase = OS_CHECK;
      otaRxSend();
      uint8_t sha[32];
      otaSha(o.buf, o.size, sha);
      const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
      esp_ota_handle_t oh = 0;
      if (memcmp(sha, o.sha, 16)) o.err = OE_HASH;
      else if (o.demo) {   // пробное обновление: «запись» — пять секунд на экране, флеш не трогаем
        o.phase = OS_WRITE;
        uint32_t t0 = millis(), sent = 0;
        while (millis() - t0 < 5000) {
          o.percent = (uint8_t)((millis() - t0) / 50);
          if (millis() - sent > 350) {
            sent = millis();
            otaRxSend();
          }
          delay(20);
        }
      } else if (!next || esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &oh) != ESP_OK) o.err = OE_FLASH;
      else {
        o.phase = OS_WRITE;
        o.percent = 0;
        uint32_t sent = millis();
        for (uint32_t off = 0; off < o.size && !o.err;) {
          uint32_t n = o.size - off < 4096 ? o.size - off : 4096;
          if (esp_ota_write(oh, o.buf + off, n) != ESP_OK) o.err = OE_FLASH;
          off += n;
          o.percent = (uint8_t)((uint64_t)off * 100 / o.size);
          if (millis() - sent > 350) {
            sent = millis();
            otaRxSend();
          }
          delay(1);
        }
        if (o.err) esp_ota_abort(oh);
        else if (esp_ota_end(oh) != ESP_OK || esp_ota_set_boot_partition(next) != ESP_OK) o.err = OE_FLASH;
      }
      otaRxFree();
      if (o.err) {
        o.phase = OS_FAIL;
        failAt = millis();
        Serial.printf("оновлення: НЕ ВДАЛОСЯ, причина %u\n", o.err);
      } else {
        o.phase = OS_DONE;
        o.percent = 100;
        o.doneMs = millis();
        Serial.println(o.demo ? "оновлення: пробне — прошивку прийнято й перевірено, не записую" : "оновлення: прошивку записано — перезапуск");
      }
      now = millis();
    }
  }
  if (o.phase == OS_DONE) {   // полторы секунды сообщать «готово» — и перезапуск на новую прошивку
    if (now - lastSend > 150) {
      lastSend = now;
      otaRxSend();
    }
    if (now - o.doneMs > 1800) {
      if (!o.demo) ESP.restart();
      o.phase = OS_IDLE;   // пробное: показали «готово» — и обратно к работе
      o.session = 0;
      o.demo = false;
    }
    return;
  }
  // сообщить о себе: по просьбе передатчика сразу (с разбросом — приёмников несколько), иначе раз в 0,3 с
  bool heard = msSince(o.lastMs, now) < 1500;
  if (heard && ((o.pollNow && now - lastSend > 60 + (esp_random() % 50)) || now - lastSend > (o.phase == OS_RECV ? 300u : 600u))) {
    lastSend = now;
    o.pollNow = false;
    otaRxSend();
  }
  if ((o.phase == OS_SAME && !heard && msSince(o.lastMs, now) > 4000) || (o.phase == OS_FAIL && now - failAt > 8000)) {
    o.phase = OS_IDLE;
    o.session = 0;
  }
}

static bool otaRxBusy() {   // приём или запись идёт: не спать, звук не играть, на экране — ход обновления
  uint8_t p = otaRx.phase;
  return p == OS_RECV || p == OS_CHECK || p == OS_WRITE || p == OS_DONE;
}

// ============================================================================================ передатчик
struct OtaPeer {
  uint8_t id[3];
  bool used;
  volatile uint8_t phase, percent, err;
  volatile uint16_t got;
  volatile uint32_t seenMs;
};
// что делает передатчик
enum { OT_IDLE = 0, OT_PREP, OT_ANNOUNCE, OT_SEND, OT_FINISH, OT_WRITE, OT_DONE, OT_NOBODY, OT_CANCELLED, OT_ERROR,
       OT_SELF, OT_RESTART, OT_SELFFAIL };   // обновление с карты: передатчик пишет прошивку в себя / перезапускается / не вышло
struct OtaTx {
  volatile uint8_t stage = OT_IDLE;
  volatile bool cancel = false, ask = false;
  bool demo = false;                 // пробное обновление: приёмники не записывают
  bool force = false;                // отладка: пройти всю передачу, даже если её никто не ждёт
  bool fromCard = false;             // раздаётся файл с карты (upd.h), а не своя прошивка; после приёмников — сам передатчик
  const uint8_t *cardImg = nullptr;  // образ из файла и его длина (ставит otaTxStartCard)
  uint32_t cardLen = 0;
  const uint8_t *cardSha = nullptr;  // отпечаток файла уже посчитан при проверке — второй раз не считать
  bool (*selfWrite)() = nullptr;     // записать этот образ в себя (upd.h)
  void (*selfDone)() = nullptr;      // перезапуск после записи (hearlink.ino)
  char version[12] = "";             // версия раздаваемой прошивки
  uint32_t session = 0, size = 0, blocks = 0, startMs = 0, endMs = 0, ctr = 0;
  volatile uint32_t sent = 0;        // блоков отправлено (с повторами)
  uint8_t sha[32];
  OtaImg im;
  const uint8_t *img = nullptr;
  uint32_t *need = nullptr;
  volatile uint32_t needN = 0;
  OtaPeer peer[PEERS_MAX];
  uint32_t seq = 0;                  // растёт с каждым запуском обновления
};
static OtaTx otaTx;
static portMUX_TYPE otaMux = portMUX_INITIALIZER_UNLOCKED;

static bool otaTxActive() {
  uint8_t s = otaTx.stage;
  return (s >= OT_PREP && s <= OT_WRITE) || s == OT_SELF || s == OT_RESTART;
}

// передатчик, задача Wi-Fi: сообщение приёмника
static void otaTxOnPacket(const uint8_t *data, int len) {
  OtaTx &o = otaTx;
  const OtaHdr *h = (const OtaHdr *)data;
  int bl = len - (int)sizeof(OtaHdr) - SEC_TAG;
  if (h->kind != OTA_STATE || h->session != o.session || !otaTxActive() || bl < (int)offsetof(OtaSt, r) || bl > (int)sizeof(OtaSt)) return;
  OtaSt s;
  const uint8_t *body = data + sizeof(OtaHdr);
  if (!secOpen(SEC_OTAST, h->session, h->n, h->from, data, OTA_AAD, body, bl, body + bl, (uint8_t *)&s)) return;
  if (bl != (int)(offsetof(OtaSt, r) + s.nr * 4) || s.nr > OTA_RANGES) return;
  OtaPeer *p = nullptr;
  for (auto &q : o.peer)
    if (q.used && !memcmp(q.id, h->from, 3)) p = &q;
  if (!p)
    for (auto &q : o.peer)
      if (!q.used) {
        memcpy(q.id, h->from, 3);
        q.used = true;
        p = &q;
        break;
      }
  if (!p) return;
  p->phase = s.phase;
  p->percent = s.percent;
  p->err = s.err;
  p->got = s.got;
  p->seenMs = millis() ? millis() : 1;
  if (s.phase == OS_RECV && o.need && (o.stage == OT_SEND || o.stage == OT_FINISH)) {
    portENTER_CRITICAL(&otaMux);
    for (int i = 0; i < s.nr; i++)
      for (uint32_t b = s.r[i].a; b < (uint32_t)s.r[i].a + s.r[i].n && b < o.blocks; b++) {
        uint32_t bit = 1u << (b & 31);
        if (!(o.need[b >> 5] & bit)) {
          o.need[b >> 5] |= bit;
          o.needN = o.needN + 1;
        }
      }
    portEXIT_CRITICAL(&otaMux);
  }
}

static void otaTxPacket(uint8_t kind, uint32_t n, const uint8_t *body, size_t bl) {
  static uint8_t pkt[OTA_PKT];
  OtaTx &o = otaTx;
  OtaHdr *h = (OtaHdr *)pkt;
  h->magic = MAGIC_OTA;
  h->ver = PROTO_VER;
  h->kit = cfg.kit;
  h->kind = kind;
  h->from[0] = h->from[1] = h->from[2] = 0;
  h->session = o.session;
  h->n = n;
  uint8_t *b = pkt + sizeof(OtaHdr);
  memcpy(b, body, bl);
  if (!secSeal(SEC_OTA, h->session, h->n, NULL, pkt, OTA_AAD, b, bl, b + bl)) return;
  for (int i = 0; i < 60 && (int32_t)(rPut - rGot) >= 5; i++) vTaskDelay(1);   // не быстрее, чем радио отдаёт в эфир
  static uint8_t breath;   // и раз в 16 пакетов — пауза в любом случае: когда эфир отдаёт быстро, очередь не копится,
  if (++breath >= 16) {    // паузы выше не случаются, и задача раздачи держала ядро дольше 5 с — сторож ронял плату (09.10)
    breath = 0;
    vTaskDelay(1);
  }
  radioSend(pkt, sizeof(OtaHdr) + bl + SEC_TAG);
}
static void otaTxAnnounce() {
  OtaTx &o = otaTx;
  OtaAnn a = {};
  a.epoch = secEpoch;
  a.size = o.size;
  a.blocks = (uint16_t)o.blocks;
  memcpy(a.sha, o.sha, 16);
  strncpy(a.version, o.version[0] ? o.version : FW_VERSION, sizeof(a.version));
  otaTxPacket(o.demo ? OTA_ANNOUNCE_DEMO : OTA_ANNOUNCE, 0x10000000u | (++o.ctr & 0xFFFFFF), (const uint8_t *)&a, sizeof(a));
}
static void otaTxSignal(uint8_t kind) {   // опрос или отмена: тело — один байт (пустое шифр не закрывает)
  OtaTx &o = otaTx;
  uint8_t one = kind;
  otaTxPacket(kind, ((uint32_t)kind << 28) | (++o.ctr & 0xFFFFFF), &one, 1);
}

// сколько приёмников в каком состоянии (для решений и экрана)
struct OtaCount {
  int want = 0, recv = 0, ready = 0, done = 0, fail = 0, same = 0, lost = 0;
};
static OtaCount otaTxCount(uint32_t lostMs) {
  OtaCount c;
  uint32_t now = millis();
  for (auto &p : otaTx.peer) {
    if (!p.used) continue;
    uint8_t ph = p.phase;
    if (ph == OS_SAME) c.same++;
    else if (ph == OS_FAIL) c.fail++;
    else {
      c.want++;
      if (ph == OS_DONE) c.done++;
      else if (msSince(p.seenMs, now) > lostMs) c.lost++;
      else if (ph == OS_RECV) c.recv++;
      else c.ready++;   // сверяет или пишет: образ у него целиком
    }
  }
  return c;
}

static void otaTxTask(void *) {
  OtaTx &o = otaTx;
  o.stage = OT_PREP;
  o.cancel = false;
  o.sent = 0;
  o.startMs = millis();
  for (auto &p : o.peer) p.used = false;
  uint32_t tp0 = millis();
  if (o.fromCard) o.img = o.cardImg;
  else o.img = otaImgOpen(o.im) ? o.im.p : nullptr;
  uint32_t tp1 = millis();
  uint32_t words = 0;
  if (o.img) {
    o.size = o.fromCard ? o.cardLen : o.im.len;
    o.blocks = (o.size + OTA_BLOCK - 1) / OTA_BLOCK;
    words = (o.blocks + 31) / 32;
    o.need = (uint32_t *)heap_caps_malloc(words * 4, MALLOC_CAP_SPIRAM);
  }
  if (!o.img || !o.need || o.blocks > 65000) {
    if (!o.fromCard) otaImgClose(o.im);
    if (o.need) free(o.need);
    o.need = nullptr;
    o.img = nullptr;
    o.endMs = millis();
    o.stage = OT_ERROR;
    Serial.println("оновлення: не вдалося прочитати власну прошивку");
    vTaskDelete(NULL);
  }
  if (o.fromCard && o.cardSha) memcpy(o.sha, o.cardSha, 32);
  else {
    otaSha(o.img, o.size, o.sha, true);
    Serial.printf("оновлення: власну прошивку %s за %u мс, відбиток пораховано за %u мс\n", o.im.copy ? "прочитано в пам'ять" : "відображено в пам'ять",
                  (unsigned)(tp1 - tp0), (unsigned)(millis() - tp1));
  }
  o.session = esp_random() | 1;
  o.ctr = 0;
  portENTER_CRITICAL(&otaMux);
  for (uint32_t i = 0; i < words; i++) o.need[i] = 0xFFFFFFFFu;
  if (o.blocks & 31) o.need[words - 1] = (1u << (o.blocks & 31)) - 1;
  o.needN = o.blocks;
  portEXIT_CRITICAL(&otaMux);
  Serial.printf("оновлення: прошивка %s%s, %u байт, %u блоків, відбиток %02x%02x%02x%02x; звук в ефірі зупинено\n", o.version, o.fromCard ? " (з картки)" : "", (unsigned)o.size,
                (unsigned)o.blocks, o.sha[0], o.sha[1], o.sha[2], o.sha[3]);
  // звук в эфире — на паузу (тем же способом, что при замере каналов), причём «налегке»: звук не готовится вовсе
  txPauseLight = true;
  txPause = true;
  for (int i = 0; i < 50 && !txPaused; i++) vTaskDelay(pdMS_TO_TICKS(2));
  vTaskDelay(pdMS_TO_TICKS(20));
  o.stage = OT_ANNOUNCE;
  uint32_t t0 = millis();
  while (millis() - t0 < 2500 && !o.cancel) {
    otaTxAnnounce();
    vTaskDelay(pdMS_TO_TICKS(60));
  }
  OtaCount c = otaTxCount(8000);
  uint8_t result = OT_DONE;
  if (o.cancel) result = OT_CANCELLED;
  else if (!c.want && !o.force) result = OT_NOBODY;
  else {
    o.stage = OT_SEND;
    uint32_t idx = 0, lastAnn = millis(), lastPoll = 0, emptySince = 0;
    for (;;) {
      uint32_t now = millis();
      if (o.cancel) {
        result = OT_CANCELLED;
        break;
      }
      if (now - lastAnn > 500) {   // объявление повторяется: приёмник, включённый позже, тоже подхватит
        lastAnn = now;
        otaTxAnnounce();
      }
      if (!o.needN) {   // всё отправлено: спрашиваем, чего кому не хватает
        o.stage = OT_FINISH;
        c = otaTxCount(8000);
        if (!c.recv) break;   // у всех, кто отвечает, образ целиком (или никто уже не отвечает)
        if (o.force && !c.want) break;
        if (!emptySince) emptySince = now;
        if (now - lastPoll > 90) {
          lastPoll = now;
          otaTxSignal(OTA_POLL);
        }
        vTaskDelay(2);
        continue;
      }
      emptySince = 0;
      // следующий нужный блок по кругу
      uint32_t b = idx, tries = 0;
      while (tries < o.blocks) {
        if ((b & 31) == 0 && !o.need[b >> 5]) {
          tries += 32;
          b += 32;
          if (b >= o.blocks) b = 0;
          continue;
        }
        if (o.need[b >> 5] & (1u << (b & 31))) break;
        tries++;
        if (++b >= o.blocks) b = 0;
      }
      if (tries >= o.blocks) {   // счёт разошёлся с картой — пересчитать
        portENTER_CRITICAL(&otaMux);
        uint32_t n = 0;
        for (uint32_t i = 0; i < o.blocks; i++) n += (o.need[i >> 5] >> (i & 31)) & 1;
        o.needN = n;
        portEXIT_CRITICAL(&otaMux);
        continue;
      }
      portENTER_CRITICAL(&otaMux);
      o.need[b >> 5] &= ~(1u << (b & 31));
      o.needN = o.needN - 1;
      portEXIT_CRITICAL(&otaMux);
      uint32_t n = b == o.blocks - 1 ? o.size - b * OTA_BLOCK : OTA_BLOCK;
      otaTxPacket(OTA_DATA, b, o.img + b * OTA_BLOCK, n);
      o.sent = o.sent + 1;
      idx = b + 1 >= o.blocks ? 0 : b + 1;
      // Передышка каждые 12 блоков, даже если радио успевает: задача раздачи не должна занимать ядро сплошь —
      // иначе простаивающей задаче ядра не достаётся ничего, и сторож перезапускает плату.
      if ((o.sent % 12) == 0) vTaskDelay(1);
    }
  }
  if (result == OT_CANCELLED)
    for (int i = 0; i < 12; i++) {
      otaTxSignal(OTA_CANCEL);
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  bool self = o.fromCard && o.selfWrite && result != OT_CANCELLED;   // обновление с карты: после приёмников — сам передатчик
  for (int i = 0; i < 60 && (int32_t)(rPut - rGot) > 0; i++) vTaskDelay(1);
  Serial.printf("оновлення: передачу закінчено за %u с, блоків надіслано %u (із повторами)%s\n", (unsigned)((millis() - o.startMs) / 1000),
                (unsigned)o.sent, self ? "; тепер записую прошивку в передавач" : "; звук в ефірі повернуто");
  if (self) {   // звук остаётся на паузе: запись во флеш дёргает оба ядра, а после неё всё равно перезапуск
    o.stage = OT_SELF;
    bool ok = o.selfWrite();
    portENTER_CRITICAL(&otaMux);
    uint32_t *nd0 = o.need;
    o.need = nullptr;
    portEXIT_CRITICAL(&otaMux);
    free(nd0);
    o.img = nullptr;
    o.endMs = millis();
    if (ok) {
      Serial.println("оновлення: прошивку записано в передавач — перезапуск");
      o.stage = OT_RESTART;
      vTaskDelay(pdMS_TO_TICKS(1500));   // дать экрану показать «перезапуск»
      if (o.selfDone) o.selfDone();      // не возвращается
    }
    Serial.println("оновлення: НЕ ВДАЛОСЯ записати прошивку в передавач — лишаюсь на колишній");
    rPut = 0;
    rGot = 0;
    rLastDoneMs = millis();
    txPause = false;
    txPauseLight = false;
    o.stage = OT_SELFFAIL;
    vTaskDelete(NULL);
  }
  // звук — обратно в эфир; дальше только слушаем, как приёмники пишут прошивку
  rPut = 0;
  rGot = 0;
  rLastDoneMs = millis();
  txPause = false;
  txPauseLight = false;
  if (result == OT_DONE) {
    o.stage = OT_WRITE;
    uint32_t t1 = millis();
    while (millis() - t1 < 120000) {
      c = otaTxCount(12000);
      if (c.done + c.lost >= c.want) break;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  portENTER_CRITICAL(&otaMux);
  uint32_t *nd = o.need;
  o.need = nullptr;
  portEXIT_CRITICAL(&otaMux);
  free(nd);
  o.img = nullptr;
  if (!o.fromCard) otaImgClose(o.im);
  o.endMs = millis();
  c = otaTxCount(12000);
  Serial.printf("оновлення: %s — оновлено %d, не вдалося %d, зникло %d, уже мали цю прошивку %d; усього %u с\n",
                result == OT_DONE ? "ГОТОВО" : result == OT_NOBODY ? "оновлювати нікого" : "СКАСОВАНО", c.done, c.fail, c.lost + c.recv + c.ready, c.same,
                (unsigned)((o.endMs - o.startMs) / 1000));
  o.stage = result;
  vTaskDelete(NULL);
}

// начать обновление (из главного цикла или экрана). false — уже идёт или сейчас нельзя.
static bool otaTxStart(bool demo = false, bool force = false, const uint8_t *cardImg = nullptr, uint32_t cardLen = 0, const char *cardVer = nullptr,
                       const uint8_t *cardSha = nullptr) {
  if (otaTxActive() || !secHave || txPause) return false;
  otaTx.demo = demo;
  otaTx.force = force;
  otaTx.fromCard = cardImg != nullptr;
  otaTx.cardImg = cardImg;
  otaTx.cardLen = cardLen;
  otaTx.cardSha = cardSha;
  strlcpy(otaTx.version, cardVer ? cardVer : FW_VERSION, sizeof(otaTx.version));
  otaTx.seq = otaTx.seq + 1;
  otaTx.stage = OT_PREP;
  return xTaskCreatePinnedToCore(otaTxTask, "ota", 6144, NULL, 19, NULL, 0) == pdPASS;
}

static const char *otaPhaseName(uint8_t ph, uint8_t err) {
  switch (ph) {
    case OS_SAME: return "уже має цю прошивку";
    case OS_RECV: return "приймає";
    case OS_CHECK: return "перевіряє";
    case OS_WRITE: return "записує";
    case OS_DONE: return "оновлено, перезапускається";
    case OS_FAIL:
      return err == OE_NOSLOT ? "немає другого розділу — потрібна заливка по USB" : err == OE_NOMEM ? "забракло пам'яті" : err == OE_HASH ? "відбиток не зійшовся"
           : err == OE_FLASH ? "помилка запису" : err == OE_LOST ? "передавач зник" : err == OE_CANCEL ? "скасовано" : "не вдалося";
    default: return "—";
  }
}
static void otaTxPrint() {
  OtaTx &o = otaTx;
  static const char *const ST[] = { "не йде", "готуюсь", "оголошую", "надсилаю", "досилаю", "приймачі записують", "готово", "оновлювати нікого", "скасовано", "помилка",
                                    "передавач записує прошивку в себе", "передавач перезапускається", "запис у передавач не вдався" };
  Serial.printf("оновлення приймачів: %s; блоків %u, лишилось надіслати %u, надіслано %u\n", ST[o.stage > 12 ? 0 : o.stage], (unsigned)o.blocks,
                (unsigned)o.needN, (unsigned)o.sent);
  for (auto &p : o.peer)
    if (p.used)
      Serial.printf("  приймач %02X%02X%02X: %s, %u %%, блоків %u, чутно %u мс тому\n", p.id[0], p.id[1], p.id[2], otaPhaseName(p.phase, p.err), p.percent, p.got,
                    (unsigned)msSince(p.seenMs));
}

// ---- Автообновление (с 2.42). Владелец 08.10: «приемники, которые будут в дальнейшем подключаться, если имеют версию
// прошивки более старую чем есть, должны автоматически выполнять обновление с передатчика».
// Раз в секунду передатчик смотрит список приёмников: кто на связи, не спит, не выключен и с прошивкой старее его
// собственной — и если такой держится 10 с, сам начинает раздачу своей прошивки (то же, что «Оновити» / M1).
// Версию приёмник сообщает точно с 2.42 (RxInfo.spare); прежние точной не сообщают — их считаем старыми.
// Раздача на ~20 с останавливает звук всем, поэтому: не чаще раза в две минуты и не больше двух попыток на приёмник
// за одно включение передатчика (не обновился дважды — значит, дело не в случайной помехе, нужен человек).
// Выключатель — настройка autoUpd (порт: M6 — включить, M7 — выключить).
static volatile uint32_t otaAutoRuns;
static void otaAutoTick() {
  static uint32_t lastTick, lastStart;
  uint32_t now = millis();
  if (!cfg.isTx || now - lastTick < 1000) return;
  lastTick = now;
  {   // копию разности каналов (с 2.43, см. proto.h) слать, только когда её понимают все приёмники на связи
    bool any = false, all = true;
    portENTER_CRITICAL(&peerMux);
    for (auto &p : peers)
      if (p.used && p.seenMs && msSince(p.seenMs, now) < 6000) {
        any = true;
        if (!(p.fwMaj > 2 || (p.fwMaj == 2 && p.fwMin >= 43))) all = false;
      }
    portEXIT_CRITICAL(&peerMux);
    txSideCopyOn = any && all;
  }
  if (!cfg.autoUpd || now < 20000 || otaTxActive() || txPause || (lastStart && now - lastStart < 120000)) return;
  uint8_t maj, mn;
  fwParts(maj, mn);
  bool go = false;
  uint8_t who[3] = {};
  portENTER_CRITICAL(&peerMux);
  for (auto &p : peers) {
    bool live = p.used && p.seenMs && msSince(p.seenMs, now) < 3000 && !(p.flags & (ST_SLEEP | ST_OFF)) && !p.wantOff;
    bool older = p.hasInfo && p.fwMaj ? (p.fwMaj < maj || (p.fwMaj == maj && p.fwMin < mn)) : (p.hasInfo || p.fw < maj * 10 + mn / 10);
    if (!live || !older || p.autoTries >= 2) {
      p.autoNeedMs = 0;
      continue;
    }
    if (!p.autoNeedMs) p.autoNeedMs = now ? now : 1;
    if (now - p.autoNeedMs >= 10000 && !go) {
      go = true;
      memcpy(who, p.id, 3);
    }
  }
  if (go)   // раздача обновит всех, кому она нужна, — попытку засчитать каждому
    for (auto &p : peers)
      if (p.autoNeedMs) {
        p.autoTries++;
        p.autoNeedMs = 0;
      }
  portEXIT_CRITICAL(&peerMux);
  if (!go) return;
  lastStart = now ? now : 1;
  bool ok = otaTxStart();
  otaAutoRuns = otaAutoRuns + (ok ? 1 : 0);
  Serial.printf("автооновлення: у приймача %02X%02X%02X прошивка старіша за %s — %s\n", who[0], who[1], who[2], FW_VERSION,
                ok ? "починаю роздачу" : "зараз неможливо");
}
