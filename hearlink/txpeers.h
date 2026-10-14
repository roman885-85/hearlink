// Передатчик: список приёмников.
//
// Каждый приёмник раз в две секунды сообщает о себе; здесь это складывается в таблицу. Таблица хранится
// в плате, поэтому приёмник, который сейчас выключен, остаётся в списке со своим именем («не на зв'язку»).
// Команды приёмнику (показать себя, имя, включить/выключить, громкость) уходят в эфир между пакетами звука,
// по четыре раза подряд — на случай потери.
#pragma once
#include <Preferences.h>
#include "txaudio.h"

#define PEERS_MAX 16
struct Peer {
  uint8_t id[3];
  bool used;
  bool wide;                  // сообщает о себе сообщением длинного вида (прошивка 2.19+): ему можно слать длинное имя
  char name[NAME_LONG];
  uint8_t volume, flags, depthMs, lossPm, lostFrames, underMs, fw;
  int8_t rssi, rssiHere;      // как он слышит передатчик и как передатчик слышит его
  uint16_t uptimeMin;
  uint32_t seenMs;            // когда последний раз сообщал о себе (0 — в этом включении ни разу)
  bool wantOff;               // каким его держит передатчик: это состояние главное, приёмник под него подводится (с 2.64)
  uint8_t dev[32];            // личный ключ приёмника (шифр 16 + подпись 16) — им закрыт новый ключ набора при смене
  bool hasDev;
  volatile uint8_t keyPush;   // сколько раз ещё послать ему новый ключ набора
  uint32_t keyPushMs;         // когда он последний раз просил ключ
  // настройки «для слуха и удобства» (приёмники с версии 2.32 сообщают их сообщением RxInfo)
  bool hasInfo;               // сообщал — значит, понимает и команды CMD_SET / CMD_EARTEST
  uint8_t par[RXP_COUNT];     // значения в том виде, как идут по радио (баланс — со сдвигом на 5)
  uint8_t earTest;            // у него идёт проверка наушников
  uint8_t fwMaj, fwMin;       // точная версия прошивки (приёмники с 2.42; у прежних — нули)
  uint8_t boost;              // усиление приёмника (с 2.48): 0…12 = 0…+24 дБ
  uint8_t lock;               // блокировка ручки приёмника (с 2.46): 0 — нет, 1 — меню, 2 — громкость, 3 — всё
  uint8_t fx[7];              // обработка «для слуха» (с 2.68): шумозаглушение, сжатие, «в паузах» — по 0…3; с 2.72 —
                              // чувствительность 0…4, порог паузы 0…4, закрытие паузы 0…2, образец шума (0 сам, 1 запомнен)
  bool hasFx;                 // приёмник сообщил их (RxInfo2) — значит, понимает CMD_SET2
  uint8_t eq[5], lowCut;      // эквалайзер приёмника (с 2.45); hasEq — сообщает ли
  bool hasEq;
  uint8_t autoTries;          // сколько раз в это включение передатчик сам пытался его обновить
  uint32_t autoNeedMs;        // с какого времени он на связи, не спит и с прошивкой старее, чем у передатчика
  uint32_t parSetMs;          // когда настройку сменили с передатчика: 2,5 с после этого сообщения приёмника её не затирают
  // счёт сообщений о себе (с 2.64; печатает P, в журнал на карте идёт «ст=»)
  uint32_t stGot, stMiss;     // получено / не дошло (по промежуткам между полученными: приёмник шлёт их раз в 2,0–2,3 с)
  uint32_t stPend;            // из полученных — пришло, пока собственный пакет передатчика ждал у драйвера
  uint16_t stGot10, stMiss10; // получено и не дошло за текущие 10 с журнала
  uint32_t stPh[11];          // через сколько после своего последнего пакета пришло сообщение: по 0,5 мс; последняя — 5 мс и дольше
  // с 2.73: по этому «сама» выбирает скорость радио (rateAutoTick) и считается, сколько звука приёмник потерял
  float rssiSm;               // как он слышит передатчик, сглажено по нескольким сообщениям (одно — уровень одного пакета)
  uint8_t badRun;             // сколько сообщений подряд были с пропавшими кадрами или потерями от 10 %
  uint32_t aN, aLossSum;      // с команды P0: сообщений и сумма их потерь (в десятых долях процента)
  uint32_t aLost, aUnder;     // миллисекунд пропавшего звука (кадры, которых не оказалось и в запасных копиях) и провалов
  uint32_t aBad, aCap;        // сообщений с пропавшими кадрами; сообщений, где счёт упёрся в предел (на деле было больше)
};
static Peer peers[PEERS_MAX];
static portMUX_TYPE peerMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool peersDirty;       // имена изменились — сохранить
static Preferences peerPrefs;

// очередь команд: шлёт задача передачи звука
struct CmdSlot {
  TxCommand2 c;      // хранится в длинном виде; в эфир идёт в прежнем, кроме смены имени приёмнику 2.19+ (wide)
  uint8_t left;      // сколько раз ещё послать
  bool wide;
};
static CmdSlot cmdQ[4];       // команды лежат открытыми; шифруются при отправке, в задаче передачи
static uint32_t cmdCtr;
static volatile uint32_t txSeenSent;   // сколько объявлений «кого слышу» ушло (для отчёта)

// ---- подключение приёмников (см. sec.h). Просьбы слушаем, только пока открыто окно добавления.
#define PAIR_ASKS 3
struct PairAsk {
  bool used;
  uint8_t id[3];
  uint8_t pub[PAIR_PUB];
  char name[NAME_LEN];
  uint16_t code;       // четыре цифры по открытому ключу — те же, что на экране приёмника
  uint32_t seenMs;
};
static PairAsk pairAsk[PAIR_ASKS];
static volatile uint32_t pairOpenUntil;     // до какого времени открыто окно добавления (0 — закрыто)
static volatile int8_t pairApproveAsk = -1; // какую просьбу одобрили (выполнит главный цикл: счёт долгий)
static struct {
  uint8_t data[sizeof(PairRsp)];
  volatile uint8_t left;
} pairOut;                                  // ответ приёмнику: шлёт задача передачи
static volatile uint32_t pairDone;          // сколько приёмников подключено с запуска (для отчёта)
// кому только что дозволили: его просьбы, ещё летящие в эфире, в список не возвращаем (иначе строка появляется снова,
// и оператор жмёт «Дозволити» второй раз — так вышло у владельца при первой же пробе, 07.10)
static uint8_t pairLastId[3];
static volatile uint32_t pairLastMs;
// удаление приёмника из набора: сначала ему уходит команда «стереть ключи» (ещё под прежним ключом), через треть
// секунды — смена ключа набора и рассылка нового оставшимся
static volatile int8_t removeIdx = -1;
static uint32_t removeAt;
static uint8_t removedId[3];
static volatile uint32_t removedMs;

