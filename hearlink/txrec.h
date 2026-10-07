// Запись эфира на карту памяти (модуль ESP32-4848S040: карта на SPI — SCK 48, MOSI 47, MISO 41, CS 42).
//
// Всё, что уходит в эфир, складывается в кольцо в PSRAM; отдельная задача дописывает его в файл WAV
// (32 кГц, моно, 16 бит — около 230 МБ в час). Заголовок файла обновляется каждые пять секунд, поэтому
// при внезапном отключении питания пропадает не больше этих секунд.
// Запись проверена на карте владельца 06.10 (4 ГБ, FAT32): файл /ZAPYS/zapys-0001.wav, 12,2 с — см. ЖУРНАЛ.md.
// Причину, по которой карты нет, видно на плитке «Запис на картку» и по команде D в порту.
// Эта же задача выполняет просьбы файлового менеджера (txfiles.h): с картой работает она одна.
#pragma once
#include <SPI.h>
#include <SD.h>
#include "sd_diskio.h"
#include "diskio.h"
#include "txaudio.h"

#define REC_RING 262144   // отсчётов в кольце (8 секунд; было 2 — медленная карта задумывается дольше)
static SPIClass recSpi(FSPI);
static int16_t *recRing;
static volatile uint32_t recHead, recTail, recLost;
static volatile bool recSd, recOn, recWantStart, recWantStop;
static void (*cardExtra)();   // добавочная работа с картой (делает задача карты, когда запись не идёт)
static volatile uint32_t recSamples, recFreeMb;
static char recName[40];
// Что с картой, когда она не подключилась (владелец 06.10: «карта памяти не определяется (сейчас вставлена)»).
// Раньше любая неудача показывалась как «картки немає». Теперь различаем: карта не отвечает вовсе (нет её, нет
// контакта) — или отвечает, но на ней не та файловая система. Карты на 64 ГБ и больше с завода размечены в exFAT,
// а в этой сборке ядра exFAT выключен (FF_FS_EXFAT 0) — такую карту надо переформатировать в FAT32.
enum { CARD_ABSENT = 0, CARD_OK = 1, CARD_BAD_FS = 2, CARD_NO_INIT = 3 };
static volatile uint8_t recCard;          // CARD_…
static volatile uint32_t recCardMb;       // объём карты, МБ (когда она отвечает)
static char recCardFs[12] = "";           // что на ней за разметка: "exFAT", "FAT32", "NTFS/exFAT", "GPT", "невідома"
static volatile uint32_t recSpiHz = 16000000;   // скорость обмена с картой (с порта: D4, D8, D16 … — для сравнения)
static volatile bool recRemount;          // с порта сменили скорость — подключить карту заново
// почему и как оборвалась запись (06.10: файлы обрывались сами через 12 с и через 0,3 с — ошибка записи на карту)
static volatile uint32_t recWrMaxMs, recWrN, recFails, recFailAt, recFailMs;
static volatile int recFailErrno;
static volatile bool recPattern;          // проверка записи: вместо звука в файл идёт счётчик отсчётов (команда порта Z2), сверка — Zv
static volatile bool recNoCache;          // не копить секторы (Z3 — для сравнения)
static volatile bool recFull;             // на карте не осталось места: запись не начата или остановлена сама
static volatile uint32_t recMemBefore, recMemAfter;   // внутренняя память до и после подключения карты, байт
static volatile bool recListAsk;          // с порта попросили список записей (команда Z)
#define REC_MIN_FREE_MB 6                 // меньше — не писать: файлу нужно место на заголовок и хвост
static volatile bool recProbeAsk;         // с порта попросили проверить карту сейчас (команда D)

// Один запрос карте «вручную» (без библиотеки): так видно, отвечает ли она вообще. Ответ 0xFF — молчит.
static uint8_t recRawCmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *extra, int n) {
  recSpi.transfer(0xFF);
  recSpi.transfer(0x40 | cmd);
  recSpi.transfer(arg >> 24);
  recSpi.transfer(arg >> 16);
  recSpi.transfer(arg >> 8);
  recSpi.transfer(arg);
  recSpi.transfer(crc);
  uint8_t r = 0xFF;
  for (int i = 0; i < 12 && (r & 0x80); i++) r = recSpi.transfer(0xFF);
  for (int i = 0; i < n; i++) extra[i] = recSpi.transfer(0xFF);
  return r;
}
static volatile uint8_t recRaw0 = 0xFF, recRaw8 = 0xFF;   // ответы карты на первый запрос (сброс) и на второй (напряжение)
static uint8_t recRaw7[4];
static volatile uint8_t recMisoIdle;                      // уровень на проводе данных от карты в покое (с подтяжкой вниз / вверх)

