// Отладка по радио: строки порта одного устройства набора видны в порту другого; команды — тоже по радио.
// Включается на передатчике: «Налашт.» › «Налагодження ефіром» (с порта G1/G0). Пока включено, передатчик раз в две
// секунды шлёт маяк; приёмник, услышав его, шесть секунд пересылает свои строки (и сам в свой порт при этом не пишет,
// если его никто не слушает — зелёный светодиод не мигает). Спящий приёмник маяка не слышит и молчит.
//
// Кто показывает: любое устройство набора, с порта которого недавно что-то приходило (consoleLive) — обычно
// передатчик на столе; чужая строка печатается с номером отправителя: «[884A94] ПРИЙМАЧ к=7 …».
// Команда другому устройству — строкой «@номер команда» в порт любого устройства набора:
//   @884A94 ?      — приёмнику с этим номером        @tx u   — передатчику        @* ?   — всем
// Ответ придёт строками с номером. Пакеты закрыты ключом набора (чужой не прочтёт и не подделает); команда годна
// только свежая — в неё вписан номер текущего пакета звука, записанная и повторённая позже не выполнится.
#pragma once

#define MAGIC_DEBUG 0x4748   // «HG»
// DBG_EVENT (с 2.59) — редкая важная строка приёмника (сам перезагрузил радио, сам перезапустился): идёт передатчику
// всегда, а не только при включённой отладке; передатчик печатает её в порт и дописывает в журнал на карте.
enum { DBG_TEXT = 0, DBG_CMD = 1, DBG_BEACON = 2, DBG_EVENT = 3, DBG_MORE = 0x80 };
struct __attribute__((packed)) DbgMsg {
  uint16_t magic;
  uint8_t ver, kit;
  uint8_t from[3], to[3];   // чьё и кому (FF FF FF — всем, 00 00 00 — передатчику)
  uint8_t kind, len;
  // ---- досюда (12 байт) — открытая часть под подписью (шифр берёт её не длиннее 16 байт: с 20 он молча отказывал,
  // и первая сборка отладки не послала в эфир ни одного пакета)
  uint32_t a, b;            // одноразовое число шифра: у текста — случайное число включения и счётчик;
                            // у команды — включение передатчика и номер пакета звука (свежесть). Подписью связаны через шифр
  // дальше текст (зашифрован) и подпись
};
#define DBG_AAD 12

static uint8_t dbgId[3];
static uint32_t dbgSession, dbgCtr;
struct DbgIn {
  uint8_t from[3], more, len;
  char text[DBG_LINE + 1];
};
static DbgIn *dbgIn;                        // чужие строки, ждущие печати (во внешней памяти)
static volatile uint8_t dbgInHead, dbgInTail;
static char dbgCmdBuf[DBG_LINE + 1];        // пришедшая команда — выполнит главный цикл
static volatile bool dbgCmdAsk;
static uint32_t dbgLastCmdB;
static bool dbgLastCmdSet;
static char dbgSendCmd[DBG_LINE + 1];       // команда на отправку (набрана в порту через «@»)
static uint8_t dbgSendTo[3];
static volatile uint8_t dbgSendLeft;        // сколько раз ещё послать (повтор — на случай потери)
static uint32_t dbgSendA, dbgSendB;
static volatile uint32_t dbgSent, dbgHeard;
static char dbgEvText[DBG_LINE + 1];        // приёмник: событие на отправку передатчику
static volatile uint8_t dbgEvLeft;          // сколько раз ещё послать (повтор — на случай потери)
static uint32_t dbgEvB, dbgEvAt;
struct DbgEvIn {
  uint8_t from[3];
  char text[DBG_LINE + 1];
};
static DbgEvIn dbgEvIn;                     // передатчик: пришедшее событие — напечатает и запишет главный цикл
static volatile bool dbgEvInFull;
static void dbgEvent(const char *text) {    // приёмник (главный цикл)
  utf8Copy(dbgEvText, text, sizeof(dbgEvText));
  dbgEvB = ++dbgCtr;
  dbgEvAt = 0;
  dbgEvLeft = 3;
}

static size_t dbgBuild(uint8_t *pkt, uint8_t kind, const uint8_t *to, uint32_t a, uint32_t b, const char *text, uint8_t len) {
  DbgMsg *m = (DbgMsg *)pkt;
  m->magic = MAGIC_DEBUG;
  m->ver = PROTO_VER;
  m->kit = cfg.kit;
  memcpy(m->from, dbgId, 3);
  memcpy(m->to, to, 3);
  m->a = a;
  m->b = b;
  m->kind = kind;
  m->len = len;
  uint8_t *body = pkt + sizeof(DbgMsg);
  if (len) memcpy(body, text, len);
  bool cmd = (kind & 0x7F) == DBG_CMD;
  if (!secSeal(cmd ? SEC_DBGCMD : SEC_DEBUG, a, b, m->from, pkt, DBG_AAD, body, len, body + len)) return 0;
  return sizeof(DbgMsg) + len + SEC_TAG;
}

