// Передатчик: мелодии в MP3 разбираются на лету; здесь же — файл с карты памяти, пущенный в эфир.
//
// Мелодии лежат в разделе данных как есть — MP3, стерео (см. tools/make_sounds.py). Разбирает их отдельная
// задача на втором ядре (декодер Helix входит в ядро Arduino для ESP32) и складывает оба канала в запас
// на четверть секунды; задача передачи звука только забирает оттуда готовые отсчёты и никогда не ждёт.
// Одновременно играет одна мелодия (голос — запись без сжатия, ему декодер не нужен).
//
// Файл с карты (mpItem == −2) идёт тем же путём: WAV читается как есть, MP3 — через тот же декодер. Частота у файлов
// любая (8…96 кГц), а эфир — 32 кГц, поэтому здесь же стоит пересчёт частоты (rs…). Запас для карты свой, больше
// (полсекунды, во внешней памяти): карта отвечает неравномерно, особенно когда на неё же идёт запись.
#pragma once
#include "mp3dec.h"
#include "esp_heap_caps.h"
#include "assets.h"
#include "fm.h"
#include "cardfmt.h"
#include <unistd.h>

#define MP3_RING 32768                      // пар отсчётов в запасе мелодии: 1 с (было 0,13 с); степень двойки
#define CARD_RING 65536                     // запас для файла с карты: 2 с (было 0,5 с). Когда на ту же карту идёт запись,
                                            // карта владельца задумывается на 1–2 с, и чтение в это время стоит (07.10: 0,24 с пропусков
                                            // в звуке файла за 40 с при запасе 0,5 с)
#define CARD_PREFILL 16000                  // звук файла начинается, когда в запасе набралось полсекунды
static int16_t (*mpRing)[2];                // во внешней памяти, выделяет задача разбора при запуске
// Разбор и экран живут на одном ядре. Раньше разбор был всегда важнее экрана, а запаса звука хватало на 0,13 с:
// кадр MP3 (12–23 мс счёта) каждый раз останавливал меню, а на перелистывании страницы, когда ядро почти целиком
// занято выводом кадра, разбор не успевал сам и не давал экрану — перелистывание шло 0,4–1,2 с с повторами кадров
// вместо 0,28 с, и мелодия при этом прерывалась (07.10: недоборов 14 912). Теперь запас — секунда, и:
//  — пока запас больше 3/4, разбор идёт с низким приоритетом (экрану не мешает);
//  — упал ниже 0,45 — приоритет выше экрана, пока не наберётся снова;
//  — на время перелистывания (mpQuiet) разбор стоит вовсе, если в запасе есть хотя бы 0,2 с.
static volatile bool mpQuiet;               // экран просит не мешать (перелистывание страницы)
static volatile uint8_t mpPrio = 3;         // нынешний приоритет задачи разбора (для отчёта)
static int16_t (*cpRing)[2];                // выделяется при первом файле
static volatile bool mpCardNow;             // запас сейчас — карточный (ставит задача разбора до того, как объявит новую запись)
static volatile uint32_t mpHead, mpTail;    // пишет задача разбора, читает задача передачи
static volatile uint32_t mpWantGen, mpDecGen;   // какую по счёту мелодию попросили и какую начали разбирать
static volatile int mpItem = -1;            // номер мелодии в разделе данных; −1 — ничего; −2 — файл с карты (cpPath)
static volatile bool mpDone;                // мелодия разобрана до конца
static bool mpStarted;                      // первый отсчёт мелодии уже отдан (пауза до него недобором не считается)
// для отчёта
static volatile uint32_t mpUnder, mpErrors, mpFrames, mpUsSum, mpUsMax;
static volatile uint32_t mpRate, mpKbps;
static volatile uint8_t mpChans;
// файл с карты
static char cpPath[FM_PATH + 8];            // что открыть (полный путь; пишет тот, кто просит, до смены mpWantGen)
static FILE *cpF;                           // открытый файл (трогает только задача разбора)
static volatile uint8_t cpErr;              // 3 — файл не открылся или не читается
static volatile uint8_t cpChans;            // каналов в файле (1 или 2) — от этого зависит, идти ли в эфир стерео
static volatile uint32_t cpReadUs, cpRsUs;  // для отчёта: сколько времени ушло на чтение карты и на пересчёт частоты
// Чтение файла — крупными кусками (16 КБ) в свой буфер во внешней памяти: карта владельца на каждое обращение
// тратит заметное время, и чтение по 1–2 КБ на каждый кадр MP3 отнимало у разбора то, чего ему и так не хватало.
// Читать надо мимо обычных fread: те набирают кусок по 128 байт, и карта получает запросы по одному сектору
// (а с выключенным буфером — по одному байту: 16 КБ читались 17 секунд, 07.10).
#define CP_BUF 16384
static uint8_t *cpBuf;
static uint32_t cpBufPos, cpBufLen;
static int cpFd = -1;
static int cpRead(uint8_t *dst, uint32_t k) {   // сколько байт отдано; меньше просимого — файл кончился или карта не ответила
  uint32_t got = 0;
  while (got < k) {
    if (cpBufPos >= cpBufLen) {
      uint32_t t0 = micros();
      int n = (int)read(cpFd, cpBuf, CP_BUF);
      cpReadUs = cpReadUs + (micros() - t0);
      if (n <= 0) break;
      cpBufLen = n;
      cpBufPos = 0;
    }
    uint32_t m = cpBufLen - cpBufPos;
    if (m > k - got) m = k - got;
    memcpy(dst + got, cpBuf + cpBufPos, m);
    cpBufPos += m;
    got += m;
  }
  return (int)got;
}

