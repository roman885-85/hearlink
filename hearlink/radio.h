// Радио: ESP-NOW широковещанием на выбранном канале и скорости.
//
// Приёмники в эфир не выходят. Передатчик — обычный участник Wi-Fi: слушает эфир перед каждой передачей.
// Чужие пакеты отсекаются по первым байтам и номеру набора.
#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "proto.h"
#include "sec.h"
#include "bbox.h"

struct RateDef {
  const char *name;
  wifi_phy_mode_t mode;
  wifi_phy_rate_t rate;
  uint16_t kbps;
  uint8_t kind;      // 0 — DSSS (11b), 1 — OFDM (11g), 2 — «дальний» режим Espressif
};
// порядок не менять: номер строки хранится в настройках
static const RateDef RATES[] = {
  { "1", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_1M_L, 1000, 0 },    { "2", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_2M_L, 2000, 0 },
  { "5.5", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_5M_L, 5500, 0 },  { "11", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_11M_L, 11000, 0 },
  { "6", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_6M, 6000, 1 },      { "9", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_9M, 9000, 1 },
  { "12", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_12M, 12000, 1 },   { "18", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_18M, 18000, 1 },
  { "24", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_24M, 24000, 1 },   { "36", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_36M, 36000, 1 },
  { "48", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_48M, 48000, 1 },   { "54", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_54M, 54000, 1 },
  { "0.5", WIFI_PHY_MODE_LR, WIFI_PHY_RATE_LORA_500K, 500, 2 },
};
static const int N_RATES = sizeof(RATES) / sizeof(RATES[0]);
enum { R_1M = 0, R_2M = 1, R_5M = 2, R_11M = 3, R_6M = 4, R_12M = 6, R_24M = 8, R_LR500 = 12 };

// Сколько микросекунд пакет из bytes байт данных занимает эфир. К данным ESP-NOW добавляет 43 байта
// (заголовок Wi-Fi, свой заголовок, контрольная сумма); перед пакетом идёт вступление: у OFDM 20 мкс,
// у DSSS 192 мкс, у «дальнего» режима — около 500 мкс (оценка, см. замер в журнале).
static inline uint32_t airUs(int bytes, int rateIdx) {
  const RateDef &r = RATES[rateIdx];
  uint32_t bits = (bytes + 43) * 8;
  if (r.kind == 1) return 20 + 4 * ((bits + 22 + r.kbps * 4 / 1000 - 1) / (r.kbps * 4 / 1000));
  return (r.kind == 2 ? 500 : 192) + bits * 1000 / r.kbps;
}
// Поток помещается, если пакет занимает не больше 45 % времени между пакетами: остальное — ожидание
// свободного эфира, сообщения приёмников и чужие сети.
static inline bool qFits(int q, int rateIdx, bool st = false) { return airUs(qPktLen(q, st && qStereoOk(q)), rateIdx) * 100 <= qFrameUs(q) * 45; }
// Качество, которое реально пойдёт в эфир: выбранное или ниже, если на этой скорости оно не помещается.
static inline int qForRate(int want, int rateIdx, bool st = false) {
  int q = want < 0 ? 0 : want >= Q_COUNT ? Q_COUNT - 1 : want;
  while (q < Q_COUNT - 1 && !qFits(q, rateIdx, st)) q++;
  return q;
}
static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// счётчики передатчика (за секунду, обнуляет отчёт)
static volatile uint32_t rSent, rRefused, rAirFail;
// Сколько пакетов отдано драйверу и ещё не ушло — это rPut − rGot: rPut меняет только задача передачи, rGot — только
// ответ драйвера. Отдельный счётчик rPending меняли обе задачи без защиты, и за каждую гонку он «уплывал» на единицу
// вверх (07.10: за шесть минут дорос до порога прореживания, и передатчик прореживал уже всё подряд).
static volatile uint32_t rLastDoneMs;     // когда драйвер последний раз ответил
static volatile int rLastErr;
static uint32_t rRestarts;                // сколько раз радио пришлось перезапускать
static volatile bool rApply;              // просьба применить новые канал/скорость/мощность
// когда ушли последние пакеты — для замера ожидания эфира. Мест 64: при 16 замер «упирался» в 32 мс
// (очередь длиннее 16 пакетов затирала свои же отметки), и настоящих задержек в 50–60 мс видно не было.
static volatile uint32_t rSendUs[64], rPut, rGot;
static volatile int32_t rPendMax;         // наибольшая очередь драйвера за секунду, пакетов (обнуляет отчёт)
static volatile uint32_t rAirSumUs, rAirN, rAirMaxUs;  // сумма, число и наибольшее ожидание (обнуляет читающий)
static volatile uint32_t rAir2SumUs, rAir2N, rAir2MaxUs;  // то же для отчёта в порт
static volatile uint32_t rSentTotal, rRefusedTotal;    // с запуска
// приём
static volatile int8_t rRssi;
static volatile uint32_t rForeign, rLastRxMs;
typedef void (*AudioPacketFn)(const Hdr *h, const uint8_t *payload);
static AudioPacketFn rOnAudio;
static void (*rOnStatus)(const uint8_t *data, int len, int8_t rssi);   // передатчик: пришли сведения приёмника (прежнего или длинного вида)
static void (*rOnCommand)(const uint8_t *data, int len);     // приёмник: пришла команда (прежнего или длинного вида)
static void (*rOnPairReq)(const PairReq *r);                 // передатчик: приёмник без ключа просит доступ
static void (*rOnPairRsp)(const PairRsp *r);                 // приёмник: передатчик прислал ключ набора
static void (*rOnKeyMsg)(const KeyMsg *m);                   // приёмник: передатчик прислал новый ключ набора
static void (*rOnKeyReq)(const KeyReq *q);                   // передатчик: приёмник просит новый ключ набора
static void (*rOnOta)(const uint8_t *data, int len);         // обновление прошивки по радио (ota.h)
static void (*rOnDebug)(const uint8_t *data, int len);       // отладка по радио (dbgair.h): строка или команда другого устройства набора
static volatile uint32_t rLastBadMs;                         // приёмник: когда пакет своего набора пришёл с чужой подписью
static volatile bool rKitDirty;                              // приёмник: номер набора сменился вслед за передатчиком — записать
// Слышен передатчик набора (любого — заголовок пакета открыт), даже если ключа нет. Нужно приёмнику без ключа:
// встать на канал передатчика и попросить доступ.
static volatile uint32_t rLastAnyMs;
static volatile uint32_t rLastAskMs;                         // приёмник: когда последний раз просил доступ (после просьбы с канала не уходить)
static volatile uint8_t rAnyCh;
static volatile uint32_t rHeardEpoch, rHeardSeq;             // последний принятый (проверенный) пакет звука
static volatile uint32_t rAudioCount;                        // сколько проверенных пакетов звука принято (с запуска)

