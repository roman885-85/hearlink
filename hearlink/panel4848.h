// Модуль ESP32-4848S040: экран 480×480 (ST7701, параллельный RGB) и сенсор GT911.
//
// Картинка лежит в памяти PSRAM, микросхема сама 40 раз в секунду выдаёт её на панель; рисование — прямо
// в эту память. Контроллер панели один раз настраивается по трёхпроводной шине (таблица — из открытой
// библиотеки Arduino_GFX, запись для этого модуля). Свободных выводов у модуля три: IO1, IO2, IO40.
#pragma once
#include <Wire.h>
#include "config.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "slide.h"
#include "esp32s3/rom/cache.h"
#include "soc/gdma_struct.h"   // какой буфер выдачи DMA закончил последним (замер попаданий «под руку»)
#include "esp_ipc.h"

#define LCD_W 480
#define LCD_H 480
#define LCD_PIN_CS 39
#define LCD_PIN_SCK 48
#define LCD_PIN_SDA 47
#define LCD_PIN_BL 38
#define TOUCH_PIN_SDA 19
#define TOUCH_PIN_SCL 45

static uint16_t *lcdFb;                 // 480×480 точек, обычный порядок байтов
static esp_lcd_panel_handle_t lcdPanel;
static uint8_t gtAddr;                  // адрес сенсора на шине (0 — не найден)

// число байтов данных, команда, данные…; 0xFE, мс — пауза; 0xFF — конец
static const uint8_t ST7701_INIT[] = {
  5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10, 
  2, 0xC0, 0x3B, 0x00, 
  2, 0xC1, 0x0D, 0x02, 
  2, 0xC2, 0x31, 0x05, 
  1, 0xCD, 0x00, 
  16, 0xB0, 0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18, 
  16, 0xB1, 0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18, 
  5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11, 
  1, 0xB0, 0x60, 
  1, 0xB1, 0x32, 
  1, 0xB2, 0x07, 
  1, 0xB3, 0x80, 
  1, 0xB5, 0x49, 
  1, 0xB7, 0x85, 
  1, 0xB8, 0x21, 
  1, 0xC1, 0x78, 
  1, 0xC2, 0x78, 
  3, 0xE0, 0x00, 0x1B, 0x02, 
  11, 0xE1, 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44, 
  12, 0xE2, 0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00, 
  4, 0xE3, 0x00, 0x00, 0x11, 0x11, 
  2, 0xE4, 0x44, 0x44, 
  16, 0xE5, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0, 
  4, 0xE6, 0x00, 0x00, 0x11, 0x11, 
  2, 0xE7, 0x44, 0x44, 
  16, 0xE8, 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0, 
  7, 0xEB, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40, 
  2, 0xEC, 0x3C, 0x00, 
  16, 0xED, 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA, 
  5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13, 
  1, 0xE5, 0xE4, 
  5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00, 
  1, 0x3A, 0x60, 
  0, 0x11, 
  0xFE, 120,
  0, 0x29, 
  0xFF
};

static void lcdWord(bool data, uint8_t v) {
  digitalWrite(LCD_PIN_SDA, data);
  digitalWrite(LCD_PIN_SCK, HIGH);
  digitalWrite(LCD_PIN_SCK, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(LCD_PIN_SDA, (v >> i) & 1);
    digitalWrite(LCD_PIN_SCK, HIGH);
    digitalWrite(LCD_PIN_SCK, LOW);
  }
}

// Множитель подсветки, %. Пока передатчик пишет прошивку в собственный флеш (upd.h), процессор на десятки
// миллисекунд теряет доступ к памяти кадра, и экран показывает повторяющиеся полосы (владелец 07.10: «во время
// обновления на экране передатчика каша из помех»). Поэтому запись идёт отрезками при погашенной подсветке (0),
// а между отрезками экран плавно загорается (100) с чистым сообщением и процентами.
static volatile uint8_t lcdDim = 100;
static void lcdBacklight(uint8_t percent) {
  ledcWrite(LCD_PIN_BL, percent * 255 / 100);
}