// ---- пересчёт частоты: любая входная → 32 кГц. Многофазный фильтр, 16 отводов, 32 фазы с линейной подстройкой между ними.
#define RS_TAPS 16
#define RS_PH 32
static float rsTab[RS_PH + 1][RS_TAPS];
static float rsHist[2][RS_TAPS * 2];        // последние 16 входных отсчётов каждого канала, записанные дважды — окно всегда подряд
static int rsW;
static uint32_t rsRate;                     // входная частота; 0 или 32000 — пересчёт не нужен
static uint32_t rsIn;                       // сколько входных отсчётов принято
// Время следующего выходного отсчёта (во входных отсчётах) и шаг — целая часть и дробная (в долях 2^32). Только целые
// числа: числа двойной точности этот процессор считает программно, и с ними пересчёт съедал 20 мс на кадр MP3 —
// файл 44,1 кГц заикался (07.10: 27 кадров в секунду вместо 38).
static uint32_t rsNextI, rsNextF, rsStepI, rsStepF;

static void rsSetup(uint32_t rate) {
  rsRate = rate == SRATE ? 0 : rate;
  rsIn = 0;
  rsNextI = 0;
  rsNextF = 0;
  rsW = 0;
  memset(rsHist, 0, sizeof(rsHist));
  if (!rsRate) return;
  rsStepI = rate / SRATE;
  rsStepF = (uint32_t)(((uint64_t)(rate % SRATE) << 32) / SRATE);
  // срез: при понижении частоты — чуть ниже половины выходной (иначе верх «заворачивается» в слышимое), при повышении — чуть ниже половины входной
  float fc = (rate > SRATE ? (float)SRATE / rate : 1.0f) * 0.92f;
  for (int p = 0; p <= RS_PH; p++) {
    float sum = 0;
    for (int j = 0; j < RS_TAPS; j++) {
      float d = (j - (RS_TAPS / 2 - 1)) - (float)p / RS_PH, x = PI * fc * d;
      float s = fabsf(x) < 1e-6f ? 1.0f : sinf(x) / x;
      float u = d / (RS_TAPS / 2);   // −1…1
      float w = fabsf(u) >= 1 ? 0.0f : 0.42f + 0.5f * cosf(PI * u) + 0.08f * cosf(2 * PI * u);
      rsTab[p][j] = fc * s * w;
      sum += rsTab[p][j];
    }
    for (int j = 0; j < RS_TAPS; j++) rsTab[p][j] /= sum;   // постоянная составляющая проходит без изменения громкости
  }
}