static int peerFind(const uint8_t *id) {
  for (int i = 0; i < PEERS_MAX; i++)
    if (peers[i].used && !memcmp(peers[i].id, id, 3)) return i;
  return -1;
}

static bool peerOnline(const Peer &p) {
  return p.seenMs && msSince(p.seenMs) < 7000;
}

static void peerCommand(const uint8_t *id, uint8_t cmd, uint8_t arg, const char *name = nullptr) {
  TxCommand2 c = {};
  c.magic = MAGIC_COMMAND;
  c.ver = PROTO_VER;
  c.kit = cfg.kit;
  memcpy(c.id, id, 3);
  c.cmd = cmd;
  c.arg = arg;
  c.epoch = secEpoch;
  portENTER_CRITICAL(&peerMux);
  int pi = peerFind(id);
  bool wide = cmd == CMD_NAME && pi >= 0 && peers[pi].wide;   // длинное имя — только приёмнику, который его поймёт
  portEXIT_CRITICAL(&peerMux);
  if (name) utf8Copy(c.name, name, wide ? NAME_LONG : NAME_LEN);
  portENTER_CRITICAL(&peerMux);
  c.ctr = ++cmdCtr;
  for (auto &q : cmdQ)
    if (!q.left) {
      q.c = c;
      q.wide = wide;
      q.left = 4;
      break;
    }
  portEXIT_CRITICAL(&peerMux);
}

// Объявление «ухожу на канал ch с пакета atSeq» — всем приёмникам набора (номер FF FF FF). Зовёт задача передачи.
static void hopAnnounce(uint8_t ch, uint32_t atSeq) {
  static TxCommand c;
  static uint32_t lastAt;
  if (atSeq != lastAt || !c.ctr) {   // новое объявление — новый номер команды; повторы идут с тем же
    memset(&c, 0, sizeof(c));
    c.magic = MAGIC_COMMAND;
    c.ver = PROTO_VER;
    c.kit = cfg.kit;
    c.id[0] = c.id[1] = c.id[2] = 0xFF;
    c.epoch = secEpoch;
    portENTER_CRITICAL(&peerMux);
    c.ctr = ++cmdCtr;
    portEXIT_CRITICAL(&peerMux);
    lastAt = atSeq;
  }
  TxCommand s2 = c;
  s2.cmd = CMD_HOP;
  s2.arg = ch;
  memcpy(s2.name, &atSeq, 4);
  secSeal(SEC_COMMAND, s2.epoch, s2.ctr, s2.id, (const uint8_t *)&s2, CMD_CLEAR, &s2.cmd, CMD_BODY, s2.tag);
  radioSend((const uint8_t *)&s2, sizeof(TxCommand));
}

// вызывается задачей передачи после каждого пакета звука: не чаще одной команды на 10 мс
static void peersPump() {
  static uint8_t skip;
  if (++skip < 5) return;
  skip = 0;
  {   // «кого слышу» — раз в две секунды (proto.h, TxSeen). Не во время объявленного перехода на другой канал:
      // в эти 0,2 с эфир нужен объявлению о переходе.
    static uint32_t seenAt, seenCtr;
    uint32_t now = millis();
    if (now - seenAt >= 2000 && !txHopCh && secHave) {
      seenAt = now;
      TxSeen s = {};
      s.magic = MAGIC_SEEN;
      s.ver = PROTO_VER;
      s.kit = cfg.kit;
      s.epoch = secEpoch;
      s.ctr = ++seenCtr;
      uint8_t n = 0;
      bool full = true;
      portENTER_CRITICAL(&peerMux);
      for (auto &p : peers) {
        if (!p.used) {
          full = false;
          continue;
        }
        if (peerOnline(p) && n < SEEN_MAX) {
          memcpy(s.ids + n * 3, p.id, 3);
          n++;
        }
      }
      portEXIT_CRITICAL(&peerMux);
      s.n = (uint8_t)(n | (full ? 0x80 : 0));
      if (secSeal(SEC_SEEN, s.epoch, s.ctr, NULL, (const uint8_t *)&s, SEEN_CLEAR, &s.n, SEEN_BODY, s.tag)) {
        radioSend((const uint8_t *)&s, sizeof(s));
        txSeenSent = txSeenSent + 1;
        return;
      }
    }
  }
  for (auto &q : cmdQ)
    if (q.left) {
      TxCommand2 s = q.c;   // копия: повтор той же команды шифруется так же
      size_t bl = q.wide ? CMD2_BODY : CMD_BODY;   // прежний вид — это начало длинного, только имя короче
      uint8_t *b = (uint8_t *)&s;
      secSeal(SEC_COMMAND, s.epoch, s.ctr, s.id, b, CMD_CLEAR, b + CMD_CLEAR, bl, b + CMD_CLEAR + bl);
      radioSend(b, CMD_CLEAR + bl + SEC_TAG);
      q.left--;
      return;
    }
  if (pairOut.left) {
    radioSend(pairOut.data, sizeof(PairRsp));
    pairOut.left = pairOut.left - 1;
    return;
  }
  static uint8_t turn;   // новый ключ набора — оставшимся приёмникам по очереди, каждому под его личным ключом
  for (int n = 0; n < PEERS_MAX; n++) {
    Peer &p = peers[(turn + n) % PEERS_MAX];
    if (!p.used || !p.hasDev || !p.keyPush || !secHave) continue;
    turn = (turn + n + 1) % PEERS_MAX;
    p.keyPush = p.keyPush - 1;
    KeyMsg m = {};
    m.magic = MAGIC_KEYMSG;
    m.ver = PROTO_VER;
    memcpy(m.id, p.id, 3);
    m.gen = secGen;
    memcpy(m.box, secKey, SEC_KEY);
    m.box[SEC_KEY] = cfg.kit;
    uint32_t ep = secEpoch;
    memcpy(m.box + SEC_KEY + 1, &ep, 4);
    SecKeys k;
    memcpy(k.enc, p.dev, 16);
    memcpy(k.mac, p.dev + 16, 16);
    k.ok = true;
    uint8_t nn[12];
    secNonce(nn, SEC_KEYMSG, m.gen, 0, m.id);
    if (secSealWith(k, nn, (const uint8_t *)&m, KEYMSG_CLEAR, m.box, sizeof(m.box), m.tag)) radioSend((const uint8_t *)&m, sizeof(m));
    memset(&k, 0, sizeof(k));
    return;
  }
}

