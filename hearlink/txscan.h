// Поиск свободного канала: передатчик на две секунды замолкает, слушает каждый из 13 каналов по 150 мс
// и считает, какую долю времени там кто-то передаёт. Встаёт на тот, где тише всего (с учётом соседних:
// каналы Wi-Fi перекрываются).
//
// СИЛЬНЫЙ СОСЕД (с 2.82). Замер 12.10: рядом с передатчиком точка доступа на канале 10 с уровнем −31 дБм, передатчик
// стоял на канале 13. По счёту занятости канал 13 был чист (чужих кадров 0,3–0,6 % времени: за три канала кадры уже
// не разбираются), а пакеты ровно раз в 2,5 с ждали эфира 22–35 мс: когда точка шлёт свои длинные медленные кадры,
// её энергия занимает и наш канал, и радио честно ждёт. Запас приёмников от этого держался 33–46 мс — это и была
// задержка звука. На канале 1 (девять каналов от точки) ожидание — 4–6 мс.
// Поэтому при замере запоминается самый сильный маяк каждой точки доступа и её настоящий канал (он записан в самом
// маяке), и каждому каналу добавляется «риск от соседа»: чем сильнее точка и чем ближе её канал, тем больше.
// Насколько сигнал точки слабее через N каналов — оценка по форме спектра Wi-Fi (0, 1, 4, 10, 27, 37, 48 дБ); опытом
// проверены только два случая: через три канала (мешает) и через девять (не мешает).
#pragma once
#include "txaudio.h"
#include "txpeers.h"

void settingsSave();

static volatile uint32_t scanAirUs;
static volatile uint8_t scanStep;            // сколько каналов уже проверено
static volatile bool scanRunning, scanDone;
static uint8_t scanBusy[13];                 // занятость, 0…100 %

// сколько времени кадр занимал эфир, мкс
static inline uint32_t frameAirUs(const wifi_promiscuous_pkt_t *p) {
  static const float R[16] = { 1, 2, 5.5f, 11, 1, 2, 5.5f, 11, 48, 24, 12, 6, 54, 36, 18, 9 };
  static const float M[8] = { 6.5f, 13, 19.5f, 26, 39, 52, 58.5f, 65 };
  float mbps, pre;
  if (p->rx_ctrl.sig_mode == 0) {
    mbps = R[p->rx_ctrl.rate & 15];
    pre = p->rx_ctrl.rate < 8 ? 192 : 20;
  } else {
    mbps = M[p->rx_ctrl.mcs & 7] * (p->rx_ctrl.cwb ? 2 : 1);
    pre = 36;
  }
  return (uint32_t)(pre + p->rx_ctrl.sig_len * 8 / mbps);
}

static int8_t scanApRssi[13];                // самый сильный маяк точки доступа, работающей на этом канале, дБм (−127 — не слышно)
static uint8_t scanNear[13];                 // «риск от сильного соседа», 0…60 — уже прибавлен к scanBusy

static void scanCb(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
  scanAirUs = scanAirUs + frameAirUs(p);
  if (type != WIFI_PKT_MGMT) return;
  // маяк (0x80) или ответ на опрос (0x50): заголовок 24 байта, 12 байт постоянных полей, дальше элементы «номер,
  // длина, данные»; элемент 3 — канал, на котором точка работает на самом деле (слышно её и с соседних)
  int len = (int)p->rx_ctrl.sig_len - 4;
  const uint8_t *f = p->payload;
  if (len < 38 || (f[0] != 0x80 && f[0] != 0x50)) return;
  if (len > 400) len = 400;
  for (int i = 36; i + 2 <= len;) {
    int id = f[i], l = f[i + 1];
    if (i + 2 + l > len) break;
    if (id == 3 && l == 1) {
      int c = f[i + 2];
      if (c >= 1 && c <= 13 && p->rx_ctrl.rssi > scanApRssi[c - 1]) scanApRssi[c - 1] = p->rx_ctrl.rssi;
      break;
    }
    i += 2 + l;
  }
}

// Риск от сильных соседей — после замера всех каналов: к занятости канала прибавляется до 60 % за точку, чья энергия
// на этом канале сильнее −70 дБм (−55 и сильнее — все 60).
static void scanNearApply() {
  static const int8_t ATT[7] = { 0, 1, 4, 10, 27, 37, 48 };
  for (int ch = 1; ch <= 13; ch++) {
    int leak = -127;
    for (int c = 1; c <= 13; c++) {
      if (scanApRssi[c - 1] <= -127) continue;
      int d = abs(ch - c), v = scanApRssi[c - 1] - ATT[d > 6 ? 6 : d];
      if (v > leak) leak = v;
    }
    int risk = leak <= -70 ? 0 : (leak + 70) * 4;
    if (risk > 60) risk = 60;
    scanNear[ch - 1] = (uint8_t)risk;
    int b = scanBusy[ch - 1] + risk;
    scanBusy[ch - 1] = (uint8_t)(b > 100 ? 100 : b);
  }
}