// Тот же запрос, но «руками» — без блока SPI в процессоре: уровни на проводах выставляет и читает сама программа.
// Разводит две причины молчания: «блок SPI не доходит до выводов» и «карта действительно не отвечает».
static uint8_t recBbByte(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(47, (out >> i) & 1);
    delayMicroseconds(3);
    digitalWrite(48, HIGH);
    delayMicroseconds(3);
    in = (in << 1) | (digitalRead(41) ? 1 : 0);
    digitalWrite(48, LOW);
  }
  return in;
}
static uint8_t recBbResp[8];              // что карта прислала после запроса сброса (первые 8 байт)
static volatile uint8_t recBbR1 = 0xFF;   // ответ на сброс при «ручном» обмене
static volatile uint8_t recBbPins;        // биты: 0 — такт (48) поднимается, 1 — опускается, 2/3 — то же для данных (47),
                                          // 4 — линия от карты (41) с подтяжкой вниз в покое, 5 — она же при выбранной карте
static volatile bool recSpiOk;            // блок SPI запустился
static volatile bool recBbAsk;

static void recBbProbe() {
  recSpi.end();
  pinMode(42, OUTPUT);
  digitalWrite(42, HIGH);
  pinMode(48, OUTPUT);
  pinMode(47, OUTPUT);
  uint8_t pins = 0;
  digitalWrite(48, HIGH); delayMicroseconds(5); if (digitalRead(48)) pins |= 1;
  digitalWrite(48, LOW);  delayMicroseconds(5); if (!digitalRead(48)) pins |= 2;
  digitalWrite(47, LOW);  delayMicroseconds(5); if (!digitalRead(47)) pins |= 8;
  digitalWrite(47, HIGH); delayMicroseconds(5); if (digitalRead(47)) pins |= 4;
  pinMode(41, INPUT_PULLDOWN);
  delay(2);
  if (digitalRead(41)) pins |= 16;
  for (int i = 0; i < 20; i++) recBbByte(0xFF);   // 160 тактов при невыбранной карте
  digitalWrite(42, LOW);
  recBbByte(0xFF);
  if (digitalRead(41)) pins |= 32;
  pinMode(41, INPUT_PULLUP);
  delay(1);
  static const uint8_t C0[6] = { 0x40, 0, 0, 0, 0, 0x95 };
  for (int i = 0; i < 6; i++) recBbByte(C0[i]);
  uint8_t r1 = 0xFF;
  for (int i = 0; i < 8; i++) {
    recBbResp[i] = recBbByte(0xFF);
    if (r1 == 0xFF && recBbResp[i] != 0xFF) r1 = recBbResp[i];
  }
  digitalWrite(42, HIGH);
  recBbByte(0xFF);
  recBbR1 = r1;
  recBbPins = pins;
  recSpiOk = recSpi.begin(48, 41, 47, 42);
}

// Была и третья проверка — «родной» режим карты (SD, одна линия данных) через блок SDMMC. Она тоже показала молчание
// (06.10), но УБРАНА: sdmmc_host_init_slot при одной линии данных сам ставит «свой» вывод D3 выходом в единицу, а по
// умолчанию это GPIO13 — у этого модуля он несёт красный цвет экрана. Экран стал красным до перезапуска.
// Вывод: блок SDMMC на этом модуле не трогать, не задав ВСЕ его выводы явно (d1…d7 = −1).

