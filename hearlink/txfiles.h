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

// ---- файл в эфир
static void cardPlay(const char *path) {
  fmFull(cpPath, sizeof(cpPath), path);
  strlcpy(fm.playName, fmBase(path), sizeof(fm.playName));
  fm.playTotalS = !strcmp(fm.info.name, fmBase(path)) ? fm.info.durS : 0;
  fm.playS = 0;
  fm.playEnd = 0;
  fm.playSeq = fm.playSeq + 1;
  cpPlayed = 0;
  cpErr = 0;
  txCardStop = false;
  mpItem = -2;
  mpStarted = false;
  mpWantGen = mpWantGen + 1;
  txCardOn = true;
  fm.playing = true;
}
static void cardStop() {
  if (fm.playing) txCardStop = true;   // звук плавно уйдёт, дальше — cardTick
}
// Присмотр: файл доигран или остановлен — отпустить задачу разбора и файл. Вызывается задачей записи 50 раз в секунду.
static void cardTick() {
  if (!fm.playing) return;
  fm.playS = cpPlayed / SRATE;
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