// из задачи Wi-Fi: приёмник набора слышит передатчик, но ключ у него старый (был выключен, когда ключ меняли)
static void peerOnKeyReq(const KeyReq *q) {
  int i = peerFind(q->id);
  if (i < 0 || !peers[i].hasDev || q->gen >= secGen) return;   // не из набора (или удалён) — ответа нет
  Peer &p = peers[i];
  uint32_t now = millis();
  if (p.keyPushMs && msSince(p.keyPushMs, now) < 800) return;
  SecKeys k;
  memcpy(k.enc, p.dev, 16);
  memcpy(k.mac, p.dev + 16, 16);
  k.ok = true;
  uint8_t nn[12], t[SEC_TAG], d = 0;
  secNonce(nn, SEC_KEYREQ, q->gen, q->rnd, q->id);
  secHwOn();
  secHwTag(k.mac, nn, (const uint8_t *)q, KEYREQ_CLEAR, NULL, 0, t);
  secHwOff();
  memset(&k, 0, sizeof(k));
  for (int j = 0; j < SEC_TAG; j++) d |= t[j] ^ q->tag[j];
  if (d) {
    secBadTag = secBadTag + 1;
    return;
  }
  p.keyPushMs = now ? now : 1;
  p.keyPush = 3;
}

// из задачи Wi-Fi: приёмник без ключа просит доступ
static void pairOnReq(const PairReq *r) {
  uint32_t now = millis();
  if (!pairOpenUntil || (int32_t)(now - pairOpenUntil) > 0) return;   // окно добавления закрыто — просьбы не слушаем
  if (pairLastMs && msSince(pairLastMs, now) < 20000 && !memcmp(pairLastId, r->id, 3)) {
    // Ему уже дозволено, а он всё просит: ответ не дошёл (приёмник без ключа обходит каналы и мог в тот миг слушать
    // другой — так второй приёмник 07.10 остался без ключа). Он только что подал голос — значит, слушает нас сейчас:
    // повторить тот же ответ.
    if (!pairOut.left) pairOut.left = 4;
    return;
  }
  int slot = -1;
  for (int i = 0; i < PAIR_ASKS; i++)
    if (pairAsk[i].used && !memcmp(pairAsk[i].id, r->id, 3)) slot = i;
  if (slot < 0)
    for (int i = 0; i < PAIR_ASKS; i++)
      if (!pairAsk[i].used || msSince(pairAsk[i].seenMs, now) > 6000) {   // свободное место или просьба, которую давно не повторяли
        slot = i;
        break;
      }
  if (slot < 0) return;
  PairAsk &a = pairAsk[slot];
  if (!a.used || memcmp(a.pub, r->pub, PAIR_PUB) || memcmp(a.id, r->id, 3)) {
    memcpy(a.id, r->id, 3);
    memcpy(a.pub, r->pub, PAIR_PUB);
    a.code = secPairCode(a.pub);
  }
  memcpy(a.name, r->name, NAME_LEN);
  a.name[NAME_LEN - 1] = 0;
  a.seenMs = now ? now : 1;
  a.used = true;
}

static void pairOpen(bool on) {
  if (on) {
    if (!pairOpenUntil) memset(pairAsk, 0, sizeof(pairAsk));
    pairOpenUntil = millis() + 180000;   // три минуты
  } else {
    pairOpenUntil = 0;
    memset(pairAsk, 0, sizeof(pairAsk));
  }
}

// главный цикл: закрыть окно по времени, выполнить одобрение
static void pairTick() {
  uint32_t now = millis();
  if (pairOpenUntil && (int32_t)(now - pairOpenUntil) > 0) pairOpen(false);
  for (auto &a : pairAsk)
    if (a.used && msSince(a.seenMs, now) > 8000) a.used = false;   // приёмник замолчал (выключили или уже подключён)
  if (removeIdx >= 0 && (int32_t)(now - removeAt) >= 0) {   // команда «стереть ключи» ушла — теперь сменить ключ набора
    int ri = removeIdx;
    removeIdx = -1;
    portENTER_CRITICAL(&peerMux);
    memcpy(removedId, peers[ri].id, 3);
    char nm[NAME_LONG];
    memcpy(nm, peers[ri].name, NAME_LONG);
    peers[ri].used = false;
    portEXIT_CRITICAL(&peerMux);
    removedMs = now ? now : 1;
    secNewKey();   // новый ключ набора, поколение + 1
    int left = 0, noDev = 0;
    for (auto &p : peers)
      if (p.used) {
        if (p.hasDev) {
          p.keyPush = 6;
          left++;
        } else noDev++;
      }
    peersDirty = true;
    Serial.printf("приймач %02X%02X%02X «%s» видалено з набору; новий ключ набору (покоління %u) надсилаю іншим приймачам: %d%s\n", removedId[0], removedId[1],
                  removedId[2], nm, (unsigned)secGen, left, noDev ? " (є приймачі без особистого ключа — їх треба додати заново)" : "");
  }
  int k = pairApproveAsk;
  if (k < 0) return;
  pairApproveAsk = -1;
  if (k >= PAIR_ASKS || !pairAsk[k].used || !secHave) return;
  PairAsk a = pairAsk[k];
  static SecPair kp;   // не на стеке: внутри большие числа
  static SecKeys wrap;
  PairRsp rsp = {};
  rsp.magic = MAGIC_PAIRRSP;
  rsp.ver = PROTO_VER;
  memcpy(rsp.id, a.id, 3);
  static SecKeys dev;
  bool ok = secPairGen(kp) && secPairKey(kp, a.pub, a.pub, kp.pub, wrap, dev);
  if (ok) {
    memcpy(rsp.pub, kp.pub, PAIR_PUB);
    memcpy(rsp.box, secKey, SEC_KEY);
    rsp.box[SEC_KEY] = cfg.kit;
    uint32_t ep = secEpoch, gn = secGen;
    memcpy(rsp.box + SEC_KEY + 1, &ep, 4);
    memcpy(rsp.box + SEC_KEY + 5, &gn, 4);
    ok = secBox(true, wrap, rsp.box, sizeof(rsp.box), rsp.tag);
  }
  if (ok) {   // приёмник — в список набора вместе с личным ключом (сведения о себе он пришлёт сам через пару секунд)
    portENTER_CRITICAL(&peerMux);
    int i = peerFind(a.id);
    if (i < 0)
      for (int n = 0; n < PEERS_MAX; n++)
        if (!peers[n].used) {
          i = n;
          memset(&peers[n], 0, sizeof(Peer));
          peers[n].used = true;
          memcpy(peers[n].id, a.id, 3);
          memcpy(peers[n].name, a.name, NAME_LEN);
          break;
        }
    if (i >= 0) {
      memcpy(peers[i].dev, dev.enc, 16);
      memcpy(peers[i].dev + 16, dev.mac, 16);
      peers[i].hasDev = true;
    } else ok = false;   // список полон
    portEXIT_CRITICAL(&peerMux);
    peersDirty = true;
    removedMs = 0;
  }
  memset(&dev, 0, sizeof(dev));
  secPairFree(kp);
  memset(&wrap, 0, sizeof(wrap));
  if (!ok) {
    Serial.println("підключення приймача: обмін ключами не вдався");
    return;
  }
  memcpy(pairOut.data, &rsp, sizeof(rsp));
  pairOut.left = 10;       // десять раз подряд, раз в 10 мс
  memcpy(pairLastId, a.id, 3);
  pairLastMs = now ? now : 1;
  pairAsk[k].used = false;
  pairDone = pairDone + 1;
  Serial.printf("приймачу %02X%02X%02X «%s» дозволено доступ до набору\n", a.id[0], a.id[1], a.id[2], a.name);
}