// Как пакеты ждут эфира на нынешнем канале — за последнюю минуту работы (для подсказки на странице «Ефір» и для ?).
// Секунда «с ожиданием», если хоть один пакет ждал 15 мс и дольше: такой задержке приёмник обязан держать запас
// не меньше — то есть на столько же позже отдаёт звук.
static volatile uint8_t airSlowSecs;         // секунд с ожиданием из последних airSecs
static volatile uint8_t airSecs;             // сколько секунд в счёте (до 60)
static volatile uint16_t airSlowMs;          // среднее из наибольших ожиданий в таких секундах, мс
static volatile uint32_t airLostMin;         // сколько миллисекунд звука пропало у играющих приёмников за те же секунды
// Потери пакетов у приёмников с хорошим сигналом (с 2.88). Владелец 14.10 (2.87): «остались некоторые лёгкие щелчки,
// как у поцарапанного CD». Замер: у 884A94 (−45…−52 дБм) на канале 1 терялось 1–6 % пакетов, двойные потери — раз в
// 3–50 с (их 4 мс уже не восстанавливаются точно), а на пробном канале 5 за минуту — ни одного пакета. Передатчик этих
// помех не слышит (они у места приёмника), и с 2.77 одиночные потери звук не рвут — «мягкий» уход их не видел вовсе.
// Теперь каждую секунду берётся наибольшая из последних долей потерь (lossPm из сообщений о себе) у играющих приёмников
// со сглаженным сигналом не слабее SOFT_LOSS_RSSI: у таких потери — это помехи, их лечит другой канал (у слабых — это
// расстояние, им помогает нижняя ступень скорости). airLossPm — среднее за минуту, ‰.
#define SOFT_LOSS_RSSI -62
#define SOFT_LOSS_PM 15                      // 1,5 % пакетов за минуту — повод для пробы другого канала
static volatile uint16_t airLossPm;
static void airStatTick(uint32_t airMaxUs, bool valid) {
  static uint16_t ring[60], lost[60];
  static uint8_t loss[60];
  static uint8_t pos, n, ch, skip;
  static uint32_t lostSeen;
  uint32_t lostNow = txLostMsAll, d = lostNow - lostSeen;
  lostSeen = lostNow;
  if (ch != cfg.channel) {   // канал сменили — счёт заново; первые секунды не считаем (приёмники перестраиваются)
    ch = cfg.channel;
    pos = n = 0;
    skip = 8;
  }
  if (skip) skip--;
  else if (valid) {
    uint32_t ms = airMaxUs / 1000;
    ring[pos] = (uint16_t)(ms > 60000 ? 60000 : ms);
    lost[pos] = (uint16_t)(d > 60000 ? 60000 : d);
    uint8_t worst = 0;
    portENTER_CRITICAL(&peerMux);
    for (auto &p : peers)
      if (p.used && peerOnline(p) && (p.flags & ST_PLAYING) && !(p.flags & ST_OFF) && p.rssiSm >= SOFT_LOSS_RSSI && p.lossPm > worst) worst = p.lossPm;
    portEXIT_CRITICAL(&peerMux);
    loss[pos] = worst;
    pos = (pos + 1) % 60;
    if (n < 60) n++;
  }
  uint32_t cnt = 0, sum = 0, ls = 0, lp = 0;
  for (int i = 0; i < n; i++) {
    ls += lost[i];
    lp += loss[i];
    if (ring[i] >= 15) {
      cnt++;
      sum += ring[i];
    }
  }
  airSecs = n;
  airSlowSecs = (uint8_t)cnt;
  airSlowMs = (uint16_t)(cnt ? sum / cnt : 0);
  airLostMin = ls;
  airLossPm = (uint16_t)(n ? lp / n : 0);
}

// Наблюдение за эфиром во время работы — чтобы понять, отчего пакеты ждут: передатчик, не прерывая передачи, слышит
// чужие кадры Wi-Fi на своём канале и считает, сколько времени за секунду они заняли. Слышно только то, что удаётся
// разобрать (помехи не от Wi-Fi и совсем слабые передачи сюда не попадут). Включается командой порта A1; обычно выключено.
static volatile bool monOn;
static volatile uint32_t monAirUs, monFrames, monMaxUs, monSlowUs;   // monSlowUs — время кадров на 1–2 Мбит/с (самые «долгие»)

