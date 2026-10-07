// Вывод в порт «с ответвлением»: всё, что прошивка печатает через Serial, идёт и в настоящий порт, и (если включена
// отладка по радио) в очередь строк, которую dbgair.h рассылает остальным устройствам набора.
//
// Зачем (владелец 07.10): «чтобы можно вести отладку приемников на расстоянии … данная функция опционально должна
// включаться в настройках»; «это нужно чтобы избавиться от постоянного подключения приемников к машине».
// Устройство набора, подключённое к компьютеру (обычно передатчик), показывает в порту строки остальных, а команда
// вида «@884A94 ?» уходит названному устройству по радио и выполняется там (см. dbgair.h).
//
// Подключать сразу после config.h и до остальных файлов прошивки: ниже имя Serial подменяется на hlSerial.
#pragma once
#include <Arduino.h>
#include "esp_heap_caps.h"

static HardwareSerial &hwSerial = Serial;   // настоящий порт — для двоичного обмена (приём файла) и чужих строк

// Когда с порта последний раз что-то приходило (кто-то сидит за компьютером и смотрит). Сам по себе приёмник в порт
// не печатает: на платах S3 у моста USB стоит зелёный светодиод передачи, и он вспыхивал от каждой строки отчёта —
// раз в секунду (владелец 07.10: «в приемниках во время спячки ничего не светится и не моргает, пока не проснется»;
// «зелёный светодиод отключить, он уже не нужен, у нас работает rgb»). В порт приёмник пишет, только пока с порта
// что-то приходит (любой знак, хоть пустая строка) и ещё две минуты после: наблюдатели в tools/ шлют пустую строку
// сами. При включении плата всё же печатает несколько строк (загрузчик процессора и одна строка прошивки).
static volatile uint32_t consoleMs;
static bool consoleLive() {
  return consoleMs && millis() - consoleMs < 120000;
}

#define DBG_LINE 200   // байт текста в одном пакете
#define DBG_Q 8        // строк в очереди на отправку (не поместилось — строка пропадает, порт от этого не страдает)
struct DbgLine {
  uint8_t len, more;   // more — строка длиннее пакета, продолжение в следующем
  char text[DBG_LINE];
};
static DbgLine *dbgOut;                    // очередь на отправку (во внешней памяти)
static volatile uint8_t dbgHead, dbgTail;
static char dbgCur[DBG_LINE + 4];
static uint8_t dbgCurN;
static portMUX_TYPE dbgMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t dbgAirUntil;      // приёмник: до какого времени слать строки по радио (продлевает маяк передатчика)
static volatile uint32_t dbgDropped;

static bool dbgAirOn() {
  return cfg.isTx ? cfg.dbgAir != 0 : (dbgAirUntil && (int32_t)(dbgAirUntil - millis()) > 0);
}
static void dbgEmit(uint8_t n, uint8_t more) {   // вызывать под dbgMux
  uint8_t next = (dbgHead + 1) % DBG_Q;
  if (!n) return;
  if (next == dbgTail) {
    dbgDropped = dbgDropped + 1;
    return;
  }
  dbgOut[dbgHead].len = n;
  dbgOut[dbgHead].more = more;
  memcpy(dbgOut[dbgHead].text, dbgCur, n);
  dbgHead = next;
}
static void dbgCapture(const uint8_t *b, size_t n) {
  if (!dbgOut || !dbgAirOn()) return;
  portENTER_CRITICAL(&dbgMux);
  for (size_t i = 0; i < n; i++) {
    uint8_t c = b[i];
    if (c == '\r') continue;
    if (c == '\n') {
      dbgEmit(dbgCurN, 0);
      dbgCurN = 0;
      continue;
    }
    if (dbgCurN >= DBG_LINE) {   // строка длиннее пакета: отдать, сколько есть, не разрывая букву
      uint8_t k = dbgCurN;
      while (k > 0 && ((uint8_t)dbgCur[k - 1] & 0xC0) == 0x80) k--;
      if (k > 0 && ((uint8_t)dbgCur[k - 1] & 0xC0) == 0xC0) k--;
      if (!k) k = dbgCurN;
      dbgEmit(k, 1);
      memmove(dbgCur, dbgCur + k, dbgCurN - k);
      dbgCurN -= k;
    }
    dbgCur[dbgCurN++] = (char)c;
  }
  portEXIT_CRITICAL(&dbgMux);
}

class TeeSerial : public Stream {
 public:
  void begin(unsigned long baud) { hwSerial.begin(baud); }
  size_t setRxBufferSize(size_t n) { return hwSerial.setRxBufferSize(n); }
  void updateBaudRate(unsigned long baud) { hwSerial.updateBaudRate(baud); }
  void setTimeout(unsigned long ms) { hwSerial.setTimeout(ms); }
  size_t readBytes(uint8_t *b, size_t n) { return hwSerial.readBytes(b, n); }
  int available() override { return hwSerial.available(); }
  int read() override { return hwSerial.read(); }
  int peek() override { return hwSerial.peek(); }
  void flush() override { hwSerial.flush(); }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    // передатчик пишет в порт всегда; приёмник — пока его слушают, и первые секунды после включения
    if (cfg.isTx || consoleLive() || millis() < 4000) hwSerial.write(b, n);
    dbgCapture(b, n);
    return n;
  }
  using Print::write;
};
static TeeSerial hlSerial;
#define Serial hlSerial