static volatile uint8_t rReviveStep;       // какая ступень оживления радио сделана последней (0 — радио отвечает)
static volatile uint32_t rDeadSinceMs;     // с какого времени драйвер молчит
static volatile uint32_t rRevived;         // сколько раз радио удалось оживить без перезапуска платы
static void radioOnSent(const esp_now_send_info_t *, esp_now_send_status_t status) {
  rLastDoneMs = millis();
  if (rReviveStep) {
    bbMark(BB_ALIVE, rReviveStep, (millis() - rDeadSinceMs) / 10);
    rReviveStep = 0;
    rRevived = rRevived + 1;
  }
  uint32_t dt = (uint32_t)esp_timer_get_time() - rSendUs[rGot & 63];
  rGot = rGot + 1;
  if (dt < 200000) {
    rAirSumUs = rAirSumUs + dt;
    rAirN = rAirN + 1;
    if (dt > rAirMaxUs) rAirMaxUs = dt;
    rAir2SumUs = rAir2SumUs + dt;
    rAir2N = rAir2N + 1;
    if (dt > rAir2MaxUs) rAir2MaxUs = dt;
  }
  if (dt >= 60000 && dt < 2000000) bbMark(BB_WAIT, 0, dt / 1000);
  if (status != ESP_NOW_SEND_SUCCESS) rAirFail = rAirFail + 1;
}

