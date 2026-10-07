// Проверочные звуки в разделе данных платы: голос «Увага! Йде перевірка звуку» (golos) и мелодии (muzyka, muzyka2 …).
// Файл собирает tools/make_sounds.py, в плату заливает tools/flash_assets.sh. Формат: «HLPK», число записей,
// записи (имя 16 байт, смещение, длина), затем сами записи. Последний байт имени — вид записи:
// 0 — звук без сжатия (32 кГц, моно, 16 бит), 1 — MP3 (32 кГц, моно или стерео; разбирает txmp3.h).
#pragma once
#include "esp_partition.h"
#include "config.h"

struct AssetRec {
  char name[16];
  uint32_t off, size;
};
static const esp_partition_t *assetPart;
// Раздел звуков отображён в память: чтение идёт через кэш. Раньше звук читался esp_partition_read — а любое такое
// чтение на доли миллисекунды выключает кэш на обоих ядрах, и вывод кадра экрана в это время стоит. Мелодия MP3
// читалась 8 раз в секунду, голос объявления — 16 раз: картинка от этого подёргивалась (владелец 07.10: «при работе
// передатчика картинка начинает дергаться и срываться»).
static const uint8_t *assetMap;
static bool assetRead(uint32_t off, void *dst, uint32_t len) {
  if (!assetPart || off + len > assetPart->size) return false;
  if (assetMap) {
    memcpy(dst, assetMap + off, len);
    return true;
  }
  return esp_partition_read(assetPart, off, dst, len) == ESP_OK;
}
static AssetRec assetTab[8];
static int assetN;
static char assetTitles[8][56];      // названия мелодий (запись «nazvy»: строки «имя=название»)

static bool assetsBegin() {
  assetPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "assets");
  if (!assetPart) return false;
  uint8_t head[8];
  if (esp_partition_read(assetPart, 0, head, 8) != ESP_OK || memcmp(head, "HLPK", 4)) return false;
  uint32_t n;
  memcpy(&n, head + 4, 4);
  if (n > 8) return false;
  if (esp_partition_read(assetPart, 8, assetTab, n * sizeof(AssetRec)) != ESP_OK) return false;
  assetN = n;
  {
    const void *ptr = nullptr;
    esp_partition_mmap_handle_t mh;
    if (esp_partition_mmap(assetPart, 0, assetPart->size, ESP_PARTITION_MMAP_DATA, &ptr, &mh) == ESP_OK) assetMap = (const uint8_t *)ptr;
  }
  for (int i = 0; i < assetN; i++)
    if (!strncmp(assetTab[i].name, "nazvy", 16)) {
      static char txt[640];
      uint32_t len = assetTab[i].size < sizeof(txt) - 1 ? assetTab[i].size : sizeof(txt) - 1;
      if (esp_partition_read(assetPart, assetTab[i].off, txt, len) != ESP_OK) break;
      txt[len] = 0;
      for (char *ln = strtok(txt, "\n"); ln; ln = strtok(NULL, "\n")) {
        char *eq = strchr(ln, '=');
        if (!eq) continue;
        *eq = 0;
        for (int k = 0; k < assetN; k++)
          if (!strncmp(assetTab[k].name, ln, 15)) strlcpy(assetTitles[k], eq + 1, sizeof(assetTitles[k]));
      }
    }
  return true;
}

// k-я мелодия (с единицы): номер записи или −1
static int assetMusic(int k) {
  for (int i = 0; i < assetN; i++)
    if (!strncmp(assetTab[i].name, "muzyka", 6) && !--k) return i;
  return -1;
}

static inline bool assetIsMp3(int i) {
  return assetTab[i].name[15] == 1;
}

// Мелодии играются по очереди: следующая запись, чьё имя начинается с «muzyka».
static int assetMusicAt = -1;
static const char *assetNextMusic() {
  for (int k = 1; k <= assetN; k++) {
    int i = (assetMusicAt + k + assetN) % assetN;
    if (!strncmp(assetTab[i].name, "muzyka", 6)) {
      assetMusicAt = i;
      return assetTab[i].name;
    }
  }
  return "muzyka";
}

static int assetFind(const char *name) {
  for (int i = 0; i < assetN; i++)
    if (!strncmp(assetTab[i].name, name, 16)) return i;
  return -1;
}
