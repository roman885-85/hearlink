// Проверка радиоканала для набора слабослышащих — без АЦП и ЦАП, на голых платах ESP32.
//
// Одна и та же прошивка заливается во все платы. По умолчанию плата — приёмник; команда «t» в порту
// делает её передатчиком, «r» — снова приёмником. Роль и настройки запоминаются в плате.
//
// Передатчик каждые 2 мс шлёт широковещательный пакет ESP-NOW такого же размера, каким будет пакет
// со звуком (заголовок + два кадра ADPCM: текущий и предыдущий). Приёмник считает, что дошло, что
// потерялось и на сколько пакеты опаздывают, и раз в секунду печатает строку; раз в 10 секунд —
// таблицу «при запасе N мс пропало бы столько-то кадров». По ней выбирается запас приёмника,
// то есть задержка звука.
//
// Порт 115200. Команды (строка и Enter):  ?  t  r  c<канал 1–13>  v<скорость: 1 2 5.5 11 6 9 12 18 24 36 48 54>
//   p<мощность, дБм 2–20>  n<длина пакета, байт 18–250>  i<интервал, мкс 1000–20000>  k<номер набора 0–255>
//   0 — обнулить счётчики

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <Preferences.h>

#define MAGIC 0x4C48  // «HL»

struct __attribute__((packed)) Hdr {
  uint16_t magic;
  uint8_t ver;       // 0 — проверочный пакет
  uint8_t kit;       // номер набора
  uint32_t boot;     // случайное число, новое при каждом запуске передатчика
  uint32_t seq;
  uint32_t tUs;      // время передатчика в момент отправки
  uint16_t intervalUs;
};

struct RateDef {
  const char *name;
  wifi_phy_mode_t mode;
  wifi_phy_rate_t rate;
};
static const RateDef RATES[] = {
  { "1", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_1M_L },   { "2", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_2M_L },
  { "5.5", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_5M_L }, { "11", WIFI_PHY_MODE_11B, WIFI_PHY_RATE_11M_L },
  { "6", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_6M },     { "9", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_9M },
  { "12", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_12M },   { "18", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_18M },
  { "24", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_24M },   { "36", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_36M },
  { "48", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_48M },   { "54", WIFI_PHY_MODE_11G, WIFI_PHY_RATE_54M },
};
static const int N_RATES = sizeof(RATES) / sizeof(RATES[0]);

// то, что обработчик приёма передаёт в основной цикл
struct RxEvt {
  uint32_t boot, seq, tTx, tRx;
  uint16_t intervalUs, len;
  int8_t rssi, noise;
  uint8_t rate, sigMode;
};

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// настройки (хранятся в плате)
static Preferences prefs;
static bool isTx = false;
static uint8_t channel = 6;
static uint8_t rateIdx = 4;  // 6 Мбит/с
static int8_t powerDbm = 15;
static uint16_t pktLen = 82;  // 18 заголовок + 2 × 32 байта ADPCM (64 отсчёта = 2 мс при 32 кГц)
static uint16_t intervalUs = 2000;
static uint8_t kit = 1;

// ------------------------------------------------------------------ передатчик

static esp_timer_handle_t txTimer;
static uint8_t txBuf[250];
static uint32_t txBoot, txSeq;
static volatile uint32_t txSent, txErr, txCbFail, txLate;  // txLate — прошлый пакет ещё не ушёл в эфир
static volatile uint32_t txAirSumUs, txAirMaxUs, txAirN;
static volatile int32_t txInFlight;
static volatile uint32_t txSendUs[16];
static volatile uint32_t txPut, txGot;
static volatile int txLastErr;  // код последнего отказа esp_now_send

static void onSent(const esp_now_send_info_t *, esp_now_send_status_t status) {
  uint32_t now = (uint32_t)esp_timer_get_time();
  uint32_t dt = now - txSendUs[txGot & 15];
  txGot = txGot + 1;
  txInFlight = txInFlight - 1;
  if (status != ESP_NOW_SEND_SUCCESS) txCbFail = txCbFail + 1;
  txAirSumUs = txAirSumUs + dt;
  txAirN = txAirN + 1;
  if (dt > txAirMaxUs) txAirMaxUs = dt;
}

static void txTick(void *) {
  Hdr *h = (Hdr *)txBuf;
  h->magic = MAGIC;
  h->ver = 0;
  h->kit = kit;
  h->boot = txBoot;
  h->seq = txSeq++;
  h->intervalUs = intervalUs;
  if (txInFlight > 0) txLate = txLate + 1;
  uint32_t now = (uint32_t)esp_timer_get_time();
  h->tUs = now;
  esp_err_t err = esp_now_send(BCAST, txBuf, pktLen);
  if (err == ESP_OK) {
    txSendUs[txPut & 15] = now;
    txPut = txPut + 1;
    txInFlight = txInFlight + 1;
    txSent = txSent + 1;
  } else {
    txErr = txErr + 1;
    txLastErr = err;
  }
}