static void pairPrint() {
  uint32_t now = millis();
  if (!pairOpenUntil) Serial.println("вікно додавання приймачів закрите (J1 — відкрити)");
  else Serial.printf("вікно додавання відкрите ще %u с\n", (unsigned)((pairOpenUntil - now) / 1000));
  for (int i = 0; i < PAIR_ASKS; i++)
    if (pairAsk[i].used)
      Serial.printf("  запит %d: приймач %02X%02X%02X «%s», код %04u (j%d — дозволити)\n", i + 1, pairAsk[i].id[0], pairAsk[i].id[1], pairAsk[i].id[2],
                    pairAsk[i].name, pairAsk[i].code, i + 1);
}

// из задачи Wi-Fi: пришли сведения приёмника
static void peerOnStatus(const uint8_t *data, int len, int8_t rssiHere) {
  if (len == (int)sizeof(RxInfo2)) {   // настройки обработки «для слуха» (2.68+): подпись и свежесть — как у остальных сообщений
    const RxInfo2 *ri = (const RxInfo2 *)data;
    uint8_t body[INFO2_BODY];
    if (!secOpen(SEC_RXINFO2, ri->echoEpoch, ri->echoSeq, ri->id, data, ST_CLEAR, data + ST_CLEAR, INFO2_BODY, data + ST_CLEAR + INFO2_BODY, body)) {
      secBadTag = secBadTag + 1;
      return;
    }
    if (ri->echoEpoch != secEpoch || (uint32_t)(txSeqNow - ri->echoSeq) > 6000) {
      secReplay = secReplay + 1;
      return;
    }
    portENTER_CRITICAL(&peerMux);
    int k = peerFind(ri->id);
    if (k >= 0) {
      if (!peers[k].parSetMs || msSince(peers[k].parSetMs) > 2500)
        for (int j = 0; j < 7; j++) peers[k].fx[j] = body[j] > (j < 3 ? 3 : 4) ? 0 : body[j];   // fx[3] и spare[0…3] идут подряд
      peers[k].hasFx = true;
    }
    portEXIT_CRITICAL(&peerMux);
    return;
  }
  if (len == (int)sizeof(RxInfo)) {   // настройки приёмника (2.32+): подпись и свежесть проверяются так же, как у сведений
    const RxInfo *ri = (const RxInfo *)data;
    uint8_t body[INFO_BODY];
    if (!secOpen(SEC_RXINFO, ri->echoEpoch, ri->echoSeq, ri->id, data, ST_CLEAR, data + ST_CLEAR, INFO_BODY, data + ST_CLEAR + INFO_BODY, body)) {
      secBadTag = secBadTag + 1;
      return;
    }
    if (ri->echoEpoch != secEpoch || (uint32_t)(txSeqNow - ri->echoSeq) > 6000) {
      secReplay = secReplay + 1;
      return;
    }
    portENTER_CRITICAL(&peerMux);
    int k = peerFind(ri->id);
    if (k >= 0) {
      // Только что сменили настройку отсюда — приёмник ещё может прислать прежнее значение (сообщение ушло раньше
      // команды): две с половиной секунды верим себе, иначе число на экране дёрнулось бы назад и снова вперёд.
      if (!peers[k].parSetMs || msSince(peers[k].parSetMs) > 2500) {
        memcpy(peers[k].par, body, RXP_COUNT);
        peers[k].earTest = body[RXP_COUNT];
        peers[k].parSetMs = 0;
      }
      uint8_t fj = body[RXP_COUNT + 1] & 15, fn = body[RXP_COUNT + 2];   // в старших битах — усиление (с 2.48)
      if (fj != peers[k].fwMaj || fn != peers[k].fwMin) peersDirty = true;    // версия запоминается (см. peersSave)
      peers[k].fwMaj = fj;
      peers[k].fwMin = fn;
      peers[k].hasEq = body[RXP_COUNT + 5] & 0x80;
      if (!peers[k].parSetMs || msSince(peers[k].parSetMs) > 2500) {
        const uint8_t *e = body + RXP_COUNT + 3;
        peers[k].eq[0] = e[0] & 15; peers[k].eq[1] = e[0] >> 4; peers[k].eq[2] = e[1] & 15; peers[k].eq[3] = e[1] >> 4;
        peers[k].eq[4] = e[2] & 15; peers[k].lowCut = (e[2] >> 4) & 1;
        peers[k].lock = (e[2] >> 5) & 3;
        peers[k].boost = body[RXP_COUNT + 1] >> 4;
      }
      peers[k].hasInfo = true;
    }
    portEXIT_CRITICAL(&peerMux);
    return;
  }
  static RxStatus2 plain;
  RxStatus2 *st = &plain;
  const RxStatus *raw = (const RxStatus *)data;   // начало у обоих видов одинаковое
  bool wide = len == (int)sizeof(RxStatus2);
  size_t bl = wide ? ST2_BODY : ST_BODY;
  memset(st, 0, sizeof(*st));
  memcpy(st, raw, ST_CLEAR);
  // подпись: сведения шлёт только приёмник с ключом набора; свежесть: в них номер недавно услышанного пакета звука
  if (!secOpen(SEC_STATUS, raw->echoEpoch, raw->echoSeq, raw->id, data, ST_CLEAR, data + ST_CLEAR, bl, data + ST_CLEAR + bl, &st->volume)) {
    secBadTag = secBadTag + 1;
    return;
  }
  if (secShadow & 2) secShadowRun((const uint8_t *)raw, 48);
  if (raw->echoEpoch != secEpoch || (uint32_t)(txSeqNow - raw->echoSeq) > 6000) {
    secReplay = secReplay + 1;
    return;
  }
  if (removedMs && msSince(removedMs) < 5000 && !memcmp(removedId, st->id, 3)) return;   // только что удалён — в список не возвращать
  portENTER_CRITICAL(&peerMux);
  int i = peerFind(st->id);
  if (i < 0)
    for (int k = 0; k < PEERS_MAX; k++)
      if (!peers[k].used) {
        i = k;
        memset(&peers[k], 0, sizeof(Peer));
        peers[k].used = true;
        memcpy(peers[k].id, st->id, 3);
        peersDirty = true;
        break;
      }
  if (i >= 0) {
    Peer &p = peers[i];
    st->name[(wide ? NAME_LONG : NAME_LEN) - 1] = 0;
    utf8Clean(st->name);   // до 2.19 клавиатура могла оставить в имени половину буквы
    p.wide = wide;
    if (strcmp(p.name, st->name)) {
      utf8Copy(p.name, st->name, NAME_LONG);
      peersDirty = true;
    }
    p.volume = st->volume;
    p.flags = st->flags;
    p.depthMs = st->depthMs;
    p.lossPm = st->lossPm;
    p.lostFrames = st->lostFrames;
    p.underMs = st->underMs;
    p.fw = st->fw;
    p.rssi = st->rssi;
    p.rssiHere = rssiHere;
    p.uptimeMin = st->uptimeMin;
    {   // для выбора скорости и для счёта потерь звука (с 2.73)
      bool cont = p.seenMs && msSince(p.seenMs) < 15000 && p.rssiSm < 0;
      p.rssiSm = cont ? p.rssiSm + (st->rssi - p.rssiSm) * 0.35f : (float)st->rssi;
      bool bad = st->lostFrames > 0 || st->lossPm >= 100;
      p.badRun = !cont ? (bad ? 1 : 0) : bad ? (p.badRun < 250 ? p.badRun + 1 : 250) : 0;
      p.aN++;
      p.aLossSum += st->lossPm;
      p.aLost += st->lostFrames * (qFrameUs(txQNow < Q_COUNT ? (int)txQNow : Q_HI) / 1000);
      p.aUnder += st->underMs;
      if (st->flags & ST_PLAYING) txLostMsAll = txLostMsAll + st->lostFrames * (qFrameUs(txQNow < Q_COUNT ? (int)txQNow : Q_HI) / 1000) + st->underMs;
      if (st->lostFrames) p.aBad++;
      if (st->lossPm == 255 || st->lostFrames == 255 || st->underMs == 255) p.aCap++;
    }
    {   // счёт: сколько сообщений дошло и сколько, судя по промежутку, пропало; и когда они приходят (см. radio.h, «замер»)
      uint32_t gap = p.seenMs ? millis() - p.seenMs : 0;
      if (gap && gap < 60000) {
        uint32_t n = (gap + 1075) / 2150;
        if (n > 1) {
          p.stMiss += n - 1;
          p.stMiss10 += n - 1;
        }
      }
      p.stGot++;
      p.stGot10++;
      uint32_t ph = ((uint32_t)esp_timer_get_time() - rDoneUs) / 500;
      p.stPh[ph > 10 ? 10 : ph]++;
      if ((int32_t)(rPut - rGot) > 0) {
        p.stPend++;
        rStMarkSlot = rGot;
        rStMarkPeer = (int8_t)i;
      }
    }
    p.seenMs = millis() ? millis() : 1;
  }
  bool resend = false, off = false;
  uint8_t id[3];
  if (i >= 0) {
    Peer &p = peers[i];
    // Включён приёмник или выключен — решает передатчик (владелец 10.10: «удаленное управление с передатчика является
    // приоритетным»). Приёмник сообщил о себе не в том состоянии — команда повторяется, пока он не подчинится: так
    // доходит и до спящего (он слушает эфир урывками), и до приёмника с прошивкой до 2.64, который включили его же
    // ручкой. До 2.64 команду повторяли 15 с, а потом передатчик принимал то состояние, что сообщил приёмник.
    bool isOff = p.flags & ST_OFF;
    if (isOff != p.wantOff) {
      resend = true;
      off = p.wantOff;
      memcpy(id, p.id, 3);
    }
  }
  portEXIT_CRITICAL(&peerMux);
  if (resend) peerCommand(id, CMD_ENABLE, off ? 0 : 1);
}

