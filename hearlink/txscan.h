// Поиск свободного канала: передатчик на две секунды замолкает, слушает каждый из 13 каналов по 150 мс
// и считает, какую долю времени там кто-то передаёт. Встаёт на тот, где тише всего (с учётом соседних:
// каналы Wi-Fi перекрываются).
#pragma once
#include "txaudio.h"

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

static void scanCb(void *buf, wifi_promiscuous_pkt_type_t) {
  scanAirUs = scanAirUs + frameAirUs((const wifi_promiscuous_pkt_t *)buf);
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

// Куда идти: самый свободный канал по последнему замеру (с учётом соседних), кроме нынешнего и тех, что «в опале».
static int hopPick() {
  uint32_t now = millis();
  int best = 0, bestScore = 1 << 30;
  for (int ch = 1; ch <= 13; ch++) {
    if (ch == cfg.channel || (chBadUntil[ch - 1] && (int32_t)(now - chBadUntil[ch - 1]) < 0)) continue;
    int sc = scanBusy[ch - 1] * 4;
    for (int d = 1; d <= 2; d++) {
      if (ch - d >= 1) sc += scanBusy[ch - d - 1] * (d == 1 ? 2 : 1);
      if (ch + d <= 13) sc += scanBusy[ch + d - 1] * (d == 1 ? 2 : 1);
    }
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
    hopTo = cfg.channel;
    hopResult = 1;
    hopAtMs = now;
    hopSeq = hopSeq + 1;
    settingsSave();
    Serial.printf("канал змінено без паузи: %u → %u (за виміром зайнятість була %u %%, там %u %%)\n", hopFrom, hopTo, scanBusy[hopFrom - 1], scanBusy[hopTo - 1]);
  }
  // Радио встало (драйвер молчит, отправки отклоняются) — это не затор: его оживляет radioSend, а уходить с канала
  // незачем и некуда. 09.10 передатчик со вставшим радио девять минут «скакал по каналам» (7 → 11 → 3), объявляя
  // переходы в эфир, которого не было, и каждый раз записывал новый канал в настройки.
  bool dead = rReviveStep || (refused && msSince(rLastDoneMs) > 300);
  if (scanRunning || txHold || txPause || txHopCh || dead) {
    // Эфир стоит не из-за затора (замер каналов, выключение, раздача прошивки приёмникам): секунды этого времени
    // «плохими» не считать. 07.10 после раздачи прошивки передатчик тут же «ушёл с занятого канала» — затора не было.
    memset(waitMs, 0, sizeof(waitMs));
    hopQuietUntil = now + 10000;
    return;
  }
  if (!force && (!cfg.autoHop || n < 5 || now < 30000 || (int32_t)(now - hopQuietUntil) < 0)) return;
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
    int sc = scanBusy[ch - 1] * 4;
    for (int d = 1; d <= 2; d++) {   // соседние каналы мешают: ближний наполовину, через один — на четверть
      if (ch - d >= 1) sc += scanBusy[ch - d - 1] * (d == 1 ? 2 : 1);
      if (ch + d <= 13) sc += scanBusy[ch + d - 1] * (d == 1 ? 2 : 1);
    }
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