// Карта не подключилась — выяснить почему. Вызывать при закрытой SD (после SD.end()).
static void recProbe() {
  recCard = CARD_ABSENT;
  recCardMb = 0;
  recCardFs[0] = 0;
  // 1) отвечает ли карта: 80 тактов «вхолостую», затем сброс (ответ должен быть 0x01)
  pinMode(42, OUTPUT);
  digitalWrite(42, HIGH);
  recSpi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 20; i++) recSpi.transfer(0xFF);
  digitalWrite(42, LOW);
  uint8_t r0 = recRawCmd(0, 0, 0x95, NULL, 0), r8 = 0xFF;
  digitalWrite(42, HIGH);
  recSpi.transfer(0xFF);
  if (r0 == 0x01) {
    digitalWrite(42, LOW);
    r8 = recRawCmd(8, 0x1AA, 0x87, recRaw7, 4);
    digitalWrite(42, HIGH);
    recSpi.transfer(0xFF);
  }
  recSpi.endTransaction();
  recRaw0 = r0;
  recRaw8 = r8;
  if (r0 != 0x01) return;   // молчит (или отвечает не то) — карты нет, нет контакта, либо она не для этого гнезда
  uint8_t pd = sdcard_init(42, &recSpi, 4000000);
  if (pd == 0xFF) return;
  if (!(disk_initialize(pd) & STA_NOINIT)) {      // карта ответила и запустилась
    uint32_t sec = sdcard_num_sectors(pd), ss = sdcard_sector_size(pd);
    recCardMb = (uint32_t)(((uint64_t)sec * (ss ? ss : 512)) >> 20);
    static uint8_t b[512];
    strlcpy(recCardFs, "невідома", sizeof(recCardFs));
    if (disk_read(pd, b, 0, 1) == RES_OK && b[510] == 0x55 && b[511] == 0xAA) {
      uint8_t pt = b[450];                         // вид первого раздела в таблице
      if (!memcmp(b + 3, "EXFAT   ", 8)) strlcpy(recCardFs, "exFAT", sizeof(recCardFs));
      else if (!memcmp(b + 82, "FAT32", 5)) strlcpy(recCardFs, "FAT32", sizeof(recCardFs));
      else if (pt == 0x07) strlcpy(recCardFs, "exFAT/NTFS", sizeof(recCardFs));
      else if (pt == 0x0B || pt == 0x0C) strlcpy(recCardFs, "FAT32", sizeof(recCardFs));
      else if (pt == 0xEE) strlcpy(recCardFs, "GPT", sizeof(recCardFs));
    }
    recCard = CARD_BAD_FS;
  } else recCard = CARD_NO_INIT;   // на сброс ответила, а дальше не запустилась
  sdcard_uninit(pd);
}

static volatile uint32_t recPatN;   // счётчик проверочного узора: идёт и тогда, когда кусок не поместился (так в файле видно, сколько потеряно)
static void recOnFrame(const int16_t *pcm) {
  if (!recOn) return;
  uint32_t h = recHead, pn = recPatN;
  recPatN = pn + FRAME;
  if (h - recTail > REC_RING - FRAME) {   // карта не успевает
    recLost = recLost + FRAME;
    return;
  }
  if (recPattern)
    for (int i = 0; i < FRAME; i++) recRing[(h + i) & (REC_RING - 1)] = (int16_t)(pn + i);
  else
    for (int i = 0; i < FRAME; i++) recRing[(h + i) & (REC_RING - 1)] = pcm[i];
  recHead = h + FRAME;
}