static SemaphoreHandle_t lcdVsync;
static bool lcdVsyncOk;
static volatile uint32_t lcdFrames;   // сколько кадров экран показал (для отчёта)
static bool IRAM_ATTR lcdOnVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t woke = pdFALSE;
  lcdFrames = lcdFrames + 1;
  xSemaphoreGiveFromISR(lcdVsync, &woke);
  return woke == pdTRUE;
}
// Дождаться начала следующего кадра экрана (не дольше 40 мс — если знака почему-то нет).
static bool lcdWaitFrame() {
  if (!lcdVsyncOk) {
    vTaskDelay(pdMS_TO_TICKS(20));
    return false;
  }
  xSemaphoreTake(lcdVsync, 0);   // прежний знак, если остался, — не в счёт: нужен следующий
  return xSemaphoreTake(lcdVsync, pdMS_TO_TICKS(40)) == pdTRUE;
}

// ---- свой вывод кадра
// Экран забирает картинку кусками по 10 строк (прерывание, 2000 раз в секунду). Раньше это делал сам драйвер — просто
// копировал кусок основного листа. Теперь кусок готовим мы (lcdFill): в обычное время — та же копия, а во время
// перелистывания каждая строка складывается из двух листов со сдвигом. Сдвиг страницы поэтому ничего не стоит (память
// не переносится) и меняется строго между кадрами: 42 шага в секунду, без разрывов картинки.
// До этого страницу двигали переносом 330 КБ во внешней памяти на каждый шаг — дольше кадра и поперёк развёртки.
static const uint16_t *lcdFbB;                 // второй лист (новая страница) — только на время перелистывания
static volatile int16_t lcdSlideY0, lcdSlideY1;   // строки, которые сдвигаются; остальные сразу берутся со второго листа
static volatile uint32_t lcdSlideReq;          // что показывать со следующего кадра: 0 — основной лист; иначе 0x800 | 0x400 (справа) | сдвиг
static volatile uint32_t lcdSlideSeen;         // что экран показывает сейчас (для ожидания «уже на экране»)
static volatile uint32_t lcdFillMaxUs, lcdFills;
static volatile uint32_t lcdFillSumUs;         // сколько времени ушло на подготовку порций кадра (для отчёта: доля ядра)
// Сравнение на железе: вывод кадра «по-старому» (драйвер копирует сам). Признак переживает перезапуск (команда порта Y1/Y0).
RTC_NOINIT_ATTR static uint32_t lcdClassicMagic;
#define LCD_CLASSIC 0x4C434C31
static volatile uint32_t lcdSlideShown, lcdSlideRepeat;   // кадров показано за перелистывание и сколько из них повторили прежний сдвиг (рывок)
static bool lcdOwnFill;                        // вывод кадра наш (иначе — запасной путь: драйвер копирует сам, сдвига нет)

