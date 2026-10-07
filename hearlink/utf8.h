// Мелочи для текста UTF-8 (имена приёмников, клавиатура передатчика). Отдельный файл — чтобы экран передатчика
// собирался и на компьютере (tools/sim) без остальной прошивки.
#pragma once
#include <stdint.h>
#include <string.h>

// Скопировать текст UTF-8 в место на max байт (с нулём), не разрывая букву.
static inline void utf8Copy(char *dst, const char *src, size_t max) {
  size_t n = strlen(src);
  if (n >= max) {
    n = max - 1;
    while (n && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  }
  memmove(dst, src, n);
  dst[n] = 0;
}
// Убрать из текста обрывки букв (байты, которые не складываются в знаки UTF-8) — на месте.
static inline void utf8Clean(char *s) {
  char *w = s;
  for (const uint8_t *r = (const uint8_t *)s; *r;) {
    int n = *r < 0x80 ? 1 : (*r & 0xE0) == 0xC0 ? 2 : (*r & 0xF0) == 0xE0 ? 3 : (*r & 0xF8) == 0xF0 ? 4 : 0;
    bool ok = n > 0;
    for (int i = 1; ok && i < n; i++) ok = (r[i] & 0xC0) == 0x80;
    if (!ok) {
      r++;
      continue;
    }
    for (int i = 0; i < n; i++) *w++ = (char)*r++;
  }
  *w = 0;
}
static inline int utf8Letters(const char *s) {
  int n = 0;
  for (; *s; s++) n += ((uint8_t)*s & 0xC0) != 0x80;
  return n;
}
