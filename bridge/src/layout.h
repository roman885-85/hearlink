// Розкладка флеш-пам'яті моста (Pi Pico, 2 МБ) — спільна для завантажувача і програми.
//   0x10000000  завантажувач (з boot2), до 28 КБ — передавач його не переписує ніколи
//   0x10007000  відомості про програму (4 КБ): розмір, CRC-32, версія — пише завантажувач після перевірки
//   0x10008000  програма моста
#pragma once
#include <stdint.h>

#define FLASH_BASE_ADDR 0x10000000u
#define LOADER_MAX 0x7000u
#define INFO_OFF 0x7000u
#define APP_OFF 0x8000u
#define APP_ADDR (FLASH_BASE_ADDR + APP_OFF)
#define APP_MAX (2u * 1024u * 1024u - APP_OFF)

#define INFO_MAGIC 0x4D695374u   // «tSiM»
#define STAY_MAGIC 0x4C4F4144u   // у watchdog scratch[0]: програма просить завантажувач чекати команд

typedef struct {
  uint32_t magic;
  uint32_t size;    // байтів програми від APP_ADDR
  uint32_t crc32;   // bp_crc32 цих байтів
  uint32_t ver;     // версія: старший байт — головна, молодший — підверсія
  uint32_t magic2;  // ~magic
} app_info_t;