static void monCb(void *buf, wifi_promiscuous_pkt_type_t) {
  const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
  uint32_t us = frameAirUs(p);
  monAirUs = monAirUs + us;
  monFrames = monFrames + 1;
  if (us > monMaxUs) monMaxUs = us;
  if (p->rx_ctrl.sig_mode == 0 && (p->rx_ctrl.rate & 3) < 2 && p->rx_ctrl.rate < 8) monSlowUs = monSlowUs + us;
}

static void monSet(bool on) {
  esp_wifi_set_promiscuous(false);
  if (on) {
    esp_wifi_set_promiscuous_rx_cb(monCb);
    esp_wifi_set_promiscuous(true);
  }
  bbMark(BB_MON, on);
  monOn = on;
}

// ---- Сам уходит с занятого канала (владелец 07.10: «делай автоматический уход на свободный канал при заторе,
// с сообщением об этом на экране и причиной»; «сделать эту функцию опциональной в настройках» — cfg.autoHop).
// Затор — это когда пакеты подолгу ждут свободного эфира. Раз в секунду смотрим прошедшую секунду; она «плохая», если
// пакет ждал эфира 45 мс и дольше, или передатчику пришлось пропустить пакеты (прореживание — 50 и больше, пропуск
// совсем — 5 и больше), или драйвер отказывал. Пять плохих секунд из восьми последних — затор затяжной: передатчик
// на две секунды замолкает, меряет занятость всех каналов и переходит туда, где заметно свободнее (не меньше чем на
// треть). Приёмники находят новый канал сами за секунду. Если свободнее нигде нет — остаётся и говорит об этом.
// После попытки — пауза (полторы минуты после перехода, три — если переходить было некуда), чтобы не метаться.
//
// БЕЗ ПАУЗЫ (владелец 07.10: «можно ли сделать переход на другой канал без потери звука… бесшовный переход»).
// Две секунды тишины уходили на замер каналов, ещё секунда — пока приёмники искали передатчик. Теперь:
//  — каналы меряются заранее: при каждом включении, пока на экране заставка и передачи ещё нет (и кнопкой
//    «Знайти вільний канал»); по этому замеру выбирается, куда идти, без нового замера;
//  — о переходе передатчик объявляет приёмникам за 0,2 с («с пакета N я на канале C», команда всем сразу, с подписью),
//    и все переходят на одном и том же пакете. Приёмник, не услышавший объявления (спал, был далеко), найдёт канал
//    обходом, как раньше, за секунду-две.
// Канал, с которого ушли, десять минут не выбирается. Если на новом канале тоже затор — через 20 с следующий переход.
// Если замера нет или все каналы «в опале» — прежний способ, с замером и двумя секундами тишины.
static volatile bool scanAuto;               // идущий поиск начат самим передатчиком из-за затора
static volatile uint8_t hopFrom, hopTo;      // последний такой случай: с какого канала и на какой
// Канал сменил оператор (экран, порт) — с 4.00 тоже переходом без паузы, с объявлением: играющие приёмники идут следом
// сразу, спящие — по объявлению, пойманному в окне прослушивания (rxHopCh, rxaudio.h), или своим поиском. До 4.00 радио
// переставлялось сразу: у играющих приёмников звук пропадал секунды на 2,5, пока они искали канал, а спящий выключенный
// приёмник находил передатчик только своим обходом каналов.
static volatile bool hopManual;
static void txChannelSet(uint8_t ch) {
  if (ch < 1 || ch > 13 || ch == cfg.channel) return;
  if (txPause || txHold || txHopCh) {   // эфир стоит (выключение, раздача прошивки) или уже идёт переход — сразу, как раньше
    cfg.channel = ch;
    rApply = true;
    return;
  }
  hopFrom = cfg.channel;
  hopManual = true;
  txHopCh = ch;
}
static volatile uint8_t hopResult;           // 0 — ещё не было, 1 — перешёл, 2 — свободнее не нашлось, остался, 3 — ищу
static volatile uint16_t hopWaitMs;          // причина: сколько пакеты ждали эфира (наибольшее за плохие секунды)
static volatile uint8_t hopSecs;             //          и сколько секунд из последних восьми были плохими
static volatile uint32_t hopAtMs, hopSeq;    // когда; номер события (экран показывает сообщение, когда номер сменился)
static volatile bool hopForce;               // с порта попросили перейти сейчас (проверка без настоящего затора)
static volatile bool hopStall;               // последний уход — из-за того, что радио на канале вставало (а не из-за ожидания)
static volatile uint32_t hopQuietUntil;      // до этого времени новых попыток не делать
static volatile bool scanSurvey;             // идущий поиск — только замер (при включении): канал не менять
static uint32_t chBadUntil[13];              // с этого канала ушли из-за затора — до этого времени его не выбирать
static volatile bool hopSeamless;            // идущий переход — без паузы (сообщение на экране другое)
static volatile uint8_t hopFake;             // отладка (порт: h3): столько ближайших секунд считать плохими — проверить сам счёт затора
static void scanStart();

