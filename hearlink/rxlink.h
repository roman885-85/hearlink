// Приёмник: обратная связь с передатчиком.
//
// Раз в две секунды (со случайной добавкой, чтобы приёмники не попадали друг на друга) приёмник шлёт
// короткий пакет о себе: имя, громкость, как слышит передатчик, потери. И выполняет команды передатчика:
// показать себя, принять имя, включиться/выключиться, громкость, тишина.
#pragma once
#include "esp_sleep.h"
#include "rxaudio.h"

void settingsSave();

static uint8_t rxId[3];
static volatile bool rxSaveAsked;
static volatile bool rxForgetAsked;        // передатчик удалил приёмник из набора: стереть ключи (делает главный цикл)
static uint32_t lkFrames, lkRecovered, lkLost, lkUnder;   // накоплено с прошлого сообщения

static const char *rxName() {
  static char def[NAME_LEN];
  if (cfg.nameLong[0]) return cfg.nameLong;
  if (cfg.name[0]) return cfg.name;
  snprintf(def, sizeof(def), tr("Приймач %02X%02X"), rxId[1], rxId[2]);
  return def;
}

// ---- настройки «для слуха и удобства» (с 2.32): одна дверь для ручки приёмника, команды передатчика и порта
static void rxLangApply() {   // язык надписей: свой выбор приёмника или как у передатчика
  uiLang = cfg.rxLang == 1 ? 0 : cfg.rxLang == 2 ? 1 : cfg.lang;
}
static int rxParamGet(uint8_t p) {
  switch (p) {
    case RXP_CLARITY: return cfg.rxClarity;
    case RXP_BALANCE: return cfg.rxBalance;
    case RXP_VOLMAX: return cfg.rxVolMax;
    case RXP_VIEW: return cfg.rxView;
    case RXP_LED: return cfg.rxLed;
    case RXP_LANG: return cfg.rxLang;
    case RXP_X_LOCK: return cfg.rxLock;
    case RXP_X_BOOST: return cfg.rxBoost;
  }
  return 0;
}
static bool rxParamSet(uint8_t p, int v) {   // значение приводится к допустимому; false — такой настройки нет
  auto lim = [](int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; };
  switch (p) {
    case RXP_CLARITY: cfg.rxClarity = lim(v, 0, 3); break;
    case RXP_BALANCE: cfg.rxBalance = lim(v, -5, 5); break;
    case RXP_VOLMAX:
      cfg.rxVolMax = lim(v, 1, 20);   // потолок шкалы: сама громкость (0…100 %) не меняется, меняется её «вес»
      break;
    case RXP_VIEW: cfg.rxView = lim(v, 0, 2); break;
    case RXP_LED: cfg.rxLed = lim(v, 0, 3); break;
    case RXP_LANG:
      cfg.rxLang = lim(v, 0, 2);
      rxLangApply();
      break;
    case RXP_X_LOCK: cfg.rxLock = lim(v, 0, 3); break;
    case RXP_X_BOOST: cfg.rxBoost = lim(v, 0, 12); break;
    default: return false;
  }
  return true;
}
// значение настройки, как оно идёт по радио (0…31)
static inline uint8_t rxParamWire(uint8_t p) {
  return (uint8_t)(rxParamGet(p) + (p == RXP_BALANCE ? 5 : 0));
}

// ---- история связи за последнюю минуту (экран «Зв'язок»): раз в секунду — уровень сигнала и доля потерянных пакетов
#define LNK_N 60
static int8_t lnkRssi[LNK_N];              // дБм; −128 — передатчика не было слышно
static uint8_t lnkLoss[LNK_N];             // потери, десятые доли процента (до 25,5 %)
static volatile uint32_t lnkPos;           // сколько замеров записано всего
static volatile uint32_t lnkAt;            // когда записан последний

