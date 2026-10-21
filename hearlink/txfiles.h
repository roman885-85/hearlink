// Передатчик: работа файлового менеджера с картой памяти. Всё делает задача записи (recTask) — по одной просьбе за раз:
// экран просит (fmAsk), задача выполняет (fmService) и пишет итог в fm (fm.h). Пуск файла в эфир — через задачу разбора
// звука (txmp3.h): здесь только просьба и присмотр за концом файла.
#pragma once
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "fm.h"
#include "cardfmt.h"

static char fmArgA[FM_PATH], fmArgB[FM_PATH];
static volatile uint8_t fmWant;          // что просят сделать
static volatile bool fmCancel;           // просят прервать (копирование, удаление папки)
static char fmWork[FM_PATH + 8], fmWork2[FM_PATH + 8];   // рабочие пути (полные, с «/sd»)
static volatile bool fmCardGone;         // карта перестала отвечать — задаче записи: отключить её

// «/ZAPYS/a.wav» → «/sd/ZAPYS/a.wav»; корень «/» → «/sd»
static void fmFull(char *dst, size_t cap, const char *p) {
  snprintf(dst, cap, "/sd%s", p[0] == '/' && !p[1] ? "" : p);
}

static bool fmAsk(int op, const char *a, const char *b);

// ---- волна файла в эфире (с 3.28; данные — cw в fm.h, рисует плеер в fmui.h)
// Отдельная задача с низшим приоритетом: создаётся на время подсчёта и сама уходит (внутренней памяти у передатчика в
// обрез — постоянный стек нельзя). Файл читает своим дескриптором, большими кусками в свой буфер во внешней памяти;
// новая просьба (другой файл) прерывает подсчёт старой.
#define CW_BUF 16384
static char cwPath[FM_PATH + 8];
static volatile uint32_t cwWant, cwSeen;
static TaskHandle_t cwTaskH;
static portMUX_TYPE cwMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t *cwBuf;
static uint32_t *cwHist;   // уровни кадров (или столбцов WAV): сколько каких — для «громкого места» ref

