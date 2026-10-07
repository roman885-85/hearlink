// Обновление с карты памяти (передатчик).
//
// Владелец 07.10: «должна быть функция обновления со следующей логикой — обновления приемников и обновления
// передатчика записывается в карту памяти, передатчик просматривает специально отведенную для этого папку и прошивки
// обновлений в ней, далее предлагает обновить, и таким образом происходит обновление».
//
// Как устроено:
//  — на карте есть папка UPDATE (передатчик создаёт её сам). В неё кладут файл прошивки *.bin — тот, что получается
//    при сборке (build/hearlink-s3/hearlink.ino.bin); прошивка у набора одна на все платы, поэтому файл один и для
//    приёмников, и для передатчика;
//  — передатчик просматривает папку при включении, при появлении карты и когда открывают окно «Оновлення»; из годных
//    файлов берёт тот, у которого версия старше; если прошивка в файле не та, на которой он работает, — предлагает;
//  — по согласию: сначала приёмникам по радио (тем же способом, что в ota.h, только раздаётся файл, а не своя
//    прошивка), затем передатчик записывает файл в свой второй раздел и перезапускается. Порядок такой нарочно:
//    пока идёт раздача, обе стороны работают на прежней прошивке и заведомо понимают друг друга.
// Файл проверяется: это образ для ESP32-S3, длина сходится с заголовком, отпечаток в хвосте образа сходится с
// содержимым (повреждённый при копировании файл не пройдёт), и в нём есть метка «HEARLINK-FW:версия» — то есть это
// прошивка набора, а не чужая. С картой, как и всё остальное, работает одна задача — задача карты (txrec.h).
#pragma once
#include <dirent.h>
#include <sys/stat.h>

#define UPD_DIR "/sd/UPDATE"
#define FW_TAG_KEY "HEARLINK-FW:"
enum { UPD_NONE = 0, UPD_NOCARD, UPD_SCAN, UPD_NOFILE, UPD_SAME, UPD_READY, UPD_BAD, UPD_OLDER };
// UPD_OLDER — в файле прошивка старше той, что работает: само не предлагается (на экране об этом сказано); поставить
// её всё же можно с порта командой M5 — на случай, когда надо вернуться на прежнюю версию.
// почему файл не годится
enum { UB_OK = 0, UB_NOTFW, UB_BROKEN, UB_SIZE, UB_FOREIGN, UB_MEM };
struct Upd {
  volatile uint8_t state = UPD_NONE;
  volatile bool scanAsk = false;
  volatile uint32_t seq = 0;       // растёт с каждым законченным просмотром папки
  char file[64] = "";              // имя найденного файла
  char version[12] = "";
  uint8_t why = UB_OK;
  uint8_t *img = nullptr;          // образ из файла (внешняя память) — пока состояние UPD_READY
  uint32_t len = 0;
  uint8_t sha[32];
  int files = 0;                   // сколько файлов *.bin в папке
  // запись в сам передатчик
  volatile uint8_t selfPercent = 0;
  volatile uint8_t selfWait = 0;   // сколько секунд до начала записи (на экране предупреждение)
  volatile bool keepOlder = false; // образ более старой прошивки оставить в памяти (попросили M5)
};
static Upd upd;

// метка версии внутри образа: «HEARLINK-FW:2.21»
static bool updFindVersion(const uint8_t *b, uint32_t len, char *out, size_t cap) {
  const size_t kl = strlen(FW_TAG_KEY);
  for (uint32_t i = 0; i + kl + 2 < len; i++) {
    if (b[i] != 'H' || memcmp(b + i, FW_TAG_KEY, kl)) continue;
    size_t n = 0;
    for (const uint8_t *c = b + i + kl; n + 1 < cap && c < b + len && *c >= ' ' && *c < 127; c++) out[n++] = (char)*c;
    out[n] = 0;
    if (n) return true;
  }
  return false;
}
static float updVerNum(const char *v) {
  int a = 0, b = 0;
  sscanf(v, "%d.%d", &a, &b);
  return a * 1000.0f + b;   // 2.9 < 2.10 < 2.21
}

// Проверить образ в памяти. Возвращает UB_…; при успехе пишет длину образа и версию.
static uint8_t updCheck(const uint8_t *b, uint32_t size, uint32_t *len, char *ver, size_t verCap) {
  if (size < 65536 || size > 0x300000) return UB_SIZE;
  const esp_image_header_t *h = (const esp_image_header_t *)b;
  if (h->magic != ESP_IMAGE_HEADER_MAGIC) return UB_NOTFW;
  if (h->chip_id != ESP_CHIP_ID_ESP32S3) return UB_FOREIGN;
  uint32_t l = otaImageLen(b, size);
  if (!l || !h->hash_appended) return UB_BROKEN;
  uint8_t sha[32];
  otaSha(b, l - 32, sha, true);
  if (memcmp(sha, b + l - 32, 32)) return UB_BROKEN;
  if (!updFindVersion(b, l, ver, verCap)) return UB_FOREIGN;
  *len = l;
  return UB_OK;
}