// из задачи Wi-Fi
static void rxOnCommand(const uint8_t *data, int len) {
  static uint32_t lastEpoch, lastCtr;
  static bool have = false;
  static struct __attribute__((packed)) {
    uint8_t cmd, arg;
    char name[NAME_LONG];
  } body;
  const TxCommand *raw = (const TxCommand *)data;   // начало у обоих видов команды одинаковое
  bool wide = len == (int)sizeof(TxCommand2);
  size_t bl = wide ? CMD2_BODY : CMD_BODY;
  bool all = raw->id[0] == 0xFF && raw->id[1] == 0xFF && raw->id[2] == 0xFF;   // всем приёмникам набора сразу
  if (!all && memcmp(raw->id, rxId, 3)) return;
  // команду принимаем только с подписью ключом набора, из нынешнего включения передатчика и новее прежних
  if (!secOpen(SEC_COMMAND, raw->epoch, raw->ctr, raw->id, data, CMD_CLEAR, data + CMD_CLEAR, bl, data + CMD_CLEAR + bl, (uint8_t *)&body)) {
    if (secHave) secBadTag = secBadTag + 1;
    return;
  }
  if (raw->epoch != secEpoch || (have && raw->epoch == lastEpoch && (int32_t)(raw->ctr - lastCtr) < 0)) {
    secReplay = secReplay + 1;
    return;
  }
  if (have && raw->epoch == lastEpoch && raw->ctr == lastCtr) return;   // повтор той же команды
  have = true;
  lastEpoch = raw->epoch;
  lastCtr = raw->ctr;
  auto *c = &body;
  if (all) {   // общая команда пока одна: переход на другой канал
    if (c->cmd == CMD_HOP && c->arg >= 1 && c->arg <= 13 && c->arg != cfg.channel) {
      uint32_t at;
      memcpy(&at, c->name, 4);
      int32_t left = (int32_t)(at - rHeardSeq);          // сколько пакетов ещё придёт на прежнем канале
      if (left < 0) left = 0;
      if (left > 2000) return;
      rxHopSeq = at;
      rxHopDeadline = millis() + (uint32_t)(left * rxSeqMs) + 40;   // left — в кадрах (в двойном пакете их два)
      rxHopNow = left == 0;
      rxHopCh = c->arg;
    }
    return;
  }
  switch (c->cmd) {
    case CMD_IDENTIFY:
      if (millis() < rxIdentifyUntil) {   // «покажи себя» дважды подряд — запустить проверку выхода на слух
        rxIdentifyUntil = 0;
        rxDacTestAsk = true;
      } else rxIdentifyUntil = millis() + 6000;
      break;
    case CMD_DACTEST: rxDacTestAsk = true; break;
    case CMD_NAME:   // имя кладём в оба поля: длинное — рабочее, прежнее (13 букв) — на случай возврата к старой прошивке
      c->name[(wide ? NAME_LONG : NAME_LEN) - 1] = 0;
      utf8Clean(c->name);
      utf8Copy(cfg.nameLong, c->name, sizeof(cfg.nameLong));
      utf8Copy(cfg.name, c->name, sizeof(cfg.name));
      rxSaveAsked = true;
      break;
    case CMD_ENABLE:
      cfg.off = c->arg ? 0 : 1;
      rxSaveAsked = true;
      break;
    case CMD_VOLUME:
      cfg.volume = c->arg > 20 ? 20 : c->arg;   // шкала всегда 0…100 %; предел громкости — её потолок (rxVolumeScaled)
      rxMute = false;
      rxSaveAsked = true;
      break;
    case CMD_MUTE: rxMute = c->arg; break;
    case CMD_STEREO:
      cfg.rxStereo = c->arg ? 1 : 0;
      rxSaveAsked = true;
      break;
    case CMD_FORGET: rxForgetAsked = true; break;
    case CMD_SET:
      if (rxParamSet(c->arg >> 5, (c->arg & 31) - ((c->arg >> 5) == RXP_BALANCE ? 5 : 0))) rxSaveAsked = true;
      break;
    case CMD_EARTEST:
      if (rxPower != PW_STANDBY && !cfg.off) rxEarStart(c->arg != 0);
      break;
    case CMD_EQ:
      if (rxEqSet(c->arg >> 4, c->arg & 15)) rxSaveAsked = true;
      break;
  }
}

// ---- подключение к набору (см. sec.h): приёмник без ключа просит доступ у передатчика, который слышит
static SecPair rxPair;
static volatile uint16_t rxPairCode;       // четыре цифры для сверки с экраном передатчика
static uint8_t rxPairRsp[sizeof(PairRsp)];
static volatile bool rxPairGot;
static volatile uint32_t rxPairAsked;      // сколько просьб послано (для отчёта)

static void rxOnPairRsp(const PairRsp *r) {   // из задачи Wi-Fi
  if (secHave || rxPairGot || memcmp(r->id, rxId, 3)) return;
  memcpy(rxPairRsp, r, sizeof(PairRsp));
  rxPairGot = true;
}

// новый ключ набора от передатчика (ключ сменили: кого-то удалили из набора)
static uint8_t rxKeyMsg[sizeof(KeyMsg)];
static volatile bool rxKeyMsgGot;
static volatile uint32_t rxKeyAsked, rxKeyTaken;   // сколько раз просили новый ключ и сколько раз получили (для отчёта)
static void rxOnKeyMsg(const KeyMsg *m) {     // из задачи Wi-Fi
  if (!secDev.ok || rxKeyMsgGot || memcmp(m->id, rxId, 3) || m->gen <= secGen) return;
  memcpy(rxKeyMsg, m, sizeof(KeyMsg));
  rxKeyMsgGot = true;
}

