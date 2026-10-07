// Языки интерфейса передатчика и приёмника. Основной — украинский: все надписи в исходниках написаны на нём.
// Английский — перевод по словарю (lang_en.h, делается из tools/lang_en.py сценарием tools/make_lang.py):
// tr("надпис") отдаёт английскую надпись, если выбран английский и она есть в словаре, иначе — исходную.
// Язык выбирают на передатчике («Налашт.» › «Мова / Language»); приёмники узнают его из пакетов звука сами.
// Сообщения в порт остаются украинскими: это для отладки, а не для людей у экрана.
#pragma once
#include <stdint.h>
#include <string.h>
#include "lang_en.h"

static volatile uint8_t uiLang;   // 0 — українська, 1 — English

static inline uint32_t langFnv(const char *s, bool *high) {
  uint32_t h = 2166136261u;
  bool hi = false;
  for (; *s; s++) {
    h ^= (uint8_t)*s;
    h *= 16777619u;
    hi |= (*s & 0x80) != 0;
  }
  if (high) *high = hi;
  return h;
}

static const char *tr(const char *s) {
  static uint32_t hash[LANG_EN_N];
  static bool ready;
  if (!uiLang || !s || !*s) return s;
  if (!ready) {   // один раз: числа-приметы всех украинских надписей словаря
    for (int i = 0; i < (int)LANG_EN_N; i++) hash[i] = langFnv(LANG_EN[i][0], nullptr);
    ready = true;
  }
  bool hi;
  uint32_t h = langFnv(s, &hi);
  if (!hi) return s;   // в надписи нет ни одной нелатинской буквы — переводить нечего
  for (int i = 0; i < (int)LANG_EN_N; i++)
    if (hash[i] == h && !strcmp(LANG_EN[i][0], s)) return LANG_EN[i][1];
  return s;
}