static void updDrop() {
  uint8_t *p = upd.img;
  upd.img = nullptr;
  upd.len = 0;
  if (p) free(p);
}

// Просмотр папки (задача карты).
static void updScan() {
  Upd &u = upd;
  if (otaTxActive()) return;   // идёт раздача — образ занят
  u.state = UPD_SCAN;
  updDrop();
  u.file[0] = 0;
  u.version[0] = 0;
  u.why = UB_OK;
  u.files = 0;
  if (!recSd) {
    u.state = UPD_NOCARD;
    u.seq = u.seq + 1;
    return;
  }
  mkdir(UPD_DIR, 0777);
  DIR *d = opendir(UPD_DIR);
  uint8_t *best = nullptr;
  uint32_t bestLen = 0;
  char bestVer[12] = "", bestFile[64] = "";
  uint8_t badWhy = UB_OK;
  char badFile[64] = "", sameFile[64] = "";
  if (d) {
    struct dirent *e;
    while ((e = readdir(d))) {
      size_t nl = strlen(e->d_name);
      if (e->d_type == DT_DIR || nl < 5 || strcasecmp(e->d_name + nl - 4, ".bin") || e->d_name[0] == '.') continue;
      u.files++;
      char path[160];
      snprintf(path, sizeof(path), UPD_DIR "/%s", e->d_name);
      struct stat st;
      uint8_t why = UB_OK;
      uint8_t *buf = nullptr;
      uint32_t len = 0;
      char ver[12] = "";
      if (stat(path, &st) != 0 || st.st_size < 65536 || st.st_size > 0x300000) why = UB_SIZE;
      else {
        // Сначала дёшево: если длина и отпечаток в хвосте файла те же, что у работающей прошивки, — это она и есть,
        // читать и пересчитывать два мегабайта незачем (так бывает при каждом включении после обновления с карты).
        uint8_t ownTail[32], fileTail[32];
        uint32_t ownLen = 0;
        int fd = open(path, O_RDONLY);
        bool same = fd >= 0 && otaOwnTail(ownTail, &ownLen) && (uint32_t)st.st_size == ownLen && lseek(fd, ownLen - 32, SEEK_SET) == (off_t)(ownLen - 32) &&
                    read(fd, fileTail, 32) == 32 && !memcmp(ownTail, fileTail, 32);
        if (fd >= 0) close(fd);
        if (same) {
          strlcpy(sameFile, e->d_name, sizeof(sameFile));
          continue;
        }
      }
      if (why != UB_OK) {
      } else if (!(buf = (uint8_t *)heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM))) why = UB_MEM;
      else {
        int fd = open(path, O_RDONLY);
        uint32_t got = 0;
        while (fd >= 0 && got < (uint32_t)st.st_size) {
          int r = read(fd, buf + got, (uint32_t)st.st_size - got > 32768 ? 32768 : (uint32_t)st.st_size - got);
          if (r <= 0) break;
          got += r;
          cardTick();   // файл в эфире (если идёт) не оставлять без присмотра
          vTaskDelay(pdMS_TO_TICKS(6));   // не спеша: экран и звук важнее
        }
        if (fd >= 0) close(fd);
        why = got == (uint32_t)st.st_size ? updCheck(buf, got, &len, ver, sizeof(ver)) : UB_BROKEN;
      }
      if (why == UB_OK && (!best || updVerNum(ver) > updVerNum(bestVer))) {
        if (best) free(best);
        best = buf;
        buf = nullptr;
        bestLen = len;
        strlcpy(bestVer, ver, sizeof(bestVer));
        strlcpy(bestFile, e->d_name, sizeof(bestFile));
      } else if (why != UB_OK && !badFile[0]) {
        badWhy = why;
        strlcpy(badFile, e->d_name, sizeof(badFile));
      }
      if (buf) free(buf);
    }
    closedir(d);
  }
  if (best) {
    strlcpy(u.file, bestFile, sizeof(u.file));
    strlcpy(u.version, bestVer, sizeof(u.version));
    bool older = updVerNum(bestVer) < updVerNum(FW_VERSION);
    if (older && sameFile[0]) {   // рядом лежит и та прошивка, что работает, — старый файл не интересен
      free(best);
      strlcpy(u.file, sameFile, sizeof(u.file));
      strlcpy(u.version, FW_VERSION, sizeof(u.version));
      u.state = UPD_SAME;
    } else if (older && !u.keepOlder) {   // старую не предлагаем и память под неё не держим (понадобится — M5 прочтёт заново)
      free(best);
      u.state = UPD_OLDER;
    } else {
      otaSha(best, bestLen, u.sha, true);
      u.img = best;
      u.len = bestLen;
      u.state = older ? UPD_OLDER : UPD_READY;
    }
  } else if (sameFile[0]) {
    strlcpy(u.file, sameFile, sizeof(u.file));
    strlcpy(u.version, FW_VERSION, sizeof(u.version));
    u.state = UPD_SAME;
  } else if (badFile[0]) {
    strlcpy(u.file, badFile, sizeof(u.file));
    u.why = badWhy;
    u.state = UPD_BAD;
  } else u.state = UPD_NOFILE;
  u.keepOlder = false;
  u.seq = u.seq + 1;
  static const char *const ST[] = { "", "картки немає", "", "файлів прошивки немає", "та сама прошивка, що працює", "Є ОНОВЛЕННЯ", "файл не годиться",
                                    "прошивка у файлі старіша за ту, що працює" };
  Serial.printf("оновлення з картки: у теці UPDATE файлів .bin — %d; %s%s%s%s%s\n", u.files, ST[u.state], u.file[0] ? ": " : "", u.file,
                u.version[0] ? ", версія " : "", u.version);
}

