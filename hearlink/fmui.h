// Экран «Картка пам'яті» — файловый менеджер (окно во весь экран, modal == M_FILES). Включается в txui.h, внутри ui.
// Владелец 06.10: «добавь файловый менеджер для работы с картой памяти, просмотром ее содержимого, просмотра,
// прослушивания, удаления, копирования в папки и создание или удаление папок, форматирование карты памяти»;
// «прослушивание файлов с карты памяти, имеется в виду пустить в эфир».
// Что есть: список папок и файлов с переходом по папкам; окно файла (сведения, «В ефір», «Копіювати», «Видалити»,
// «Переглянути» для текста); «Нова тека»; «Інше» — обновить список, удалить открытую папку, форматировать карту.
// Копирование — в два шага: «Копіювати» в окне файла, затем открыть нужную папку и нажать «Вставити сюди».
// С картой работает задача записи; экран только просит (uiFm) и читает состояние из fm (fm.h).

constexpr int ID_FM_BACK = 200, ID_FM_CLOSE = 201, ID_FM_PREV = 202, ID_FM_NEXT = 203, ID_FM_NEWDIR = 204, ID_FM_MORE = 205;
constexpr int ID_FM_PASTE = 206, ID_FM_UNCLIP = 207, ID_FM_STOP = 208;
constexpr int ID_FM_ROW = 210;   // +строка списка
constexpr int ID_FM_PLAY = 220, ID_FM_COPY = 221, ID_FM_DEL = 222, ID_FM_VIEW = 223, ID_FM_X = 224, ID_FM_YES = 225, ID_FM_NO = 226;
constexpr int ID_FM_RMDIR = 227, ID_FM_FORMAT = 228, ID_FM_ABORT = 229, ID_FM_REFRESH = 230;
enum { FS_LIST, FS_ITEM, FS_CONFIRM, FS_BUSY, FS_TEXT, FS_MENU };
enum { FQ_DEL_FILE = 1, FQ_DEL_DIR, FQ_FORMAT, FQ_FORMAT2 };
static const int FM_ROWS = 6;
static int fmSt = FS_LIST, fmPage = 0, fmQ = 0, fmBusyOp = 0;
static char fmCur[FM_PATH] = "/";        // открытая папка
static char fmClip[FM_PATH] = "";        // файл, выбранный для копирования
static char fmSelPath[FM_PATH] = "";     // файл, чьё окно открыто
static char fmSelName[FM_NAME] = "";
static uint32_t fmSelSize = 0;
static uint32_t fmListFrom = 0, fmInfoFrom = 0, fmSeenList = 0, fmSeenDone = 0, fmNoteUntil = 0, fmSeenEndSeq = 0;
static int fmSeenProg = -1, fmSeenPlay = -2;
static bool fmWantList = false, fmWantInfo = false, fmListBad = false, fmInfoOk = false, fmNoteShown = false;
static char fmNoteText[160] = "";