// главный цикл
static void rxPairTick() {
  static uint32_t nextAsk;
  uint32_t now0 = millis();
  if (rxForgetAsked) {   // удалён из набора
    rxForgetAsked = false;
    secForget();
    Serial.println("передавач видалив приймач з набору: ключі стерто");
    return;
  }
  if (rKitDirty || rxLangDirty) {   // номер набора или язык сменились вслед за передатчиком
    rKitDirty = false;
    rxLangDirty = false;
    settingsSave();
  }
  if (rxKeyMsgGot) {     // пришёл новый ключ набора: он закрыт личным ключом этого приёмника
    KeyMsg m;
    memcpy(&m, rxKeyMsg, sizeof(m));
    uint8_t nn[12], box[sizeof(m.box)];
    secNonce(nn, SEC_KEYMSG, m.gen, 0, m.id);
    if (secDev.ok && m.gen > secGen && secOpenWith(secDev, nn, (const uint8_t *)&m, KEYMSG_CLEAR, m.box, sizeof(m.box), m.tag, box)) {
      uint32_t ep;
      memcpy(&ep, box + SEC_KEY + 1, 4);
      if (cfg.kit != box[SEC_KEY]) {
        cfg.kit = box[SEC_KEY];
        settingsSave();
      }
      secStore(box, ep, m.gen);
      rxKeyTaken = rxKeyTaken + 1;
      Serial.printf("отримано новий ключ набору (покоління %u)\n", (unsigned)secGen);
    }
    memset(box, 0, sizeof(box));
    memset(&m, 0, sizeof(m));
    rxKeyMsgGot = false;
    return;
  }
  if (secHave) {
    secPairFree(rxPair);
    rxPairGot = false;
    // свой передатчик слышен, но подпись у его пакетов другая, а годных нет уже полторы секунды: ключ набора сменили,
    // пока приёмник был выключен. Попросить новый — просьба подписана личным ключом, ответит только свой передатчик.
    if (secDev.ok && rLastBadMs && msSince(rLastBadMs, now0) < 600 && msSince(rLastRxMs, now0) > 1500 && (int32_t)(now0 - nextAsk) >= 0) {
      nextAsk = now0 + 900 + (esp_random() % 300);
      KeyReq q = {};
      q.magic = MAGIC_KEYREQ;
      q.ver = PROTO_VER;
      memcpy(q.id, rxId, 3);
      q.gen = secGen;
      q.rnd = esp_random();
      uint8_t nn[12];
      secNonce(nn, SEC_KEYREQ, q.gen, q.rnd, q.id);
      secHwOn();
      secHwTag(secDev.mac, nn, (const uint8_t *)&q, KEYREQ_CLEAR, NULL, 0, q.tag);
      secHwOff();
      rxSend((const uint8_t *)&q, sizeof(q));
      rxKeyAsked = rxKeyAsked + 1;
    }
    return;
  }
  if (!rxPair.ready) {
    if (!secPairGen(rxPair)) return;
    rxPairCode = secPairCode(rxPair.pub);
  }
  uint32_t now = millis();
  if (rxPairGot) {   // передатчик ответил: достать ключ набора
    PairRsp r;
    memcpy(&r, rxPairRsp, sizeof(r));
    static SecKeys wrap, dev;
    if (secPairKey(rxPair, r.pub, rxPair.pub, r.pub, wrap, dev) && secBox(false, wrap, r.box, sizeof(r.box), r.tag)) {
      uint32_t ep, gn;
      memcpy(&ep, r.box + SEC_KEY + 1, 4);
      memcpy(&gn, r.box + SEC_KEY + 5, 4);
      cfg.kit = r.box[SEC_KEY];
      settingsSave();
      secStoreDev(dev);
      secStore(r.box, ep, gn ? gn : 1);
      Serial.printf("підключено до набору %u\n", cfg.kit);
    }
    memset(&wrap, 0, sizeof(wrap));
    memset(&dev, 0, sizeof(dev));
    memset(&r, 0, sizeof(r));
    rxPairGot = false;
    return;
  }
  if (msSince(rLastAnyMs, now) > 400 || (int32_t)(now - nextAsk) < 0) return;   // передатчика не слышно — молчим
  uint8_t ch = rAnyCh;
  if (ch >= 1 && ch <= 13 && ch != cfg.channel) {   // услышали с соседнего канала — встать точно
    cfg.channel = ch;
    rApply = true;
    return;
  }
  nextAsk = now + 600 + (esp_random() % 300);
  PairReq q = {};
  q.magic = MAGIC_PAIRREQ;
  q.ver = PROTO_VER;
  memcpy(q.id, rxId, 3);
  memcpy(q.pub, rxPair.pub, PAIR_PUB);
  utf8Copy(q.name, rxName(), NAME_LEN);   // в просьбе о доступе имя прежней длины: передатчик покажет начало
  rxSend((const uint8_t *)&q, sizeof(q));
  rLastAskMs = now ? now : 1;
  rxPairAsked = rxPairAsked + 1;
}