// ---- терпеливая работа с картой
// Библиотека SD ждёт карту не дольше 0,5 с на каждом шаге и при первой же заминке возвращает ошибку. После ошибки
// система файлов считает файл испорченным — запись обрывалась сама: у карты владельца (старая, почти полная, 4 ГБ)
// первый файл оборвался через 12 с, второй через 0,3 с, третий через 3 с (06.10; одна запись куска шла 824 мс).
// Поэтому между системой файлов и библиотекой стоит прослойка: при ошибке ждём, пока карта освободится (до 4 с),
// и повторяем ту же работу — до пяти раз. Функции ff_sd_… — из sd_diskio.cpp библиотеки (в заголовке их нет).
extern "C" {
#include "ff.h"
#include "diskio_impl.h"
}
DSTATUS ff_sd_initialize(uint8_t pdrv);
DSTATUS ff_sd_status(uint8_t pdrv);
DRESULT ff_sd_read(uint8_t pdrv, uint8_t *buffer, DWORD sector, UINT count);
DRESULT ff_sd_write(uint8_t pdrv, const uint8_t *buffer, DWORD sector, UINT count);
DRESULT ff_sd_ioctl(uint8_t pdrv, uint8_t cmd, void *buff);
static volatile uint32_t sdRetries, sdGiveUps, sdSettleMaxMs, sdOps;
static volatile uint32_t sdN1, sdUs1, sdNm, sdSecM, sdUsM, sdMaxUs;   // записи по одному сектору и по несколько: число, время; самая долгая
static volatile uint32_t recClusterKb;                                 // размер кластера на карте, КБ
// Дождаться, пока карта освободится. stop — сорвалась запись нескольких секторов подряд: карта могла остаться в приёме
// данных, тогда после ожидания ей шлём знак конца записи и ждём ещё раз.
static void sdSettle(uint32_t maxMs, bool stop) {
  uint32_t t0 = millis();
  recSpi.beginTransaction(SPISettings(recSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(42, LOW);
  for (int pass = 0; pass < (stop ? 2 : 1); pass++) {
    while (recSpi.transfer(0xFF) != 0xFF && millis() - t0 < maxMs) vTaskDelay(1);
    if (stop && !pass) {
      recSpi.transfer(0xFD);
      recSpi.transfer(0xFF);
    }
  }
  digitalWrite(42, HIGH);
  recSpi.transfer(0xFF);
  recSpi.endTransaction();
  uint32_t d = millis() - t0;
  if (d > sdSettleMaxMs) sdSettleMaxMs = d;
}
// ---- накопитель записи
// Система файлов отдаёт карте звук по одному кластеру (у карты владельца кластер 4 КБ), и каждое такое обращение карта
// обдумывает ~55 мс, сколько бы байт в нём ни было: выходит 73 КБ/с при нужных 64 — запись отставала и теряла звук
// (06.10). Пока идёт запись, идущие подряд секторы копятся здесь (до 64 КБ) и уходят на карту одним обращением.
// Порядок не нарушается: всё, что не продолжает накопленное (другое место, чтение этих секторов, «сохранить»),
// сначала сбрасывает накопленное на карту. Вне записи накопитель выключен.
#define SDW_MAX 128                        // секторов (64 КБ)
static uint8_t *sdwBuf;
static uint32_t sdwStart, sdwN;
static volatile bool sdwOn;
static volatile uint32_t sdwFlushes;
static DRESULT sdxWriteNow(unsigned char pdrv, const unsigned char *buf, uint32_t sector, unsigned count);
static DRESULT sdwFlush(unsigned char pdrv) {
  if (!sdwN) return RES_OK;
  uint32_t n = sdwN;
  sdwN = 0;
  sdwFlushes = sdwFlushes + 1;
  return sdxWriteNow(pdrv, sdwBuf, sdwStart, n);
}
static DRESULT sdxWrite(unsigned char pdrv, const unsigned char *buf, uint32_t sector, unsigned count) {
  if (sdwN && (!sdwOn || sector != sdwStart + sdwN || sdwN + count > SDW_MAX)) {
    DRESULT r = sdwFlush(pdrv);
    if (r != RES_OK) return r;
  }
  if (sdwOn && sdwBuf && count < SDW_MAX) {
    if (!sdwN) sdwStart = sector;
    memcpy(sdwBuf + sdwN * 512, buf, count * 512);
    sdwN += count;
    return RES_OK;
  }
  return sdxWriteNow(pdrv, buf, sector, count);
}
static DRESULT sdxWriteNow(unsigned char pdrv, const unsigned char *buf, uint32_t sector, unsigned count) {
  sdOps = sdOps + 1;
  uint32_t t0 = micros();
  for (int tr = 0;; tr++) {
    DRESULT r = ff_sd_write(pdrv, buf, sector, count);
    if (r != RES_ERROR) {
      uint32_t d = micros() - t0;
      if (d >= 20000) bbMark(BB_CARD, 1, d / 1000);
      if (count == 1) {
        sdN1 = sdN1 + 1;
        sdUs1 = sdUs1 + d;
      } else {
        sdNm = sdNm + 1;
        sdSecM = sdSecM + count;
        sdUsM = sdUsM + d;
      }
      if (d > sdMaxUs) sdMaxUs = d;
      return r;
    }
    if (tr >= 5) {
      sdGiveUps = sdGiveUps + 1;
      return r;
    }
    sdRetries = sdRetries + 1;
    sdSettle(4000, count > 1);
  }
}
static DRESULT sdxRead(unsigned char pdrv, unsigned char *buf, uint32_t sector, unsigned count) {
  if (sdwN && sector < sdwStart + sdwN && sector + count > sdwStart) {   // читают то, что ещё не на карте
    DRESULT r = sdwFlush(pdrv);
    if (r != RES_OK) return r;
  }
  for (int tr = 0;; tr++) {
    DRESULT r = ff_sd_read(pdrv, buf, sector, count);
    if (r != RES_ERROR) return r;
    if (tr >= 5) {
      sdGiveUps = sdGiveUps + 1;
      return r;
    }
    sdRetries = sdRetries + 1;
    sdSettle(4000, false);
  }
}
static DRESULT sdxIoctl(unsigned char pdrv, unsigned char cmd, void *buff) {
  if (cmd == CTRL_SYNC && sdwN) {
    DRESULT r = sdwFlush(pdrv);
    if (r != RES_OK) return r;
  }
  for (int tr = 0;; tr++) {
    DRESULT r = ff_sd_ioctl(pdrv, cmd, buff);
    if (r != RES_ERROR || cmd != CTRL_SYNC) return r;
    if (tr >= 5) {
      sdGiveUps = sdGiveUps + 1;
      return r;
    }
    sdRetries = sdRetries + 1;
    sdSettle(4000, false);
  }
}
static DSTATUS sdxInit(unsigned char pdrv) {
  return ff_sd_initialize(pdrv);
}
static DSTATUS sdxStatus(unsigned char pdrv) {
  return ff_sd_status(pdrv);
}
static const ff_diskio_impl_t sdxImpl = { sdxInit, sdxStatus, sdxRead, sdxWrite, sdxIoctl };
// Номер диска, под которым библиотека подключила карту (поле защищённое — достаём через наследника).
static uint8_t sdPdrv() {
  struct Peek : fs::SDFS {
    static uint8_t get(fs::SDFS &s) {
      return s.*(&Peek::_pdrv);
    }
  };
  return Peek::get(SD);
}
// Подключить карту и поставить прослойку. Вызывать вместо SD.begin.
static bool sdMount() {
  sdwN = 0;
  sdwOn = false;
  if (!sdwBuf) sdwBuf = (uint8_t *)heap_caps_malloc(SDW_MAX * 512, MALLOC_CAP_SPIRAM);
  if (!SD.begin(42, recSpi, recSpiHz, "/sd", 3)) return false;   // открытых файлов не больше трёх: запись и два для копирования
  uint8_t pd = sdPdrv();
  if (pd < FF_VOLUMES) {
    ff_diskio_register(pd, &sdxImpl);
    char drv[3] = { (char)('0' + pd), ':', 0 };
    FATFS *fs = NULL;
    DWORD fre = 0;
    if (f_getfree(drv, &fre, &fs) == FR_OK && fs) recClusterKb = fs->csize / 2;
  }
  return true;
}

static char logPath[40];   // файл журнала работы этого включения (пусто — ещё не заведён); сам журнал — ниже
#include "txfiles.h"   // файловый менеджер: работа с картой идёт этой же задачей

// Заголовок занимает ровно сектор (512 байт): между описанием записи и самим звуком стоит пустой кусок «JUNK».
// Так звук в файле начинается с границы сектора, и куски звука (по 32 КБ) карта получает целыми секторами, без
// обрезков. С заголовком в 44 байта каждый кусок рвался на два-три отдельных обращения к карте, и медленная карта
// не успевала за звуком (06.10: за минуту потеряно 10 секунд). Проигрыватели кусок «JUNK» пропускают.
#define REC_HDR 512
static void recHeader(File &f, uint32_t samples) {
  static uint8_t h[REC_HDR];
  uint32_t data = samples * 2, riff = data + REC_HDR - 8, rate = SRATE, bps = SRATE * 2, junk = REC_HDR - 12 - 24 - 8 - 8;
  static const uint8_t H0[36] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0 };
  memset(h, 0, sizeof(h));
  memcpy(h, H0, 36);
  memcpy(h + 4, &riff, 4);
  memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &bps, 4);
  memcpy(h + 36, "JUNK", 4);
  memcpy(h + 40, &junk, 4);
  memcpy(h + REC_HDR - 8, "data", 4);
  memcpy(h + REC_HDR - 4, &data, 4);
  uint32_t pos = f.position();
  f.seek(0);
  f.write(h, REC_HDR);
  if (pos > REC_HDR) f.seek(pos);
}

