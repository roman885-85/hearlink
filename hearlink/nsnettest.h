// Проба нейросетевого шумоподавления ESP-SR (NSNet2) — не в эфире, а над файлом с карты памяти передатчика.
//
// Владелец 11.10.2026: «скачивай модель и пробуй нейросеть». В ядре Arduino 3.3.12 лежит ESP-SR 2.5.3 с кодом NSNet
// (libnsnet.a), но без самой модели; модель nsnet2 (338 КБ) взята из репозитория espressif/esp-sr, упакована его же
// pack_model.py и кладётся на карту: /NS/nsnet2.bin. Сеть работает на 16 кГц, моно; здесь запись эфира (32 кГц)
// прореживается вдвое, идёт через сеть и пишется рядом в файл WAV 16 кГц — его снимают tools/getfile.py и слушают.
// Заодно меряется главное: сколько времени уходит на кусок звука и сколько памяти берёт сеть.
//   порт передатчика: Fn<вход.wav>|<выход.wav>|<с какой секунды>|<сколько секунд>
#pragma once
#include "model_path.h"
extern "C" {   // в этих заголовках ESP-SR 2.5.3 нет пометки «язык C» — без неё сборщик ищет имя на манер C++
#include "esp_nsn_iface.h"
#include "esp_nsn_models.h"
}