// ВАЖНО — подгрузка в кэш. Лист экрана лежит во внешней памяти, и копия порции оттуда «в лоб» идёт 295 мкс: 60 %
// второго ядра уходило только на вывод кадра, страница спектра рисовалась 30–50 мс вместо 10, меню при MP3 в эфире
// почти стояло (версии 2.4–2.5, 06–07.10; владелец: «страница с анализатором спектра начала тормозить сильно, раньше
// работала почти идеально»). Штатный драйвер делает хитрее, и я это упустил, когда забрал вывод кадра себе: сразу после
// копии он велит процессору заранее подтянуть в кэш следующую порцию (Cache_Start_DCache_Preload) — аппаратно, пока
// ядро занято другим. К следующему прерыванию данные уже в кэше, и копия идёт в несколько раз быстрее.
// Замер свободного времени второго ядра (команда порта Y2, условные единицы): без подгрузки 1350, у штатного
// драйвера 8375, вовсе без копии 10576.
static volatile bool lcdNoPreload;     // опыт: выключить подгрузку (команда порта Y3; Y4 — включить обратно)
// Замер вывода кадра в движении (перелистывание): сколько порций, сумма и наибольшее время, сколько порций дольше шага
// развёртки (493 мкс) и сколько раз прерывание пришлось «вдогонку» (началось меньше чем через 400 мкс после прежнего —
// значит, прежнее затянулось и отставание копится: так картинка и рвётся). Печатает u, обнуляет новое перелистывание.
static volatile uint32_t lcdSlFills, lcdSlSumUs, lcdSlMaxUs, lcdSlOver, lcdSlChase, lcdIdleOver, lcdIdleChase;
static volatile uint8_t lcdSlideMode = 0;   // опыт (порт: Y5…Y8): как готовить порцию в движении
// ---- ошибка драйвера: срыв картинки «до отключения питания» (обойдена в 2.49)
// Кадр уходит на экран порциями по 10 строк через два буфера во внутренней памяти: пока один передаётся, другой
// наполняется. Какой из двух наполнять, драйвер (ESP-IDF 5.5.1 из сборки Arduino 3.3.3) решает по чётности счётчика
// прерываний «порция ушла» (bb_eof_count) — и при перезапуске выдачи в каждом кадре (CONFIG_LCD_RGB_RESTART_IN_VSYNC,
// так собраны библиотеки Arduino) этот счётчик не сбрасывает НИКОГДА. Стоит двум таким прерываниям слиться в одно
// (ядро экрана полмиллисекунды не принимало прерываний: запись настроек во флеш, работа с каналом радио…), и чётность
// сбита насовсем: драйвер с этой минуты велит наполнять тот буфер, который как раз передаётся. Картинка съезжает на
// 10 строк и рвётся в каждой порции; все счётчики при этом в норме (порций по-прежнему 48 на кадр), а лечит только
// перезапуск. Владелец, 09.10: «срыв происходит без закономерности, но перед срывом картинка подвисает менее чем на
// пол секунды… лечится только отключением и включением питания». В ESP-IDF это позже исправили («DMA restart always
// relaunches from bounce buffer 0. Keep the software index in sync»), в наших библиотеках исправления нет.
// Обход: буфер выбираем сами — по месту порции в кадре. Выдача в каждом кадре заново начинается с буфера 0, значит
// порция № s всегда уходит из буфера s % 2, что бы ни насчитал драйвер. В исправном состоянии это тот же буфер,
// который называет драйвер, так что для исправной работы ничего не меняется.
static uint8_t *lcdBb[2];                           // оба буфера выдачи: узнаём по первым двум вызовам (до запуска выдачи)
static volatile uint32_t lcdBbFlips, lcdBbWrong;    // сколько раз драйвер сбивался и сколько порций велел положить не в тот буфер
static volatile uint32_t lcdBbHit;                  // порций, положенных в буфер, который в этот миг передаётся (видимый брак)
static volatile bool lcdBbFlipped;                  // драйвер сбит сейчас
static volatile bool lcdBbTrustDriver;              // опыт (порт: Y10, обратно Y11): класть, куда велит драйвер, — как до 2.49
static int8_t lcdDmaCh = -1;                        // канал DMA, которым кадр уходит на экран

// Какой из двух буферов выдачи DMA закончил передавать последним (0/1; -1 — не понять): по описателю, на котором
// DMA в последний раз отметил конец порции. Так буфер для наполнения выбирал сам драйвер до ESP-IDF 5.5.
static inline int IRAM_ATTR lcdBbDone(int len) {
  if (lcdDmaCh < 0) return -1;
  uint32_t d = GDMA.channel[lcdDmaCh].out.eof_des_addr;
  if (d < 0x3FC80000u || d >= 0x3FD00000u) return -1;   // описатели лежат во внутренней памяти
  uint8_t *p = (uint8_t *)((const uint32_t *)d)[1];
  if (p >= lcdBb[0] && p < lcdBb[0] + len) return 0;
  if (p >= lcdBb[1] && p < lcdBb[1] + len) return 1;
  return -1;
}