// Поиск задержек (07.10: у передатчика раз в 2,5 с отправка встаёт на ~30 мс): самая долгая обработка принятого пакета
// и какого он вида, самая долгая перенастройка радио, самый долгий вызов «после отправки». Печатает команда u.
static volatile uint32_t dgRecvMaxUs, dgRecvMagic, dgApplyMaxUs, dgApplyN, dgAfterMaxUs, dgSendMaxUs;
static void radioOnRecv2(const esp_now_recv_info_t *info, const uint8_t *data, int len);
static void radioOnRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  uint32_t t0 = (uint32_t)esp_timer_get_time();
  radioOnRecv2(info, data, len);
  uint32_t d = (uint32_t)esp_timer_get_time() - t0;
  if (d > dgRecvMaxUs) {
    dgRecvMaxUs = d;
    dgRecvMagic = len >= 2 ? (uint32_t)(data[0] | data[1] << 8) : 0;
  }
}
static void radioOnRecv2(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  static uint8_t plain[PKT_MAX];   // расшифрованный звук (эта функция — только в задаче Wi-Fi)
  const Hdr *h = (const Hdr *)data;
  if (len < 4) return;
  if (len == (int)sizeof(PairReq) && h->magic == MAGIC_PAIRREQ && h->ver == PROTO_VER) {
    bbMark(BB_PKT, data[0], len);
    if (rOnPairReq) rOnPairReq((const PairReq *)data);
    return;
  }
  if (len == (int)sizeof(PairRsp) && h->magic == MAGIC_PAIRRSP && h->ver == PROTO_VER) {
    if (rOnPairRsp) rOnPairRsp((const PairRsp *)data);
    return;
  }
  if (len == (int)sizeof(KeyMsg) && h->magic == MAGIC_KEYMSG && h->ver == PROTO_VER) {
    if (rOnKeyMsg) rOnKeyMsg((const KeyMsg *)data);
    return;
  }
  if (len == (int)sizeof(KeyReq) && h->magic == MAGIC_KEYREQ && h->ver == PROTO_VER) {
    bbMark(BB_PKT, data[0], len);
    if (rOnKeyReq) rOnKeyReq((const KeyReq *)data);
    return;
  }
  if ((len == (int)sizeof(RxStatus) || len == (int)sizeof(RxStatus2) || len == (int)sizeof(RxInfo)) && h->magic == MAGIC_STATUS && h->ver == PROTO_VER && h->kit == cfg.kit) {
    if (cfg.isTx) bbMark(BB_PKT, data[0], len);
    if (rOnStatus) rOnStatus(data, len, info->rx_ctrl->rssi);
    return;
  }
  if ((len == (int)sizeof(TxCommand) || len == (int)sizeof(TxCommand2)) && h->magic == MAGIC_COMMAND && h->ver == PROTO_VER && h->kit == cfg.kit) {
    if (rOnCommand) rOnCommand(data, len);
    return;
  }
  if (len >= 25 && h->magic == 0x4F48 && h->ver == PROTO_VER && h->kit == cfg.kit) {   // MAGIC_OTA: 16 байт заголовка, тело и подпись
    if (rOnOta) rOnOta(data, len);
    return;
  }
  if (len >= 28 && h->magic == 0x4748 && h->ver == PROTO_VER && h->kit == cfg.kit) {   // MAGIC_DEBUG: 20 байт заголовка и подпись
    if (cfg.isTx) bbMark(BB_PKT, data[0], len);
    if (rOnDebug) rOnDebug(data, len);
    return;
  }
  uint8_t aq = len >= (int)sizeof(Hdr) ? h->q & Q_MASK : 255;
  bool ast = len >= (int)sizeof(Hdr) && (h->q & Q_STEREO);
  if (aq >= Q_COUNT || h->magic != MAGIC || h->ver != PROTO_VER || (h->q & ~(Q_MASK | Q_STEREO | Q_LANG_EN)) || (ast && !qStereoOk(aq)) ||
      (len != qPktLen(aq, ast) && !(aq == Q_HI && ast && len == qPktLen(aq, ast) + SC_LEN))) {
    rForeign = rForeign + 1;
    return;
  }
  rPktSc = len != qPktLen(aq, ast);   // «найвища» стерео с копией разности каналов (с 2.43)
  if (!rOnAudio) return;   // передатчику чужой звук не нужен
  rLastAnyMs = millis();
  rAnyCh = h->flags >> 4;
  if (!secHave) return;    // ключа нет — приёмник ещё не подключён к набору
  // Номер набора — только подпись для людей: «свой» пакет определяет подпись ключом набора. Сменили номер на
  // передатчике — приёмники идут за ним сами (раньше они молча глохли).
  int plen = len - (int)sizeof(Hdr) - SEC_TAG;
  if (!secOpen(SEC_AUDIO, h->boot, h->seq, NULL, data, sizeof(Hdr), data + sizeof(Hdr), plen, data + len - SEC_TAG, plain)) {
    if (h->kit == cfg.kit) {     // свой номер набора, но подпись чужая: подделка — или на передатчике сменили ключ набора
      secBadTag = secBadTag + 1;
      rLastBadMs = millis() ? millis() : 1;
    } else rForeign = rForeign + 1;
    return;
  }
  if (h->kit != cfg.kit) {
    cfg.kit = h->kit;
    rKitDirty = true;
  }
  if (secShadow & 2) secShadowRun(data + sizeof(Hdr), plen);
  if (h->boot < secEpoch) {      // запись прежнего включения передатчика
    secReplay = secReplay + 1;
    return;
  }
  if (h->boot > secEpoch) {      // передатчик включили заново
    secEpoch = h->boot;
    secEpochDirty = true;
  }
  rRssi = info->rx_ctrl->rssi;
  rLastRxMs = millis();
  rHeardEpoch = h->boot;
  rHeardSeq = h->seq;
  rAudioCount = rAudioCount + 1;
  rOnAudio(h, plain);
}