// Следующий пакет на отправку (0 — нечего). Один вызов — один пакет: кто шлёт, тот и выдерживает паузы.
static size_t dbgNext(uint8_t *pkt) {
  static const uint8_t ALL[3] = { 0xFF, 0xFF, 0xFF };
  static uint32_t beaconAt;
  if (!secHave || !dbgOut) return 0;
  if (dbgSendLeft) {
    dbgSendLeft = dbgSendLeft - 1;
    return dbgBuild(pkt, DBG_CMD, dbgSendTo, dbgSendA, dbgSendB, dbgSendCmd, (uint8_t)strlen(dbgSendCmd));
  }
  if (cfg.isTx && cfg.dbgAir && millis() - beaconAt >= 2000) {
    beaconAt = millis();
    return dbgBuild(pkt, DBG_BEACON, ALL, dbgSession, ++dbgCtr, "+", 1);   // один знак: пустой текст шифр не закрывает
  }
  if (dbgTail == dbgHead) return 0;
  if (!dbgAirOn()) {   // отладку выключили — недосланное выбросить
    dbgTail = dbgHead;
    return 0;
  }
  const DbgLine &l = dbgOut[dbgTail];
  size_t n = dbgBuild(pkt, DBG_TEXT | (l.more ? DBG_MORE : 0), ALL, dbgSession, ++dbgCtr, l.text, l.len);
  dbgTail = (dbgTail + 1) % DBG_Q;
  if (n) dbgSent = dbgSent + 1;
  return n;
}

// Приём (задача Wi-Fi).
static void dbgOnPacket(const uint8_t *data, int len) {
  const DbgMsg *m = (const DbgMsg *)data;
  int tl = len - (int)sizeof(DbgMsg) - SEC_TAG;
  if (tl < 0 || tl != m->len || tl > DBG_LINE || !secHave || !dbgIn || !memcmp(m->from, dbgId, 3)) return;
  uint8_t kind = m->kind & 0x7F;
  bool cmd = kind == DBG_CMD;
  char text[DBG_LINE + 1];
  const uint8_t *body = data + sizeof(DbgMsg);
  if (!secOpen(cmd ? SEC_DBGCMD : SEC_DEBUG, m->a, m->b, m->from, data, DBG_AAD, body, tl, body + tl, (uint8_t *)text)) return;
  text[tl] = 0;
  if (kind == DBG_BEACON) {
    if (!cfg.isTx) dbgAirUntil = millis() + 6000;
    return;
  }
  if (kind == DBG_EVENT) {
    static uint8_t lastFrom[3];
    static uint32_t lastA, lastB;
    static bool have;
    if (!cfg.isTx || dbgEvInFull) return;
    if (have && !memcmp(lastFrom, m->from, 3) && lastA == m->a && lastB == m->b) return;   // повтор того же события
    have = true;
    memcpy(lastFrom, m->from, 3);
    lastA = m->a;
    lastB = m->b;
    memcpy(dbgEvIn.from, m->from, 3);
    memcpy(dbgEvIn.text, text, tl + 1);
    dbgEvInFull = true;
    return;
  }
  if (kind == DBG_TEXT) {
    dbgHeard = dbgHeard + 1;
    if (!consoleLive()) return;   // нас никто не слушает — чужие строки ни к чему
    uint8_t next = (dbgInHead + 1) % DBG_Q;
    if (next == dbgInTail) return;
    DbgIn &d = dbgIn[dbgInHead];
    memcpy(d.from, m->from, 3);
    d.more = m->kind & DBG_MORE ? 1 : 0;
    d.len = (uint8_t)tl;
    memcpy(d.text, text, tl + 1);
    dbgInHead = next;
    return;
  }
  if (cmd) {
    static const uint8_t ALL[3] = { 0xFF, 0xFF, 0xFF }, TXID[3] = { 0, 0, 0 };
    if (memcmp(m->to, dbgId, 3) && memcmp(m->to, ALL, 3) && !(cfg.isTx && !memcmp(m->to, TXID, 3))) return;
    uint32_t ep = cfg.isTx ? (uint32_t)secEpoch : (uint32_t)rHeardEpoch, sq = cfg.isTx ? (uint32_t)txSeqNow : (uint32_t)rHeardSeq;
    if (m->a != ep || ((uint32_t)(sq - m->b) > 5000 && (uint32_t)(m->b - sq) > 300)) return;   // не свежая — запись или чужое включение
    if (dbgLastCmdSet && m->b == dbgLastCmdB) return;                                            // повтор той же команды
    dbgLastCmdSet = true;
    dbgLastCmdB = m->b;
    if (dbgCmdAsk) return;
    memcpy(dbgCmdBuf, text, tl + 1);
    dbgCmdAsk = true;
  }
}

