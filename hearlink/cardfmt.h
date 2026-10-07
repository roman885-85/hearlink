// Разбор заголовков звуковых файлов с карты: WAV и MP3. Нужен и списку файлов (сведения о файле), и проигрывателю.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"

struct WavFmt {
  uint32_t rate = 0, dataPos = 0, dataLen = 0;
  uint16_t chans = 0, bits = 0, block = 0, tag = 0;
};

// 0 — хорошо; 2 — вид записи не поддержан; 3 — файл повреждён или это не WAV
static int wavParse(FILE *f, WavFmt &w) {
  uint8_t h[12];
  w = WavFmt();
  if (fseek(f, 0, SEEK_END)) return 3;
  long sz = ftell(f);
  if (fseek(f, 0, SEEK_SET) || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return 3;
  bool haveFmt = false;
  long at = 12;
  for (int guard = 0; guard < 64 && at + 8 <= sz; guard++) {
    uint8_t c[8];
    if (fseek(f, at, SEEK_SET) || fread(c, 1, 8, f) != 8) return 3;
    uint32_t len = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
    if (!memcmp(c, "fmt ", 4)) {
      uint8_t b[40];
      uint32_t k = len < 40 ? len : 40;
      if (k < 16 || fread(b, 1, k, f) != k) return 3;
      w.tag = b[0] | b[1] << 8;
      w.chans = b[2] | b[3] << 8;
      w.rate = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
      w.block = b[12] | b[13] << 8;
      w.bits = b[14] | b[15] << 8;
      if (w.tag == 0xFFFE && k >= 26) w.tag = b[24] | b[25] << 8;   // расширенный заголовок: настоящий вид записи — дальше
      haveFmt = true;
    } else if (!memcmp(c, "data", 4)) {
      if (!haveFmt) return 3;
      w.dataPos = at + 8;
      // у недописанного файла (питание пропало во время записи) длина в заголовке нулевая или больше файла — берём до конца
      w.dataLen = len == 0 || (uint64_t)w.dataPos + len > (uint64_t)sz ? (uint32_t)(sz - w.dataPos) : len;
      break;
    }
    at += 8 + len + (len & 1);
  }
  if (!w.dataPos) return 3;
  if (w.tag != 1 || w.chans < 1 || w.chans > 2 || (w.bits != 8 && w.bits != 16 && w.bits != 24 && w.bits != 32) || w.rate < 8000 || w.rate > 96000 ||
      w.block != w.chans * w.bits / 8)
    return 2;
  return 0;
}

struct Mp3Info {
  uint32_t rate = 0, durS = 0, audioPos = 0;
  uint16_t kbps = 0;
  uint8_t chans = 0;
};

// Где кончается метка ID3 в начале файла (0 — метки нет).
static long mp3SkipTag(FILE *f) {
  uint8_t b[10];
  if (fseek(f, 0, SEEK_SET) || fread(b, 1, 10, f) != 10 || memcmp(b, "ID3", 3)) return 0;
  return 10 + (((long)(b[6] & 0x7F) << 21) | ((long)(b[7] & 0x7F) << 14) | ((long)(b[8] & 0x7F) << 7) | (b[9] & 0x7F)) + ((b[5] & 0x10) ? 10 : 0);
}

// 0 — хорошо; 3 — кадров MP3 в файле не нашлось. Вызывать только из одной задачи (общий буфер).
static int mp3Parse(FILE *f, Mp3Info &m) {
  static const uint16_t BR1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };   // MPEG-1, слой 3
  static const uint16_t BR2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };       // MPEG-2 и 2.5
  static const uint32_t SR[4][3] = { { 11025, 12000, 8000 }, { 0, 0, 0 }, { 22050, 24000, 16000 }, { 44100, 48000, 32000 } };
  static uint8_t *buf;   // во внешней памяти: внутренняя у передатчика в обрез
  const int BUFN = 2048;
  if (!buf) buf = (uint8_t *)heap_caps_malloc(BUFN, MALLOC_CAP_SPIRAM);
  m = Mp3Info();
  if (!buf) return 3;
  if (fseek(f, 0, SEEK_END)) return 3;
  long sz = ftell(f), start = mp3SkipTag(f);
  if (start >= sz) return 3;
  for (long at = start; at < start + 262144 && at + 4 <= sz;) {
    if (fseek(f, at, SEEK_SET)) return 3;
    int n = (int)fread(buf, 1, BUFN, f);
    if (n < 4) break;
    for (int i = 0; i + 4 <= n; i++) {
      if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) continue;
      int ver = (buf[i + 1] >> 3) & 3, layer = (buf[i + 1] >> 1) & 3, bri = buf[i + 2] >> 4, sri = (buf[i + 2] >> 2) & 3;
      if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) continue;
      uint32_t rate = SR[ver][sri], kbps = ver == 3 ? BR1[bri] : BR2[bri];
      uint32_t flen = (ver == 3 ? 144000u : 72000u) * kbps / rate + ((buf[i + 2] >> 1) & 1);
      uint8_t nx[2];   // настоящий кадр: следующий начинается ровно там, где положено
      if (!fseek(f, at + i + flen, SEEK_SET) && fread(nx, 1, 2, f) == 2 && (nx[0] != 0xFF || (nx[1] & 0xE0) != 0xE0)) continue;
      m.rate = rate;
      m.kbps = kbps;
      m.chans = (buf[i + 3] >> 6) == 3 ? 1 : 2;
      m.audioPos = at + i;
      uint32_t spf = ver == 3 ? 1152 : 576, frames = 0;
      int xo = 4 + (ver == 3 ? (m.chans == 1 ? 17 : 32) : (m.chans == 1 ? 9 : 17));
      uint8_t x[12];   // метка «Xing»/«Info»: в ней число кадров — по нему длительность точная и при переменной плотности
      if (!fseek(f, at + i + xo, SEEK_SET) && fread(x, 1, 12, f) == 12 && (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4)) && (x[7] & 1))
        frames = (uint32_t)x[8] << 24 | x[9] << 16 | x[10] << 8 | x[11];
      uint32_t bytes = (uint32_t)(sz - at - i);
      if (frames) {
        m.durS = (uint32_t)((uint64_t)frames * spf / rate);
        if (m.durS) m.kbps = (uint16_t)((uint64_t)bytes * 8 / m.durS / 1000);
      } else m.durS = kbps ? (uint32_t)((uint64_t)bytes * 8 / (kbps * 1000)) : 0;
      return 0;
    }
    at += n - 3;
  }
  return 3;
}