// Уровень по ходу подсчёта: такой, что громче него лишь 0,5 % кадров.
static uint8_t cwRef(uint32_t n) {
  uint32_t lim = n - n / 200, sum = 0;
  for (int v = 1; v < 256; v++) {
    sum += cwHist[v];
    if (sum >= lim) return (uint8_t)v;
  }
  return 255;
}
// Служебная часть кадра MP3 → оценка уровня, дБ (см. fm.h); 0 — кадр без звука.
static uint8_t cwMp3Level(const uint8_t *f, bool mpeg1, int nch, bool crc) {
  const uint8_t *p = f + 4 + (crc ? 2 : 0);
  uint32_t bit = 0;
  auto get = [&](int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++, bit++) v = v << 1 | ((p[bit >> 3] >> (7 - (bit & 7))) & 1);
    return v;
  };
  uint32_t gg = 0, p23 = 0;
  if (mpeg1) {
    get(9);
    get(nch == 1 ? 5 : 3);
    get(4 * nch);
    for (int gr = 0; gr < 2; gr++)
      for (int ch = 0; ch < nch; ch++) {
        uint32_t a = get(12);
        get(9);
        uint32_t g = get(8);
        get(30);   // scalefac_compress, окно, таблицы, флаги
        if (a > p23) p23 = a;
        if (g > gg) gg = g;
      }
  } else {   // MPEG-2 и 2.5: одна гранула
    get(8);
    get(nch == 1 ? 1 : 2);
    for (int ch = 0; ch < nch; ch++) {
      uint32_t a = get(12);
      get(9);
      uint32_t g = get(8);
      get(34);
      if (a > p23) p23 = a;
      if (g > gg) gg = g;
    }
  }
  if (p23 < 8) return 0;
  float l = 0.65f * gg + 3.0f * log2f((float)p23 + 1);
  return (uint8_t)(l > 255 ? 255 : l < 1 ? 1 : l + 0.5f);
}
static void cwMp3(FILE *f, uint32_t want) {
  static const uint16_t BR1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
  static const uint16_t BR2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
  static const uint32_t SR[4][3] = { { 11025, 12000, 8000 }, { 0, 0, 0 }, { 22050, 24000, 16000 }, { 44100, 48000, 32000 } };
  if (fseek(f, 0, SEEK_END)) return;
  uint32_t size = (uint32_t)ftell(f), start = (uint32_t)mp3SkipTag(f), end = size;
  uint8_t tag[3];
  if (size > 128 && !fseek(f, size - 128, SEEK_SET) && fread(tag, 1, 3, f) == 3 && !memcmp(tag, "TAG", 3)) end = size - 128;
  if (start >= end) return;
  cw.start = start;
  cw.end = end;
  if (!cw.off) {   // места кадров и их уровни — во внешней памяти, один раз и с запасом (1,3 МБ: до ~114 мин 44,1 кГц);
                   // не освобождаются никогда — экран может читать их в любой миг
    cw.off = (uint32_t *)heap_caps_malloc(262144 * 4, MALLOC_CAP_SPIRAM);
    cw.flev = (uint8_t *)heap_caps_malloc(262144, MALLOC_CAP_SPIRAM);
    cw.cap = cw.off && cw.flev ? 262144 : 0;
  }
  int fd = fileno(f);
  if (fd < 0 || lseek(fd, start, SEEK_SET) != (off_t)start) return;
  uint8_t colMax[CW_N] = {};
  uint32_t pos = start, histN = 0, frames = 0;
  int have = 0, i = 0, lockVer = -1, lockSri = -1, lastCol = 0;
  bool eof = false;
  memset(cwHist, 0, 256 * 4);
  for (;;) {
    if (cwWant != want) return;
    if (have - i < 2048 && !eof) {   // в буфере — больше самого длинного кадра (1441 байт)
      memmove(cwBuf, cwBuf + i, have - i);
      pos += i;
      have -= i;
      i = 0;
      int n = (int)read(fd, cwBuf + have, CW_BUF - have);
      if (n <= 0) eof = true;
      else have += n;
      if (pos + have >= end) {
        if (pos + have > end) have = end > pos ? end - pos : 0;
        eof = true;
      }
    }
    if (i + 4 > have) break;
    const uint8_t *h = cwBuf + i;
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) {
      i++;
      continue;
    }
    int ver = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3, bri = h[2] >> 4, sri = (h[2] >> 2) & 3;
    if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3 || (lockVer >= 0 && (ver != lockVer || sri != lockSri))) {
      i++;
      continue;
    }
    bool mpeg1 = ver == 3;
    uint32_t rate = SR[ver][sri], kbps = mpeg1 ? BR1[bri] : BR2[bri];
    int flen = (int)((mpeg1 ? 144000u : 72000u) * kbps / rate + ((h[2] >> 1) & 1));
    if (i + flen + 2 <= have && (cwBuf[i + flen] != 0xFF || (cwBuf[i + flen + 1] & 0xE0) != 0xE0)) {   // следующий кадр не там — ложное начало
      i++;
      continue;
    }
    if (i + 4 + 2 + 32 > have) break;   // оборванный хвост
    lockVer = ver;
    lockSri = sri;
    if (!frames) {
      cw.rate = rate;
      cw.spf = mpeg1 ? 1152 : 576;
    }
    int nch = (h[3] >> 6) == 3 ? 1 : 2;
    uint8_t l = cwMp3Level(h, mpeg1, nch, !(h[1] & 1));
    uint32_t at = pos + i;
    if (frames < cw.cap) {
      cw.off[frames] = at;
      cw.flev[frames] = l;
    }
    frames++;
    cw.frames = frames < cw.cap ? frames : cw.cap;
    if (l) {
      cwHist[l]++;
      histN++;
    }
    int c = (int)((uint64_t)(at - start) * CW_N / (end - start));   // пока считаем — столбец по месту в файле
    if (c >= CW_N) c = CW_N - 1;
    if (l > colMax[c]) colMax[c] = l;
    if (c > lastCol || (frames & 255) == 0) {   // готовые столбцы — на экран
      for (int k = 0; k < c; k++) cw.lev[k] = colMax[k];
      if (histN) cw.ref = cwRef(histN);
      cw.ready = (uint16_t)c;
      cw.pct = (uint8_t)((uint64_t)(at - start) * 100 / (end - start));
      lastCol = c;
    }
    i += flen;
  }
  if (!frames) return;
  // Всё посчитано: столбцы — по времени (номеру кадра), а не по месту в файле (у переменного потока это разное).
  uint32_t n = frames < cw.cap ? frames : cw.cap;
  if (n == frames)
    for (int c = 0; c < CW_N; c++) {
      uint32_t a = (uint64_t)c * n / CW_N, b = (uint64_t)(c + 1) * n / CW_N;
      uint8_t m = 0;
      for (uint32_t k = a; k < b || k == a; k++)
        if (k < n && cw.flev[k] > m) m = cw.flev[k];
      cw.lev[c] = m;
    }
  else
    for (int c = 0; c < CW_N; c++) cw.lev[c] = colMax[c];
  if (histN) cw.ref = cwRef(histN);
  cw.durMs = (uint32_t)((uint64_t)frames * cw.spf * 1000 / cw.rate);
  cw.ready = CW_N;
  cw.pct = 100;
  cw.done = true;
}
static void cwWav(FILE *f, uint32_t want) {
  WavFmt w;
  if (wavParse(f, w) || !w.block || !w.rate) return;
  cw.rate = w.rate;
  cw.block = w.block;
  cw.start = w.dataPos;
  cw.end = w.dataPos + w.dataLen;
  int fd = fileno(f);
  if (fd < 0) return;
  uint32_t total = w.dataLen / w.block, histN = 0;
  int bs = w.bits / 8;
  memset(cwHist, 0, 256 * 4);
  cw.durMs = (uint32_t)((uint64_t)total * 1000 / w.rate);
  for (int c = 0; c < CW_N; c++) {
    if (cwWant != want) return;
    uint32_t a = (uint64_t)c * total / CW_N, b = (uint64_t)(c + 1) * total / CW_N;
    uint32_t span = b > a ? b - a : 1, peak = 0;
    for (int part = 0; part < 3; part++) {   // три выборки по 256 отсчётов внутри столбца
      uint32_t s = a + (uint64_t)span * part / 3, k = 256;
      if (s >= total) break;
      if (s + k > total) k = total - s;
      uint32_t byte = w.dataPos + s * w.block, len = k * w.block;
      if (len > CW_BUF) len = CW_BUF - CW_BUF % w.block;
      if (lseek(fd, byte, SEEK_SET) != (off_t)byte) return;
      int got = (int)read(fd, cwBuf, len);
      if (got <= 0) return;
      for (int j = 0; j + (int)w.block <= got; j += w.block)
        for (int ch = 0; ch < w.chans; ch++) {
          const uint8_t *q = cwBuf + j + ch * bs;
          int v = bs == 1 ? (q[0] - 128) << 8 : (int16_t)(q[bs - 2] | q[bs - 1] << 8);
          uint32_t av = v < 0 ? -v : v;
          if (av > peak) peak = av;
        }
    }
    uint8_t l = peak ? (uint8_t)(20 * log10f((float)peak + 1) + 0.5f) : 0;   // 0…90 дБ (16 бит)
    cw.lev[c] = l;
    if (l) {
      cwHist[l]++;
      histN++;
      cw.ref = cwRef(histN);
    }
    cw.ready = (uint16_t)(c + 1);
    cw.pct = (uint8_t)((c + 1) * 100 / CW_N);
  }
  cw.done = true;
}
static void cwTask(void *) {
  for (;;) {
    uint32_t want;
    portENTER_CRITICAL(&cwMux);
    want = cwWant;
    bool idle = want == cwSeen;
    if (idle) cwTaskH = NULL;   // работы нет — уйти (новая просьба создаст задачу заново)
    portEXIT_CRITICAL(&cwMux);
    if (idle) vTaskDelete(NULL);
    cwSeen = want;
    cw.done = false;
    cw.failed = false;
    cw.ready = 0;
    cw.pct = 0;
    cw.ref = 0;
    cw.frames = 0;
    cw.durMs = 0;
    cw.rate = 0;   // пока не узнали заново — переход недоступен (cardSeekMs)
    cw.spf = 0;
    cw.start = cw.end = 0;
    cw.block = 0;
    memset((void *)cw.lev, 0, sizeof(cw.lev));
    cw.gen = want;
    if (!cwBuf) cwBuf = (uint8_t *)heap_caps_malloc(CW_BUF, MALLOC_CAP_SPIRAM);
    if (!cwHist) cwHist = (uint32_t *)heap_caps_malloc(256 * 4, MALLOC_CAP_SPIRAM);
    FILE *f = cwBuf && cwHist ? fopen(cwPath, "rb") : NULL;
    if (!f) {
      cw.failed = true;
      continue;
    }
    cw.mp3 = fmKind(cwPath) != FK_WAV;
    if (cw.mp3) cwMp3(f, want);
    else cwWav(f, want);
    fclose(f);
    if (!cw.done && cwWant == want) cw.failed = true;
  }
}
static void cwStart(const char *full) {   // полный путь («/sd/…»)
  bool need;
  portENTER_CRITICAL(&cwMux);
  strlcpy(cwPath, full, sizeof(cwPath));
  cwWant = cwWant + 1;
  need = !cwTaskH;
  if (need) cwTaskH = (TaskHandle_t)1;   // занято: задача создаётся
  portEXIT_CRITICAL(&cwMux);
  if (need && xTaskCreatePinnedToCore(cwTask, "wave", 4096, NULL, 1, &cwTaskH, 1) != pdPASS) cwTaskH = NULL;
}