// ---- «МЯГКИЙ» УХОД (с 2.83; владелец 12.10: «делай мягкий уход с канала»).
// Уход при заторе срабатывает, когда пакеты ждут эфира от 45 мс — когда звук уже рвётся. Но и ожидания по 15–35 мс
// даром не проходят: приёмник обязан держать запас не меньше — и на столько же позже отдаёт звук (замер 12.10: на
// канале 13 рядом с сильной сетью на канале 10 запас 33–46 мс; на канале 1 опускался до 24). Поэтому при включённой
// настройке «Канал при заторі»:
//  — если за последнюю минуту в 15 и более секундах пакет ждал эфира 15 мс и дольше, а по замеру каналов (с учётом
//    сильных соседей) есть канал заметно свободнее (не меньше чем на треть) или просто чистый — передатчик переходит
//    туда без паузы;
//  — это ПРОБА: через минуту на новом канале сравнивается, что вышло. Ожиданий не стало меньше хотя бы на 30 % или у
//    приёмников звук стал пропадать заметно чаще (вдвое и ещё на 60 мс в минуту) — возвращается обратно и этот
//    канал полчаса не трогает; главный приоритет — непрерывный звук, а не задержка;
//  — опыт работы на каждом канале (секунд с ожиданием на минуту) помнится полчаса: канал, где уже было не лучше,
//    второй раз не пробуется;
//  — без играющих приёмников не уходит (задержка никому не мешает), первые полторы минуты после включения — тоже.
// Цена ухода: приёмник на самом краю слышимости может не услышать объявление о переходе и найти канал обходом —
// это 3–5 с без звука у него одного.
static volatile bool hopSoft;                // последний уход — «мягкий» (или возврат после его пробы)
static volatile uint8_t hopBackWhy;          // он — возврат: 1 — на новом канале ожиданий не меньше, 2 — у приёмников звук пропадал чаще,
                                             // 3 — приёмники с хорошим сигналом теряли пакеты не реже (с 2.88)
static volatile uint8_t hopLossPm;           // уход — из-за потерь пакетов у приёмника с хорошим сигналом: сколько их было, ‰ (0 — не из-за них)
static uint8_t chExp[13];                    // опыт работы на канале: секунд с ожиданием на минуту (действителен, пока chExpMs свежий)
static uint32_t chExpMs[13];                 // когда этот опыт получен (0 — не работали)
static uint8_t softFrom, softTo;             // идёт проба: с какого канала ушли (0 — пробы нет) и на какой
static uint8_t softSlow;                     //   сколько там было секунд с ожиданием на минуту
static uint32_t softLost;                    //   и сколько звука пропадало у приёмников за минуту, мс
static uint16_t softLossPm;                  //   и сколько пакетов теряли приёмники с хорошим сигналом, ‰ (с 2.88)
static bool softByLoss;                      //   проба — из-за этих потерь, а не из-за ожиданий эфира
static uint8_t softFails;                    // сколько проб подряд кончилось возвратом
static uint32_t softQuietUntil;              // до этого времени «мягких» уходов не делать
static volatile bool hopSoftForce;           // с порта (h4): начать пробу сейчас, не глядя на счёт ожиданий и на замер; через минуту — возврат