static void fmDraw();
static void drawModal();
static void fmNote(const char *text) {   // сообщение внизу экрана на четыре секунды (вместо подсказки)
  strlcpy(fmNoteText, tr(text), sizeof(fmNoteText));
  fmNoteUntil = millis() + 4000;
  fmNoteShown = false;
}
static bool fmRoot() {
  return fmCur[0] == '/' && !fmCur[1];
}
static void fmJoin(char *dst, size_t cap, const char *dir, const char *name) {
  snprintf(dst, cap, "%s%s%s", dir, dir[1] ? "/" : "", name);
}
static void fmList() {   // попросить список открытой папки; готов — когда fm.listSeq уйдёт с fmListFrom
  fmListFrom = fm.listSeq;
  fmWantList = !uiFm(FM_LIST, fmCur, NULL);
  fmListBad = false;
}
static void fmInfo() {
  fmInfoFrom = fm.infoSeq;
  fmInfoOk = false;
  fmWantInfo = !uiFm(FM_INFO, fmSelPath, NULL);
}
static void fmSize(uint32_t bytes, char *t, size_t n) {
  if (bytes < 1024) snprintf(t, n, tr("%u Б"), (unsigned)bytes);
  else if (bytes < 1048576) snprintf(t, n, tr("%u КБ"), (unsigned)(bytes >> 10));
  else if (bytes < 104857600) snprintf(t, n, tr("%u,%u МБ"), (unsigned)(bytes >> 20), (unsigned)((bytes & 0xFFFFF) * 10 >> 20));
  else snprintf(t, n, tr("%u МБ"), (unsigned)(bytes >> 20));
}
static void fmDur(uint32_t s, char *t, size_t n) {
  if (s >= 3600) snprintf(t, n, tr("%u год %u хв"), (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
  else if (s >= 60) snprintf(t, n, tr("%u хв %u с"), (unsigned)(s / 60), (unsigned)(s % 60));
  else snprintf(t, n, tr("%u с"), (unsigned)s);
}
static bool fmListReady() {   // список на экране — именно открытой папки
  return !fmWantList && fm.listSeq != fmListFrom && fm.op != FM_LIST && !fmListBad && !strcmp(fm.path, fmCur);
}
static int fmPages() {
  return fm.n ? (fm.n + FM_ROWS - 1) / FM_ROWS : 1;
}
static bool fmPlayingThis(const char *name) {   // этот файл сейчас в эфире (сверяем имя: папка у него та же, что на экране)
  return fm.playing && !strcmp(fm.playName, name);
}

// ---- значки
static void fmIcon(int x, int y, int kind, bool dir, uint16_t c) {   // x, y — середина; 24 × 22
  if (dir) {
    g.box(x - 11, y - 9, 11, 6, 2, c);
    g.box(x - 11, y - 6, 22, 15, 3, c);
  } else if (kind == FK_WAV || kind == FK_MP3 || kind == FK_SOUNDX) {   // нота
    g.circle(x - 4, y + 6, 4.5f, c);
    g.fill(x - 1, y - 9, 3, 16, c);
    g.fill(x - 1, y - 9, 9, 3, c);
  } else {   // лист
    g.box(x - 8, y - 10, 16, 20, 2, c);
    g.fill(x - 4, y - 4, 8, 2, C_CARD);
    g.fill(x - 4, y + 1, 8, 2, C_CARD);
  }
}
static void fmArrow(const Box &b, bool left, uint16_t c) {
  float cx = b.x + b.w / 2.0f, cy = b.y + b.h / 2.0f, s = left ? 1 : -1;
  g.line(cx + 4 * s, cy - 8, cx - 4 * s, cy, 2.6f, c);
  g.line(cx - 4 * s, cy, cx + 4 * s, cy + 8, 2.6f, c);
}

// ---- где что стоит
static Box fmWin() {   // окно поверх списка — по состоянию
  switch (fmSt) {
    case FS_ITEM: return Box{ 20, 62, 440, 356 };
    case FS_TEXT: return Box{ 16, 40, 448, 400 };
    case FS_MENU: return Box{ 40, 100, 400, fmRoot() ? (int16_t)236 : (int16_t)292 };
    case FS_BUSY: return Box{ 32, 140, 416, 200 };
    default: return Box{ 28, 120, 424, 240 };
  }
}
static Box fmBox(int id) {
  Box w = fmWin();
  int by = w.y + w.h - 62;   // нижний ряд кнопок окна
  switch (id) {
    case ID_FM_BACK: return Box{ 10, 8, 52, 46 };
    case ID_FM_CLOSE: return Box{ 418, 8, 52, 46 };
    case ID_FM_PREV: return Box{ 12, 378, 48, 44 };
    case ID_FM_NEXT: return Box{ 122, 378, 48, 44 };
    case ID_FM_NEWDIR: return Box{ 180, 378, 150, 44 };
    case ID_FM_MORE: return Box{ 338, 378, 130, 44 };
    case ID_FM_PASTE: return Box{ 180, 378, 160, 44 };
    case ID_FM_UNCLIP: return Box{ 346, 378, 122, 44 };
    case ID_FM_STOP: return Box{ 356, 432, 112, 40 };
    // окно файла: два ряда по две кнопки и «Закрити»
    case ID_FM_PLAY: return Box{ (int16_t)(w.x + 16), (int16_t)(w.y + 176), 200, 50 };
    case ID_FM_COPY: return Box{ (int16_t)(w.x + 224), (int16_t)(w.y + 176), 200, 50 };
    case ID_FM_VIEW: return Box{ (int16_t)(w.x + 16), (int16_t)(w.y + 234), 200, 50 };
    case ID_FM_DEL:   // рядом с «Переглянути», а если её нет (не текст) — во всю ширину
      if (fmKind(fmSelName) != FK_TEXT) return Box{ (int16_t)(w.x + 16), (int16_t)(w.y + 234), 408, 50 };
      return Box{ (int16_t)(w.x + 224), (int16_t)(w.y + 234), 200, 50 };
    case ID_FM_X: return Box{ (int16_t)(w.x + 16), (int16_t)by, (int16_t)(w.w - 32), 46 };
    case ID_FM_NO: return Box{ (int16_t)(w.x + 16), (int16_t)by, (int16_t)(w.w / 2 - 22), 46 };
    case ID_FM_YES: return Box{ (int16_t)(w.x + w.w / 2 + 6), (int16_t)by, (int16_t)(w.w / 2 - 22), 46 };
    case ID_FM_ABORT: return Box{ (int16_t)(w.x + 16), (int16_t)by, (int16_t)(w.w - 32), 46 };
    // окно «Інше»
    case ID_FM_REFRESH: return Box{ (int16_t)(w.x + 16), (int16_t)(w.y + 58), (int16_t)(w.w - 32), 48 };
    case ID_FM_RMDIR: return Box{ (int16_t)(w.x + 16), (int16_t)(w.y + 114), (int16_t)(w.w - 32), 48 };
    case ID_FM_FORMAT: return Box{ (int16_t)(w.x + 16), (int16_t)(w.y + (fmRoot() ? 114 : 170)), (int16_t)(w.w - 32), 48 };
  }
  if (id >= ID_FM_ROW && id < ID_FM_ROW + FM_ROWS) return Box{ 12, (int16_t)(84 + (id - ID_FM_ROW) * 48), 456, 44 };
  return Box{ 0, 0, 0, 0 };
}

static void fmRow(int k, bool pressed) {
  Box b = fmBox(ID_FM_ROW + k);
  int i = fmPage * FM_ROWS + k;
  if (i >= fm.n || !fm.ent) return;
  const FmEntry &e = fm.ent[i];
  bool air = !e.dir && fmPlayingThis(e.name);
  card(b, pressed ? C_SECONDARY : C_CARD, air ? C_OK : C_BORDER);
  fmIcon(b.x + 26, b.y + b.h / 2, e.kind, e.dir, e.dir ? C_PRIMARY : air ? C_OK : C_MUTED_FG);
  char t[32] = "»";
  if (!e.dir) fmSize(e.size, t, sizeof(t));
  int w = g.text(b.x + b.w - 14, b.y + b.h / 2 + 5, t, e.dir ? F_TXT : F_CAP, e.dir ? C_BORDER : C_MUTED_FG, AL_R);
  g.text(b.x + 50, b.y + b.h / 2 + 6, e.name, F_TXT, C_FG, AL_L, b.w - 50 - w - 28);
}

static void fmItem(int id, bool pressed) {
  Box b = fmBox(id);
  if (id >= ID_FM_ROW && id < ID_FM_ROW + FM_ROWS) return fmRow(id - ID_FM_ROW, pressed);
  switch (id) {
    case ID_FM_BACK:
      card(b, pressed ? C_SECONDARY : C_CARD);
      fmArrow(b, true, C_FG);
      break;
    case ID_FM_CLOSE: {
      card(b, pressed ? C_SECONDARY : C_CARD);
      float cx = b.x + b.w / 2.0f, cy = b.y + b.h / 2.0f;
      g.line(cx - 7, cy - 7, cx + 7, cy + 7, 2.6f, C_FG);
      g.line(cx - 7, cy + 7, cx + 7, cy - 7, 2.6f, C_FG);
      break;
    }
    case ID_FM_PREV:
    case ID_FM_NEXT:
      card(b, pressed ? C_SECONDARY : C_CARD);
      fmArrow(b, id == ID_FM_PREV, C_FG);
      break;
    case ID_FM_NEWDIR: drawButton(b, tr("Нова тека"), 0, pressed); break;
    case ID_FM_MORE: drawButton(b, tr("Інше"), 0, pressed); break;
    case ID_FM_PASTE: drawButton(b, tr("Вставити сюди"), 1, pressed); break;
    case ID_FM_UNCLIP: drawButton(b, tr("Скасувати"), 0, pressed); break;
    case ID_FM_STOP: drawButton(b, tr("Зупинити"), 2, pressed); break;
    case ID_FM_PLAY:
      if (fmPlayingThis(fmSelName)) drawButton(b, tr("Зупинити"), 2, pressed);
      else if (fmInfoOk && fm.info.playable) drawButton(b, tr("В ефір"), 1, pressed);
      else {   // пустить нельзя — кнопка погашена
        card(b, C_BG, C_BORDER);
        g.text(b.x + b.w / 2, b.y + b.h / 2 + 6, tr("В ефір"), F_TXTB, C_BORDER, AL_C);
      }
      break;
    case ID_FM_COPY: drawButton(b, tr("Копіювати"), 0, pressed); break;
    case ID_FM_VIEW: drawButton(b, tr("Переглянути"), 0, pressed); break;
    case ID_FM_DEL: drawButton(b, tr("Видалити"), 2, pressed); break;
    case ID_FM_X: drawButton(b, tr("Закрити"), 0, pressed); break;
    case ID_FM_NO: drawButton(b, tr("Скасувати"), 0, pressed); break;
    case ID_FM_YES:
      drawButton(b, fmQ == FQ_FORMAT ? tr("Далі") : fmQ == FQ_FORMAT2 ? tr("Стерти все") : tr("Видалити"), 2, pressed);
      break;
    case ID_FM_ABORT: drawButton(b, tr("Скасувати"), 0, pressed); break;
    case ID_FM_REFRESH: drawButton(b, tr("Оновити список"), 0, pressed); break;
    case ID_FM_RMDIR: drawButton(b, tr("Видалити цю теку"), 2, pressed); break;
    case ID_FM_FORMAT: drawButton(b, tr("Форматувати картку"), 2, pressed); break;
  }
}
static void fmHot(int id) {
  if (hotN < 32) hot[hotN++] = Hot{ fmBox(id), id };
  fmItem(id, false);
}

// ---- низ экрана: файл в эфире, сообщение или подсказка
static void fmFoot() {
  char t[FM_NAME + 80], a[24], b[24];
  g.fill(0, 428, W, 52, C_BG);
  bool note = fmNoteUntil && millis() < fmNoteUntil;
  fmNoteShown = note;
  if (fm.playing && !note) {
    Box c = Box{ 12, 432, 336, 40 };
    card(c, C_CARD, C_OK);
    float k = 0.5f + 0.5f * sinf(millis() / 1000.0f * 3);
    g.circle(c.x + 18, c.y + 20, 4 + 1.5f * k, C_OK);
    fmtTime(fm.playS, a, sizeof(a));
    fmtTime(fm.playTotalS, b, sizeof(b));
    if (fm.playTotalS) snprintf(t, sizeof(t), "%s / %s", a, b);
    else snprintf(t, sizeof(t), "%s", a);
    int w = g.text(c.x + c.w - 10, c.y + 26, t, F_CAPB, C_FG, AL_R);
    g.text(c.x + 32, c.y + 26, fm.playName, F_CAP, C_FG, AL_L, c.w - 32 - w - 22);
    fmItem(ID_FM_STOP, pressId == ID_FM_STOP);
    return;
  }
  if (note) {
    g.box(12, 434, 456, 36, 18, C_PRIMARY);
    g.text(240, 458, fmNoteText, F_CAPB, C_PRIMARY_FG, AL_C, 432);
    return;
  }
  if (fmClip[0]) {
    snprintf(t, sizeof(t), tr("Копіюється «%s». Відкрийте теку й натисніть «Вставити сюди»."), fmBase(fmClip));
    wrap(16, 446, 448, 18, t, F_CAP, C_PRIMARY, 2);
  } else wrap(16, 446, 448, 18, "Торкніться файлу: пустити в ефір, скопіювати, видалити. Тека відкривається дотиком.", F_CAP, C_MUTED_FG, 2);
}

// ---- основа: шапка, список, низ
static void fmScreen() {
  char t[FM_PATH + 40];
  g.fill(0, 0, W, H, C_BG);
  fmHot(ID_FM_BACK);
  fmHot(ID_FM_CLOSE);
  g.text(74, 30, fmRoot() ? tr("Картка пам'яті") : fmBase(fmCur), F_H2, C_FG, AL_L, 330);
  snprintf(t, sizeof(t), tr("вільно %u МБ з %u МБ"), (unsigned)fm.freeMb, (unsigned)fm.totalMb);
  int w = g.text(410, 50, t, F_CAP, C_MUTED_FG, AL_R);
  if (!fmRoot()) {   // длинный путь показываем с конца
    const char *p = fmCur;
    while (*p && m2::Gfx::textW(p, F_CAP) > 330 - w) {
      p++;
      while (((uint8_t)*p & 0xC0) == 0x80) p++;
    }
    g.text(74, 50, p, F_CAP, C_MUTED_FG);
  }
  g.fill(12, 66, 456, 1, C_BORDER);
  bool ready = fmListReady();
  if (!ready || !fm.n) {
    const char *m = fmListBad ? "Не вдалося прочитати теку" : !ready ? "Читаю картку…" : "Тека порожня";
    g.text(240, 220, tr(m), F_TXT, C_MUTED_FG, AL_C);
  } else
    for (int k = 0; k < FM_ROWS && fmPage * FM_ROWS + k < fm.n; k++) fmHot(ID_FM_ROW + k);
  if (ready && fmPages() > 1) {
    fmHot(ID_FM_PREV);
    fmHot(ID_FM_NEXT);
    snprintf(t, sizeof(t), "%d / %d", fmPage + 1, fmPages());
    g.text(91, 406, t, F_CAPB, C_FG, AL_C);
  } else if (ready && fm.n) {
    snprintf(t, sizeof(t), tr("записів: %d"), fm.n);
    g.text(16, 406, t, F_CAP, C_MUTED_FG);
  }
  if (ready && fm.more) {
    snprintf(t, sizeof(t), tr("ще %d не показано"), fm.more);
    g.text(16, 374, t, F_CAP, C_WARN);
  }
  if (fmClip[0]) {
    fmHot(ID_FM_PASTE);
    fmHot(ID_FM_UNCLIP);
  } else {
    fmHot(ID_FM_NEWDIR);
    fmHot(ID_FM_MORE);
  }
  fmFoot();
  if (fm.playing && !fmNoteShown && hotN < 32) hot[hotN++] = Hot{ fmBox(ID_FM_STOP), ID_FM_STOP };
}

static void fmWinFrame(const char *title, uint16_t edge = C_BORDER) {
  Box w = fmWin();
  hotN = 0;                       // под окном ничего не нажимается
  g.fillA(0, 0, W, H, 0x0000, 150);
  card(w, C_CARD, edge);
  if (title) g.text(w.x + 20, w.y + 36, title, F_H, C_FG, AL_L, w.w - 40);
}

static void fmItemInfo() {   // строки сведений в окне файла (перерисовываются, когда сведения прочитаны)
  Box w = fmWin();
  char t[160], a[40], b[40];
  g.fill(w.x + 16, w.y + 86, w.w - 32, 84, C_CARD);
  fmSize(fmSelSize, a, sizeof(a));
  uint8_t kind = fmKind(fmSelName);
  snprintf(t, sizeof(t), "%s · %s", kind == FK_WAV ? tr("Звук WAV") : kind == FK_MP3 ? tr("Звук MP3") : kind == FK_SOUNDX ? tr("Звук") : kind == FK_TEXT ? tr("Текст") : tr("Файл"), a);
  g.text(w.x + 20, w.y + 106, t, F_TXT, C_FG);
  if (!fmInfoOk) {
    g.text(w.x + 20, w.y + 132, tr("Читаю файл…"), F_CAP, C_MUTED_FG);
    return;
  }
  const FmInfo &i = fm.info;
  if (i.playable) {
    fmDur(i.durS, a, sizeof(a));
    if (i.kind == FK_MP3) snprintf(b, sizeof(b), tr("%u кбіт/с"), (unsigned)i.kbps);
    else snprintf(b, sizeof(b), tr("%u біт"), (unsigned)i.bits);
    snprintf(t, sizeof(t), "%s · %u,%u %s · %s · %s", a, (unsigned)(i.rate / 1000), (unsigned)(i.rate % 1000 / 100), tr("кГц"), i.chans == 2 ? tr("стерео") : tr("моно"), b);
    g.text(w.x + 20, w.y + 132, t, F_CAP, C_MUTED_FG, AL_L, w.w - 40);
    if (fmPlayingThis(fmSelName)) g.text(w.x + 20, w.y + 156, tr("Зараз в ефірі"), F_CAPB, C_OK);
  } else {
    const char *why = i.why == 2 ? "Такий запис в ефір не піде: потрібен MP3 або WAV (звичайний, не стиснений)."
                      : i.why == 3 ? "Файл пошкоджено, або це не той запис, яким він названий."
                                   : "Це не звук — в ефір його пустити не можна.";
    wrap(w.x + 20, w.y + 132, w.w - 40, 20, why, F_CAP, i.why == 1 ? C_MUTED_FG : C_WARN, 2);
  }
}

static void fmOverlay() {
  char t[FM_NAME + 200];
  Box w = fmWin();
  switch (fmSt) {
    case FS_ITEM:
      fmWinFrame(NULL);
      g.text(w.x + 20, w.y + 40, fmSelName, F_TXTB, C_FG, AL_L, w.w - 40);
      g.fill(w.x + 16, w.y + 62, w.w - 32, 1, C_BORDER);
      fmItemInfo();
      fmHot(ID_FM_PLAY);
      fmHot(ID_FM_COPY);
      if (fmKind(fmSelName) == FK_TEXT) fmHot(ID_FM_VIEW);
      fmHot(ID_FM_DEL);
      fmHot(ID_FM_X);
      break;
    case FS_TEXT:
      fmWinFrame(NULL);
      g.text(w.x + 20, w.y + 36, fmSelName, F_TXTB, C_FG, AL_L, w.w - 40);
      g.fill(w.x + 16, w.y + 52, w.w - 32, 1, C_BORDER);
      if (fm.info.text[0]) wrap(w.x + 20, w.y + 78, w.w - 40, 20, fm.info.text, F_CAP, C_FG, 12);
      else g.text(w.x + 20, w.y + 82, tr("Файл порожній"), F_CAP, C_MUTED_FG);
      fmHot(ID_FM_X);
      break;
    case FS_MENU:
      fmWinFrame(tr("Інші дії"));
      fmHot(ID_FM_REFRESH);
      if (!fmRoot()) fmHot(ID_FM_RMDIR);
      fmHot(ID_FM_FORMAT);
      fmHot(ID_FM_X);
      break;
    case FS_CONFIRM:
      fmWinFrame(fmQ == FQ_DEL_FILE ? tr("Видалити файл?") : fmQ == FQ_DEL_DIR ? tr("Видалити теку?") : fmQ == FQ_FORMAT ? tr("Форматувати картку?") : tr("Точно стерти все?"), C_DANGER);
      if (fmQ == FQ_DEL_FILE) snprintf(t, sizeof(t), tr("«%s» буде стерто з картки назавжди."), fmSelName);
      else if (fmQ == FQ_DEL_DIR) snprintf(t, sizeof(t), tr("Теку «%s» буде стерто разом з усім, що в ній (записів: %d)."), fmBase(fmCur), fm.n + fm.more);
      else if (fmQ == FQ_FORMAT) snprintf(t, sizeof(t), "%s", tr("Усі файли й теки на картці буде стерто назавжди. Картка отримає розмітку FAT32."));
      else snprintf(t, sizeof(t), tr("На картці зайнято %u МБ. Після форматування повернути файли не вдасться."), (unsigned)(fm.totalMb - fm.freeMb));
      wrap(w.x + 20, w.y + 72, w.w - 40, 22, t, F_TXT, C_MUTED_FG, 4);
      fmHot(ID_FM_NO);
      fmHot(ID_FM_YES);
      break;
    case FS_BUSY: {
      const char *title = fmBusyOp == FM_COPY ? "Копіюю…" : fmBusyOp == FM_DELETE ? "Видаляю…" : fmBusyOp == FM_FORMAT ? "Форматую картку…" : "Працюю з карткою…";
      fmWinFrame(tr(title));
      if (fmBusyOp == FM_FORMAT) wrap(w.x + 20, w.y + 72, w.w - 40, 22, "Це триває до хвилини. Не вимикайте передавач і не виймайте картку.", F_TXT, C_MUTED_FG, 3);
      else if (fmBusyOp == FM_COPY) {
        g.text(w.x + 20, w.y + 70, fmBase(fmClip), F_CAP, C_MUTED_FG, AL_L, w.w - 40);
        fmSeenProg = -1;
        fmHot(ID_FM_ABORT);
      } else if (fmBusyOp == FM_DELETE) fmHot(ID_FM_ABORT);
      break;
    }
  }
}
static void fmProgress() {
  Box w = fmWin();
  char t[16];
  int p = fm.progress > 100 ? 100 : fm.progress;
  g.fill(w.x + 16, w.y + 84, w.w - 32, 44, C_CARD);
  g.box(w.x + 20, w.y + 92, w.w - 100, 14, 7, C_SECONDARY);
  if (p) g.box(w.x + 20, w.y + 92, 14 + (w.w - 114) * p / 100, 14, 7, C_PRIMARY);
  snprintf(t, sizeof(t), "%d %%", p);
  g.text(w.x + w.w - 20, w.y + 105, t, F_TXTB, C_FG, AL_R);
}

static void fmDraw() {
  hotN = 0;
  fmScreen();
  if (fmSt != FS_LIST) fmOverlay();
  if (fmSt == FS_BUSY && fmBusyOp == FM_COPY) fmProgress();
  fmSeenPlay = fm.playing ? (int)fm.playS : -1;
}

static void fmOpen() {
  modal = M_FILES;
  fmSt = FS_LIST;
  strlcpy(fmCur, "/", sizeof(fmCur));
  fmPage = 0;
  fmSeenDone = fm.doneSeq;
  fmList();
  needFull = true;
}
static void fmClose() {
  fmClip[0] = 0;
  modal = M_NONE;
  needFull = true;
}
static void fmBusy(int op, const char *a, const char *b) {   // долгая работа: окно ожидания
  if (!uiFm(op, a, b)) {
    fmSt = FS_LIST;
    fmNote("Картка зайнята — спробуйте ще раз");
    fmDraw();
    return;
  }
  fmBusyOp = op;
  fmSt = FS_BUSY;
  fmDraw();
}

// Клавиатура вернула имя новой папки (пусто — передумали).
static void fmNewDir(const char *name) {
  modal = M_FILES;
  fmSt = FS_LIST;
  needFull = true;
  if (!name || !name[0]) return;
  char p[FM_PATH];
  fmJoin(p, sizeof(p), fmCur, name);
  if (uiFm(FM_MKDIR, p, NULL)) {
    fmBusyOp = FM_MKDIR;
    fmSt = FS_BUSY;
  } else fmNote("Картка зайнята — спробуйте ще раз");
}

static void fmActivate(int id) {
  if (id >= ID_FM_ROW && id < ID_FM_ROW + FM_ROWS) {
    int i = fmPage * FM_ROWS + id - ID_FM_ROW;
    if (fmSt != FS_LIST || i >= fm.n || !fmListReady()) return;
    const FmEntry &e = fm.ent[i];
    if (e.dir) {
      char p[FM_PATH];
      fmJoin(p, sizeof(p), fmCur, e.name);
      if (strlen(fmCur) + strlen(e.name) + 2 >= sizeof(fmCur)) return fmNote("Надто довгий шлях");
      strlcpy(fmCur, p, sizeof(fmCur));
      fmPage = 0;
      fmList();
    } else {
      fmJoin(fmSelPath, sizeof(fmSelPath), fmCur, e.name);
      strlcpy(fmSelName, e.name, sizeof(fmSelName));
      fmSelSize = e.size;
      fmInfo();
      fmSt = FS_ITEM;
    }
    fmDraw();
    return;
  }
  switch (id) {
    case ID_FM_CLOSE: fmClose(); return;
    case ID_FM_BACK:
      if (fmRoot()) return fmClose();
      {
        char *sl = strrchr(fmCur, '/');
        if (sl == fmCur) fmCur[1] = 0;
        else if (sl) *sl = 0;
      }
      fmPage = 0;
      fmList();
      break;
    case ID_FM_PREV: fmPage = fmPage > 0 ? fmPage - 1 : fmPages() - 1; break;
    case ID_FM_NEXT: fmPage = fmPage + 1 < fmPages() ? fmPage + 1 : 0; break;
    case ID_FM_NEWDIR:
      kbText[0] = 0;
      kbShift = true;
      kbDigits = false;
      kbForDir = true;
      modal = M_KEYS;
      drawModal();
      return;
    case ID_FM_MORE: fmSt = FS_MENU; break;
    case ID_FM_REFRESH:
      fmSt = FS_LIST;
      fmList();
      break;
    case ID_FM_PASTE: return fmBusy(FM_COPY, fmClip, fmCur);
    case ID_FM_UNCLIP: fmClip[0] = 0; break;
    case ID_FM_STOP: uiFm(FM_STOP, NULL, NULL); break;
    case ID_FM_PLAY:
      if (fmPlayingThis(fmSelName)) uiFm(FM_STOP, NULL, NULL);
      else if (fmInfoOk && fm.info.playable) {
        if (uiFm(FM_PLAY, fmSelPath, NULL)) fmNote("Файл пішов в ефір");
      } else return;
      fmSt = FS_LIST;
      break;
    case ID_FM_COPY:
      strlcpy(fmClip, fmSelPath, sizeof(fmClip));
      fmSt = FS_LIST;
      break;
    case ID_FM_VIEW:
      if (fmInfoOk) fmSt = FS_TEXT;
      break;
    case ID_FM_DEL:
      fmQ = FQ_DEL_FILE;
      fmSt = FS_CONFIRM;
      break;
    case ID_FM_RMDIR:
      if (fmRoot()) return;   // сама карта — не папка: её не удаляют, а форматируют
      fmQ = FQ_DEL_DIR;
      fmSt = FS_CONFIRM;
      break;
    case ID_FM_FORMAT:
      fmQ = FQ_FORMAT;
      fmSt = FS_CONFIRM;
      break;
    case ID_FM_X:
    case ID_FM_NO: fmSt = fmSt == FS_TEXT ? FS_ITEM : FS_LIST; break;
    case ID_FM_YES:
      if (fmQ == FQ_FORMAT) {
        fmQ = FQ_FORMAT2;
        break;
      }
      if (fmQ == FQ_DEL_FILE) return fmBusy(FM_DELETE, fmSelPath, NULL);
      if (fmQ == FQ_DEL_DIR) return fmBusy(FM_DELETE, fmCur, NULL);
      return fmBusy(FM_FORMAT, NULL, NULL);
    case ID_FM_ABORT: uiFm(FM_CANCEL, NULL, NULL); return;
    default: return;
  }
  fmDraw();
}

// Чем кончилась работа с картой — словами.
static const char *fmWhy(int r) {
  switch (r) {
    case FM_E_NOCARD: return "Картки немає";
    case FM_E_NOSPACE: return "На картці не вистачає місця";
    case FM_E_BUSY: return "Файл зараз в ефірі або записується";
    case FM_E_CANCEL: return "Скасовано";
    case FM_E_NAME: return "Така назва не годиться або вже зайнята";
    case FM_E_DEEP: return "Надто багато вкладених тек";
    case FM_E_REC: return "Спершу зупиніть запис";
    default: return "Не вдалося — картка не відповіла";
  }
}

// Каждый кадр, пока окно открыто.
static void fmLive() {
  if (!view.sd && fmBusyOp != FM_FORMAT) {   // карту вынули (после форматирования она подключается заново — это не «вынули»)
    fmClose();
    toast(tr("Картку пам'яті вийнято"));
    return;
  }
  if (fmWantList && fmSt != FS_BUSY) {   // карта была занята — просим снова
    uint32_t from = fm.listSeq;
    if (uiFm(FM_LIST, fmCur, NULL)) {
      fmWantList = false;
      fmListFrom = from;
    }
  }
  if (fmWantInfo && fmSt == FS_ITEM) {
    uint32_t from = fm.infoSeq;
    if (uiFm(FM_INFO, fmSelPath, NULL)) {
      fmWantInfo = false;
      fmInfoFrom = from;
    }
  }
  bool redraw = false;
  if (!fmWantList && fm.listSeq != fmListFrom && fm.listSeq != fmSeenList && fm.op != FM_LIST) {   // список пришёл (или не прочитался)
    fmSeenList = fm.listSeq;
    fmListBad = !fm.listOk;
    if (fmPage >= fmPages()) fmPage = fmPages() - 1;
    redraw = fmSt == FS_LIST;
  }
  if (fmSt == FS_ITEM && !fmInfoOk && !fmWantInfo && fm.infoSeq != fmInfoFrom && fm.op != FM_INFO && !strcmp(fm.info.name, fmSelName)) {   // сведения о файле
    fmInfoOk = true;
    fmItemInfo();
    fmItem(ID_FM_PLAY, false);
  }
  if (fm.doneSeq != fmSeenDone) {
    fmSeenDone = fm.doneSeq;
    int op = fm.lastOp, r = fm.result;
    if (fmSt == FS_BUSY && op == fmBusyOp) {   // кончилась долгая работа, которой ждём
      fmSt = FS_LIST;
      fmBusyOp = 0;
      if (r) fmNote(fmWhy(r));
      else if (op == FM_COPY) {
        fmNote("Скопійовано");
        fmClip[0] = 0;
      } else if (op == FM_MKDIR) fmNote("Теку створено");
      else if (op == FM_FORMAT) {
        fmNote("Картку відформатовано");
        strlcpy(fmCur, "/", sizeof(fmCur));
        fmClip[0] = 0;
      } else if (op == FM_DELETE) {
        fmNote("Видалено");
        if (fmQ == FQ_DEL_DIR && !fmRoot()) {   // удалили открытую папку — выйти из неё
          char *sl = strrchr(fmCur, '/');
          if (sl == fmCur) fmCur[1] = 0;
          else if (sl) *sl = 0;
        }
      }
      fmQ = 0;
      fmPage = 0;
      fmList();
      redraw = true;
    }
  }
  if (redraw) {
    fmDraw();
    return;
  }
  if (fmSt == FS_BUSY && fmBusyOp == FM_COPY && fm.progress != fmSeenProg) {
    fmSeenProg = fm.progress;
    fmProgress();
  }
  if (frameNo & 1) {   // ожидание без хода в процентах — бегущие точки (правило: работа должна быть видна)
    Box w = fmWin();
    if (fmSt == FS_BUSY && fmBusyOp != FM_COPY) waitDots(w.x + w.w / 2 - 18, fmBusyOp == FM_FORMAT ? w.y + 160 : w.y + 100, C_CARD);
    else if (fmSt == FS_ITEM && !fmInfoOk) waitDots(w.x + 24 + m2::Gfx::textW(tr("Читаю файл…"), F_CAP), w.y + 127, C_CARD);
    else if (fmSt == FS_LIST && !fmListReady() && !fmListBad) waitDots(222, 246, C_BG);
  }
  if (fmSt != FS_LIST) return;
  bool note = fmNoteUntil && millis() < fmNoteUntil;
  int pl = fm.playing ? (int)fm.playS : -1;
  if (note != fmNoteShown || (pl >= 0) != (fmSeenPlay >= 0)) {   // сообщение появилось или ушло; файл пошёл в эфир или кончился
    fmDraw();
    return;
  }
  if (pl != fmSeenPlay || (fm.playing && !note && frameNo % 6 == 0)) {   // время идёт; огонёк дышит
    fmSeenPlay = pl;
    fmFoot();
  }
}

// Файл в эфире кончился — сказать об этом, где бы владелец ни был (на любой странице).
static void fmEndNotice() {
  uint8_t e = fm.playEnd;
  if (!e || fm.playing || fmSeenEndSeq == fm.playSeq) return;
  fmSeenEndSeq = fm.playSeq;
  const char *m = e == 1 ? "Файл доіграв — в ефірі знову звук джерела" : e == 3 ? "Файл не читається — в ефірі знову звук джерела" : NULL;
  if (!m) return;
  if (modal == M_FILES) fmNote(m);
  else toast(tr(m));
}