// ---- файл в эфир
static void cardPlay(const char *path) {
  fmFull(cpPath, sizeof(cpPath), path);
  strlcpy(fm.playName, fmBase(path), sizeof(fm.playName));
  fm.playTotalS = !strcmp(fm.info.name, fmBase(path)) ? fm.info.durS : 0;
  fm.playS = 0;
  fm.playMs = 0;
  fm.playEnd = 0;
  fm.playSeq = fm.playSeq + 1;
  cpPlayed = 0;
  cpErr = 0;
  cpPause = false;
  cpSeekReqByte = 0xFFFFFFFF;
  cpSeekByte = 0xFFFFFFFF;
  txCardStop = false;
  mpItem = -2;
  mpStarted = false;
  mpWantGen = mpWantGen + 1;
  txCardOn = true;
  fm.playing = true;
  cwStart(cpPath);   // волна для плеера — в фоне
}
static void cardStop() {
  if (fm.playing) txCardStop = true;   // звук плавно уйдёт, дальше — cardTick
}
// Присмотр: файл доигран или остановлен — отпустить задачу разбора и файл. Вызывается задачей записи 50 раз в секунду.
static void cardTick() {
  if (!fm.playing) return;
  fm.playS = cpPlayed / SRATE;
  fm.playMs = (uint32_t)((uint64_t)cpPlayed * 1000 / SRATE);
  if (cw.done && cw.durMs && cw.gen == cwSeen) fm.playTotalS = (cw.durMs + 500) / 1000;   // у MP3 — точная, по счёту кадров
  if (txCardOn) return;
  mpItem = -1;
  mpWantGen = mpWantGen + 1;   // задача разбора закроет файл
  txTestReset = true;          // проверочный звук, если он выбран, начнётся заново
  fm.playing = false;
}

