// Файловый менеджер карты памяти: то, что общее у экрана (fmui.h) и у задачи карты (txfiles.h).
// Экран только просит (uiFm) и читает состояние отсюда; с картой работает одна задача — та же, что пишет запись.
#pragma once
#include <stdint.h>
#include <string.h>

#define FM_MAX 120    // столько записей папки держим в списке (остальные считаем и говорим, сколько не показано)
#define FM_NAME 256   // имя целиком: длиннее 255 байт система файлов карты сама не отдаёт (кириллица — два байта на букву)
#define FM_PATH 640

enum { FM_NONE = 0, FM_LIST, FM_MKDIR, FM_DELETE, FM_COPY, FM_FORMAT, FM_INFO, FM_PLAY, FM_STOP, FM_CANCEL };
enum { FM_OK = 0, FM_E_NOCARD, FM_E_FAIL, FM_E_NOSPACE, FM_E_BUSY, FM_E_CANCEL, FM_E_NAME, FM_E_DEEP, FM_E_REC };
enum { FK_OTHER = 0, FK_WAV, FK_MP3, FK_TEXT, FK_SOUNDX };   // FK_SOUNDX — звук, но не тот, что мы умеем (FLAC, M4A, OGG…)

struct FmEntry {
  char name[FM_NAME];
  uint32_t size;
  uint8_t dir, kind;
};

struct FmInfo {            // что узнали о выбранном файле
  char name[FM_NAME];
  uint8_t kind = FK_OTHER;
  bool playable = false;   // можно пустить в эфир
  uint8_t why = 0;         // почему нельзя: 1 — не звук, 2 — вид записи не поддержан, 3 — файл повреждён
  uint32_t size = 0, rate = 0, durS = 0;
  uint16_t kbps = 0;
  uint8_t chans = 0, bits = 0;
  char text[600] = "";     // начало текстового файла
};

struct Fm {
  // --- пишет задача карты
  volatile uint8_t op = FM_NONE;     // что делается сейчас (FM_NONE — свободна)
  volatile uint8_t progress = 0;     // 0…100 (копирование); у форматирования хода нет
  volatile uint8_t lastOp = FM_NONE; // что сделано последним и чем кончилось
  volatile uint8_t result = FM_OK;
  volatile uint32_t doneSeq = 0;     // растёт с каждой законченной работой
  volatile uint32_t listSeq = 0;     // растёт с каждой попыткой прочитать список (listOk — удалась ли)
  volatile bool listOk = false;
  volatile uint32_t infoSeq = 0;     // растёт с каждым чтением сведений о файле
  char path[FM_PATH] = "/";          // папка, чей список сейчас в ent
  int n = 0, more = 0;               // записей в списке; сколько в папке ещё (не поместились)
  FmEntry *ent = nullptr;
  FmInfo info;
  uint32_t freeMb = 0, totalMb = 0;
  // --- файл в эфире
  volatile bool playing = false;
  volatile uint8_t playEnd = 0;      // чем кончилось последнее проигрывание: 1 — файл доигран, 2 — остановлен, 3 — ошибка чтения
  char playName[FM_NAME] = "";
  volatile uint32_t playS = 0, playTotalS = 0;
  volatile uint32_t playSeq = 0;     // растёт с каждым пуском файла в эфир
};

static Fm fm;

static inline uint8_t fmKind(const char *name) {
  const char *dot = nullptr;
  for (const char *c = name; *c; c++)
    if (*c == '.') dot = c;
  if (!dot) return FK_OTHER;
  char e[6] = "";
  for (int i = 0; i < 5 && dot[1 + i]; i++) e[i] = (char)(dot[1 + i] | 0x20);
  if (!strcmp(e, "wav")) return FK_WAV;
  if (!strcmp(e, "mp3")) return FK_MP3;
  if (!strcmp(e, "txt") || !strcmp(e, "log") || !strcmp(e, "csv") || !strcmp(e, "ini")) return FK_TEXT;
  for (const char *x : { "flac", "m4a", "aac", "ogg", "opus", "wma", "amr", "aif", "aiff", "ape", "m4b" })
    if (!strcmp(e, x)) return FK_SOUNDX;
  return FK_OTHER;
}

// Последняя часть пути («/ZAPYS/a.wav» → «a.wav»; «/» → «»)
static inline const char *fmBase(const char *path) {
  const char *b = path;
  for (const char *c = path; *c; c++)
    if (*c == '/') b = c + 1;
  return b;
}