// Скорость радио «сама» (переписано в 2.73 после замера 12.10). До этого — лестница 5,5 → 2 → 1 → 0,5 Мбит/с, по
// которой при качестве «найвища» идти было некуда: ниже 5,5 она спускалась, только если туда помещалось ВЫБРАННОЕ
// качество. Владелец 12.10: «звук с приемников пропадает, а если на среднем уровне сигнала, вообще отключается»;
// «главный приоритет — стабильная связь и беспрерывный звук даже при слабом сигнале».
// Замер 12.10 (tools/ratescan.py; дальний приёмник, уровень меняли мощностью передатчика, по 36 с на режим; второе
// число — сколько звука пропало за эти 36 с: кадры, которых не оказалось и в запасных копиях):
//   сигнал     5,5 «найвища»       2 «стандартна»     1 «мова»             0,5 «дальня»
//   −77 дБм    2,5 %, 0 мс         3,6 %, 28 мс       2,7 %, 0 мс          1,2 %, 0 мс
//   −82…−83    >25 %, звука нет    17 %, 196 мс       9 %, 8 мс            7,5 %, 120 мс
//   −86…−89    —                   >25 %, звука нет   16 %, 208 мс (−86)   24 %, 2 с (−87)
//                                                     >25 %, нет (−88)
// Обрыв у 5,5 Мбит/с — между −77 и −80 дБм, у 1 Мбит/с — около −87: девять децибел запаса. 2 Мбит/с даёт из них
// три и теряет больше (пакет длинный, а кадр короткий — эфир занят на треть), 0,5 — не лучше 1. Поэтому ступеней
// две: 5,5 Мбит/с с выбранным качеством и 1 Мбит/с, где качество само опускается до «мова» (qForRate, radio.h).
// На 11 и выше «сама» не поднимается с 06.10: «выше 5,5 — потери даже с приёмником рядом» (замер тогда же).
// Вниз — быстро: самый слабый из играющих приёмников слышит передатчик на −75 дБм и хуже (сглаженно) либо два его
// сообщения подряд — с пропавшим звуком при сигнале слабее −68 (у сильного приёмника потери — это занятый эфир,
// скорость тут не поможет; не считаются и секунды, когда передатчик сам не мог выйти в эфир). Вверх — не спеша:
// две минуты подряд все слышат лучше −70 дБм и без пропаж. Смена ступени — это перестройка всех приёмников (у
// каждого пауза в звуке на 40–70 мс), поэтому частить нельзя: вернулись наверх, а в ближайшие две минуты пришлось
// спуститься снова — следующая попытка вдвое позже (от двух минут до двадцати); десять минут наверху — срок прежний.
// Приёмник пропал со связи слабым и не сказав, что засыпает, — три минуты держим нижнюю ступень: на 5,5 он мог
// просто перестать слышать передатчик. Выключенные с передатчика в счёт не идут: они не играют.
// С 2.78 ступени работают и при скорости, выбранной вручную: выбранная — ВЕРХНЯЯ ступень, вниз (до 1 Мбит/с) —
// только ради слабого приёмника и с сообщением на экране. Владелец 12.10 держал постоянную 5,5 Мбит/с и получал
// ровно то, от чего ступени придуманы: «при уровне сигнала 75-80 приемник теряет сеть полностью» (журнал: на −72…−74
// потери 10–23 %, на −77…−84 звука нет), — а главным назвал «стабильную связь и безпрерывный звук даже при слабом
// сигнале». «Сама» отличается лишь тем, что верхняя ступень у неё всегда 5,5. Выбраны 1 или 0,5 Мбит/с — опускаться
// некуда. Для замеров на строго заданной скорости нижняя ступень запрещается с порта: Ql0 (не запоминается).
static volatile uint8_t rateWhy;      // для «?»: 0 — приёмников нет, 1 — все слышат хорошо, 2 — слабый сигнал, 3 — у слабого
                                      // пропадает звук, 4 — слабый пропал со связи, 5 — сигнал уже хороший, ждём срока,
                                      // 6 — нижней ступени нет (выбрана 1 или 0,5 Мбит/с) либо она запрещена (Ql0)