static volatile uint32_t rxStatusSent;   // сколько раз приёмник сообщил о себе
// ---- «кого слышу» (proto.h, TxSeen): слышит ли нас передатчик
static volatile uint32_t rxSeenMs;       // когда мы последний раз были в списке передатчика (0 — ни разу)
static volatile uint32_t rxRosterMs;     // когда пришло последнее объявление (0 — ни разу: передатчик до 2.59)
static volatile uint32_t rxRosterN;      // сколько объявлений принято
static volatile bool rxRosterFull;       // у передатчика список приёмников полон — нас в нём может не быть законно
// из задачи Wi-Fi
static void rxOnSeen(const uint8_t *data, int len) {
  static uint32_t lastEpoch, lastCtr;
  static bool have;
  const TxSeen *raw = (const TxSeen *)data;
  uint8_t body[SEEN_BODY];
  if (!secOpen(SEC_SEEN, raw->epoch, raw->ctr, NULL, data, SEEN_CLEAR, data + SEEN_CLEAR, SEEN_BODY, data + SEEN_CLEAR + SEEN_BODY, body)) {
    if (secHave) secBadTag = secBadTag + 1;
    return;
  }
  // только из нынешнего включения передатчика и новее прежних: записанное и повторённое объявление не примем
  if (raw->epoch != secEpoch || (have && raw->epoch == lastEpoch && (int32_t)(raw->ctr - lastCtr) <= 0)) {
    secReplay = secReplay + 1;
    return;
  }
  have = true;
  lastEpoch = raw->epoch;
  lastCtr = raw->ctr;
  uint8_t n = body[0] & 0x7F;
  if (n > SEEN_MAX) n = SEEN_MAX;
  bool me = false;
  for (uint8_t i = 0; i < n; i++)
    if (!memcmp(body + 1 + i * 3, rxId, 3)) me = true;
  uint32_t now = millis();
  rxRosterFull = body[0] & 0x80;
  rxRosterMs = now ? now : 1;
  rxRosterN = rxRosterN + 1;
  if (me) rxSeenMs = now ? now : 1;
}

// Сверка (главный цикл, приёмник в работе). Передатчик слышен и объявляет, кого слышит; мы за это время не меньше
// четырёх раз сообщили о себе — а нас в списке нет дольше RX_UNSEEN_MS. Значит, наши сообщения до него не доходят:
//   1) перезагрузить радио (около 20 мс без приёма);
//   2) не помогло за следующие RX_UNSEEN_MS — перезапустить плату без заставки (2–3 с без звука). Один раз: если и
//      после перезапуска нас не слышат, дело не в нас (например, передатчик дальше, чем достаёт приёмник), и снова
//      перезапускаться — только рвать звук; остаётся изредка перезагружать радио (через 1, 2, 4, 8, потом по 10 минут).
// Всё забывается, когда нас слышат десять минут подряд. Повторный перезапуск — не раньше чем через полчаса работы и
// только если после прошлого нас хоть раз услышали.
#define RX_UNSEEN_MS 12000
#define RX_HEAL_BOOT 0x52584842u
RTC_NOINIT_ATTR static uint32_t rxHealBootMagic;   // плата перезапущена оттого, что передатчик не слышал приёмник
RTC_NOINIT_ATTR static uint32_t rxHealBootCount;   // сколько таких перезапусков с включения питания
RTC_NOINIT_ATTR static uint32_t rxFakeKeep;        // проверка n12=4: «неисправность» переживает перезапуск платы
static bool rxHealBoot;                            // этот запуск — после такого перезапуска
static bool rxQuietBoot;                           // и потому без заставки и «запуска» (до первого выхода в работу)
static void (*rxSelfRestart)();                    // перезапустить плату (ставит hearlink.ino); не возвращается
static volatile uint8_t rxUnseenStage;             // для отчёта: 0 — порядок, 1 — радио перезагружали, 2 — плату перезапускали
static void rxSeenTick() {
  static uint32_t since, statusAt, seenSince, backoffMs = 60000;
  static bool everSeen, reinitDone;
  uint32_t now = millis();
  if (rxSeenMs && msSince(rxSeenMs, now) < 9000) {   // нас слышат (передатчик помнит приёмник 7 с, объявляет раз в 2 с)
    since = 0;
    everSeen = true;
    if (!seenSince) seenSince = now ? now : 1;
    else if (now - seenSince > 600000) {
      reinitDone = false;
      backoffMs = 60000;
      rxUnseenStage = 0;
    }
    return;
  }
  seenSince = 0;
  bool roster = rxRosterMs && msSince(rxRosterMs, now) < 5000;
  if (rxPower != PW_RUN || !secHave || cfg.off || rxOtaMute || !roster || rxRosterFull || msSince(rLastRxMs, now) > 400) {
    since = 0;
    return;
  }
  if (!since) {
    since = now ? now : 1;
    statusAt = rxStatusSent;
    return;
  }
  if (now - since < RX_UNSEEN_MS || rxStatusSent - statusAt < 4) return;
  since = 0;
  if (!reinitDone) {
    reinitDone = true;
    rxUnseenStage = 1;
    rxRadioHeal(2);
    return;
  }
  if (rxSelfRestart && (!rxHealBoot || (everSeen && now > 1800000))) {
    rxUnseenStage = 2;
    rxSelfRestart();
    return;
  }
  if (rxHealMs && msSince(rxHealMs, now) > backoffMs) {
    backoffMs = backoffMs >= 300000 ? 600000 : backoffMs * 2;
    rxRadioHeal(2);
  }
}

