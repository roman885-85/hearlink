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
  bool wantOff;               // каким его хотят видеть с передатчика
  uint32_t enforceUntil;      // до этого времени команду «включить/выключить» повторять, если приёмник ещё не такой
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
  uint8_t eq[5], lowCut;      // эквалайзер приёмника (с 2.45); hasEq — сообщает ли
  bool hasEq;
  uint8_t autoTries;          // сколько раз в это включение передатчик сам пытался его обновить
  uint32_t autoNeedMs;        // с какого времени он на связи, не спит и с прошивкой старее, чем у передатчика
  uint32_t parSetMs;          // когда настройку сменили с передатчика: 2,5 с после этого сообщения приёмника её не затирают
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
      peers[k].fwMaj = body[RXP_COUNT + 1] & 15;   // в старших битах — усиление (с 2.48)
      peers[k].fwMin = body[RXP_COUNT + 2];
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
    p.seenMs = millis() ? millis() : 1;
  }
  bool resend = false, off = false;
  uint8_t id[3];
  if (i >= 0) {
    Peer &p = peers[i];
    bool isOff = p.flags & ST_OFF;
    if (isOff != p.wantOff) {
      if (millis() < p.enforceUntil) {   // команда ещё не дошла — повторить
        resend = true;
        off = p.wantOff;
        memcpy(id, p.id, 3);
      } else {                           // приёмник включили его же ручкой — так тому и быть
        p.wantOff = isOff;
        peersDirty = true;
      }
    }
  }
  portEXIT_CRITICAL(&peerMux);
  if (resend) peerCommand(id, CMD_ENABLE, off ? 0 : 1);
}

// Скорость радио «сама». Замер 06.10 на столе (сигнал у приёмника −53 и −33 дБм): на 6, 12 и 24 Мбит/с терялось
// 3–8 % пакетов, на 11, 5,5, 2 и 1 — 0–2 %. Старый способ передачи (одна несущая) лучше держит узкие помехи
// вроде Bluetooth. Поэтому лестница — из таких скоростей: 11 → 5,5 → 2 → 1 → 0,5 Мбит/с.
// Смотрим на самого слабого из приёмников на связи. Вниз — когда он слышит плохо или два отчёта подряд теряет
// больше 4 % пакетов; вверх — когда сигнал с запасом и потерь меньше 1 %. Если шаг вниз потерь не убавил
// (мешают столкновения, а они длинному пакету вреднее) — шаг назад и минута без новых попыток.
// Ниже 5,5 лестница идёт, только если туда помещается выбранное качество звука.
static void rateAutoTick() {
  static uint32_t last, noDownUntil;
  static uint8_t bad, good, trial;     // trial: сколько отчётов осталось до оценки шага вниз
  static int lossWas, lossSum;
  if (!cfg.rateAuto || millis() - last < 2500) return;
  last = millis();
  int worst = 0, loss = 0, n = 0;
  for (auto &p : peers)
    if (p.used && peerOnline(p)) {
      if (!n || p.rssi < worst) worst = p.rssi;
      if (p.lossPm > loss) loss = p.lossPm;
      n++;
    }
  // Потолок — 5,5 Мбит/с. На 11 лестница начиналась до 06.10; сняли: на 8 м она теряла больше, чем 5,5
  // (3,5 против 2,3 %), а у владельца «выше 5,5 — потери даже с приёмником рядом» (замер: на 11 передатчик раз
  // упёрся в занятый эфир на 68 мс и потерял пакеты, на 6–24 — 0,3–0,6 % при сигнале −21 дБм, на 5,5 — 0,0 %).
  static const uint8_t LAD[] = { R_5M, R_2M, R_1M, R_LR500 };
  static const int8_t DOWN[] = { -80, -86, -90 };   // сигнал слабее — на ступень ниже
  static const int8_t UP[] = { -72, -78, -84 };     // сильнее — на ступень выше
  int low = 0, pos = 0;
  for (int i = 0; i < 4; i++) {
    if (qFits(cfg.quality, LAD[i])) low = i;
    if (LAD[i] == cfg.rateIdx) pos = i;
  }
  if (!n) {
    pos = 0;
    bad = good = trial = 0;
  } else {
    bad = loss > 40 ? bad + 1 : 0;       // потери — в десятых долях процента
    good = loss < 10 ? good + 1 : 0;
    if (trial) {
      lossSum += loss;
      if (!--trial && lossSum / 3 * 10 > lossWas * 7 && pos > 0 && worst >= DOWN[pos - 1]) {   // не помогло
        pos--;
        noDownUntil = millis() + 60000;
        bad = 0;
      }
    } else if (pos < low && worst < DOWN[pos]) pos++;
    else if (pos < low && bad >= 2 && millis() > noDownUntil) {
      pos++;
      lossWas = loss;
      lossSum = 0;
      trial = 3;
      bad = 0;
    } else if (pos > 0 && worst > UP[pos - 1] && good >= 4) {
      pos--;
      good = 0;
    }
  }
  if (pos > low) pos = low;
  uint8_t cur = cfg.rateIdx, want = LAD[pos];
  if (want != cur) {
    cfg.rateIdx = want;
    rApply = true;
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

static void peersPrint() {
  int n = 0;
  for (int i = 0; i < PEERS_MAX; i++) {
    const Peer &p = peers[i];
    if (!p.used) continue;
    n++;
    Serial.printf("  приймач %02X%02X%02X «%s»: %s%s%s, гучність %u, сигнал %d дБм (тут %d), запас %u мс, втрати %.1f %%, пропусків %u, провалів %u мс, версія %.1f, вихід %s%s%s\n",
                  p.id[0], p.id[1], p.id[2], p.name, peerOnline(p) ? "на зв'язку" : "не на зв'язку", p.flags & ST_OFF ? ", ВИМКНЕНИЙ" : "",
                  p.flags & ST_SLEEP ? ", СПИТЬ" : "", p.volume, p.rssi, p.rssiHere, p.depthMs, p.lossPm / 10.0f, p.lostFrames, p.underMs, p.fw / 10.0f,
                  p.flags & ST_STEREO ? "два канали" : "протифаза", p.fw < 16 ? "" : p.flags & ST_OUTLIVE ? " (живий)" : " (СТОЇТЬ)",
                  p.flags & ST_MUTE ? ", тиша" : "");
    if (p.hasInfo)
      Serial.printf("    налаштування: чіткість %u, баланс %d, межа гучності %u, вигляд %u, світлодіод %u, мова %u%s\n", p.par[RXP_CLARITY], (int)p.par[RXP_BALANCE] - 5,
                    p.par[RXP_VOLMAX], p.par[RXP_VIEW], p.par[RXP_LED], p.par[RXP_LANG], p.earTest ? "; іде перевірка навушників" : "");
    if (p.hasEq)   // приёмники с 2.45: эквалайзер и (с 2.46) блокировка ручки
      Serial.printf("    еквалайзер %d %d %d %d %d дБ, зріз низів %u; блокування ручки %u (0 немає, 1 меню, 2 гучність, 3 усе); підсилення +%u дБ; прошивка %u.%02u\n",
                    (p.eq[0] - 6) * 2, (p.eq[1] - 6) * 2, (p.eq[2] - 6) * 2, (p.eq[3] - 6) * 2, (p.eq[4] - 6) * 2, p.lowCut, p.lock, p.boost * 2, p.fwMaj, p.fwMin);
  }
  if (!n) Serial.println("  приймачів у списку немає");
}