// Напечатать пришедшие чужие строки (главный цикл). Пишем прямо в настоящий порт: обратно в эфир они не уходят.
static void dbgPrintHeard() {
  static bool cont;
  while (dbgIn && dbgInTail != dbgInHead) {
    const DbgIn &d = dbgIn[dbgInTail];
    if (!cont) hwSerial.printf("[%02X%02X%02X] ", d.from[0], d.from[1], d.from[2]);
    hwSerial.write((const uint8_t *)d.text, d.len);
    cont = d.more;
    if (!cont) hwSerial.write('\n');
    dbgInTail = (dbgInTail + 1) % DBG_Q;
  }
}

// «@номер команда» из порта: отправить команду другому устройству набора.
static void dbgRemote(const String &s) {
  int sp = s.indexOf(' ');
  String who = sp > 0 ? s.substring(1, sp) : s.substring(1), what = sp > 0 ? s.substring(sp + 1) : String("");
  what.trim();
  uint8_t to[3] = { 0xFF, 0xFF, 0xFF };
  if (who == "tx" || who == "TX") to[0] = to[1] = to[2] = 0;
  else if (who != "*") {
    if (who.length() != 6) {
      Serial.println("кому: @884A94 (номер приймача), @tx (передавач) або @* (усім)");
      return;
    }
    for (int i = 0; i < 3; i++) to[i] = (uint8_t)strtoul(who.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
  }
  if (!what.length() || what.length() > DBG_LINE) return;
  if (!secHave) {
    Serial.println("пристрій не в наборі — команду не надіслано");
    return;
  }
  strlcpy(dbgSendCmd, what.c_str(), sizeof(dbgSendCmd));
  memcpy(dbgSendTo, to, 3);
  dbgSendA = cfg.isTx ? (uint32_t)secEpoch : (uint32_t)rHeardEpoch;
  dbgSendB = cfg.isTx ? (uint32_t)txSeqNow : (uint32_t)rHeardSeq;
  dbgSendLeft = 3;
  Serial.printf("команду «%s» надіслано ефіром%s\n", dbgSendCmd, cfg.isTx && !cfg.dbgAir ? " (відповіді не буде: увімкніть «Налагодження ефіром» — G1)" : "");
}

// Передатчик: пакеты шлёт задача передачи звука (после очередного пакета звука), не чаще одного в 20 мс.
static void (*dbgPrevAfter)();
static void dbgAfterSend() {
  static uint8_t pkt[PKT_MAX];
  static uint32_t last;
  if (dbgPrevAfter) dbgPrevAfter();
  if (millis() - last < 20) return;
  size_t n = dbgNext(pkt);
  if (n) {
    last = millis();
    radioSend(pkt, n);
  }
}
// Приёмник: шлёт главный цикл, не чаще одного пакета в 15 мс.
static void dbgPumpRx() {
  static uint8_t pkt[PKT_MAX];
  static uint32_t last;
  if (rxPower == PW_STANDBY || millis() - last < 15) return;
  if (rxTxBusy()) return;   // прошлый пакет ещё у драйвера — следующий не кладём поверх (см. rxSend в radio.h)
  if (dbgEvLeft && secHave && millis() - dbgEvAt >= 700 && rLastRxMs && msSince(rLastRxMs) < 400) {   // событие — передатчику
    static const uint8_t TXID[3] = { 0, 0, 0 };
    size_t n = dbgBuild(pkt, DBG_EVENT, TXID, dbgSession, dbgEvB, dbgEvText, (uint8_t)strlen(dbgEvText));
    dbgEvAt = millis() ? millis() : 1;
    dbgEvLeft = dbgEvLeft - 1;
    if (n) {
      last = millis();
      rxSend(pkt, n);
    }
    return;
  }
  size_t n = dbgNext(pkt);
  if (n) {
    last = millis();
    rxSend(pkt, n);
  }
}

static void dbgBegin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  memcpy(dbgId, mac + 3, 3);
  dbgSession = esp_random();
  dbgOut = (DbgLine *)heap_caps_calloc(DBG_Q, sizeof(DbgLine), MALLOC_CAP_SPIRAM);
  dbgIn = (DbgIn *)heap_caps_calloc(DBG_Q, sizeof(DbgIn), MALLOC_CAP_SPIRAM);
  rOnDebug = dbgOnPacket;
  if (cfg.isTx) {
    dbgPrevAfter = txAfterSend;
    txAfterSend = dbgAfterSend;
  }
}