static inline void mpPut(int16_t l, int16_t r) {
  uint32_t hd = mpHead;
  int16_t *d = mpCardNow ? cpRing[hd & (CARD_RING - 1)] : mpRing[hd & (MP3_RING - 1)];
  d[0] = l;
  d[1] = r;
  mpHead = hd + 1;
}

// Один входной отсчёт (оба канала) → ноль, один или несколько выходных в запас.
static inline void rsPush(int16_t l, int16_t r) {
  if (!rsRate) {
    mpPut(l, r);
    return;
  }
  rsHist[0][rsW] = rsHist[0][rsW + RS_TAPS] = l;
  rsHist[1][rsW] = rsHist[1][rsW + RS_TAPS] = r;
  rsW = (rsW + 1) & (RS_TAPS - 1);
  uint32_t i = rsIn++;   // номер только что принятого отсчёта; окно — отсчёты i−15 … i, начинается с rsHist[·][rsW]
  // Выходной отсчёт с целой частью времени b считается по окну b−7 … b+8, то есть когда пришёл отсчёт b+8. Все более
  // ранние уже выданы на прошлых шагах, поэтому здесь b всегда равно i−8 и окно — ровно то, что лежит в rsHist.
  while ((int32_t)(i - rsNextI) >= RS_TAPS / 2) {
    int p = rsNextF >> 27;                                    // 32 фазы
    float a = (rsNextF & 0x07FFFFFF) * (1.0f / 134217728.0f), sl = 0, sr = 0;   // и доля между соседними фазами
    const float *c0 = rsTab[p], *c1 = rsTab[p + 1], *hl = rsHist[0] + rsW, *hr = rsHist[1] + rsW;
    for (int j = 0; j < RS_TAPS; j++) {
      float c = c0[j] + (c1[j] - c0[j]) * a;
      sl += hl[j] * c;
      sr += hr[j] * c;
    }
    int il = (int)lroundf(sl), ir = (int)lroundf(sr);
    mpPut(il > 32767 ? 32767 : il < -32768 ? -32768 : il, ir > 32767 ? 32767 : ir < -32768 ? -32768 : ir);
    uint32_t f = rsNextF + rsStepF;
    rsNextI += rsStepI + (f < rsNextF ? 1 : 0);
    rsNextF = f;
  }
}

// Открыть файл с карты. wav — это WAV (иначе MP3); size — сколько байт звука читать; pos — с какого места.
static bool cpOpen(bool &wav, WavFmt &wf, uint32_t &size, uint32_t &pos) {
  cpErr = 0;
  if (!cpRing) cpRing = (int16_t(*)[2])heap_caps_malloc(CARD_RING * 4, MALLOC_CAP_SPIRAM);
  if (!cpBuf) cpBuf = (uint8_t *)heap_caps_malloc(CP_BUF, MALLOC_CAP_SPIRAM);
  cpBufPos = cpBufLen = 0;
  cpF = cpRing && cpBuf ? fopen(cpPath, "rb") : NULL;
  if (!cpF) return false;
  wav = fmKind(cpPath) == FK_WAV;
  if (wav) {
    if (wavParse(cpF, wf)) return false;
    pos = wf.dataPos;
    size = wf.dataPos + wf.dataLen;
    cpChans = wf.chans;
  } else {
    pos = (uint32_t)mp3SkipTag(cpF);
    fseek(cpF, 0, SEEK_END);
    size = (uint32_t)ftell(cpF);
    cpChans = 2;   // уточнится на первом кадре
  }
  cpFd = fileno(cpF);   // заголовок разобран через FILE, дальше читаем только напрямую
  return cpFd >= 0 && lseek(cpFd, pos, SEEK_SET) == (off_t)pos;
}