// ---- журнал работы на карту
// Раз в 10 секунд передатчик дописывает в файл /LOG/log-NNNN.txt (новый файл на каждое включение) строку: канал,
// пакеты, отказы, ожидание эфира, прореживание, уровень и по каждому приёмнику — сигнал, потери, провалы, сон.
// Нужен для проверок в настоящем зале, где компьютера рядом нет (владелец 07.10: «сегодня проверка на реальном
// зале»): потом по файлу видно, что происходило. Строку готовит главный цикл (hearlink.ino, logEmit), а пишет
// эта задача — с картой работает она одна. Файл открывается и закрывается на каждую строку: при внезапном
// выключении пропадает не больше последней строки. Час работы — около 100 КБ.
static char logText[900];
static volatile uint32_t logSeq, logDone;   // строк подготовлено / записано
static volatile uint32_t logLines, logFails;
static char *logExtra;                      // добавочный текст в журнал (слепок «чёрного ящика»); память — PSRAM
static volatile uint32_t logExtraSeq, logExtraDone;
static void logAppend(bool extra = false) {
  if (!logPath[0]) {   // первое обращение в этом включении: папка и следующий по счёту файл
    mkdir("/sd/LOG", 0777);
    int n = 1;
    DIR *d = opendir("/sd/LOG");
    if (d) {
      struct dirent *e;
      while ((e = readdir(d))) {
        int k = 0;
        if (sscanf(e->d_name, "log-%d.txt", &k) == 1 && k >= n) n = k + 1;
      }
      closedir(d);
    }
    snprintf(logPath, sizeof(logPath), "/sd/LOG/log-%04d.txt", n);
  }
  FILE *f = fopen(logPath, "a");
  if (f) {
    fputs(extra ? logExtra : logText, f);
    fclose(f);
    logLines = logLines + 1;
  } else {
    logFails = logFails + 1;
    logPath[0] = 0;   // папки нет (карту сменили или отформатировали) — в следующий раз начать заново
  }
  if (extra) logExtraDone = logExtraSeq;
  else logDone = logSeq;
}