static void txStart() {
  txBoot = esp_random();
  txSeq = 0;
  txPut = 0;
  txGot = 0;
  txInFlight = 0;
  for (int i = sizeof(Hdr); i < (int)sizeof(txBuf); i++) txBuf[i] = (uint8_t)esp_random();
  esp_timer_create_args_t a = {};
  a.callback = txTick;
  a.name = "tx";
  esp_timer_create(&a, &txTimer);
  esp_timer_start_periodic(txTimer, intervalUs);
}

static void txReport() {
  uint32_t sent = txSent, err = txErr, fail = txCbFail, late = txLate;
  uint32_t n = txAirN, sum = txAirSumUs, mx = txAirMaxUs;
  txSent = 0;
  txErr = 0;
  txCbFail = 0;
  txLate = 0;
  txAirN = 0;
  txAirSumUs = 0;
  txAirMaxUs = 0;
  Serial.printf("ПЕРЕДАВАЧ к=%u шв=%s Мбіт/с %u байт кожні %u мкс | надіслано %u, відмов %u, не пішло в ефір %u, "
                "черга %u | до ефіру: сер. %u мкс, макс. %u мкс\n",
                channel, RATES[rateIdx].name, pktLen, intervalUs, (unsigned)sent, (unsigned)err, (unsigned)fail,
                (unsigned)late, (unsigned)(n ? sum / n : 0), (unsigned)mx);
  Serial.printf("  без відповіді зараз: %d%s%s\n", (int)txInFlight, err ? ", код відмови: " : "", err ? esp_err_to_name(txLastErr) : "");
}

// ------------------------------------------------------------------ приёмник

static QueueHandle_t rxQ;
static volatile uint32_t rxOverflow, rxForeign;

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int)sizeof(Hdr)) return;
  const Hdr *h = (const Hdr *)data;
  if (h->magic != MAGIC || h->kit != kit) {
    rxForeign = rxForeign + 1;
    return;
  }
  RxEvt e;
  e.boot = h->boot;
  e.seq = h->seq;
  e.tTx = h->tUs;
  e.tRx = info->rx_ctrl->timestamp;
  e.intervalUs = h->intervalUs;
  e.len = len;
  e.rssi = info->rx_ctrl->rssi;
  e.noise = info->rx_ctrl->noise_floor;
  e.rate = info->rx_ctrl->rate;
  e.sigMode = info->rx_ctrl->sig_mode;
  if (xQueueSend(rxQ, &e, 0) != pdTRUE) rxOverflow = rxOverflow + 1;
}

// запасы приёмника, для которых считаем пропавшие кадры
static const uint16_t DEPTH_MS[] = { 4, 6, 8, 10, 12, 16, 24 };
static const int N_DEPTH = sizeof(DEPTH_MS) / sizeof(DEPTH_MS[0]);

static bool rxHave;
static uint32_t rxBoot, rxLastSeq, rxRef;
static bool ownOk[N_DEPTH];           // кадр rxLastSeq успел своим пакетом
static uint32_t failRed[N_DEPTH];     // пропало кадров, если каждый пакет несёт ещё и предыдущий кадр
static uint32_t failPlain[N_DEPTH];   // пропало кадров без повтора
static uint32_t totFrames, totLost, totDouble;
// за секунду
static uint32_t sGot, sLost, sDouble, sDup, sJitMaxUs;
static int32_t sRssiSum, sRssiMin, sNoiseSum;
static uint8_t sRate, sSig;
static int32_t sMinRel, prevMinRel;
static bool havePrevMin;
static int32_t minHist[11];
static int nHist;
static uint32_t secs;

static void rxResetTotals() {
  memset(failRed, 0, sizeof(failRed));
  memset(failPlain, 0, sizeof(failPlain));
  totFrames = totLost = totDouble = 0;
  secs = 0;
  nHist = 0;
}