static void mp3Task(void *) {
  static uint8_t in[4096];
  static int16_t pcm[1152 * 2];
  mpRing = (int16_t(*)[2])heap_caps_malloc(MP3_RING * 4, MALLOC_CAP_SPIRAM);
  if (!mpRing) vTaskDelete(NULL);
  HMP3Decoder h = NULL;
  uint32_t gen = 0, pos = 0, size = 0;
  int have = 0, item = -1;
  uint8_t *ptr = in;
  bool eof = true, card = false, wav = false;
  WavFmt wf;
  for (;;) {
    uint32_t want = mpWantGen;
    if (want != gen) {             // попросили новую мелодию: всё с начала
      if (cpF) {
        fclose(cpF);
        cpF = NULL;
      }
      item = mpItem;
      card = item == -2;
      wav = false;
      pos = 0;
      have = 0;
      ptr = in;
      eof = false;
      size = item >= 0 ? assetTab[item].size : 0;
      bool ok = item >= 0;
      if (card) {
        ok = cpOpen(wav, wf, size, pos);
        if (!ok) cpErr = 3;
      }
      // Рабочая память декодера (около 30 КБ) должна лежать во внутренней памяти. Он берёт её обычным malloc,
      // а в этой сборке всё крупнее 4 КБ уходит во внешнюю память — медленную и занятую экраном: разбор кадра
      // шёл 19–64 мс вместо нескольких, и сторож перезапускал плату (06.10). На время выделения порог поднят.
      if (h) MP3FreeDecoder(h);
      h = NULL;
      if (ok && !wav) {
        heap_caps_malloc_extmem_enable(64 * 1024);
        h = MP3InitDecoder();
        heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
      }
      rsSetup(wav ? wf.rate : SRATE);   // у MP3 частота станет известна на первом кадре
      mpCardNow = card;            // читатель в это время запас не трогает — ждёт, пока номера сойдутся
      mpHead = 0;
      mpTail = 0;
      mpDone = !ok || (!wav && !h);
      gen = want;
      mpDecGen = want;
      continue;
    }
    if ((item < 0 && !card) || mpDone) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    uint32_t cap = card ? CARD_RING : MP3_RING, need = card ? 2400 : 1152;   // из одного куска выходит до 2304 отсчётов (8 кГц → 32 кГц)
    {
      uint32_t fill = mpHead - mpTail;
      if (cap - fill < need) {   // запас полон
        vTaskDelay(pdMS_TO_TICKS(4));
        continue;
      }
      if (mpQuiet && fill > 6400) {   // экран перелистывает страницу, запаса хватает — не мешать
        vTaskDelay(pdMS_TO_TICKS(4));
        continue;
      }
      uint8_t want = fill < cap * 45 / 100 ? 3 : fill > cap * 3 / 4 ? 1 : mpPrio;
      if (want != mpPrio) {
        mpPrio = want;
        vTaskPrioritySet(NULL, want);
      }
    }
    if (wav) {                     // несжатая запись: кусок до 512 отсчётов, любые 8/16/24/32 бита → 16
      uint32_t blk = wf.block, left = size - pos, k = 512 * blk;
      if (k > left) k = left - left % blk;
      int got = k ? cpRead(in, k) : 0;
      if (got < (int)blk) {
        if (k && got < (int)k) cpErr = 3;   // должно было прочитаться, а не прочиталось — карту вынули или она сбоит
        mpDone = true;
        continue;
      }
      pos += got;
      int n = got / blk, bs = wf.bits / 8;
      uint32_t r0 = micros();
      for (int i = 0; i < n; i++) {
        int16_t s[2];
        for (int c = 0; c < wf.chans; c++) {
          const uint8_t *b = in + i * blk + c * bs;
          s[c] = bs == 1 ? (int16_t)((b[0] - 128) << 8) : (int16_t)(b[bs - 2] | b[bs - 1] << 8);
        }
        rsPush(s[0], wf.chans == 2 ? s[1] : s[0]);
      }
      cpRsUs = cpRsUs + (micros() - r0);
      vTaskDelay(1);
      continue;
    }
    if (have < 2048 && !eof) {     // держим во входном буфере больше самого длинного кадра (1440 байт)
      memmove(in, ptr, have);
      ptr = in;
      uint32_t left = size - pos, k = sizeof(in) - have;
      if (k > left) k = left;
      if (card) {
        int got = k ? cpRead(in + have, k) : 0;
        if (got < (int)k) {
          eof = true;
          cpErr = 3;
        }
        if (got > 0) {
          pos += got;
          have += got;
        }
      } else if (k && assetRead(assetTab[item].off + pos, in + have, k)) {
        pos += k;
        have += k;
      } else eof = true;
      if (pos >= size) eof = true;
    }
    int off = MP3FindSyncWord(ptr, have);
    if (off < 0) {                 // в буфере нет начала кадра
      have = 0;
      ptr = in;
      if (eof) mpDone = true;
      continue;
    }
    ptr += off;
    have -= off;
    uint8_t *was = ptr;
    uint32_t t0 = micros();
    int err = MP3Decode(h, &ptr, &have, pcm, 0);
    uint32_t dt = micros() - t0;
    if (err == ERR_MP3_INDATA_UNDERFLOW && eof) {   // оборванный последний кадр
      mpDone = true;
      continue;
    }
    if (err == ERR_MP3_MAINDATA_UNDERFLOW) continue;   // начало потока: данных кадра ещё не набралось — так и должно быть
    if (err) {
      mpErrors = mpErrors + 1;
      if (ptr == was && have > 0) {   // с места не сдвинулись — шагнуть самим, иначе зациклимся
        ptr++;
        have--;
      }
      if (have <= 0 && eof) mpDone = true;   // хвост файла — метка, а не звук
      continue;
    }
    MP3FrameInfo fi;
    MP3GetLastFrameInfo(h, &fi);
    mpRate = fi.samprate;
    mpKbps = fi.bitrate / 1000;
    mpChans = fi.nChans;
    mpFrames = mpFrames + 1;
    mpUsSum = mpUsSum + dt;
    if (dt > mpUsMax) mpUsMax = dt;
    if (card) {
      cpChans = fi.nChans;
      if ((uint32_t)fi.samprate != (rsRate ? rsRate : (uint32_t)SRATE)) rsSetup(fi.samprate);
    }
    int n = fi.nChans == 2 ? fi.outputSamps / 2 : fi.outputSamps;
    uint32_t r0 = micros();
    for (int i = 0; i < n; i++) {
      if (fi.nChans == 2) rsPush(pcm[2 * i], pcm[2 * i + 1]);
      else rsPush(pcm[i], pcm[i]);
    }
    if (card) cpRsUs = cpRsUs + (micros() - r0);
    // Отдать время остальным. Эта задача важнее экрана, и пока она разбирает кадр, меню стоит. Кадр MP3 44,1 кГц с
    // карты разбирается ~19 мс из 26, что на него отведены, — свободных миллисекунд мало, и раздавать их надо с умом:
    // запас звука в порядке (больше 0,75 с) — экрану 5 мс, средний — 3 мс, запас тает — 1 мс (совсем не отдавать
    // нельзя: пробовал отдавать раз в восемь кадров — один кадр меню растянулся на 13 секунд, 07.10).
    uint32_t fill = mpHead - mpTail;
    vTaskDelay(card ? (fill > 24000 ? 5 : fill > 12000 ? 3 : 1) : 1);
  }
}

static void mp3Open(int item) {
  mpItem = item;
  mpStarted = false;
  mpWantGen = mpWantGen + 1;
}

// 1 — отсчёт есть; 0 — пока не готово (играть тишину); −1 — мелодия кончилась
static inline int mp3Get(int16_t &l, int16_t &r) {
  if (mpDecGen != mpWantGen) return 0;
  uint32_t t = mpTail;
  if (t == mpHead) {
    if (mpDone) return -1;
    if (mpStarted) mpUnder = mpUnder + 1;
    return 0;
  }
  if (mpCardNow && !mpStarted && !mpDone && mpHead - t < CARD_PREFILL) return 0;   // файл с карты: сначала набрать запас
  if (!mpCardNow && !mpRing) return 0;
  const int16_t *s = mpCardNow ? cpRing[t & (CARD_RING - 1)] : mpRing[t & (MP3_RING - 1)];
  l = s[0];
  r = s[1];
  mpTail = t + 1;
  mpStarted = true;
  return 1;
}