static void rxLinkBegin() {
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  memcpy(rxId, mac + 3, 3);
  rOnSeen = rxOnSeen;
  rOnCommand = rxOnCommand;
  rOnPairRsp = rxOnPairRsp;
  rOnKeyMsg = rxOnKeyMsg;
}

// вызывать из основного цикла; счётчики за секунду передаёт отчёт
static void rxLinkCount(uint32_t frames, uint32_t recovered, uint32_t lost, uint32_t underSamples) {
  {   // замер для экрана «Зв'язок»
    uint32_t tot = frames + recovered + lost, i = lnkPos % LNK_N;
    bool sig = rLastRxMs && msSince(rLastRxMs) < 1200;
    uint32_t pm = tot ? (recovered + lost) * 1000 / tot : 0;
    lnkRssi[i] = sig ? (int8_t)rRssi : -128;
    lnkLoss[i] = pm > 255 ? 255 : pm;
    lnkAt = millis();
    lnkPos = lnkPos + 1;
  }
  lkFrames += frames;
  lkRecovered += recovered;
  lkLost += lost;
  lkUnder += underSamples;
}

#include "rxled.h"   // цветной светодиод: что светится в каком состоянии

static void rxPowerGo(uint8_t st, uint8_t why = 0) {
  rxPowerWhy = why;
  rxPowerAt = millis();
  rxPower = st;
}

// Вывод кнопки ручки: снять с него всё, что относится к прерываниям и пробуждению.
// Во сне кнопка служит «будильником»: для этого её выводу ставится вид прерывания «пока уровень низкий». Этот вид
// оставался на выводе и после пробуждения, а программный перезапуск платы (им кончается обновление по радио, им же
// перезапускает меню и команда порта) настройку выводов НЕ сбрасывает. При следующем запуске pinMode видит на выводе
// «прежний вид прерывания» и сам его включает — обработчика нет, уровень при нажатии низкий всё время, прерывание
// идёт без конца, и сторож прерываний перезапускает плату. Владелец 07.10 (после обновления до 2.32 по радио):
// «кнопка на энкодере поломана, при нажатии вместо меню зависание и перезагрузка» — оба приёмника. Жило это с 2.19
// (первое обновление по радио): первое нажатие после «сон → обновление» вешало приёмник, сброс сторожем чистил
// выводы, и дальше кнопка работала — до следующего обновления.
// Зовётся при запуске (до pinMode) и после каждого пробуждения; перед сном «будильник» ставится заново.
static void rxKeyPinsClean() {
  for (int pin : { PIN_ENC_SW, PIN_KEY_CON, PIN_KEY_BAK }) {
    gpio_wakeup_disable((gpio_num_t)pin);
    gpio_intr_disable((gpio_num_t)pin);
    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
  }
}

static volatile uint32_t rxWakes;   // сколько раз вышли из ожидания (для отчёта)
static volatile uint32_t rxSleeps;  // сколько кругов сна прошло (для отчёта)
static void rxLinkTick();