static volatile int8_t rateWhyRssi;   // сигнал самого слабого играющего приёмника
static uint8_t rateWhyId[3];
static volatile uint32_t rateSteps;   // сколько раз скорость менялась сама в это включение
static volatile bool rateLadderOff;   // проверка (порт: Ql0): скорость стоит на выбранной, вниз не идёт
static volatile uint8_t rateKick;     // оператор только что выбрал скорость или качество (или сменил «Слабкий сигнал»): если стоим
                                      // на нижней ступени — подняться сразу, когда приёмники это позволяют, иначе объяснить на экране
// сообщение на экран о смене ступени (показывает ui_tx.h, когда номер сменился): 1 — опустились, 2 — вернулись,
// 3 — оператор выбрал скорость или качество, а подняться нельзя: почему (rateNoteWhy — как rateWhy: 2, 3 или 4)
static volatile uint32_t rateNoteSeq;
static volatile uint8_t rateNoteKind, rateNoteWhy;
static volatile int8_t rateNoteRssi;
static char rateNoteName[NAME_LONG];
static inline uint8_t rateTopNow() {   // верхняя ступень: у «самой» — 5,5 Мбит/с, иначе выбранная вручную
  return cfg.rateAuto ? (uint8_t)R_5M : cfg.rateTop < N_RATES ? cfg.rateTop : cfg.rateIdx;
}
// лестница сейчас держит нижнюю ступень (а выбрана скорость выше)
static inline bool rateHeldLow() {
  uint8_t top = rateTopNow();
  return cfg.rateLadder && !rateLadderOff && top != R_1M && top != R_LR500 && cfg.rateIdx == R_1M;
}
static void rateAutoTick() {
  // upMul — во сколько раз растянут срок возвращения: удваивается, когда после возвращения пришлось опуститься снова
  static uint32_t last, goodSince, stepDownMs, stepUpMs, upMul = 1;
  uint32_t now = millis();
  bool kick = rateKick != 0;
  if (now - last < 1000 && !kick) return;
  last = now;
  rateKick = 0;
  if (kick) {   // оператор распорядился сам — прежние «штрафные» сроки не в счёт
    upMul = 1;
    stepDownMs = 0;
  }
  if (!cfg.rateAuto && cfg.rateTop >= N_RATES) cfg.rateTop = cfg.rateIdx;   // настройки от прошивки до 2.78: выбранной считаем ту, что стоит
  uint8_t top = rateTopNow();
  if (top == R_1M || top == R_LR500 || rateLadderOff || !cfg.rateLadder) {   // опускаться некуда или запрещено
    rateWhy = 6;
    rateWhyRssi = 0;
    goodSince = 0;
    if (cfg.rateIdx != top) {
      cfg.rateIdx = top;
      rApply = true;
    }
    return;
  }
  float worst = 0;
  int n = 0;
  bool lossy = false, ghost = false;
  uint8_t wid[3] = { 0, 0, 0 };
  char wname[NAME_LONG] = "";
  portENTER_CRITICAL(&peerMux);
  for (auto &p : peers) {
    if (!p.used || !p.seenMs || (p.flags & ST_OFF)) continue;
    if (peerOnline(p)) {
      bool bad = p.badRun >= 2 && p.rssiSm < -68.0f;
      if (!n || p.rssiSm < worst || (bad && !lossy)) {
        if (!n || p.rssiSm < worst) worst = p.rssiSm;
        memcpy(wid, p.id, 3);
        memcpy(wname, p.name, NAME_LONG);
      }
      if (bad) lossy = true;
      n++;
    } else if (msSince(p.seenMs) < 180000 && p.rssiSm <= -72.0f && !(p.flags & ST_SLEEP)) ghost = true;
  }
  portEXIT_CRITICAL(&peerMux);
  bool jam = txSkipMs && msSince(txSkipMs) < 4000;
  bool low = cfg.rateIdx == R_1M;
  bool weak = n && worst <= -75.0f, strong = !n || worst >= -70.0f;
  uint8_t want = low ? (uint8_t)R_1M : top;   // на любой иной скорости, чем нижняя, встаём на верхнюю ступень
  uint8_t why = !n ? 0 : 1;
  if (!low) {
    if (weak || (lossy && !jam) || ghost) {
      want = R_1M;
      why = weak ? 2 : lossy ? 3 : 4;
      if (stepUpMs && msSince(stepUpMs) < 120000 && upMul < 16) upMul *= 2;
    } else if (stepUpMs && msSince(stepUpMs) > 600000) upMul = 1;
  } else {
    // Срок возвращения (с 2.83): сигнал слабейшего с запасом (−66 дБм и лучше — на 9 дБ выше порога ухода вниз) —
    // 30 с ровного приёма; у самого порога возврата (−70…−66) — прежние 120 с. До 2.83 было 120 с всегда, и после
    // пары переходов туда-обратно срок вырастал до 20 минут: при хорошем сигнале передатчик выглядел застрявшим.
    uint32_t wait = (n && worst < -66.0f ? 120000u : 30000u) * upMul;
    if (wait > 600000) wait = 600000;
    bool ok = strong && !lossy && !ghost;
    why = !strong ? 2 : lossy ? 3 : ghost ? 4 : 5;
    if (!ok) goodSince = 0;
    else if (!goodSince) goodSince = now | 1;
    if (ok && msSince(goodSince) >= wait && (!stepDownMs || msSince(stepDownMs) >= wait)) {
      want = top;
      why = !n ? 0 : 1;
    }
    if (kick) {   // оператор выбрал скорость или качество: подняться сейчас, если приёмники это позволяют
      if (!weak && !lossy && !ghost) {
        want = top;
        why = !n ? 0 : 1;
      } else {   // нельзя — сказать на экране почему
        wname[NAME_LONG - 1] = 0;
        memcpy(rateNoteName, wname, NAME_LONG);
        rateNoteWhy = weak ? 2 : lossy ? 3 : 4;
        rateNoteRssi = n ? (int8_t)lroundf(worst) : 0;
        rateNoteKind = 3;
        rateNoteSeq = rateNoteSeq + 1;
      }
    }
  }
  rateWhy = why;
  rateWhyRssi = n ? (int8_t)lroundf(worst) : 0;
  memcpy(rateWhyId, wid, 3);
  if (want != cfg.rateIdx) {
    bool down = want == R_1M;
    if (down) stepDownMs = now | 1;
    else stepUpMs = now | 1;
    goodSince = 0;
    rateSteps = rateSteps + 1;
    cfg.rateIdx = want;
    rApply = true;
    if (down || low) {   // сказать оператору, что и почему (смена с прежней «не той» скорости на верхнюю — без сообщения)
      wname[NAME_LONG - 1] = 0;
      memcpy(rateNoteName, wname, NAME_LONG);
      rateNoteKind = down ? 1 : 2;
      rateNoteSeq = rateNoteSeq + 1;
    }
  }
}