static bool IRAM_ATTR lcdFill(esp_lcd_panel_handle_t, void *buf, int pos, int len, void *) {
  static uint32_t cur;
  uint8_t *dst = (uint8_t *)buf;
  int strip = pos / (len / 2);
  if (!lcdBb[1]) {   // запуск выдачи: драйвер сам кладёт порцию 0 в буфер 0 и порцию 1 в буфер 1
    if (strip == 0 && !lcdBb[0]) lcdBb[0] = dst;
    else if (strip == 1 && lcdBb[0] && dst != lcdBb[0]) lcdBb[1] = dst;
  } else {
    static uint8_t okRun;
    uint8_t *own = lcdBb[strip & 1];
    if (own != dst) {
      lcdBbWrong = lcdBbWrong + 1;
      if (!lcdBbFlipped) {
        lcdBbFlips = lcdBbFlips + 1;
        lcdBbFlipped = true;
      }
      okRun = 0;
    } else if (lcdBbFlipped && ++okRun >= 3) lcdBbFlipped = false;   // при начале кадра драйвер называет буфер верно — два таких вызова не в счёт
    if (!lcdBbTrustDriver) dst = own;
    int done = lcdBbDone(len);
    if (done >= 0 && dst != lcdBb[done]) lcdBbHit = lcdBbHit + 1;
  }
  if (pos == 0) {   // начало кадра: новый сдвиг берём только здесь — весь кадр рисуется с одним и тем же
    uint32_t nw = lcdSlideReq;
    if ((nw & 0x800) && (nw & 0x3FF) < LCD_W) {   // страница в движении: каждый кадр обязан быть с новым сдвигом
      lcdSlideShown = lcdSlideShown + 1;
      if (nw == cur) lcdSlideRepeat = lcdSlideRepeat + 1;
    }
    cur = nw;
    lcdSlideSeen = cur;
  }
  int64_t t0 = esp_timer_get_time();
  static int64_t prevT0;
  static int chunkNo;
  bool chase = t0 - prevT0 < 400;
  prevT0 = t0;
  if (pos == 0) chunkNo = 0;
  else chunkNo++;
  int next = pos + len / 2;   // следующая порция (в точках); после последней — начало листа
  if (next >= LCD_W * LCD_H) next = 0;
  if (!(cur & 0x800)) {
    memcpy(dst, lcdFb + pos, len);
    if (!lcdNoPreload) Cache_Start_DCache_Preload((uint32_t)(lcdFb + next), len, 0);
  } else {
    uint16_t *d = (uint16_t *)dst;
    const uint16_t *a = lcdFb + pos, *b = lcdFbB + pos;
    int off = cur & 0x3FF, y0 = lcdSlideY0, y1 = lcdSlideY1;
    bool right = cur & 0x400;
    for (int y = pos / LCD_W, n = len / (LCD_W * 2); n > 0; n--, y++, d += LCD_W, a += LCD_W, b += LCD_W) {
      if (y < y0 || y >= y1) memcpy(d, b, LCD_W * 2);
      else slideRow(d, a, b, off, right, LCD_W);
    }
    if (lcdSlideMode == 1) goto done;   // опыт: в движении без подгрузки
    // Подгрузка следующей порции. В движении строка складывается из двух листов, а подгрузить заранее можно только
    // один кусок памяти. Строки вне движущейся полосы (шапка, вкладки) берутся целиком из нового листа — им и
    // подгружаем новый; в полосе — тот лист, из которого берётся больше. До 2.28 шапке и вкладкам в первой половине
    // движения подгружался прежний лист: порция готовилась 600 мкс вместо 60, шесть-семь таких подряд отставали от
    // развёртки, и картинка в шапке и на вкладках срывалась (владелец 07.10: «картинка начинает дергаться и срываться,
    // особенно при переключении закладок меню»).
    if (!lcdNoPreload) {
      int ny = next / LCD_W, rows = len / (LCD_W * 2);
      bool outside = lcdSlideMode == 2 ? false : (ny + rows <= y0 || ny >= y1);
      Cache_Start_DCache_Preload((uint32_t)((outside || off > LCD_W / 2 ? lcdFbB : lcdFb) + next), len, 0);
    }
  }
done:
  uint32_t du = (uint32_t)(esp_timer_get_time() - t0);
  if (cur & 0x800) {
    lcdSlFills = lcdSlFills + 1;
    lcdSlSumUs = lcdSlSumUs + du;
    if (du > lcdSlMaxUs) lcdSlMaxUs = du;
    if (du > 493) lcdSlOver = lcdSlOver + 1;
    if (chase) lcdSlChase = lcdSlChase + 1;
  } else {
    if (du > 493) lcdIdleOver = lcdIdleOver + 1;
    if (chase) lcdIdleChase = lcdIdleChase + 1;
  }
  if (du > lcdFillMaxUs) lcdFillMaxUs = du;
  lcdFillSumUs = lcdFillSumUs + du;
  lcdFills = lcdFills + 1;
  return false;
}