#define REC_CHUNK 16384   // отсчётов в одном куске записи: 32 КБ, полсекунды звука
static void recTask(void *) {
  int16_t *chunk = (int16_t *)heap_caps_malloc(REC_CHUNK * 2, MALLOC_CAP_SPIRAM);
  File f;
  uint32_t lastTry = 0, lastHdr = 0;
  recRing = (int16_t *)heap_caps_malloc(REC_RING * 2, MALLOC_CAP_SPIRAM);
  recSpiOk = recSpi.begin(48, 41, 47, 42);
  for (;;) {
    uint32_t now = millis();
    cardTick();   // файл в эфире: время и конец
    if (recSd && fmCardGone) {   // файловый менеджер не смог открыть корень карты — её вынули
      fmCardGone = false;
      if (recOn) recWantStop = true;
      else {
        SD.end();
        recSd = false;
        recCard = CARD_ABSENT;
      }
    }
    if (!recSd) {
      logDone = logSeq;   // карты нет — строку журнала не держим
      logPath[0] = 0;
      if (fmWant) {   // карты нет — просьбе файлового менеджера сразу отказ
        fm.lastOp = fmWant;
        fmWant = 0;
        fm.result = FM_E_NOCARD;
        fm.doneSeq = fm.doneSeq + 1;
      }
      if (recProbeAsk) {
        recProbeAsk = false;
        lastTry = 0;
      }
      if (now - lastTry > 3000 || !lastTry) {
        lastTry = now ? now : 1;
        recMemBefore = ESP.getFreeHeap();
        if (recRing && chunk && sdMount()) {
          recMemAfter = ESP.getFreeHeap();
          recFull = false;
          recFreeMb = (uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20);
          fm.freeMb = recFreeMb;
          fm.totalMb = (uint32_t)(SD.totalBytes() >> 20);
          recCardMb = (uint32_t)(SD.cardSize() >> 20);
          recCard = CARD_OK;
          recSd = true;
        } else {
          SD.end();
          if (recBbAsk) {
            recBbAsk = false;
            recBbProbe();
          }
          recProbe();   // почему не подключилась: нет карты или не та разметка
        }
      }
      recWantStart = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (cardExtra && !recOn) cardExtra();   // обновление с карты (upd.h): поиск файла прошивки в папке UPDATE
    if (logSeq != logDone) logAppend();   // строка журнала работы ждёт записи
    if (logExtraSeq != logExtraDone && logExtra) logAppend(true);
    if (recRemount && !recOn) {   // сменили скорость обмена — подключить карту заново
      recRemount = false;
      SD.end();
      recSd = false;
      lastTry = 0;
      continue;
    }
    if (fmWant) {   // просьба файлового менеджера
      uint8_t op = fmWant;
      fmService();
      if (!recSd) continue;   // форматирование не удалось, карта не подключилась заново
      if (op == FM_MKDIR || op == FM_DELETE || op == FM_COPY) {
        recFreeMb = (uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20);
        fm.freeMb = recFreeMb;
      }
    }
    if (recListAsk) {   // список записей — в порт
      recListAsk = false;
      File d = SD.open("/ZAPYS");
      int n = 0;
      for (File e = d ? d.openNextFile() : File(); e; e = d.openNextFile()) {
        Serial.printf("  %s  %u байт (%.1f с)\n", e.name(), (unsigned)e.size(), e.size() > REC_HDR ? (e.size() - REC_HDR) / 2.0f / SRATE : 0.0f);
        n++;
        e.close();
      }
      if (d) d.close();
      Serial.printf("записів у /ZAPYS: %d; вільно %u МБ\n", n, (unsigned)recFreeMb);
    }
    if (recWantStart && !recOn && recFreeMb < REC_MIN_FREE_MB) {   // места нет — не начинать: иначе файл оборвётся на первой же секунде
      recWantStart = false;
      recFull = true;
    }
    if (recWantStart && !recOn) {
      recWantStart = false;
      recFull = false;
      SD.mkdir("/ZAPYS");
      int n = 1;
      File d = SD.open("/ZAPYS");
      for (File e = d.openNextFile(); e; e = d.openNextFile()) {
        int k = 0;
        if (sscanf(e.name(), "zapys-%d.wav", &k) == 1 && k >= n) n = k + 1;
        e.close();
      }
      d.close();
      snprintf(recName, sizeof(recName), "/ZAPYS/zapys-%04d.wav", n);
      f = SD.open(recName, FILE_WRITE);
      if (f) {
        recHeader(f, 0);
        recSamples = 0;
        recWrMaxMs = 0;
        recWrN = 0;
        recLost = 0;
        recTail = recHead;
        recPatN = 0;
        sdwOn = !recNoCache;
        recOn = true;
        lastHdr = now;
      } else recSd = false;
    }
    if (recOn) {
      uint32_t avail = recHead - recTail;
      bool fail = false;
      while (avail >= REC_CHUNK || (recWantStop && avail)) {
        uint32_t n = avail > REC_CHUNK ? REC_CHUNK : avail, t = recTail;
        for (uint32_t i = 0; i < n; i++) chunk[i] = recRing[(t + i) & (REC_RING - 1)];
        errno = 0;
        uint32_t w0 = millis();
        size_t wr = f.write((const uint8_t *)chunk, n * 2);
        uint32_t wd = millis() - w0;
        recWrN = recWrN + 1;
        if (wd > recWrMaxMs) recWrMaxMs = wd;
        if (wr != n * 2) {
          recFails = recFails + 1;
          recFailErrno = errno;
          recFailAt = recSamples * 2;
          recFailMs = wd;
          fail = true;
          break;
        }
        recTail = t + n;
        recSamples = recSamples + n;
        avail -= n;
      }
      uint32_t usedMb = (uint32_t)(((uint64_t)recSamples * 2) >> 20);
      if (!fail && usedMb + REC_MIN_FREE_MB > recFreeMb) {   // место кончается — закрыть файл как положено, пока он цел
        recWantStop = true;
        recFull = true;
      }
      if (fail || recWantStop) {
        recOn = false;
        recWantStop = false;
        recHeader(f, recSamples);
        f.close();
        sdwOn = false;
        recPattern = false;
        if (fail) {
          SD.end();
          recSd = false;
          recCard = CARD_ABSENT;
        } else {
          recFreeMb = (uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20);
          fm.freeMb = recFreeMb;
        }
      } else if (now - lastHdr > 5000) {
        lastHdr = now;
        recHeader(f, recSamples);
        f.flush();
      }
    } else recWantStop = false;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void recBegin() {
  txOnFrame = recOnFrame;
  xTaskCreatePinnedToCore(recTask, "rec", 12288, NULL, 1, NULL, 1);   // было 8192: файловому менеджеру нужны длинные пути
}