// оценка канала по замеру: своя занятость и соседних (ближний — наполовину, через один — на четверть)
static int chanScore(int ch) {
  int sc = scanBusy[ch - 1] * 4;
  for (int d = 1; d <= 2; d++) {
    if (ch - d >= 1) sc += scanBusy[ch - d - 1] * (d == 1 ? 2 : 1);
    if (ch + d <= 13) sc += scanBusy[ch + d - 1] * (d == 1 ? 2 : 1);
  }
  return sc;
}
static inline bool chExpFresh(int ch, uint32_t now) {
  return chExpMs[ch - 1] && msSince(chExpMs[ch - 1], now) < 1800000;
}
static int peersPlayingNow() {
  int n = 0;
  portENTER_CRITICAL(&peerMux);
  for (auto &p : peers)
    if (p.used && peerOnline(p) && (p.flags & ST_PLAYING) && !(p.flags & ST_OFF)) n++;
  portEXIT_CRITICAL(&peerMux);
  return n;
}
// Куда идти «мягко»: канал заметно свободнее по замеру, не «в опале», и по опыту (если он есть) заметно лучше
// нынешнего. Из знакомых — где ожиданий было меньше всего; иначе — лучший по замеру. 0 — некуда.
static int hopPickSoft(uint32_t now, int slowNow, bool anyway) {
  int cur = chanScore(cfg.channel), best = 0, bestKey = 1 << 30;
  for (int ch = 1; ch <= 13; ch++) {
    if (ch == cfg.channel || (chBadUntil[ch - 1] && (int32_t)(now - chBadUntil[ch - 1]) < 0)) continue;
    int sc = chanScore(ch);
    bool known = chExpFresh(ch, now);
    if (!anyway) {
      // заметно свободнее по замеру (на треть) — или просто чистый (оценка до 40: замер такие каналы не различает,
      // а опыт на нынешнем плохой — остаётся проба)
      if (sc * 3 > cur * 2 && sc > 40) continue;
      if (known && chExp[ch - 1] * 10 > slowNow * 7) continue;
    }
    int key = known && !anyway ? chExp[ch - 1] : 1000 + sc;   // знакомые хорошие — вперёд (проверке с порта опыт не указ)
    if (key < bestKey) {
      bestKey = key;
      best = ch;
    }
  }
  return best;
}
static void hopSoftGo(uint32_t now, int to, uint8_t backWhy) {
  hopFrom = cfg.channel;
  hopTo = cfg.channel;
  hopWaitMs = airSlowMs ? airSlowMs : 1;
  hopSecs = airSlowSecs;
  hopStall = false;
  hopSoft = true;
  hopBackWhy = backWhy;
  hopLossPm = !backWhy && softByLoss ? (uint8_t)(airLossPm > 255 ? 255 : airLossPm) : 0;
  hopQuietUntil = now + 20000;
  txHopCh = (uint8_t)to;
}
static void hopSoftTick(uint32_t now) {
  bool force = hopSoftForce;
  hopSoftForce = false;
  uint8_t ch = cfg.channel;
  if (softFrom && ch != softTo) softFrom = 0;   // канал сменили не мы (оператор, затор) — проба отменяется, возвращаться незачем
  if (softFrom && softFrom != ch) {   // идёт проба нового канала: итог — когда наберётся минута работы на нём
    if (airSecs < 60) return;
    int slow = airSlowSecs, from = softFrom;
    uint32_t lost = airLostMin;
    unsigned lossNew = airLossPm;
    softFrom = 0;
    chExp[ch - 1] = (uint8_t)slow;
    chExpMs[ch - 1] = now | 1;
    // «лучше»: ожиданий меньше хотя бы на 30 % — или (с 2.88) приёмники с хорошим сигналом теряют пакетов меньше хотя бы
    // на 30 %. «Хуже»: звук у приёмников пропадает вдвое чаще (и ещё на 60 мс в минуту) — или пакетов теряется больше на
    // 1 %. У пробы с порта softSlow = softLossPm = 0: она всегда кончается возвратом.
    bool worseLost = lost > softLost * 2 + 60, worse = worseLost || lossNew > softLossPm + 10;
    bool gainWait = softSlow && slow * 10 <= softSlow * 7, gainLoss = softLossPm >= SOFT_LOSS_PM && lossNew * 10 <= softLossPm * 7u;
    if (!worse && (gainWait || gainLoss)) {
      softFails = 0;
      softQuietUntil = now + 120000;
      Serial.printf("м'який перехід: канал %u кращий — секунд з очікуванням на хвилину %d (було %u), у приймачів пропало звуку %u мс (було %u), пакетів губили %u ‰ (було %u) — лишаюсь\n",
                    ch, slow, softSlow, (unsigned)lost, (unsigned)softLost, lossNew, (unsigned)softLossPm);
      return;
    }
    if (softFails < 250) softFails++;
    softQuietUntil = now + (softFails >= 2 ? 3600000 : 600000);
    chBadUntil[ch - 1] = (now + 1800000) ? now + 1800000 : 1;   // сюда полчаса не ходить
    chBadUntil[from - 1] = 0;                                    // а туда, откуда пришли, вернуться можно
    Serial.printf("м'який перехід: канал %u не кращий — секунд з очікуванням на хвилину %d (було %u), у приймачів пропало звуку %u мс (було %u), пакетів губили %u ‰ (було %u) — повертаюсь на канал %d\n",
                  ch, slow, softSlow, (unsigned)lost, (unsigned)softLost, lossNew, (unsigned)softLossPm, from);
    hopSoftGo(now, from, worseLost ? 2 : softByLoss ? 3 : 1);
    return;
  }
  softFrom = 0;
  bool byLoss = airSlowSecs < 15 && airLossPm >= SOFT_LOSS_PM;   // повод — не ожидания эфира, а потери у приёмника с хорошим сигналом
  if (!force) {
    if (!cfg.autoHop || now < 90000 || airSecs < 60 || (airSlowSecs < 15 && !byLoss)) return;
    if ((int32_t)(now - softQuietUntil) < 0 || (int32_t)(now - hopQuietUntil) < 0) return;
    if (!peersPlayingNow()) return;
  }
  if (!scanDone) return;
  chExp[ch - 1] = airSlowSecs;
  chExpMs[ch - 1] = now | 1;
  // Помехи у места приёмника замер каналов передатчиком не показывает (канал 1 у него «лучший», а 884A94 там терял до
  // 6 % пакетов): для такой пробы — лучший по замеру из остальных, без требования «заметно свободнее».
  int pick = hopPickSoft(now, airSlowSecs, force || byLoss);
  if (!pick) {   // заметно свободнее по замеру нигде нет — остаёмся; спросим снова через десять минут
    softQuietUntil = now + 600000;
    Serial.printf("м'який перехід: на каналі %u пакети чекали ефіру 15 мс і довше у %u секундах із 60, але помітно вільнішого каналу за виміром немає — лишаюсь\n", ch, (unsigned)airSlowSecs);
    return;
  }
  softFrom = ch;
  softTo = (uint8_t)pick;
  softSlow = force ? 0 : airSlowSecs;   // проба с порта всегда кончается возвратом (см. «не лучше»): прибор остаётся на своём канале
  softLost = airLostMin;
  softLossPm = force ? 0 : airLossPm;
  softByLoss = !force && byLoss;
  chBadUntil[ch - 1] = (now + 600000) ? now + 600000 : 1;   // как при заторе: десять минут сюда не возвращаться (возврат после пробы это снимает)
  if (softByLoss) Serial.printf("м'який перехід: на каналі %u приймач із добрим сигналом губив %u ‰ пакетів за хвилину (завади біля нього) — пробую канал %d без паузи\n", ch, (unsigned)airLossPm, pick);
  else Serial.printf("м'який перехід: на каналі %u пакети чекали ефіру 15 мс і довше у %u секундах із 60 (до %u мс) — пробую канал %d без паузи\n", ch, (unsigned)airSlowSecs, (unsigned)airSlowMs, pick);
  hopSoftGo(now, pick, 0);
}
static void hopSoftPrint() {
  uint32_t now = millis();
  Serial.printf("м'який перехід: %s; секунд з очікуванням 15 мс і довше — %u із %u, у приймачів пропало звуку %u мс, пакетів губили (з добрим сигналом) %u ‰ (поріг %u); ",
                softFrom && softFrom != cfg.channel ? "ІДЕ ПРОБА нового каналу" : "проби немає", (unsigned)airSlowSecs, (unsigned)airSecs, (unsigned)airLostMin, (unsigned)airLossPm, (unsigned)SOFT_LOSS_PM);
  if (softFrom && softFrom != cfg.channel) Serial.printf("прийшли з каналу %u (там %u с, %u мс); ", softFrom, softSlow, (unsigned)softLost);
  Serial.printf("спокій ще %d с; проб із поверненням підряд %u; досвід каналів 1…13 (секунд на хвилину, – немає):", (int32_t)(softQuietUntil - now) > 0 ? (int)((softQuietUntil - now) / 1000) : 0, softFails);
  for (int c = 1; c <= 13; c++) Serial.printf(chExpFresh(c, now) ? " %u" : " –", chExp[c - 1]);
  Serial.print("; оцінка каналів за виміром:");
  for (int c = 1; c <= 13; c++) Serial.printf(" %d", chanScore(c));
  Serial.println();
}

