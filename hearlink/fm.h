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
  volatile uint32_t playMs = 0;      // сыграно, мс (с 3.28 — для головки плеера)
  volatile uint32_t playSeq = 0;     // растёт с каждым пуском файла в эфир
};

static Fm fm;

// Волна файла в эфире — для плеера (с 3.28, fmui.h). Владелец 20.10: «при воспроизведении из карты памяти, должен
// открываться плеер с полосой воспроизведения для быстрой перемотки и перехода на нужную часть трека, отображение как в
// audacity (уже готовый вариант есть в программе slovo для в плеере фонограмм)». Считает её задача волны (txfiles.h,
// cwTask) слева направо, пока файл играет; экран рисует то, что готово. Уровень столбца — в децибелах (0…~200), высота
// на экране — 10^((уровень − ref) / 20): ref — громкое место файла (99,5 % кадров тише), а не самый громкий кадр, чтобы
// один выброс не сплющил всю волну. MP3 целиком не разбирается (передатчику не по силам: кадр 44,1 кГц с карты и так
// стоит ~19 мс из 26) — уровень кадра оценивается по его служебной части: 0,65 дБ на шаг «общего усиления» (global_gain)
// и 3 дБ на каждое удвоение бит кадра (part2_3_length); на трёх мелодиях из проверочных звуков столбцы этой оценки и
// настоящей волны (ffmpeg) совпадают на 0,84–0,89 (сценарий — tools/wave/gg3.py). Заодно запоминается место каждого
// кадра в файле — переход по времени точный и у MP3 с переменным потоком. WAV — по выборкам отсчётов внутри столбца.
#ifndef SRATE
#define SRATE 32000   // частота эфира — из config.h; здесь только для имитатора экрана (tools/sim), где config.h нет
#endif
#define CW_N 220   // столбцов (на экране — по 2 точки)
struct CardWave {
  volatile uint32_t gen = 0;        // растёт с каждым новым файлом
  volatile uint16_t ready = 0;      // столбцов посчитано слева направо (до CW_N)
  volatile bool done = false;       // посчитано всё (у MP3 — и места всех кадров)
  volatile bool failed = false;     // файл не прочитался
  volatile uint8_t pct = 0;         // ход, %
  volatile uint8_t ref = 0;         // уровень, принятый за полную высоту
  bool mp3 = false;
  uint8_t lev[CW_N] = {};           // уровень столбца, дБ (0 — тишина или ещё не посчитан)
  uint32_t *off = nullptr;          // MP3: место кадра в файле (по номеру кадра)
  uint8_t *flev = nullptr;          // MP3: уровень кадра
  volatile uint32_t frames = 0;     // MP3: кадров найдено
  uint32_t cap = 0;                 // сколько кадров помещается в off/flev
  uint32_t rate = 0, spf = 0;       // частота; отсчётов в кадре MP3
  uint32_t start = 0, end = 0;      // байты звука в файле: [start, end)
  uint16_t block = 0;               // WAV: байт на отсчёт всех каналов
  volatile uint32_t durMs = 0;      // длительность (у MP3 — точная, когда done)
};
static CardWave cw;
// Переход по треку и пауза: просит экран (или порт), выполняет задача передачи (txCardSound в txaudio.h) — плавно
// уводит звук, перезапускает разбор с байта cpSeekReqByte и ставит время cpSeekReqPlayed (отсчёты 32 кГц).
static volatile uint32_t cpSeekReqByte = 0xFFFFFFFF, cpSeekReqPlayed;
static volatile bool cpPause;
// Перейти на ms от начала файла (с 3.28).
static void cardSeekMs(uint32_t ms) {
  if (!fm.playing || !cw.rate) return;
  uint32_t dur = cw.durMs ? cw.durMs : fm.playTotalS * 1000;
  if (dur && ms >= dur) ms = dur > 500 ? dur - 500 : 0;
  uint32_t byte;
  if (cw.mp3) {
    uint32_t fr = cw.spf ? (uint32_t)((uint64_t)ms * cw.rate / 1000 / cw.spf) : 0;
    if (fr < cw.frames) byte = cw.off[fr];   // место кадра известно — точно
    else {                                   // подсчёт ещё не дошёл — по месту в файле (у переменного потока время примерное)
      byte = cw.start + (uint32_t)((uint64_t)(cw.end - cw.start) * ms / (dur ? dur : 1));
      if (byte >= cw.end) byte = cw.end - 1;
    }
  } else {
    uint64_t smp = (uint64_t)ms * cw.rate / 1000;
    byte = cw.start + (uint32_t)(smp * cw.block);
    if (byte >= cw.end) byte = cw.end > cw.block ? cw.end - cw.block : cw.start;
  }
  cpSeekReqPlayed = (uint32_t)((uint64_t)ms * SRATE / 1000);
  cpSeekReqByte = byte;
}
static void cardPause(bool on) {
  if (fm.playing) cpPause = on;
}


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