// Один круг сна в ожидании. Радио выключается, процессор засыпает на полсекунды (его будит таймер или кнопка),
// потом радио включается и 70 мс слушает эфир на запомненном канале; каждый четвёртый круг, если там тихо и канал
// ищется сам, — пробегает остальные каналы (по 25 мс). Если передатчик слышен, приёмник раз в ~2 с сообщает ему
// о себе («сплю») и ещё 80 мс ждёт ответной команды: так передатчик может его разбудить.
// Возвращает true, если услышан свой передатчик (два проверенных пакета звука).
static bool rxSleepCycle() {
  static uint8_t round;
  if (!ledDark()) {   // светодиод ещё горит — задача экрана его сейчас погасит; во сне ничего светиться не должно
    delay(15);
    return false;
  }
  Serial.flush();
  for (int i = 0; i < 30 && rxTxBusy(); i++) delay(1);   // свой пакет ещё у драйвера — дать ему уйти
  esp_now_deinit();
  esp_wifi_stop();
  esp_sleep_enable_timer_wakeup(500000);
  if (cfg.board == BOARD_DEVKIT) {   // ручка и кнопки тоже будят (выводы подтянуты к питанию; нажатие — «земля»)
    gpio_wakeup_enable((gpio_num_t)PIN_ENC_SW, GPIO_INTR_LOW_LEVEL);   // остальные кнопки модуля не используются (владелец 07.10)
    esp_sleep_enable_gpio_wakeup();
  }
  esp_light_sleep_start();
  bool byKey = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
  if (cfg.board == BOARD_DEVKIT) rxKeyPinsClean();   // «будильник» нужен только во сне (см. rxKeyPinsClean)
  rxSleeps = rxSleeps + 1;
  esp_wifi_start();
  esp_wifi_set_ps(WIFI_PS_NONE);
  radioStartEspNow();
  uint32_t c0 = rAudioCount, t0 = millis();
  while (millis() - t0 < 70 && rAudioCount - c0 < 2) delay(5);
  if (rAudioCount - c0 < 2 && cfg.autoChannel && (++round & 3) == 0) {   // на своём канале тихо — заглянуть на остальные
    uint8_t home = cfg.channel;
    for (int ch = 1; ch <= 13 && rAudioCount - c0 < 2; ch++) {
      if (ch == home) continue;
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      cfg.channel = ch;
      uint32_t t1 = millis();
      while (millis() - t1 < 25 && rAudioCount - c0 < 2) delay(5);
    }
    if (rAudioCount - c0 < 2) {
      cfg.channel = home;
      esp_wifi_set_channel(home, WIFI_SECOND_CHAN_NONE);
    }
  }
  bool heard = rAudioCount - c0 >= 2;
  if (heard) {
    uint32_t sent = rxStatusSent;
    rxLinkTick();                 // сообщить о себе, если пора
    if (rxStatusSent != sent) {   // и подождать ответной команды («включись»)
      uint32_t t2 = millis();
      while (millis() - t2 < 80 && cfg.off) delay(5);
    }
  }
  if (!heard && secDev.ok && rLastBadMs && msSince(rLastBadMs) < 200) {
    // свой передатчик слышен, но подпись у него уже другая: пока приёмник спал, ключ набора сменили.
    // Попросить новый и подождать ответа, не выключая радио, — иначе спящего приёмника уже не разбудить.
    uint32_t asked = rxKeyAsked;
    rxPairTick();
    if (rxKeyAsked != asked) {
      uint32_t t3 = millis();
      while (millis() - t3 < 150 && !rxKeyMsgGot) delay(5);
      rxPairTick();
    }
  }
  if (byKey) delay(250);          // нажали кнопку — дать экрану её заметить
  return heard;
}