// Опыт (порт: Y9): сбить драйвер нарочно — на 0,7 мс закрыть прерывания на ядре экрана, чтобы два прерывания
// «порция ушла» слились в одно. Повторяем, пока чётность у драйвера не переменится (в окно попадает то одно
// прерывание, то два). До 2.49 после этого картинка срывалась до отключения питания.
static void lcdIrqBlock(void *) {
  portDISABLE_INTERRUPTS();
  esp_rom_delay_us(700);
  portENABLE_INTERRUPTS();
}
static void lcdProvokeFlip() {
  if (!lcdBb[1]) {
    Serial.println("вивід кадру не свій — дослід неможливий");
    return;
  }
  bool was = lcdBbFlipped;
  int tries = 0;
  while (tries < 300) {
    tries++;
    esp_ipc_call_blocking(1, lcdIrqBlock, NULL);
    delay(40);   // больше кадра: если бы драйвер выправлялся сам, он бы успел
    if (lcdBbFlipped != was) break;
  }
  Serial.printf("дослід: переривання екрана закрито %d разів по 0,7 мс — драйвер %s (було: %s); збивався від запуску %u разів\n", tries,
                lcdBbFlipped ? "ЗБИТИЙ із черги буферів" : "у черзі буферів", was ? "збитий" : "у черзі", (unsigned)lcdBbFlips);
}

// Сдвиг страницы: off — 0…480, dir > 0 — новая входит справа. b == NULL — вернуться к основному листу.
// Возвращает false, если экран работает по запасному пути и сдвигать не умеет.
static bool lcdSlide(const uint16_t *b, int off, int dir, int y0, int y1) {
  if (!lcdOwnFill) return false;
  if (!b) {
    lcdSlideReq = 0;
    return true;
  }
  if (!(lcdSlideReq & 0x800)) {   // начало нового перелистывания
    lcdSlideShown = 0;
    lcdSlideRepeat = 0;
    lcdSlFills = 0;
    lcdSlSumUs = 0;
    lcdSlMaxUs = 0;
    lcdSlOver = 0;
    lcdSlChase = 0;
  }
  lcdFbB = b;
  lcdSlideY0 = y0;
  lcdSlideY1 = y1;
  lcdSlideReq = 0x800 | (dir > 0 ? 0x400 : 0) | (off < 0 ? 0 : off > LCD_W ? LCD_W : off);
  return true;
}