struct __attribute__((packed)) PeerRecOld {   // запись «list2» (до 2.19): имя 13 букв
  uint8_t id[3];
  char name[NAME_LEN];
  uint8_t off, hasDev;
  uint8_t dev[32];
};
struct __attribute__((packed)) PeerRec {      // запись «list3»: имя до 32 букв
  uint8_t id[3];
  char name[NAME_LONG];
  uint8_t off, hasDev;
  uint8_t dev[32];
};
// Последняя известная версия прошивки каждого приёмника — отдельной записью «fw1» (запись «list3» не трогаем: её
// читают и прежние прошивки). Нужна передатчику сразу при включении: двойные пакеты (proto.h, Q_DOUBLE) можно слать,
// только если ВСЕ приёмники из списка — на связи они или нет — умеют их принимать. 10.10, первая проба 2.60: правило
// было «все, кто на связи», а спящий приёмник со старой прошивкой на связи не бывает — он просыпается, только услышав
// два пакета за 70 мс, одиночные же шли раз в 0,3 с; оба приёмника остались спать, и обновить их было нечем.
struct __attribute__((packed)) PeerFw {
  uint8_t id[3];
  uint8_t maj, mn;
};

static void peersSave() {
  PeerRec *rec = (PeerRec *)heap_caps_calloc(PEERS_MAX, sizeof(PeerRec), MALLOC_CAP_SPIRAM);   // 1,6 КБ — не из внутренней памяти
  if (!rec) return;
  int n = 0;
  for (int i = 0; i < PEERS_MAX; i++)
    if (peers[i].used) {
      memcpy(rec[n].id, peers[i].id, 3);
      memcpy(rec[n].name, peers[i].name, NAME_LONG);
      rec[n].off = peers[i].wantOff;
      rec[n].hasDev = peers[i].hasDev;
      memcpy(rec[n].dev, peers[i].dev, 32);
      n++;
    }
  uint32_t t0 = millis();
  peerPrefs.putBytes("list3", rec, n * sizeof(PeerRec));
  {
    PeerFw fw[PEERS_MAX];
    int k = 0;
    for (int i = 0; i < PEERS_MAX; i++)
      if (peers[i].used && peers[i].fwMaj) {
        memcpy(fw[k].id, peers[i].id, 3);
        fw[k].maj = peers[i].fwMaj;
        fw[k].mn = peers[i].fwMin;
        k++;
      }
    if (k) peerPrefs.putBytes("fw1", fw, k * sizeof(PeerFw));
    else peerPrefs.remove("fw1");
  }
  bbMark(BB_NVS, 1, millis() - t0);
  memset(rec, 0, PEERS_MAX * sizeof(PeerRec));   // в записях — личные ключи приёмников
  free(rec);
  peersDirty = false;
}

static void peerOnKeyReq(const KeyReq *q);

static void peersBegin() {
  peerPrefs.begin("hl-peers", false);
  // список прежнего вида («list», без личных ключей) не читаем: те приёмники всё равно надо подключать заново
  PeerRec *rec = (PeerRec *)heap_caps_calloc(PEERS_MAX, sizeof(PeerRec), MALLOC_CAP_SPIRAM);
  size_t len = rec ? peerPrefs.getBytes("list3", rec, PEERS_MAX * sizeof(PeerRec)) : 0;
  if (rec && len) {
    for (size_t i = 0; i < len / sizeof(PeerRec) && i < PEERS_MAX; i++) {
      peers[i].used = true;
      memcpy(peers[i].id, rec[i].id, 3);
      memcpy(peers[i].name, rec[i].name, NAME_LONG);
      peers[i].name[NAME_LONG - 1] = 0;
      peers[i].wantOff = rec[i].off;
      peers[i].hasDev = rec[i].hasDev;
      memcpy(peers[i].dev, rec[i].dev, 32);
    }
  } else if (rec) {   // списка нового вида ещё нет — взять прежний («list2», имена по 13 букв); сохранится уже новым
    PeerRecOld *old = (PeerRecOld *)rec;
    len = peerPrefs.getBytes("list2", old, PEERS_MAX * sizeof(PeerRecOld));
    for (size_t i = 0; i < len / sizeof(PeerRecOld) && i < PEERS_MAX; i++) {
      peers[i].used = true;
      memcpy(peers[i].id, old[i].id, 3);
      memcpy(peers[i].name, old[i].name, NAME_LEN);
      peers[i].name[NAME_LEN - 1] = 0;
      peers[i].wantOff = old[i].off;
      peers[i].hasDev = old[i].hasDev;
      memcpy(peers[i].dev, old[i].dev, 32);
    }
    if (len) peersDirty = true;
  }
  if (rec) {
    memset(rec, 0, PEERS_MAX * sizeof(PeerRec));
    free(rec);
  }
  {   // последние известные версии приёмников (см. PeerFw)
    PeerFw fw[PEERS_MAX];
    size_t fl = peerPrefs.getBytes("fw1", fw, sizeof(fw));
    for (size_t i = 0; i < fl / sizeof(PeerFw); i++) {
      int k = peerFind(fw[i].id);
      if (k >= 0) {
        peers[k].fwMaj = fw[i].maj;
        peers[k].fwMin = fw[i].mn;
      }
    }
  }
  rOnStatus = peerOnStatus;
  rOnPairReq = pairOnReq;
  rOnKeyReq = peerOnKeyReq;
  txAfterSend = peersPump;
  txHopSend = hopAnnounce;
}