static void rxHandle(const RxEvt &e) {
  if (!rxHave || e.boot != rxBoot) {  // первый пакет или передатчик перезапустился
    rxHave = true;
    rxBoot = e.boot;
    rxLastSeq = e.seq;
    rxRef = e.tRx - e.tTx;
    havePrevMin = false;
    sMinRel = INT32_MAX;
    nHist = 0;
    for (int d = 0; d < N_DEPTH; d++) ownOk[d] = true;
    sGot++;
    return;
  }
  int32_t gap = (int32_t)(e.seq - rxLastSeq);
  if (gap <= 0) {
    sDup++;
    return;
  }
  // опоздание пакета относительно самого быстрого за последние секунды
  int32_t rel = (int32_t)((e.tRx - e.tTx) - rxRef);
  if (rel < sMinRel) sMinRel = rel;
  int32_t base = havePrevMin && prevMinRel < sMinRel ? prevMinRel : sMinRel;
  uint32_t jit = (uint32_t)(rel - base);
  if (jit > sJitMaxUs) sJitMaxUs = jit;

  for (int d = 0; d < N_DEPTH; d++) {
    uint32_t depth = DEPTH_MS[d] * 1000u;
    bool own = jit <= depth;
    bool rescue = jit + e.intervalUs <= depth;  // этот пакет успевает принести и предыдущий кадр
    // кадр rxLastSeq: спасает только следующий за ним пакет
    if (!ownOk[d] && !(gap == 1 && rescue)) failRed[d]++;
    // кадры, чьи собственные пакеты потерялись
    if (gap >= 2) {
      failRed[d] += gap - 2;
      if (!rescue) failRed[d]++;
    }
    failPlain[d] += (gap - 1) + (own ? 0 : 1);
    ownOk[d] = own;
  }
  totFrames += gap;
  totLost += gap - 1;
  sLost += gap - 1;
  if (gap >= 3) {
    totDouble += gap - 2;
    sDouble += gap - 2;
  }
  rxLastSeq = e.seq;
  sGot++;
  sRssiSum += e.rssi;
  sNoiseSum += e.noise;
  if (e.rssi < sRssiMin) sRssiMin = e.rssi;
  sRate = e.rate;
  sSig = e.sigMode;
}

static const char *rxRateName(uint8_t rate, uint8_t sig) {
  if (sig != 0) return "11n";
  static const char *const N[16] = { "1", "2", "5.5", "11", "?", "2", "5.5", "11",
                                     "48", "24", "12", "6", "54", "36", "18", "9" };
  return N[rate & 15];
}

static void rxReport() {
  secs++;
  if (sGot == 0) {
    Serial.printf("ПРИЙМАЧ к=%u набір %u | сигналу немає%s\n", channel, kit, rxForeign ? " (є чужі пакети)" : "");
    rxForeign = 0;
    setLed(false);
    return;
  }
  // расхождение кварцев: как самый быстрый пакет «уезжает» за 10 секунд
  if (sMinRel != INT32_MAX) {
    if (nHist < 11) minHist[nHist++] = sMinRel;
    else {
      memmove(minHist, minHist + 1, 10 * sizeof(minHist[0]));
      minHist[10] = sMinRel;
    }
  }
  char drift[24] = "—";
  if (nHist == 11) snprintf(drift, sizeof(drift), "%+.1f", (minHist[10] - minHist[0]) / 10.0);
  Serial.printf("ПРИЙМАЧ к=%u шв=%s | прийнято %u, втрачено %u (підряд ≥2: %u), повторів %u | запізнення макс. "
                "%u.%u мс | сигнал %d дБм (мін. %d), шум %d | годинники %s мкс/с%s\n",
                channel, rxRateName(sRate, sSig), (unsigned)sGot, (unsigned)sLost, (unsigned)sDouble, (unsigned)sDup,
                (unsigned)(sJitMaxUs / 1000), (unsigned)(sJitMaxUs % 1000 / 100), (int)(sRssiSum / (int32_t)sGot),
                (int)sRssiMin, (int)(sNoiseSum / (int32_t)sGot), drift, rxOverflow ? " | ЧЕРГА ПЕРЕПОВНЕНА" : "");
  setLed(sLost == 0 || (secs & 1));  // горит — без потерь, мигает — потери
  if (secs % 10 == 0 && totFrames) {
    Serial.printf("  за %u с: кадрів %u, втрачено пакетів %u (%.3f %%), підряд ≥2 — %u\n", (unsigned)secs,
                  (unsigned)totFrames, (unsigned)totLost, 100.0 * totLost / totFrames, (unsigned)totDouble);
    Serial.print("  запас, мс:            ");
    for (int d = 0; d < N_DEPTH; d++) Serial.printf("%8u", DEPTH_MS[d]);
    Serial.print("\n  пропало без повтору:  ");
    for (int d = 0; d < N_DEPTH; d++) Serial.printf("%8u", (unsigned)failPlain[d]);
    Serial.print("\n  пропало з повтором:   ");
    for (int d = 0; d < N_DEPTH; d++) Serial.printf("%8u", (unsigned)failRed[d]);
    Serial.println();
  }
  if (sMinRel != INT32_MAX) {
    prevMinRel = sMinRel;
    havePrevMin = true;
  }
  sGot = sLost = sDouble = sDup = sJitMaxUs = 0;
  sRssiSum = sNoiseSum = 0;
  sRssiMin = 0;
  sMinRel = INT32_MAX;
  rxOverflow = 0;
  rxForeign = 0;
}