struct NsnetJob {
  char in[160], out[160];
  uint32_t fromS, lenS;
  volatile bool done;
};
static void nsnetTask(void *arg) {
  NsnetJob *j = (NsnetJob *)arg;
  uint32_t heap0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ps0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  FILE *fm = fopen("/sd/NS/nsnet2.bin", "rb");
  uint8_t *model = nullptr;
  size_t msize = 0;
  if (fm) {
    fseek(fm, 0, SEEK_END);
    msize = ftell(fm);
    fseek(fm, 0, SEEK_SET);
    model = (uint8_t *)heap_caps_aligned_alloc(16, msize, MALLOC_CAP_SPIRAM);
    if (model && fread(model, 1, msize, fm) != msize) {
      free(model);
      model = nullptr;
    }
    fclose(fm);
  }
  if (!model) {
    Serial.println("нейромережа: моделі /NS/nsnet2.bin на картці немає (або забракло пам'яті)");
    j->done = true;
    vTaskDelete(NULL);
  }
  srmodel_list_t *models = srmodel_load(model);
  char *name = models ? esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL) : nullptr;
  Serial.printf("нейромережа: модель %u байт, у списку моделей %d, назва «%s»\n", (unsigned)msize, models ? models->num : -1, name ? name : "—");
  const esp_nsn_iface_t *nsn = name ? esp_nsnet_handle_from_name(name) : nullptr;
  esp_nsn_data_t *h = nsn ? nsn->create(name) : nullptr;
  if (!h) {
    Serial.println("нейромережа: мережу створити не вдалося");
    j->done = true;
    vTaskDelete(NULL);
  }
  int chunk = nsn->get_samp_chunksize(h), rate = nsn->get_samp_rate(h);
  Serial.printf("нейромережа: шматок %d відліків, частота %d Гц (%.1f мс); пам'яті взято: внутрішньої %d КБ, PSRAM %d КБ\n", chunk, rate, chunk * 1000.0f / rate,
                (int)(heap0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024, (int)(ps0 - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) / 1024);
  FILE *fi = fopen(j->in, "rb"), *fo = fi ? fopen(j->out, "wb") : nullptr;
  if (!fi || !fo || chunk <= 0 || chunk > 1024) {
    Serial.println("нейромережа: файл не відкрився");
    if (fi) fclose(fi);
    if (fo) fclose(fo);
    nsn->destroy(h);
    j->done = true;
    vTaskDelete(NULL);
  }
  // заголовок записи эфира — 512 байт (txrec.h); дальше 32 кГц, моно, 16 бит
  fseek(fi, 512 + (long)j->fromS * 64000, SEEK_SET);
  uint8_t hdr[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x80, 0x3E, 0, 0, 0x00, 0x7D, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
  fwrite(hdr, 1, 44, fo);
  int16_t *raw = (int16_t *)heap_caps_malloc(chunk * 2 * 2, MALLOC_CAP_SPIRAM), *x = (int16_t *)heap_caps_malloc(chunk * 2, MALLOC_CAP_SPIRAM),
          *y = (int16_t *)heap_caps_malloc(chunk * 2, MALLOC_CAP_SPIRAM);
  static const float HB[11] = { 0.0064f, 0, -0.0526f, 0, 0.2964f, 0.5f, 0.2964f, 0, -0.0526f, 0, 0.0064f };   // полуполосный фильтр перед прореживанием
  float tail[10] = {};
  uint32_t n = 0, total = j->lenS * 16000u, usSum = 0, usMax = 0, calls = 0, t0 = millis();
  while (n < total && fread(raw, 2, chunk * 2, fi) == (size_t)chunk * 2) {
    for (int i = 0; i < chunk; i++) {   // 32 кГц → 16 кГц
      float a = 0;
      for (int k = 0; k < 11; k++) {
        int idx = 2 * i - 10 + k;
        a += HB[k] * (idx < 0 ? tail[10 + idx] : raw[idx]);
      }
      x[i] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
    }
    for (int k = 0; k < 10; k++) tail[k] = raw[chunk * 2 - 10 + k];
    uint32_t t = (uint32_t)esp_timer_get_time();
    nsn->process(h, x, y);
    uint32_t d = (uint32_t)esp_timer_get_time() - t;
    usSum += d;
    calls++;
    if (d > usMax) usMax = d;
    fwrite(y, 2, chunk, fo);
    n += chunk;
    if ((calls & 63) == 0) vTaskDelay(1);
  }
  uint32_t bytes = n * 2, riff = 36 + bytes;
  fseek(fo, 4, SEEK_SET);
  fwrite(&riff, 4, 1, fo);
  fseek(fo, 40, SEEK_SET);
  fwrite(&bytes, 4, 1, fo);
  fclose(fo);
  fclose(fi);
  float chunkMs = chunk * 1000.0f / rate;
  Serial.printf("нейромережа: оброблено %.1f с звуку за %.1f с; на шматок %.1f мс звуку — у середньому %u мкс, найдовше %u мкс (%.0f %% одного ядра)\n", n / (float)rate,
                (millis() - t0) / 1000.0f, chunkMs, calls ? (unsigned)(usSum / calls) : 0u, (unsigned)usMax, calls ? usSum / calls / (chunkMs * 10.0f) : 0.0f);
  free(raw);
  free(x);
  free(y);
  nsn->destroy(h);
  j->done = true;
  vTaskDelete(NULL);
}
static void nsnetTest(const String &in, const String &rest) {
  static NsnetJob j;
  int b1 = rest.indexOf('|'), b2 = b1 >= 0 ? rest.indexOf('|', b1 + 1) : -1;
  snprintf(j.in, sizeof(j.in), "/sd%s", in.c_str());
  snprintf(j.out, sizeof(j.out), "/sd%s", (b1 >= 0 ? rest.substring(0, b1) : rest).c_str());
  j.fromS = b1 >= 0 ? (uint32_t)rest.substring(b1 + 1).toInt() : 0;
  j.lenS = b2 >= 0 ? (uint32_t)rest.substring(b2 + 1).toInt() : 20;
  j.done = false;
  if (xTaskCreatePinnedToCore(nsnetTask, "nsnet", 16384, &j, 19, NULL, 1) != pdPASS) {
    Serial.println("нейромережа: задачу не створено");
    return;
  }
  for (uint32_t t0 = millis(); !j.done && millis() - t0 < 300000;) delay(50);
}