static bool lcdBegin() {
  pinMode(LCD_PIN_CS, OUTPUT);
  pinMode(LCD_PIN_SCK, OUTPUT);
  pinMode(LCD_PIN_SDA, OUTPUT);
  digitalWrite(LCD_PIN_CS, HIGH);
  digitalWrite(LCD_PIN_SCK, LOW);
  ledcAttach(LCD_PIN_BL, 5000, 8);
  lcdBacklight(0);
  for (const uint8_t *p = ST7701_INIT; *p != 0xFF;) {
    if (*p == 0xFE) {
      delay(p[1]);
      p += 2;
      continue;
    }
    uint8_t n = *p++;
    digitalWrite(LCD_PIN_CS, LOW);
    lcdWord(false, *p++);
    while (n--) lcdWord(true, *p++);
    digitalWrite(LCD_PIN_CS, HIGH);
  }
  esp_lcd_rgb_panel_config_t pc = {};
  pc.clk_src = LCD_CLK_SRC_DEFAULT;
  pc.timings.pclk_hz = 12000000;
  pc.timings.h_res = LCD_W;
  pc.timings.v_res = LCD_H;
  pc.timings.hsync_pulse_width = 8;
  pc.timings.hsync_back_porch = 50;
  pc.timings.hsync_front_porch = 10;
  pc.timings.vsync_pulse_width = 8;
  pc.timings.vsync_back_porch = 20;
  pc.timings.vsync_front_porch = 10;
  pc.data_width = 16;
  pc.bits_per_pixel = 16;
  pc.num_fbs = 1;
  pc.bounce_buffer_size_px = LCD_W * 10;   // выдача через буфер во внутренней памяти: иначе картинка «плывёт» при работе Wi-Fi
  pc.dma_burst_size = 64;
  pc.hsync_gpio_num = 16;
  pc.vsync_gpio_num = 17;
  pc.de_gpio_num = 18;
  pc.pclk_gpio_num = 21;
  pc.disp_gpio_num = -1;
  static const int8_t D[16] = { 4, 5, 6, 7, 15, 8, 20, 3, 46, 9, 10, 11, 12, 13, 14, 0 };   // синий 0–4, зелёный 0–5, красный 0–4
  for (int i = 0; i < 16; i++) pc.data_gpio_nums[i] = D[i];
  // Основной лист держим сами (драйверу он не нужен — кадр отдаёт lcdFill). Если так не выйдет — запасной путь:
  // лист у драйвера, как было до 2.4; тогда страницы просто сменяются без движения.
  bool classic = lcdClassicMagic == LCD_CLASSIC;
  lcdFb = classic ? NULL : (uint16_t *)heap_caps_aligned_calloc(64, LCD_W * LCD_H, 2, MALLOC_CAP_SPIRAM);
  pc.flags.no_fb = 1;
  pc.num_fbs = 0;
  lcdOwnFill = lcdFb && esp_lcd_new_rgb_panel(&pc, &lcdPanel) == ESP_OK;
  for (int i = 0; i < 5; i++)   // канал DMA экрана: тот, что подключён к LCD_CAM (5)
    if (GDMA.channel[i].out.peri_sel.sel == 5) lcdDmaCh = i;
  if (!lcdOwnFill) {
    Serial.println(classic ? "екран: вивід кадру по-старому (для порівняння, Y0 — повернути свій)" : "екран: свій вивід кадру не запустився — працюю по-старому, без руху сторінок");
    if (lcdFb) heap_caps_free(lcdFb);
    lcdFb = NULL;
    pc.flags.no_fb = 0;
    pc.num_fbs = 1;
    pc.flags.fb_in_psram = 1;
    if (esp_lcd_new_rgb_panel(&pc, &lcdPanel) != ESP_OK) return false;
  }
  // Начало каждого кадра экрана (он обновляется 42 раза в секунду: 12 МГц / (548 × 518)). По этому знаку меню рисует
  // свои кадры — ровно один на кадр экрана, иначе движение идёт то с повтором, то с пропуском.
  lcdVsync = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = lcdOnVsync;
  if (lcdOwnFill) cbs.on_bounce_empty = lcdFill;
  lcdVsyncOk = lcdVsync && esp_lcd_rgb_panel_register_event_callbacks(lcdPanel, &cbs, NULL) == ESP_OK;
  if (esp_lcd_panel_reset(lcdPanel) != ESP_OK || esp_lcd_panel_init(lcdPanel) != ESP_OK) return false;
  if (!lcdOwnFill) {
    void *fb = NULL;
    if (esp_lcd_rgb_panel_get_frame_buffer(lcdPanel, 1, &fb) != ESP_OK || !fb) return false;
    lcdFb = (uint16_t *)fb;
  }
  return true;
}

// ---- сенсор GT911
static bool gtRead(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)gtAddr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool touchBegin() {
  Wire.begin(TOUCH_PIN_SDA, TOUCH_PIN_SCL, 400000);
  for (uint8_t a : { 0x5D, 0x14 }) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      gtAddr = a;
      return true;
    }
  }
  return false;
}

// Что сейчас говорит сенсор: 1 — палец на экране (x, y, id — номер касания, size — пятно), 0 — пальца нет,
// −1 — нового замера ещё нет.
static int gtSample(int &x, int &y, int &id, int &size) {
  uint8_t st;
  if (!gtAddr || !gtRead(0x814E, &st, 1) || !(st & 0x80)) return -1;
  int r = 0;
  uint8_t b[7];
  if ((st & 0x0F) && gtRead(0x814F, b, 7)) {
    id = b[0];
    x = b[1] | (b[2] << 8);
    y = b[3] | (b[4] << 8);
    size = b[5] | (b[6] << 8);
    r = 1;
  }
  Wire.beginTransmission(gtAddr);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0);
  Wire.endTransmission();
  return r;
}

// Палец на экране? Координаты — как их отдаёт сенсор. Прямой опрос: нужен там, где задача сенсора (ниже) стоит —
// в выключенном состоянии передатчика.
static bool touchRead(int &x, int &y) {
  static bool down;
  static int lx, ly;
  static uint32_t lastMs;
  if (!gtAddr) return false;
  int id, size;
  int r = gtSample(lx, ly, id, size);
  if (r >= 0) {
    down = r == 1;
    lastMs = millis();
  } else if (down && millis() - lastMs > 150) down = false;
  x = lx;
  y = ly;
  return down;
}