// ---- список папки
static int fmCmp(const void *a, const void *b) {
  const FmEntry *x = (const FmEntry *)a, *y = (const FmEntry *)b;
  if (x->dir != y->dir) return y->dir - x->dir;   // папки — первыми
  return strcasecmp(x->name, y->name);
}
static int fmDoList(const char *path) {
  fmFull(fmWork, sizeof(fmWork), path);
  DIR *d = opendir(fmWork);
  if (d && !fm.ent) fm.ent = (FmEntry *)heap_caps_calloc(FM_MAX, sizeof(FmEntry), MALLOC_CAP_SPIRAM);
  if (!d || !fm.ent) {
    if (d) closedir(d);
    else if (path[0] == '/' && !path[1]) fmCardGone = true;   // не открылся сам корень — карты больше нет
    fm.listOk = false;
    fm.listSeq = fm.listSeq + 1;
    return FM_E_FAIL;
  }
  fm.n = 0;
  int n = 0, more = 0;
  size_t base = strlen(fmWork);
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.' || !strcmp(e->d_name, "System Volume Information")) continue;   // служебное — не показывать
    if (n >= FM_MAX) {
      more++;
      continue;
    }
    FmEntry &x = fm.ent[n];
    strlcpy(x.name, e->d_name, FM_NAME);
    x.dir = e->d_type == DT_DIR;
    x.kind = x.dir ? 0 : fmKind(x.name);
    x.size = 0;
    if (!x.dir && base + 1 + strlen(e->d_name) < sizeof(fmWork)) {
      struct stat st;
      fmWork[base] = '/';
      strcpy(fmWork + base + 1, e->d_name);
      if (!stat(fmWork, &st)) x.size = (uint32_t)st.st_size;
      fmWork[base] = 0;
    }
    n++;
  }
  closedir(d);
  qsort(fm.ent, n, sizeof(FmEntry), fmCmp);
  strlcpy(fm.path, path, sizeof(fm.path));
  fm.more = more;
  fm.n = n;
  fm.listOk = true;
  fm.listSeq = fm.listSeq + 1;
  return FM_OK;
}

