// Одна строка экрана во время перелистывания: прежняя страница уезжает, новая въезжает следом.
// a — строка прежней страницы, b — новой, off — на сколько точек уже сдвинулись (0…w).
// right: новая входит справа (прежняя уходит влево), иначе — слева.
// Вызывается из прерывания экрана (на каждую строку кадра), поэтому всегда встраивается и ничего не зовёт, кроме memcpy.
#pragma once
#include <stdint.h>
#include <string.h>

static inline __attribute__((always_inline)) void slideRow(uint16_t *d, const uint16_t *a, const uint16_t *b, int off, bool right, int w) {
  if (right) {
    memcpy(d, a + off, (w - off) * 2);
    memcpy(d + w - off, b, off * 2);
  } else {
    memcpy(d, b + w - off, off * 2);
    memcpy(d + off, a, (w - off) * 2);
  }
}