// Переходы между состояниями приёмника. Вызывать из основного цикла.
static void rxPowerTick() {
  static uint32_t sigSince;
  uint32_t now = millis(), in = now - rxPowerAt;
  bool heard = rLastRxMs && msSince(rLastRxMs, now) < 250;
  if (!heard) sigSince = 0;
  else if (!sigSince) sigSince = now ? now : 1;
  bool signal = sigSince && now - sigSince > 300;                   // идёт ровно, а не один случайный пакет
  uint32_t quiet = rLastRxMs ? (uint32_t)msSince(rLastRxMs, now) : now;   // сколько уже нет сигнала
  // Пока задача экрана не выяснила, есть ли экран, с места не трогаемся. Иначе при включении рядом с работающим
  // передатчиком приёмник успевал услышать его раньше, чем находился экран (экран поднимается ~0,9 с), считал, что
  // экрана нет, и проскакивал заставку и «ЗАПУСК» прямо в работу (владелец 07.10: «также при включении, когда
  // вещание идет, процесс запуска тот же» — то есть должен быть тем же, что и при пробуждении). Плате без экрана
  // это стоит долю секунды; дольше полутора секунд не ждём.
  if (!rxScreenProbed && cfg.board == BOARD_DEVKIT && now < 1500) return;
  // без экрана показывать нечего — заставку и сообщения не выдерживаем
  uint32_t splashMs = rxScreenFound ? RX_SPLASH_MS : 0, startMs = rxScreenFound ? RX_START_MS : 0, goingMs = rxScreenFound || rxPowerWhy == WHY_NOSIGNAL ? RX_GOING_MS : 300;   // «передатчик не найден» держим и без экрана: светодиод 4 с мигает красным
  if (rxQuietBoot) {   // плата перезапущена сама, чтобы её снова услышал передатчик (rxSeenTick): звук вернуть сразу
    splashMs = 0;
    startMs = 0;
    if (rxPower == PW_RUN || now > 20000) rxQuietBoot = false;
  }
  if (!secHave) {   // не подключён к набору: в ожидание не уходим — приёмник просит доступ, на экране код
    if (rxPower == PW_STANDBY) {
      rxAudioOn = true;
      rxPowerGo(PW_SEARCH, WHY_KEY);
    } else if (rxPower != PW_SEARCH) rxPowerGo(PW_SEARCH, WHY_KEY);
    else if (rxPowerWhy == WHY_KEY || in >= splashMs) rxPowerAt = now - (rxPowerWhy == WHY_KEY ? 0 : splashMs);
    return;
  }
  switch (rxPower) {
    case PW_SEARCH:   // включение (или нажатие ручки в ожидании): заставка, потом ждём передатчик, всего 10 с
      if (rxPowerWhy != WHY_KEY && in < splashMs) break;
      if (cfg.off) rxPowerGo(PW_GOING, WHY_OFF);
      else if (signal) rxPowerGo(PW_STARTING);
      else if (rxUiBusy) rxPowerAt = now - (rxPowerWhy == WHY_KEY ? 0 : splashMs);   // в меню — отсчёт стоит (без передатчика тоже надо уметь сменить набор и канал)
      else if (in > RX_WAIT_MS) rxPowerGo(PW_GOING, WHY_NOSIGNAL);
      break;
    case PW_STARTING:
      if (cfg.off) rxPowerGo(PW_GOING, WHY_OFF);
      else if (in >= startMs) rxPowerGo(PW_RUN);
      break;
    case PW_RUN:
      if (cfg.off) rxPowerGo(PW_GOING, WHY_OFF);
      else if (quiet > RX_WAIT_MS && !rxUiBusy) rxPowerGo(PW_GOING, WHY_NOSIGNAL);
      break;
    case PW_GOING:
      if (rxPowerWhy == WHY_OFF ? !cfg.off : (signal && !cfg.off)) rxPowerGo(PW_RUN);   // передумали: включили обратно / сигнал вернулся
      else if (in > goingMs) {
        rxAudioOn = false;
        rxPowerGo(PW_STANDBY, cfg.off ? WHY_OFF : WHY_NOSIGNAL);
      }
      break;
    case PW_STANDBY: {
      if (!rxAudioStopped) break;          // выход звука ещё опускается к «земле»
      if (cfg.off) rxPowerWhy = WHY_OFF;   // пока ждали сигнала, выключили с передатчика
      bool heard = signal;
      if (cfg.off || rxPowerWhy == WHY_NOSIGNAL) {
        if (!rxUiAwake) heard = rxSleepCycle();   // спим; на подсказке после нажатия ручки — бодрствуем
        if (cfg.off) break;                        // выключен с передатчика: будит только команда «включись»
        if (rxPowerWhy == WHY_NOSIGNAL && !heard) break;
      }
      // появился сигнал; или включили с передатчика; или нажали ручку (WHY_KEY ставит экран)
      uint8_t why = rxPowerWhy;
      rxAudioOn = true;
      rxWakes = rxWakes + 1;
      if (why == WHY_KEY && !heard) rxPowerGo(PW_SEARCH, WHY_KEY);   // передатчика нет — поищем ещё 10 с
      else rxPowerGo(PW_WAKING);
      break;
    }
    case PW_WAKING:
      if (cfg.off) rxPowerGo(PW_GOING, WHY_OFF);
      else if (in >= splashMs) rxPowerGo(PW_STARTING);
      break;
  }
}

