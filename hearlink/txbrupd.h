// Передатчик 4.x: обновление моста с передатчика, команды мосту и его стан в порт. Подключается после txaudio.h (нужны
// txPause и вход); приём обратного канала — в txbridge.h (его зовёт звуковая задача, он подключён раньше).
#pragma once
#include "bridge_fw.h"   // образ программы моста для его обновления отсюда (собирает bridge/build.sh → make_images.py)
#include "driver/uart.h"

// ---- Обновление моста с передатчика (с 4.00).
// Загрузчик моста (bridge/loader) слушает команды на GP5 — это вывод 43 передатчика, его порт, — а отвечает по GP4: тому
// же проводу, по которому идут данные I2S (вывод 40). На время обновления передатчик слушает вывод 40 вторым UART (вход
// I2S видит там ответы загрузчика — звук со входа глушится), шлёт мосту «в загрузчик», затем образ кусками по 4 КБ с
// подтверждением каждого, сверку CRC-32 и «запуск». 60 КБ на 115 200 — около 8 с. Сам — когда мост на связи и сообщает
// версию, отличную от встроенной (выключатель «Налашт.» › «Оновлювати міст сам», cfg.brAutoUpd; не больше трёх раз за
// включение); вручную — порт C9. Мост молчит дольше 20 с — раз в минуту спросить загрузчик: после неудачи мост остаётся
// в нём (программа не цела) и ждёт — тогда дописать.
enum { BU_IDLE = 0, BU_ENTER, BU_HELLO, BU_WRITE, BU_FINISH, BU_BOOT, BU_APP, BU_DONE, BU_FAIL };
struct BrUpd {
  volatile uint8_t st = BU_IDLE;
  volatile uint8_t pct = 0;      // ход, %
  const char *why = "";          // почему не вышло (порт)
  uint32_t off = 0, t0 = 0, sentMs = 0, probeMs = 0, startMs = 0;
  uint8_t tries = 0, runs = 0;   // повторов шага; запусков в это включение
  bool probe = false;            // только спросить загрузчик (мост молчит)
  bool uartOn = false;
  volatile bool ask = false;     // попросили с порта (C9)
  bp_rx_t *rx = nullptr;         // разбор ответов загрузчика и кадр записи — из кучи только на время обновления
  uint8_t *frame = nullptr;
};
static BrUpd brUpd;
static bool brUpdBusy() { return brUpd.st >= BU_ENTER && brUpd.st <= BU_APP; }
static bool brUpdVerOther() { return txBr.st.verMajor != BRIDGE_FW_VER_MAJOR || txBr.st.verMinor != BRIDGE_FW_VER_MINOR; }
static uint32_t brRd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void brUpdUartOff() {
  if (!brUpd.uartOn) return;
  uart_driver_delete(UART_NUM_1);   // вывод 40 — снова только вход I2S (сигнал RX UART1 с выключенного блока ни на что не влияет)
  brUpd.uartOn = false;
}
static void brUpdEnd(uint8_t st, const char *why) {
  BrUpd &u = brUpd;
  brUpdUartOff();
  free(u.rx);
  free(u.frame);
  u.rx = nullptr;
  u.frame = nullptr;
  u.why = why;
  u.st = st;
  txBrMute = false;
  txBr.kick = true;   // мосту — сразу, какой вход играть
  if (st == BU_FAIL) Serial.printf("міст: оновлення НЕ ВДАЛОСЯ — %s\n", why);
  else if (st == BU_DONE) Serial.printf("міст: оновлено до %u.%02u (%u байт) за %u с\n", BRIDGE_FW_VER_MAJOR, BRIDGE_FW_VER_MINOR,
                                        (unsigned)BRIDGE_FW_SIZE, (unsigned)((millis() - u.startMs) / 1000));
}
static bool brUpdBegin(bool probe) {
  BrUpd &u = brUpd;
  u.rx = (bp_rx_t *)heap_caps_malloc(sizeof(bp_rx_t), MALLOC_CAP_INTERNAL);
  u.frame = (uint8_t *)heap_caps_malloc(4096 + 16, MALLOC_CAP_INTERNAL);
  uart_config_t c = {};
  c.baud_rate = 115200;
  c.data_bits = UART_DATA_8_BITS;
  c.parity = UART_PARITY_DISABLE;
  c.stop_bits = UART_STOP_BITS_1;
  c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  c.source_clk = UART_SCLK_DEFAULT;
  u.uartOn = u.rx && u.frame && uart_driver_install(UART_NUM_1, 1024, 0, 0, NULL, 0) == ESP_OK;
  if (!u.uartOn || uart_param_config(UART_NUM_1, &c) != ESP_OK ||
      uart_set_pin(UART_NUM_1, UART_PIN_NO_CHANGE, PIN_I2S_DIN_4848, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    brUpdEnd(probe ? BU_IDLE : BU_FAIL, "немає пам'яті або UART");
    return false;
  }
  memset(u.rx, 0, sizeof(bp_rx_t));
  u.probe = probe;
  u.off = 0;
  u.tries = 0;
  u.pct = 0;
  u.why = "";
  u.t0 = millis();
  u.startMs = u.t0;
  u.sentMs = 0;
  if (!probe) u.runs++;
  else Serial.println("міст мовчить — питаю, чи не сидить він у завантажувачі");
  txBrMute = true;
  if (probe) u.st = BU_HELLO;
  else {
    static const uint8_t L[4] = { 'L', 'O', 'A', 'D' };
    txBridgeSend(BP_ENTER_LOADER, L, 4);   // программа моста перезапустится в загрузчик (~0,1 с)
    u.st = BU_ENTER;
  }
  return true;
}
// кусок образа: off и до 4 КБ (кратно 256, хвост — 0xFF); кадр собирается прямо в u.frame
static void brUpdSendWrite() {
  BrUpd &u = brUpd;
  uint32_t len = BRIDGE_FW_SIZE - u.off > 4096 ? 4096 : BRIDGE_FW_SIZE - u.off, pad = (len + 255) & ~255u;
  uint8_t *f = u.frame;
  uint16_t L = (uint16_t)(1 + 4 + pad);
  f[0] = BP_SYNC0;
  f[1] = BP_SYNC1;
  f[2] = (uint8_t)L;
  f[3] = (uint8_t)(L >> 8);
  f[4] = BP_WRITE;
  memcpy(f + 5, &u.off, 4);
  memcpy(f + 9, bridgeFw + u.off, len);
  memset(f + 9 + len, 0xFF, pad - len);
  uint16_t c = bp_crc16(f + 2, (size_t)L + 2, 0xFFFF);
  f[5 + 4 + pad] = (uint8_t)c;
  f[6 + 4 + pad] = (uint8_t)(c >> 8);
  hwSerial.write(f, 7 + 4 + pad);
}
static void brUpdTick() {
  BrUpd &u = brUpd;
  uint32_t now = millis();
  if (!brUpdBusy()) {
    bool start = u.ask, probe = false;
    u.ask = false;
    if (!start && cfg.brAutoUpd && cfg.input == IN_I2S && !txPause && now > 15000 && u.runs < 3) {
      if (txBridgeOk() && brUpdVerOther()) start = true;
      else if (!txBridgeOk() && (!txBr.lastMs || now - txBr.lastMs > 20000) && now - u.probeMs > 60000) {
        u.probeMs = now;
        start = probe = true;
      }
    }
    if (start) brUpdBegin(probe);
    return;
  }
  // ответ загрузчика (последний целый кадр за этот шаг)
  uint8_t cmd = 0, d[16];
  if (u.uartOn) {
    uint8_t b[128];
    int n = uart_read_bytes(UART_NUM_1, b, sizeof(b), 0);
    for (int i = 0; i < n; i++)
      if (bp_rx_feed(u.rx, b[i])) {
        cmd = bp_rx_cmd(u.rx);
        memcpy(d, bp_rx_data(u.rx), bp_rx_len(u.rx) < sizeof(d) ? bp_rx_len(u.rx) : sizeof(d));
      }
  }
  switch (u.st) {
    case BU_ENTER:
      if (now - u.t0 > 400) {
        u.st = BU_HELLO;
        u.sentMs = 0;
        u.t0 = now;
      }
      break;
    case BU_HELLO:
      if (cmd == BP_R_HELLO) {   // d: версия загрузчика, программа цела, её версия, размер, CRC-32
        uint32_t ver = brRd32(d + 2);
        bool same = d[1] && ver == ((BRIDGE_FW_VER_MAJOR << 8) | BRIDGE_FW_VER_MINOR) && brRd32(d + 10) == BRIDGE_FW_CRC32;
        if (u.probe && same) {   // мост просто сидел в загрузчике (60 с после неудачного запуска) — отпустить
          static const uint8_t z[1] = { 0 };
          txBridgeSend(BP_BOOT, z, 0);
          brUpdEnd(BU_IDLE, "");
          break;
        }
        if (u.probe) u.runs++;
        u.probe = false;
        u.st = BU_WRITE;
        u.off = 0;
        u.tries = 0;
        u.sentMs = 0;
        u.t0 = now;
        Serial.printf("міст: завантажувач %u відповів (програма %s, версія %u.%02u) — записую %u.%02u\n", d[0], d[1] ? "ціла" : "НЕ ціла",
                      (unsigned)(ver >> 8), (unsigned)(ver & 0xFF), BRIDGE_FW_VER_MAJOR, BRIDGE_FW_VER_MINOR);
      } else if (now - u.t0 > (u.probe ? 1500u : 5000u)) {
        brUpdEnd(u.probe ? BU_IDLE : BU_FAIL, "завантажувач моста не відповів");
      } else if (!u.sentMs || now - u.sentMs > 250) {
        txBridgeSend(BP_HELLO, nullptr, 0);
        u.sentMs = now;
      }
      break;
    case BU_WRITE:
      if (cmd == BP_R_ACK && d[0] == BP_WRITE && brRd32(d + 2) == u.off) {
        if (d[1] == BP_ACK_OK) {
          u.off += 4096;
          u.tries = 0;
          u.sentMs = 0;
          u.pct = (uint8_t)(u.off >= BRIDGE_FW_SIZE ? 99 : (uint64_t)u.off * 99 / BRIDGE_FW_SIZE);
          if (u.off >= BRIDGE_FW_SIZE) u.st = BU_FINISH;
        } else if (++u.tries > 3) brUpdEnd(BU_FAIL, "міст не записав шматок (флеш)");
        else u.sentMs = 0;
      } else if (u.sentMs && now - u.sentMs > 1500) {   // кусок 4 КБ идёт ~0,36 с; ответа нет — ещё раз
        if (++u.tries > 3) brUpdEnd(BU_FAIL, "міст не підтверджує запис");
        else u.sentMs = 0;
      }
      if (u.st == BU_WRITE && !u.sentMs) {
        brUpdSendWrite();
        u.sentMs = millis() | 1;
      }
      break;
    case BU_FINISH:
      if (cmd == BP_R_ACK && d[0] == BP_FINISH) {
        if (d[1] == BP_ACK_OK) {
          u.st = BU_BOOT;
          u.sentMs = 0;
        } else brUpdEnd(BU_FAIL, d[1] == BP_ACK_CRC ? "CRC записаного не зійшовся" : "міст не прийняв завершення");
      } else if (!u.sentMs || now - u.sentMs > 3000) {
        if (u.sentMs && ++u.tries > 3) {
          brUpdEnd(BU_FAIL, "міст не відповів на завершення");
          break;
        }
        uint8_t f[10];
        uint32_t size = BRIDGE_FW_SIZE, crc = BRIDGE_FW_CRC32;
        uint16_t ver = (BRIDGE_FW_VER_MAJOR << 8) | BRIDGE_FW_VER_MINOR;
        memcpy(f, &size, 4);
        memcpy(f + 4, &crc, 4);
        memcpy(f + 8, &ver, 2);
        txBridgeSend(BP_FINISH, f, 10);
        u.sentMs = now;
      }
      break;
    case BU_BOOT:
      if (cmd == BP_R_ACK && d[0] == BP_BOOT) {
        brUpdUartOff();   // дальше на выводе 40 — снова звук: программа моста запускается
        u.st = BU_APP;
        u.t0 = now;
      } else if (!u.sentMs || now - u.sentMs > 500) {
        if (u.sentMs && ++u.tries > 6) {
          brUpdEnd(BU_FAIL, "міст не запустив програму");
          break;
        }
        txBridgeSend(BP_BOOT, nullptr, 0);
        u.sentMs = now;
      }
      break;
    case BU_APP:   // ждём стан от новой программы
      if (txBridgeOk() && txBr.lastMs - u.t0 < 0x80000000u && !brUpdVerOther()) {
        u.pct = 100;
        brUpdEnd(BU_DONE, "");
      } else if (now - u.t0 > 8000) brUpdEnd(BU_FAIL, "міст не озвався після оновлення");
      break;
  }
}

// Главный цикл: разобрать обратный канал и раз в секунду (или сразу после смены входа) напомнить мосту, какой вход играть.
static void txBridgeTick() {
  static uint8_t enc[64];
  static int en = -1;   // −1 — ждём нуля-разделителя (начало потока или потеря)
  uint32_t put = brPut, got = brGot;
  if (put - got > BR_RING) {   // главный цикл отстал больше чем на 128 мс — пропущенное не разобрать
    brLost = brLost + (put - got - BR_RING);
    got = put - BR_RING;
    en = -1;
  }
  while (got != put) {
    uint8_t b = brRing[got & (BR_RING - 1)];
    got++;
    if (!b) {
      if (en > 0) txBridgeFrame(enc, en);
      en = 0;
    } else if (en >= 0 && en < (int)sizeof(enc)) enc[en++] = b;
    else en = -1;
  }
  brGot = got;
  brUpdTick();
  if (brUpdBusy()) return;   // идёт обновление: мосту — только его кадры
  static uint32_t lastSet;
  uint32_t now = millis();
  if (txBr.kick || now - lastSet >= 1000) {
    txBr.kick = false;
    lastSet = now;
    uint8_t d[4] = { (uint8_t)(cfg.bridgeSrc == BP_SRC_AES ? BP_SRC_AES : BP_SRC_PCM), BP_FMT_LJ, txBr.gen, ++txBr.seq };
    txBridgeSend(BP_SET, d, sizeof(d));
  }
}

// Порт: C — стан моста; C1 — цифровой вход, C2 — аналоговый; C9 — обновить мост сейчас; проверочный сигнал моста вместо
// входа (с моста 1.02): C3 — выключить, C4 — тон 1 кГц, C5 — розовый шум, C6 — развёртка 20 Гц → 16 кГц.
static void txBridgePrint() {
  static const char *const ACT[] = { "нічого", "цифровий вхід (AES)", "аналоговий вхід (PCM1808)", "тиша", "перевірочний сигнал моста", "перемикання" };
  static const char *const GEN[] = { "немає", "тон 1 кГц", "рожевий шум", "розгортка 20 Гц – 16 кГц" };
  static const char *const AES[] = { "немає сигналу", "ловлю", "є", "частота, якої міст не бере" };
  uint32_t now = millis();
  Serial.printf("міст: обрано %s; перевірочний сигнал: %s; кадрів стану %u, зіпсованих %u, байтів зворотного каналу втрачено %u, команд надіслано %u\n",
                cfg.bridgeSrc == BP_SRC_AES ? "цифровий вхід" : "аналоговий вхід", GEN[txBr.gen <= BP_GEN_SWEEP ? txBr.gen : 0], (unsigned)txBr.frames,
                (unsigned)txBr.bad, (unsigned)brLost, (unsigned)txBr.sent);
  {
    static const char *const ST[] = { "немає", "у завантажувач", "чекаю завантажувач", "записую", "перевіряю", "запускаю", "чекаю програму", "зроблено", "НЕ ВДАЛОСЯ" };
    Serial.printf("  вбудована програма моста %u.%02u (%u байт, CRC-32 %08x); оновлювати сам: %s; оновлення: %s%s%s, %u %%, запусків %u\n",
                  BRIDGE_FW_VER_MAJOR, BRIDGE_FW_VER_MINOR, (unsigned)BRIDGE_FW_SIZE, (unsigned)BRIDGE_FW_CRC32, cfg.brAutoUpd ? "так" : "ні",
                  ST[brUpd.st <= BU_FAIL ? brUpd.st : 0], brUpd.why[0] ? " — " : "", brUpd.why, brUpd.pct, brUpd.runs);
  }
  if (!txBr.lastMs) {
    Serial.println("  міст МОВЧИТЬ: жодного кадру стану (не підключений, не прошитий або без живлення)");
    return;
  }
  const bp_status_t &s = txBr.st;
  Serial.printf("  %s; версія %u.%02u (завантажувач %u), працює %u с; звучить: %s; AES: %s %u Гц, помилок парності %u\n",
                txBridgeOk() ? "на зв'язку" : "НЕ НА ЗВ'ЯЗКУ", s.verMajor, s.verMinor, s.loaderVer, (unsigned)s.uptime,
                ACT[s.act <= BP_ACT_SWITCH ? s.act : 0], AES[s.aes <= BP_AES_RATE ? s.aes : 0], (unsigned)s.aesRate, (unsigned)s.aesParity);
  Serial.printf("  пік Л −%.1f П −%.1f дБ; запас %d, поправка %+.1f ppm, провали %u/%u; перескоки такту %u; ядро звуку %u‰; "
                "остання команда №%u (надіслано №%u); стан %u мс тому\n",
                s.peakL / 2.0, s.peakR / 2.0, s.level, s.ppm10 / 10.0, s.under, s.over, s.resync, s.cpu1, s.seq, txBr.seq,
                (unsigned)(now - txBr.lastMs));
}