// ------------------------------------------------------------------ общее

static void setLed(bool on) {
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

static void applyRadio() {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_max_tx_power(powerDbm * 4);
  esp_now_rate_config_t rc = {};
  rc.phymode = RATES[rateIdx].mode;
  rc.rate = RATES[rateIdx].rate;
  esp_err_t err = esp_now_set_peer_rate_config(BCAST, &rc);
  if (err != ESP_OK) Serial.printf("швидкість не встановлено: помилка 0x%x\n", err);
}

static void save() {
  prefs.putBool("tx", isTx);
  prefs.putUChar("ch", channel);
  prefs.putUChar("rate", rateIdx);
  prefs.putChar("pwr", powerDbm);
  prefs.putUShort("len", pktLen);
  prefs.putUShort("intv", intervalUs);
  prefs.putUChar("kit", kit);
}

static void help() {
  Serial.println("команди: ? | t — передавач | r — приймач | c<канал 1–13> | v<швидкість 1 2 5.5 11 6 9 12 18 24 36 48 54>");
  Serial.println("         p<потужність 2–20 дБм> | n<довжина пакета 18–250> | i<інтервал 1000–20000 мкс> | k<набір 0–255> | 0 — обнулити");
  Serial.printf("зараз: %s, канал %u, швидкість %s Мбіт/с, потужність %d дБм, пакет %u байт, інтервал %u мкс, набір %u\n",
                isTx ? "передавач" : "приймач", channel, RATES[rateIdx].name, powerDbm, pktLen, intervalUs, kit);
}

static void command(String s) {
  s.trim();
  if (!s.length()) return;
  char c = s[0];
  String arg = s.substring(1);
  arg.trim();
  long v = arg.toInt();
  bool restart = false;
  switch (c) {
    case '?': help(); return;
    case 't': isTx = true; restart = true; break;
    case 'r': isTx = false; restart = true; break;
    case 'c':
      if (v < 1 || v > 13) return;
      channel = v;
      break;
    case 'v': {
      int i = 0;
      while (i < N_RATES && arg != RATES[i].name) i++;
      if (i == N_RATES) return;
      rateIdx = i;
      break;
    }
    case 'p':
      if (v < 2 || v > 20) return;
      powerDbm = v;
      break;
    case 'n':
      if (v < (long)sizeof(Hdr) || v > 250) return;
      pktLen = v;
      break;
    case 'i':
      if (v < 1000 || v > 20000) return;
      intervalUs = v;
      restart = isTx;
      break;
    case 'k':
      if (v < 0 || v > 255) return;
      kit = v;
      break;
    case '0': rxResetTotals(); return;
    default: return;
  }
  save();
  if (restart) {
    Serial.println("перезапуск…");
    delay(100);
    ESP.restart();
  }
  applyRadio();
  rxResetTotals();
  help();
}

void setup() {
  Serial.begin(115200);
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  prefs.begin("hl", false);
  isTx = prefs.getBool("tx", false);
  channel = prefs.getUChar("ch", channel);
  rateIdx = prefs.getUChar("rate", rateIdx);
  if (rateIdx >= N_RATES) rateIdx = 4;
  powerDbm = prefs.getChar("pwr", powerDbm);
  pktLen = prefs.getUShort("len", pktLen);
  intervalUs = prefs.getUShort("intv", intervalUs);
  kit = prefs.getUChar("kit", kit);

  WiFi.mode(WIFI_STA);
  wifi_country_t cc = {};
  memcpy(cc.cc, "UA", 2);
  cc.schan = 1;
  cc.nchan = 13;
  cc.policy = WIFI_COUNTRY_POLICY_MANUAL;
  esp_wifi_set_country(&cc);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW не запустився");
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);
  applyRadio();
  Serial.println();
  help();

  if (isTx) {
    esp_now_register_send_cb(onSent);
    txStart();
  } else {
    rxQ = xQueueCreate(512, sizeof(RxEvt));
    sMinRel = INT32_MAX;
    esp_now_register_recv_cb(onRecv);
  }
}

void loop() {
  static uint32_t lastReport;
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      command(line);
      line = "";
    } else if (line.length() < 32) line += ch;
  }
  if (!isTx && rxQ) {
    RxEvt e;
    while (xQueueReceive(rxQ, &e, 0) == pdTRUE) rxHandle(e);
  }
  uint32_t now = millis();
  if (now - lastReport >= 1000) {
    lastReport += 1000;
    if (now - lastReport >= 1000) lastReport = now;
    if (isTx) {
      txReport();
      setLed((now / 1000) & 1);
    } else rxReport();
  }
  delay(1);
}