// Удалить приёмник из набора: он теряет доступ к звуку. Сам список меняется через треть секунды (см. pairTick).
static void peerRemove(int i) {
  if (i < 0 || i >= PEERS_MAX || !peers[i].used || removeIdx >= 0) return;
  peerCommand(peers[i].id, CMD_FORGET, 0);   // если он на связи — пусть сотрёт ключи (уходит ещё под прежним ключом)
  removeAt = millis() + 350;
  removeIdx = i;
}

static void peerForget(int i) {
  portENTER_CRITICAL(&peerMux);
  peers[i].used = false;
  portEXIT_CRITICAL(&peerMux);
  peersDirty = true;
}

// P0 — начать счёт сообщений заново (для сравнения «до и после»)
static void peersStatZero() {
  portENTER_CRITICAL(&peerMux);
  for (int i = 0; i < PEERS_MAX; i++) {
    Peer &p = peers[i];
    p.stGot = p.stMiss = p.stPend = 0;
    p.stGot10 = p.stMiss10 = 0;
    p.aN = p.aLossSum = p.aLost = p.aUnder = p.aBad = p.aCap = 0;
    memset(p.stPh, 0, sizeof(p.stPh));
    rStDtSum[i] = 0;
    rStDtN[i] = 0;
  }
  rDtAllSum = 0;
  rDtAllN = 0;
  portEXIT_CRITICAL(&peerMux);
}

static void peersPrint() {
  int n = 0;
  for (int i = 0; i < PEERS_MAX; i++) {
    const Peer &p = peers[i];
    if (!p.used) continue;
    n++;
    Serial.printf("  приймач %02X%02X%02X «%s»: %s%s%s, гучність %u, сигнал %d дБм (тут %d), запас %u мс, втрати %.1f %%, пропусків %u, провалів %u мс, версія %s, вихід %s%s%s\n",
                  p.id[0], p.id[1], p.id[2], p.name, peerOnline(p) ? "на зв'язку" : "не на зв'язку", p.flags & ST_OFF ? ", ВИМКНЕНИЙ" : "",
                  p.flags & ST_SLEEP ? ", СПИТЬ" : "", p.volume, p.rssi, p.rssiHere, p.depthMs, p.lossPm / 10.0f, p.lostFrames, p.underMs, (p.fwMaj ? String(p.fwMaj) + "." + String(p.fwMin) : String(p.fw / 10.0f, 1)).c_str(),
                  p.flags & ST_STEREO ? "два канали" : "протифаза", p.fw < 16 ? "" : p.flags & ST_OUTLIVE ? " (живий)" : " (СТОЇТЬ)",
                  p.flags & ST_MUTE ? ", тиша" : "");
    if (p.hasInfo)
      Serial.printf("    налаштування: чіткість %u, баланс %d, межа гучності %u, вигляд %u, світлодіод %u, мова %u%s\n", p.par[RXP_CLARITY], (int)p.par[RXP_BALANCE] - 5,
                    p.par[RXP_VOLMAX], p.par[RXP_VIEW], p.par[RXP_LED], p.par[RXP_LANG], p.earTest ? "; іде перевірка навушників" : "");
    if (p.hasEq)   // приёмники с 2.45: эквалайзер и (с 2.46) блокировка ручки
      Serial.printf("    еквалайзер %d %d %d %d %d дБ, зріз низів %u; блокування ручки %u (0 немає, 1 меню, 2 гучність, 3 усе); підсилення +%u дБ; прошивка %u.%02u\n",
                    (p.eq[0] - 6) * 2, (p.eq[1] - 6) * 2, (p.eq[2] - 6) * 2, (p.eq[3] - 6) * 2, (p.eq[4] - 6) * 2, p.lowCut, p.lock, p.boost * 2, p.fwMaj, p.fwMin);
    if (p.hasFx) Serial.printf("    обробка звуку: шумозаглушення %u, стискання %u, у паузах %u (i16, i17, i18 = 0…3); чутливість %u (i19 = 0…4), поріг пауз +%u дБ (i20 = 0…4), закриття пауз %u (i21 = 0…2), зразок шуму %s (i22 = 2 — запам'ятати зараз, 0 — сам)\n", p.fx[0], p.fx[1], p.fx[2], p.fx[3], 3 + 3 * p.fx[4], p.fx[5], p.fx[6] ? "запам'ятовано" : "сам");
    if (p.stGot) {
      Serial.printf("    повідомлень про себе: отримано %u, не дійшло близько %u (%.1f %%); із отриманих %u прийшли, поки власний пакет передавача чекав ефіру — такий пакет ішов до ефіру в середньому %u мкс (звичайний — %u мкс)\n", (unsigned)p.stGot, (unsigned)p.stMiss, 100.0f * p.stMiss / (p.stGot + p.stMiss), (unsigned)p.stPend, rStDtN[i] ? (unsigned)(rStDtSum[i] / rStDtN[i]) : 0u, rDtAllN ? (unsigned)(rDtAllSum / rDtAllN) : 0u);
      Serial.printf("    коли приходять (від попереднього власного пакета, кроком 0,5 мс; остання — 5 мс і довше): %u %u %u %u %u %u %u %u %u %u %u\n", (unsigned)p.stPh[0], (unsigned)p.stPh[1], (unsigned)p.stPh[2], (unsigned)p.stPh[3], (unsigned)p.stPh[4], (unsigned)p.stPh[5], (unsigned)p.stPh[6], (unsigned)p.stPh[7], (unsigned)p.stPh[8], (unsigned)p.stPh[9], (unsigned)p.stPh[10]);
    }
    if (p.aN) Serial.printf("    звук від команди P0: повідомлень %u, втрати в середньому %.1f %%; пропало звуку %u мс (у %u повідомленнях), провалів %u мс%s; сигнал згладжено %.0f дБм\n", (unsigned)p.aN, p.aLossSum / 10.0f / p.aN, (unsigned)p.aLost, (unsigned)p.aBad, (unsigned)p.aUnder, p.aCap ? "; лік упирався в межу — насправді більше" : "", p.rssiSm);
  }
  if (!n) Serial.println("  приймачів у списку немає");
}