// задача карты: просмотреть папку, когда просят и когда карта появилась
static void updService() {
  static bool hadCard;
  bool card = recSd;
  if (card != hadCard) {
    hadCard = card;
    upd.scanAsk = true;
  }
  if (!upd.scanAsk) return;
  upd.scanAsk = false;
  updScan();
}

// Записать образ из файла в собственный второй раздел (задача раздачи, после приёмников). true — записано и проверено.
// Запись во флеш на десятки миллисекунд отнимает у процессора память кадра, и экран в это время показывает полосы.
// Поэтому: подсветка гаснет → 1,7 с записи → экран снова чистый → подсветка загорается на полторы секунды → и так
// до конца (около двенадцати кругов, ~50 с): экран ровно «дышит». В светлые промежутки на нём крупно, во весь экран:
// «ОНОВЛЕННЯ ПРОШИВКИ — НЕ ВИМИКАЙТЕ ЖИВЛЕННЯ!!!» и ход записи (drawSelfUpdate в txui.h).
static bool updSelfWrite() {
  Upd &u = upd;
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  esp_ota_handle_t oh = 0;
  u.selfPercent = 0;
  if (!u.img || !next || u.len > next->size) return false;
  for (int k = 8; k >= 1; k--) {   // сначала крупное предупреждение на экране: что сейчас будет и что экран будет мигать
    u.selfWait = k;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  u.selfWait = 0;
  vTaskDelay(pdMS_TO_TICKS(1500));   // и полторы секунды — экран записи («НЕ ВИМИКАЙТЕ ЖИВЛЕННЯ»), прежде чем он погаснет в первый раз
  auto dark = [] {
    lcdDim = 0;
    vTaskDelay(pdMS_TO_TICKS(450));   // подсветка успевает погаснуть (её ведёт задача экрана)
    lcdBacklight(0);
  };
  auto light = [](uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(90));    // пара кадров — и картинка снова чистая
    lcdDim = 100;
    vTaskDelay(pdMS_TO_TICKS(ms));
  };
  dark();
  bool ok = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &oh) == ESP_OK;
  uint32_t off = 0;
  while (ok && off < u.len) {
    uint32_t t0 = millis();
    while (ok && off < u.len && millis() - t0 < 1700) {
      uint32_t n = u.len - off < 4096 ? u.len - off : 4096;
      ok = esp_ota_write(oh, u.img + off, n) == ESP_OK;
      off += n;
      u.selfPercent = (uint8_t)((uint64_t)off * 100 / u.len);
      if (((off >> 12) & 7) == 0) vTaskDelay(1);   // раз в 32 КБ уступить ядро
    }
    if (ok && off < u.len) {
      light(1500);
      dark();
    }
  }
  if (!ok) {
    if (oh) esp_ota_abort(oh);
    light(0);
    return false;
  }
  ok = esp_ota_end(oh) == ESP_OK && esp_ota_set_boot_partition(next) == ESP_OK;   // сверка записанного и назначение раздела — тоже в темноте
  light(0);
  return ok;
}