// Куда идти: самый свободный канал по последнему замеру (с учётом соседних), кроме нынешнего и тех, что «в опале».
static int hopPick() {
  uint32_t now = millis();
  int best = 0, bestScore = 1 << 30;
  for (int ch = 1; ch <= 13; ch++) {
    if (ch == cfg.channel || (chBadUntil[ch - 1] && (int32_t)(now - chBadUntil[ch - 1]) < 0)) continue;
    int sc = chanScore(ch);
    int far = abs(ch - cfg.channel);   // при равных — тот, что дальше от нынешнего (соседний канал делит с ним эфир)
    sc = sc * 16 - (far > 5 ? 5 : far);
    if (sc < bestScore) {
      bestScore = sc;
      best = ch;
    }
  }
  return best;
}

static void hopTick(uint32_t airMaxUs, uint32_t thinned, uint32_t skipped, uint32_t refused) {
  static uint16_t waitMs[8];   // наибольшее ожидание в каждой из восьми последних секунд (0 — секунда хорошая)
  static uint8_t pos;
  uint32_t now = millis();
  if (hopFake) {
    hopFake = hopFake - 1;
    airMaxUs = 120000;
  }
  bool bad = airMaxUs >= 45000 || thinned >= 50 || skipped >= 5 || refused > 0;
  waitMs[pos] = bad ? (uint16_t)(airMaxUs / 1000 > 65000 ? 65000 : airMaxUs / 1000 ? airMaxUs / 1000 : 1) : 0;
  pos = (pos + 1) & 7;
  int n = 0, worst = 0;
  for (int i = 0; i < 8; i++) {
    n += waitMs[i] != 0;
    if (waitMs[i] > worst) worst = waitMs[i];
  }
  // Радио дважды за пять минут вставало на одном канале — канал для него плохой: 09.10 на занятом канале 6 оно
  // вставало каждые 1–2 минуты, на свободных 7 и 10 — раз за 19 минут. Уходим, как при заторе (когда радио уже ожило).
  static uint32_t deathsSeen, deathMs;
  static uint8_t deathCh;
  static bool stallHop;
  if (bbDeaths != deathsSeen) {
    deathsSeen = bbDeaths;
    if (deathMs && now - deathMs < 300000 && deathCh == cfg.channel && cfg.autoHop) stallHop = true;
    deathMs = now ? now : 1;
    deathCh = cfg.channel;
  }
  bool force = hopForce;
  hopForce = false;
  bool byStall = false;
  if (stallHop && !rReviveStep && msSince(rLastDoneMs) < 100 && now > 30000) {
    stallHop = false;
    force = byStall = true;
  }
  if (txHopDone) {   // переход без паузы сделан задачей передачи: записать канал, сказать в порт и на экран
    txHopDone = false;
    bool manual = hopManual;
    hopManual = false;
    settingsSave();
    if (manual) Serial.printf("канал змінено вручну, без паузи (з оголошенням приймачам): %u → %u\n", hopFrom, (unsigned)cfg.channel);
    else {   // «сам змінив канал» на экране «Ефір» — только про свои уходы с затора
      hopTo = cfg.channel;
      hopResult = 1;
      hopAtMs = now;
      hopSeq = hopSeq + 1;
      Serial.printf("канал змінено без паузи: %u → %u (за виміром зайнятість була %u %%, там %u %%)\n", hopFrom, hopTo, scanBusy[hopFrom - 1], scanBusy[hopTo - 1]);
    }
  }
  // Радио встало (драйвер молчит, отправки отклоняются) — это не затор: его оживляет radioSend, а уходить с канала
  // незачем и некуда. 09.10 передатчик со вставшим радио девять минут «скакал по каналам» (7 → 11 → 3), объявляя
  // переходы в эфир, которого не было, и каждый раз записывал новый канал в настройки.
  bool dead = rReviveStep || (refused && msSince(rLastDoneMs) > 300);
  airStatTick(airMaxUs, !(scanRunning || txHold || txPause || txHopCh || dead) && !hopFake);
  if (scanRunning || txHold || txPause || txHopCh || dead) {
    // Эфир стоит не из-за затора (замер каналов, выключение, раздача прошивки приёмникам): секунды этого времени
    // «плохими» не считать. 07.10 после раздачи прошивки передатчик тут же «ушёл с занятого канала» — затора не было.
    memset(waitMs, 0, sizeof(waitMs));
    hopQuietUntil = now + 10000;
    return;
  }
  if (!force && (!cfg.autoHop || n < 5 || now < 30000 || (int32_t)(now - hopQuietUntil) < 0)) {
    hopSoftTick(now);   // затора нет — может быть, пора уйти «мягко» (или подвести итог пробы)
    return;
  }
  softFrom = 0;   // настоящий затор важнее пробы
  hopSoft = false;
  hopBackWhy = 0;
  hopFrom = cfg.channel;
  hopTo = cfg.channel;
  hopWaitMs = byStall ? 1 : worst;   // (0 экран понимает как «проверка с порта»)
  hopSecs = n;
  hopStall = byStall;
  memset(waitMs, 0, sizeof(waitMs));
  chBadUntil[cfg.channel - 1] = (now + 600000) ? now + 600000 : 1;   // сюда десять минут не возвращаться
  int pick = scanDone ? hopPick() : 0;
  if (pick) {   // есть замер — переходим без паузы
    hopQuietUntil = now + 20000;
    if (byStall) Serial.printf("РАДІО ДВІЧІ СТАВАЛО на каналі %u за п'ять хвилин — переходжу на канал %d без паузи\n", cfg.channel, pick);
    else Serial.printf("ЗАТОР на каналі %u: %d поганих секунд із 8, пакети чекали до %d мс — переходжу на канал %d без паузи\n", cfg.channel, n, worst, pick);
    txHopCh = pick;
    return;
  }
  hopResult = 3;
  hopAtMs = now;
  hopSeq = hopSeq + 1;
  hopQuietUntil = now + 180000;   // если перейдём — сократится до полутора минут (см. scanTask)
  memset(chBadUntil, 0, sizeof(chBadUntil));
  if (byStall) Serial.printf("РАДІО ДВІЧІ СТАВАЛО на каналі %u за п'ять хвилин — шукаю вільніший канал\n", cfg.channel);
  else Serial.printf("ЗАТОР на каналі %u: %d поганих секунд із 8, пакети чекали до %d мс — шукаю вільніший канал\n", cfg.channel, n, worst);
  scanAuto = true;
  scanStart();
}

