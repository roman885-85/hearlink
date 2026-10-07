// Заглушка среды Arduino для сборки экрана передатчика на компьютере (tools/sim).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define PROGMEM
#define PI 3.14159265358979f
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define pgm_read_ptr(p) (*(void *const *)(p))
#define IRAM_ATTR
uint32_t millis();
inline uint32_t micros() { return millis() * 1000u; }