// ---- Сенсор опрашивает своя задача, 125 раз в секунду, и складывает касания в очередь с отметками времени.
// Раньше сенсор читался раз за проход цикла экрана: пока экран рисовал (перерисовка клавиатуры — десятки
// миллисекунд), быстрое нажатие проходило мимо целиком, а положение пальца бралось «какое застали» — в том числе
// в миг отрыва, когда сенсор отдаёт самую неточную точку (владелец 07.10: «клавиатура на передатчике не всегда
// записывает верные символы», «низкая отзывчивость введения символов»). Теперь ни одно касание не теряется, а у
// клавиатуры есть вся дорожка пальца со временем.
struct TouchEv {
  uint32_t ms;
  int16_t x, y;
  uint8_t down;
  uint8_t size;
};
static QueueHandle_t touchQ;
static volatile bool touchHold;          // задаче сенсора стоять (сенсор опрашивают напрямую)
static volatile uint32_t touchLost;      // событий не поместилось в очередь
// проверка без пальца (порт: Ux<x>,<y>,<мс>[,<сдвиг>]): «касание» в точке на столько-то мс; в последние 20 мс точка
// уходит в сторону на сдвиг — как при отрыве настоящего пальца
// Несколько касаний подряд одной командой: Ux<пауза мс>:<x>,<y>,<мс>[,<сдвиг>];<x>,<y>,<мс>… — так проверяется быстрый набор
// (команды по одной приходят с порта неровно и «склеиваются»).
struct FakeTap {
  int16_t x, y, dx;
  uint16_t ms;
};
static FakeTap fakeTap[24];
static volatile uint8_t fakeN;
static volatile uint16_t fakeGap;
static volatile bool fakeAsk;

static void touchPush(uint32_t ms, int x, int y, bool down, int size) {
  TouchEv e = { ms, (int16_t)x, (int16_t)y, (uint8_t)down, (uint8_t)(size > 255 ? 255 : size) };
  if (xQueueSend(touchQ, &e, 0) == pdTRUE) return;
  TouchEv drop;   // очередь полна (экран надолго занят): выбросить самое старое — «отпустил» терять нельзя
  xQueueReceive(touchQ, &drop, 0);
  touchLost = touchLost + 1;
  xQueueSend(touchQ, &e, 0);
}
static void touchTask(void *) {
  bool down = false;
  int lx = 0, ly = 0, curId = -1;
  uint32_t lastMs = 0;
  TickType_t tick = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&tick, pdMS_TO_TICKS(8));
    if (touchHold) {
      down = false;
      continue;
    }
    uint32_t now = millis();
    if (fakeAsk) {   // «касания» с порта
      for (int i = 0; i < fakeN; i++) {
        uint32_t t0 = millis(), dur = fakeTap[i].ms;
        int x0 = fakeTap[i].x, y0 = fakeTap[i].y, dx = fakeTap[i].dx;
        for (;;) {
          uint32_t t = millis() - t0;
          if (t >= dur) break;
          touchPush(millis(), x0 + (dur - t <= 20 ? dx : 0), y0, true, 40);
          vTaskDelay(pdMS_TO_TICKS(8));
        }
        touchPush(millis(), x0 + dx, y0, false, 0);
        vTaskDelay(pdMS_TO_TICKS(fakeGap < 8 ? 8 : fakeGap));
      }
      fakeAsk = false;
      tick = xTaskGetTickCount();
      continue;
    }
    int x, y, id, size;
    int r = gtSample(x, y, id, size);
    if (r == 1) {
      if (down && id != curId) touchPush(now, lx, ly, false, 0);   // на экране уже другой палец: прежний — «отпущен»
      curId = id;
      lx = x;
      ly = y;
      down = true;
      lastMs = now;
      touchPush(now, x, y, true, size);
    } else if (r == 0) {
      if (down) touchPush(now, lx, ly, false, 0);
      down = false;
      lastMs = now;
    } else if (down && now - lastMs > 150) {   // сенсор замолчал, не сказав «отпустил»
      down = false;
      touchPush(now, lx, ly, false, 0);
    }
  }
}
static void touchTaskBegin() {
  if (touchQ) return;
  touchQ = xQueueCreate(48, sizeof(TouchEv));
  xTaskCreatePinnedToCore(touchTask, "touch", 3072, NULL, 4, NULL, 1);
}