static void radioSetParams() {
  esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_max_tx_power(cfg.powerDbm * 4);
  // приёмник отвечает редко и коротко — всегда на самой стойкой обычной скорости (1 Мбит/с)
  int ri = cfg.isTx ? cfg.rateIdx : R_1M;
  esp_now_rate_config_t rc = {};
  rc.phymode = RATES[ri].mode;
  rc.rate = RATES[ri].rate;
  esp_now_set_peer_rate_config(BCAST, &rc);
}

static bool radioStartEspNow() {
  if (esp_now_init() != ESP_OK) return false;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);
  if (cfg.isTx) esp_now_register_send_cb(radioOnSent);
  esp_now_register_recv_cb(radioOnRecv);   // передатчик тоже слушает: приёмники сообщают о себе
  radioSetParams();
  rPut = 0;
  rGot = 0;
  rLastDoneMs = millis();
  return true;
}

static bool radioBegin() {
  WiFi.mode(WIFI_STA);
  wifi_country_t cc = {};
  memcpy(cc.cc, "UA", 2);
  cc.schan = 1;
  cc.nchan = 13;
  cc.policy = WIFI_COUNTRY_POLICY_MANUAL;
  esp_wifi_set_country(&cc);
  // Не esp_wifi_set_ps: ядро Arduino на каждый запуск радио (событие STA_START, позже этой строки) само ставит
  // режим, который помнит у себя, — а помнит оно «экономный». Так оно запомнит «без экономии».
  WiFi.setSleep(WIFI_PS_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);
  // «дальний» режим Espressif (0,5 Мбит/с) включён всегда и у всех: обычные пакеты он принимать не мешает
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  return radioStartEspNow();
}

// Канал, скорость и мощность меняются только между пакетами — вызывать из той же задачи, что шлёт.
static void radioApplyIfAsked() {
  if (!rApply) return;
  uint32_t t0 = (uint32_t)esp_timer_get_time();
  for (int i = 0; i < 30 && (int32_t)(rPut - rGot) > 0; i++) vTaskDelay(1);
  dgApplyN = dgApplyN + 1;
  uint32_t d = (uint32_t)esp_timer_get_time() - t0;
  if (d > dgApplyMaxUs) dgApplyMaxUs = d;
  radioSetParams();
  bbMark(BB_APPLY, cfg.channel, d / 1000);
  rPut = 0;
  rGot = 0;
  rLastDoneMs = millis();
  rApply = false;
}

// Драйвер Wi-Fi целиком: выгрузить и поднять заново. Простая остановка (esp_wifi_stop/start) его буферы отправки
// не возвращает — после неё все отправки так и отклонялись. Настройки буферов — как у ядра Arduino (wifiLowLevelInit).
static bool radioReinit() {
  esp_now_deinit();
  esp_wifi_stop();
  if (esp_wifi_deinit() != ESP_OK) return false;
  wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
  ic.static_tx_buf_num = 0;
  ic.dynamic_tx_buf_num = 32;
  ic.tx_buf_type = 1;
  ic.cache_tx_buf_num = 4;
  ic.static_rx_buf_num = 4;
  ic.dynamic_rx_buf_num = 32;
  if (esp_wifi_init(&ic) != ESP_OK) return false;
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  wifi_country_t cc = {};
  memcpy(cc.cc, "UA", 2);
  cc.schan = 1;
  cc.nchan = 13;
  cc.policy = WIFI_COUNTRY_POLICY_MANUAL;
  esp_wifi_set_country(&cc);
  if (esp_wifi_start() != ESP_OK) return false;
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  return radioStartEspNow();
}