static void rxLinkTick() {
  static uint32_t nextMs = 1500;
  uint32_t now = millis();
  {   // настройку сменили с передатчика — записать, но в паузе звука (см. rxQuietToSave)
    static uint32_t saveDue;
    if (rxSaveAsked) {
      rxSaveAsked = false;
      if (!saveDue) saveDue = now ? now : 1;
    }
    if (saveDue && rxQuietToSave(saveDue, 8000)) {
      saveDue = 0;
      settingsSave();
    }
  }
  {   // канал, найденный поиском, запомнить: в следующий раз приёмник начнёт с него и поймает передатчик сразу
    static uint8_t savedCh = 0;
    static uint32_t since = 0;
    if (!savedCh) savedCh = cfg.channel;
    if (cfg.channel == savedCh || !rxPlaying) since = now;
    else if (now - since > 5000) {   // пять секунд ровного приёма на новом канале
      savedCh = cfg.channel;
      settingsSave();
    }
  }
  if (secEpochDirty && rxPlaying) {   // передатчик включили заново — запомнить его новую эпоху (пакеты прежних уже не примем)
    static uint32_t since;
    if (!since) since = now ? now : 1;
    else if (now - since > 5000) {
      since = 0;
      secSaveEpoch();
    }
  }
  static uint32_t lastEcho = 0xFFFFFFFF;
  // Передатчика не слышно — молчим. Порог 400 мс (до 2.59 — секунда): через 600 мс тишины приёмник начинает обходить
  // каналы, и сведения, отправленные между 600-й и 1000-й миллисекундой, попадали под смену канала.
  if (now < nextMs || msSince(rLastRxMs, now) > 400 || !secHave) return;
  if (rxHopCh) return;                 // объявлен переход на другой канал — сообщим о себе уже там
  if (rHeardSeq == lastEcho) return;   // одноразовое число сведений — номер услышанного пакета: дважды одно не используем
  nextMs = now + 2000 + (esp_random() % 300);   // и в ожидании так же: иначе передатчик сочтёт приёмник пропавшим
  RxStatus2 st = {};   // длинного вида: передатчик по длине узнаёт, что этому приёмнику можно слать длинное имя
  st.magic = MAGIC_STATUS;
  st.ver = PROTO_VER;
  st.kit = cfg.kit;
  memcpy(st.id, rxId, 3);
  st.echoEpoch = rHeardEpoch;
  st.echoSeq = rHeardSeq;
  lastEcho = st.echoSeq;
  st.volume = cfg.volume;
  st.flags = (rxMute ? ST_MUTE : 0) | (cfg.off ? ST_OFF : 0) | (rxPlaying ? ST_PLAYING : 0) | (rxScreenFound ? ST_SCREEN : 0) |
             (cfg.rxStereo ? ST_STEREO : 0) | (rxPower == PW_STANDBY || rxPower == PW_GOING ? ST_SLEEP : 0) | (rxOutLive() ? ST_OUTLIVE : 0);
  st.rssi = rRssi;
  st.depthMs = (uint8_t)(rxTargetMs + 0.5f);
  uint32_t total = lkFrames + lkRecovered + lkLost;
  uint32_t pm = total ? (lkRecovered + lkLost) * 1000 / total : 0;
  st.lossPm = pm > 255 ? 255 : pm;
  st.lostFrames = lkLost > 255 ? 255 : lkLost;
  uint32_t um = lkUnder * 1000 / SRATE;
  st.underMs = um > 255 ? 255 : um;
  st.uptimeMin = now / 60000;
  st.fw = (uint8_t)lroundf(atof(FW_VERSION) * 10);
  utf8Copy(st.name, rxName(), NAME_LONG);
  lkFrames = lkRecovered = lkLost = lkUnder = 0;
  if (!secSeal(SEC_STATUS, st.echoEpoch, st.echoSeq, st.id, (const uint8_t *)&st, ST_CLEAR, &st.volume, ST2_BODY, st.tag)) return;
  if (rxSend((const uint8_t *)&st, sizeof(st)) != ESP_OK) return;   // драйвер не взял — сторож отправок разберётся (radio.h)
  rxStatusSent = rxStatusSent + 1;
  // Второй пакет — только когда первый ушёл (ждём ответа драйвера, обычно 2–4 мс, не дольше 25): до 2.59 оба шли
  // подряд, и второй всегда ложился драйверу поверх ещё не ушедшего первого.
  for (int i = 0; i < 25 && rxTxBusy(); i++) delay(1);
  // и вслед — настройки «для слуха и удобства»: по ним окно приёмника на передатчике показывает, что сейчас выбрано
  RxInfo in = {};
  in.magic = MAGIC_STATUS;
  in.ver = PROTO_VER;
  in.kit = cfg.kit;
  memcpy(in.id, rxId, 3);
  in.echoEpoch = st.echoEpoch;
  in.echoSeq = st.echoSeq;
  for (uint8_t p = 0; p < RXP_COUNT; p++) in.p[p] = rxParamWire(p);
  in.earTest = rxEarTest ? 1 : 0;
  fwParts(in.spare[0], in.spare[1]);
  if (rxOldSim == 2) {   // проверка: назваться прошивкой 2.56 (см. rxOldSim в radio.h)
    in.spare[0] = 2;
    in.spare[1] = 56;
  }
  in.spare[0] = (uint8_t)((in.spare[0] & 15) | ((cfg.rxBoost > 12 ? 0 : cfg.rxBoost) << 4));   // версия и усиление
  in.spare[2] = (uint8_t)(cfg.rxEq[0] | (cfg.rxEq[1] << 4));
  in.spare[3] = (uint8_t)(cfg.rxEq[2] | (cfg.rxEq[3] << 4));
  in.spare[4] = (uint8_t)(cfg.rxEq[4] | (cfg.rxLowCut ? 0x10 : 0) | ((cfg.rxLock & 3) << 5) | 0x80);   // старший бит — «эквалайзер есть»
  if (secSeal(SEC_RXINFO, in.echoEpoch, in.echoSeq, in.id, (const uint8_t *)&in, ST_CLEAR, in.p, INFO_BODY, in.tag)) rxSend((const uint8_t *)&in, sizeof(in));
  for (int i = 0; i < 25 && rxTxBusy(); i++) delay(1);   // и его дождаться: следом может идти смена канала или сон
}