// ---- удаление: файл или папка со всем содержимым. fmWork[0…len) — полный путь.
static int fmRmTree(size_t len, int depth) {
  struct stat st;
  if (stat(fmWork, &st)) return FM_E_FAIL;
  if (!S_ISDIR(st.st_mode)) return unlink(fmWork) ? FM_E_FAIL : FM_OK;
  if (depth > 8) return FM_E_DEEP;
  for (;;) {   // после каждого удаления папка читается заново: не держим её открытой, пока в ней же удаляем
    DIR *d = opendir(fmWork);
    if (!d) return FM_E_FAIL;
    struct dirent *e = readdir(d);
    bool found = e != NULL;
    if (found) {
      if (len + 1 + strlen(e->d_name) >= sizeof(fmWork)) {
        closedir(d);
        return FM_E_NAME;
      }
      fmWork[len] = '/';
      strcpy(fmWork + len + 1, e->d_name);
    }
    closedir(d);
    if (!found) break;
    int r = fmRmTree(strlen(fmWork), depth + 1);
    fmWork[len] = 0;
    if (r) return r;
    if (fmCancel) return FM_E_CANCEL;
    vTaskDelay(1);
  }
  return rmdir(fmWork) ? FM_E_FAIL : FM_OK;
}
// Занят ли путь идущей записью или файлом в эфире (сам файл или папка над ним).
static bool fmInUse(const char *full) {
  size_t l = strlen(full);
  char rec[80];
  snprintf(rec, sizeof(rec), "/sd%s", recName);
  if (recOn && !strncmp(rec, full, l) && (rec[l] == 0 || rec[l] == '/')) return true;
  if (fm.playing && !strncmp(cpPath, full, l) && (cpPath[l] == 0 || cpPath[l] == '/')) return true;
  return false;
}
static int fmDoDelete(const char *path) {
  if (path[0] != '/' || !path[1]) return FM_E_FAIL;   // корень не удаляется
  fmFull(fmWork, sizeof(fmWork), path);
  if (fmInUse(fmWork)) return FM_E_BUSY;
  return fmRmTree(strlen(fmWork), 0);
}

static int fmDoMkdir(const char *path) {
  fmFull(fmWork, sizeof(fmWork), path);
  struct stat st;
  if (!stat(fmWork, &st)) return FM_E_NAME;   // такое имя уже есть
  return mkdir(fmWork, 0777) ? FM_E_NAME : FM_OK;
}