// Отправить пакет. Если драйвер перестал отвечать (такое бывает: все отправки отклоняются с ESP_ERR_ESPNOW_NO_MEM
// и сами не возобновляются) — оживлять по ступеням, а не молчать:
//   1) через 0,3 с молчания — ESP-NOW и радио заново (быстро, но помогает не всегда);
//   2) ещё через 0,3 с — драйвер Wi-Fi целиком выгрузить и поднять;
//   3) ещё через 0,5 с — перезапустить плату без заставки (rOnDead ставит hearlink.ino): эфир вернётся через секунды.
static void (*rOnDead)();
static bool quickBoot;                     // этот запуск — быстрый перезапуск после вставшего радио (без заставки)
static bool updatedBoot;                   // этот запуск — после того как передатчик обновил сам себя с карты
static volatile uint8_t bbTestStep;        // отладка (порт: Q7…Q9): выполнить ступень оживления 1…3 на здоровом радио
static volatile uint8_t rReviveMax = 3;    // отладка (порт: Q0…Q3): до какой ступени доходить (0 — не оживлять вовсе)
static volatile uint8_t rReviveFrom = 1;   // отладка: с какой ступени начинать
static void radioSend(const uint8_t *pkt, size_t len) {
  static bool refusing;
  radioApplyIfAsked();
  uint32_t s0 = (uint32_t)esp_timer_get_time();
  esp_err_t err = esp_now_send(BCAST, pkt, len);
  uint32_t sd = (uint32_t)esp_timer_get_time() - s0;
  if (sd > dgSendMaxUs) dgSendMaxUs = sd;
  if (err == ESP_OK) {
    rSendUs[rPut & 63] = (uint32_t)esp_timer_get_time();
    rPut = rPut + 1;
    rSent = rSent + 1;
    rSentTotal = rSentTotal + 1;
    refusing = false;
  } else {
    rRefused = rRefused + 1;
    rRefusedTotal = rRefusedTotal + 1;
    rLastErr = err;
    if (!refusing) {   // первая отказанная отправка: запомнить, что было перед ней
      refusing = true;
      bbMark(BB_REFUSE, cfg.channel, (uint32_t)err & 0xFFFF);
      if (!rReviveStep) bbFreeze((int32_t)(rPut - rGot), msSince(rLastDoneMs), err, cfg.channel);
    }
  }
  uint32_t quiet = msSince(rLastDoneMs);
  uint8_t step;
  if (bbTestStep) {
    step = bbTestStep;
    bbTestStep = 0;
    if (!rReviveStep) rDeadSinceMs = millis();
  } else {
    if (quiet <= 300 || !rReviveMax) return;
    if (!rReviveStep) rDeadSinceMs = rLastDoneMs;
    step = rReviveStep ? rReviveStep + 1 : rReviveFrom;
    if (step > rReviveMax) step = rReviveMax;                    // выше не идём (отладка): повторять последнюю разрешённую
    if (rReviveStep == 2 && step == 3 && quiet <= 500) return;   // после полной перезагрузки драйвера дать ему полсекунды
  }
  uint32_t t0 = millis();
  rRestarts++;
  if (step <= 1) {
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    radioStartEspNow();
  } else if (step == 2) {
    radioReinit();
    rPut = 0;
    rGot = 0;
  } else {
    bbMark(BB_REVIVE, 3, 0);
    if (rOnDead) rOnDead();   // не возвращается
  }
  rReviveStep = step;
  rLastDoneMs = millis();
  bbMark(BB_REVIVE, step, millis() - t0);
}

// Приёмник: если своего передатчика не слышно, обойти каналы по кругу.
static void radioScanTick() {
  static uint32_t lastHop;
  uint32_t now = millis();
  if (rApply) {
    radioSetParams();
    rApply = false;
    lastHop = now;
    return;
  }
  // без ключа: на канале, где слышен передатчик, задержаться на 2,5 с (успеть попросить доступ) и идти дальше —
  // передатчиков рядом может быть несколько, а окно добавления открыто на одном
  if (!secHave && msSince(rLastAnyMs, now) < 300 && now - lastHop < 2500) return;
  if (!secHave && rLastAskMs && msSince(rLastAskMs, now) < 600) return;   // только что попросил доступ — дождаться ответа на этом канале
  if (!cfg.autoChannel || msSince(rLastRxMs, now) < 600 || now - lastHop < 60) return;
  lastHop = now;
  cfg.channel = cfg.channel % 13 + 1;
  esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);
}