static void scanTask(void *) {
  const int dwellMs = 150;
  scanStep = 0;
  txPause = true;
  for (int i = 0; i < 50 && !txPaused; i++) vTaskDelay(pdMS_TO_TICKS(2));
  vTaskDelay(pdMS_TO_TICKS(30));
  bbMark(BB_SCAN, 1);
  memset(scanApRssi, -127, sizeof(scanApRssi));
  esp_wifi_set_promiscuous_rx_cb(scanCb);
  esp_wifi_set_promiscuous(true);
  for (int ch = 1; ch <= 13; ch++) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    vTaskDelay(pdMS_TO_TICKS(8));
    scanAirUs = 0;
    vTaskDelay(pdMS_TO_TICKS(dwellMs));
    uint32_t pct = scanAirUs / (dwellMs * 10);
    scanBusy[ch - 1] = pct > 100 ? 100 : pct;
    scanStep = ch;
  }
  esp_wifi_set_promiscuous(false);
  bbMark(BB_SCAN, 0);
  scanNearApply();
  if (monOn) monSet(true);   // наблюдение за эфиром было включено — вернуть
  if (scanSurvey) {          // только замер (при включении): канал остаётся прежним
    scanSurvey = false;
    radioSetParams();
    rPut = 0;
    rGot = 0;
    rLastDoneMs = millis();
    txPause = false;
    scanDone = true;
    scanRunning = false;
    vTaskDelete(NULL);
  }
  int best = cfg.channel, bestScore = 1 << 30, curScore = 0;
  for (int ch = 1; ch <= 13; ch++) {
    int sc = chanScore(ch);   // соседние каналы мешают: ближний наполовину, через один — на четверть
    if (ch == cfg.channel) curScore = sc;
    if (sc < bestScore || (sc == bestScore && ch == cfg.channel)) {
      bestScore = sc;
      best = ch;
    }
  }
  if (!scanAuto) memset(chBadUntil, 0, sizeof(chBadUntil));   // замер по кнопке — всё с чистого листа
  if (scanAuto) {   // уход из-за затора: переходим, только если там заметно свободнее (не меньше чем на треть)
    uint8_t from = cfg.channel;
    if (best != from && bestScore * 3 > curScore * 2) best = from;
    hopTo = best;
    hopResult = best != from ? 1 : 2;
    hopAtMs = millis();
    if (best != from) hopQuietUntil = hopAtMs + 90000;
    hopSeq = hopSeq + 1;
    if (best != from) Serial.printf("канал змінено: %u → %d (зайнятість була %u %%, там %u %%)\n", from, best, scanBusy[from - 1], scanBusy[best - 1]);
    else Serial.printf("вільнішого каналу немає — лишаюсь на каналі %u (зайнятість %u %%)\n", from, scanBusy[from - 1]);
    scanAuto = false;
  }
  cfg.channel = best;
  radioSetParams();
  rPut = 0;
  rGot = 0;
  rLastDoneMs = millis();
  txPause = false;
  settingsSave();
  scanDone = true;
  scanRunning = false;
  vTaskDelete(NULL);
}

static void scanStart() {
  if (scanRunning) return;
  scanRunning = true;
  xTaskCreatePinnedToCore(scanTask, "scan", 4096, NULL, 3, NULL, 1);
}