// ---- копирование файла в папку. Имя то же; если занято — «имя-2.расширение», «имя-3…».
static int fmDoCopy(const char *src, const char *dstDir) {
  if (recOn) return FM_E_REC;   // запись идёт той же задачей: пока копируем, она бы стояла
  fmFull(fmWork, sizeof(fmWork), src);
  struct stat st;
  if (stat(fmWork, &st) || S_ISDIR(st.st_mode)) return FM_E_FAIL;
  uint32_t size = (uint32_t)st.st_size;
  uint32_t freeMb = (uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20);
  if ((size >> 20) + 2 > freeMb) return FM_E_NOSPACE;
  const char *name = fmBase(src), *dot = strrchr(name, '.');
  int stem = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
  char dir[FM_PATH + 8];
  fmFull(dir, sizeof(dir), dstDir);
  for (int k = 1;; k++) {
    int w = k == 1 ? snprintf(fmWork2, sizeof(fmWork2), "%s/%s", dir, name) : snprintf(fmWork2, sizeof(fmWork2), "%s/%.*s-%d%s", dir, stem, name, k, name + stem);
    if (w >= (int)sizeof(fmWork2) || strlen(fmBase(fmWork2)) >= FM_NAME) return FM_E_NAME;
    if (stat(fmWork2, &st)) break;
    if (k >= 99) return FM_E_NAME;
  }
  static uint8_t *buf;
  const size_t CH = 32768;
  if (!buf) buf = (uint8_t *)heap_caps_malloc(CH, MALLOC_CAP_SPIRAM);
  if (!buf) return FM_E_FAIL;
  int a = open(fmWork, O_RDONLY);
  if (a < 0) return FM_E_FAIL;
  int b = open(fmWork2, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (b < 0) {
    close(a);
    return FM_E_FAIL;
  }
  int r = FM_OK;
  uint32_t done = 0;
  sdwOn = true;   // секторы копятся и уходят на карту крупными кусками — иначе медленная карта копирует втрое дольше
  for (;;) {
    int n = read(a, buf, CH);
    if (n < 0) r = FM_E_FAIL;
    if (n <= 0) break;
    if (write(b, buf, n) != n) {
      r = FM_E_NOSPACE;
      break;
    }
    done += n;
    fm.progress = size ? (uint8_t)((uint64_t)done * 100 / size) : 100;
    if (fmCancel) {
      r = FM_E_CANCEL;
      break;
    }
    cardTick();      // файл в эфире (если идёт) не должен остаться без присмотра
    vTaskDelay(1);
  }
  close(a);
  if (close(b) && !r) r = FM_E_FAIL;
  sdwOn = false;
  if (r) unlink(fmWork2);   // недописанную копию не оставлять
  return r;
}

// ---- сведения о файле
static int fmDoInfo(const char *path) {
  FmInfo &i = fm.info;
  i = FmInfo();
  strlcpy(i.name, fmBase(path), sizeof(i.name));
  i.kind = fmKind(i.name);
  fmFull(fmWork, sizeof(fmWork), path);
  struct stat st;
  if (stat(fmWork, &st)) return FM_E_FAIL;
  i.size = (uint32_t)st.st_size;
  i.why = i.kind == FK_SOUNDX ? 2 : 1;
  if (i.kind == FK_OTHER || i.kind == FK_SOUNDX) return FM_OK;
  FILE *f = fopen(fmWork, "rb");
  if (!f) return FM_E_FAIL;
  if (i.kind == FK_WAV) {
    WavFmt w;
    int r = wavParse(f, w);
    i.why = r;
    i.rate = w.rate;
    i.chans = w.chans;
    i.bits = w.bits;
    if (!r) {
      i.playable = true;
      i.durS = w.dataLen / (w.rate * w.block);
    }
  } else if (i.kind == FK_MP3) {
    Mp3Info m;
    int r = mp3Parse(f, m);
    i.why = r;
    if (!r) {
      i.playable = true;
      i.rate = m.rate;
      i.chans = m.chans;
      i.kbps = m.kbps;
      i.durS = m.durS;
    }
  } else {   // текст: начало файла; перевод строки — пробелом, оборванную в конце букву не показывать
    fseek(f, 0, SEEK_SET);
    int n = (int)fread(i.text, 1, sizeof(i.text) - 1, f);
    if (n < 0) n = 0;
    while (n > 0 && ((uint8_t)i.text[n - 1] & 0xC0) == 0x80) n--;
    if (n > 0 && ((uint8_t)i.text[n - 1] & 0xC0) == 0xC0) n--;
    i.text[n] = 0;
    for (int k = 0; k < n; k++)
      if ((uint8_t)i.text[k] < 32) i.text[k] = ' ';
  }
  fclose(f);
  return FM_OK;
}

// ---- форматирование: вся карта заново, FAT32 (маленькая карта — FAT16). Карта перед этим отключается от системы файлов.
static int fmDoFormat() {
  if (recOn) return FM_E_REC;
  if (fm.playing) return FM_E_BUSY;
  SD.end();
  int r = FM_E_FAIL;
  uint8_t pd = sdcard_init(42, &recSpi, recSpiHz);
  if (pd != 0xFF) {
    ff_diskio_register(pd, &sdxImpl);   // та же терпеливая прослойка, что и при обычной работе
    if (!(disk_initialize(pd) & STA_NOINIT)) {
      const UINT WL = 32768;
      BYTE *work = (BYTE *)heap_caps_malloc(WL, MALLOC_CAP_SPIRAM);
      if (work) {
        char drv[3] = { (char)('0' + pd), ':', 0 };
        const MKFS_PARM opt = { (BYTE)(FM_FAT | FM_FAT32), 0, 0, 0, 0 };
        FRESULT fr = f_mkfs(drv, &opt, work, WL);
        Serial.printf("форматування картки: код %d\n", (int)fr);
        if (fr == FR_OK) r = FM_OK;
        heap_caps_free(work);
      }
    }
    sdcard_uninit(pd);
  }
  // подключить карту заново — до того, как объявить работу законченной: экран не должен увидеть «карты нет»
  logPath[0] = 0;   // журнал работы — в новый файл на чистой карте
  if (sdMount()) {
    recFreeMb = (uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20);
    fm.freeMb = recFreeMb;
    fm.totalMb = (uint32_t)(SD.totalBytes() >> 20);
    recFull = false;
  } else {
    SD.end();
    recSd = false;
    recCard = CARD_NO_INIT;
    r = FM_E_FAIL;
  }
  return r;
}

static bool fmAsk(int op, const char *a, const char *b) {
  if (op == FM_CANCEL) {
    fmCancel = true;
    return true;
  }
  if (op == FM_STOP) {
    cardStop();
    return true;
  }
  if (op == FM_PLAY) {
    if (!recSd || !a) return false;
    cardPlay(a);
    return true;
  }
  if (!recSd || fmWant || fm.op) return false;
  strlcpy(fmArgA, a ? a : "", sizeof(fmArgA));
  strlcpy(fmArgB, b ? b : "", sizeof(fmArgB));
  fmCancel = false;
  fm.progress = 0;
  fmWant = op;
  return true;
}

// Выполнить одну просьбу.
static void fmService() {
  uint8_t op = fmWant;
  if (!op) return;
  fm.op = op;
  fmWant = 0;
  int r = FM_E_FAIL;
  switch (op) {
    case FM_LIST: r = fmDoList(fmArgA); break;
    case FM_MKDIR: r = fmDoMkdir(fmArgA); break;
    case FM_DELETE: r = fmDoDelete(fmArgA); break;
    case FM_COPY: r = fmDoCopy(fmArgA, fmArgB); break;
    case FM_INFO: r = fmDoInfo(fmArgA); break;
    case FM_FORMAT: r = fmDoFormat(); break;
  }
  if (op == FM_INFO) fm.infoSeq = fm.infoSeq + 1;
  fm.result = r;
  fm.lastOp = op;
  fm.op = FM_NONE;
  fm.doneSeq = fm.doneSeq + 1;
}
