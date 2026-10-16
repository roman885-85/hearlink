// Цветной экран приёмника — железо (с 3.03): модуль IPS 1,9″ 170×320 на ST7789 (MSP1901: GND VCC SCL SDA RES DC CS
// BLK), шина SPI, горизонтально 320×170. Что рисуется — rxcolor.h, как кадр уходит полосами — rxstrip.h.
//
// Выводы — на правой гребёнке платы ESP32-S3, подальше от выводов звука (I2S 10–14, PDM 17–18 — на левой):
//   SCL → 42, SDA → 41, DC → 40, CS → 39, RES → 2, BLK → 1 (яркость: ШИМ 30 кГц — выше слышимого: частота
//   регулировки подсветки не должна пролезть в звук через питание), VCC → 3,3 В, GND.
// Выводы шины SPI2 «напрямую» (IO MUX) — 10–13, а они заняты звуком, поэтому шина идёт через матрицу выводов: 40 МГц
// (кадр целиком 21,8 мс), по настройке — 80 МГц (10,9 мс; с другой платой ESP32 модуль на 80 МГц работал и с кабелем
// 50 см — github.com/mboehmerm/IPS-Display-ST7789-170x320).
// Посылка кадра идёт по DMA, процессор в это время рисует следующую полосу. Экран определить нельзя (у модуля нет
// линии чтения) — его выбирают перемычкой: вывод 21 на «землю» (PIN_RX_COLOR, с 3.13; до того — настройкой).
#pragma once
#include "rxstrip.h"
#include "m2simd.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"

#define RXLCD_PIN_SCK 42
#define RXLCD_PIN_MOSI 41
#define RXLCD_PIN_DC 40
#define RXLCD_PIN_CS 39
#define RXLCD_PIN_RST 2
#define RXLCD_PIN_BLK 1

static esp_lcd_panel_handle_t rxLcdPanel;
static esp_lcd_panel_io_handle_t rxLcdIo;
static volatile uint32_t rxLcdDone;         // сколько кусков кадра уже ушло (считает прерывание DMA)
static uint32_t rxLcdQueued;                // сколько отдано на отправку
static SemaphoreHandle_t rxLcdSem;
static rxc::Strips *rxLcdStrips;
static uint8_t rxLcdBrightNow = 255;
static bool rxLcdAsleep;

static bool IRAM_ATTR rxLcdOnDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
  rxLcdDone = rxLcdDone + 1;
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(rxLcdSem, &hp);
  return hp == pdTRUE;
}

struct RxLcdOut : rxc::Out {
  uint32_t at[2] = { 0, 0 };   // номер отправки, после которой полоса свободна
  void wait(int b) override {
    while ((int32_t)(rxLcdDone - at[b]) < 0)
      if (xSemaphoreTake(rxLcdSem, pdMS_TO_TICKS(60)) != pdTRUE) break;   // не дождались — не виснем
  }
  void send(int b, int x, int y, int w, int h, uint16_t *px) override {
    m2::sSwap16(px, (uint32_t)(w * h));   // экран ждёт старший байт первым
    rxLcdQueued++;
    at[b] = rxLcdQueued;
    esp_lcd_panel_draw_bitmap(rxLcdPanel, x, y, x + w, y + h, px);
  }
};
static RxLcdOut rxLcdOut;

// Яркость 1…5 → доля подсветки; 0 — погасить. Владелец 15.10 о первой пробе (уровень 3 был 32 %): «по ощущению, слабая
// подсветка» — теперь 3 — 62 %, 5 — полная.
static void rxLcdBright(uint8_t level) {
  static const uint16_t DUTY[6] = { 0, 160, 360, 640, 860, 1023 };
  if (level > 5) level = 5;
  if (level == rxLcdBrightNow) return;
  rxLcdBrightNow = level;
  ledcWrite(RXLCD_PIN_BLK, DUTY[level]);
}
static void rxLcdSleep(bool on) {   // экран погашен: подсветка выключена, контроллер спит
  if (!rxLcdPanel || on == rxLcdAsleep) return;
  rxLcdAsleep = on;
  if (on) {
    rxLcdBright(0);
    esp_lcd_panel_disp_sleep(rxLcdPanel, true);
  } else {
    esp_lcd_panel_disp_sleep(rxLcdPanel, false);
    if (rxLcdStrips) rxLcdStrips->invalidate();   // после сна — кадр целиком
  }
}

// Шина и сила её выводов (с 3.11). Владелец 15.10: «обновления картинки отдаются легкими щелчками в звуке… при
// статической картинке звук чистый… на олед такого не слышно». Замер 3.10 (Y22, 884A94): пока шина гонит кадры без
// остановки, шум радио тот же (−90,0…−90,4 дБм против −90,5), потерь пакетов нет — помеха не радио, а наводка уже в
// звуковой части (питание, «земля», провода шины рядом с проводами ЦАП: PCM5102 берёт такт из линии BCK). Её дают
// крутые фронты: до 3.11 шина шла на 40 МГц самыми сильными выводами драйвера (2). Теперь — 20 МГц и самые слабые выводы
// (0): фронты в разы положе. Скорость почти та же: кадр целиком упирается не в шину, а в процессор (сравнение отпечатков,
// перестановка байтов, копирование из внешней памяти) — 55 мс при 40 МГц, а на шине из них 22.
static int8_t rxLcdCap = 0;   // сила выводов по настройке: 0 — самые слабые (с 3.11), 2 — как ставит драйвер (n27, бит 16)
static uint32_t rxLcdMhz(uint8_t opt) {
  return (opt & 4) ? 80 : (opt & 8) ? 40 : 20;
}
// cap −1 — по настройке
static void rxLcdDrive(int cap) {
  gpio_drive_cap_t c = (gpio_drive_cap_t)(cap < 0 ? rxLcdCap : cap);
  for (int pin : { RXLCD_PIN_SCK, RXLCD_PIN_MOSI, RXLCD_PIN_DC, RXLCD_PIN_CS }) gpio_set_drive_capability((gpio_num_t)pin, c);
}

// opt: бит 0 — модуль повёрнут на 180°, бит 1 — порядок цветов BGR, бит 2 — шина 80 МГц, бит 3 — 40 МГц (как до 3.11;
// без них — 20 МГц), бит 4 — сильные выводы шины (как до 3.11; без него — самые слабые).
static bool rxLcdBegin(uint8_t opt) {
  pinMode(RXLCD_PIN_BLK, OUTPUT);
  digitalWrite(RXLCD_PIN_BLK, LOW);   // пока экран не готов — тёмный
  rxLcdSem = xSemaphoreCreateBinary();
  spi_bus_config_t bus = {};
  bus.sclk_io_num = RXLCD_PIN_SCK;
  bus.mosi_io_num = RXLCD_PIN_MOSI;
  bus.miso_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = rxc::W * rxc::SH * 2 + 64;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = RXLCD_PIN_CS;
  io.dc_gpio_num = RXLCD_PIN_DC;
  io.spi_mode = 0;
  io.pclk_hz = rxLcdMhz(opt) * 1000 * 1000;
  io.trans_queue_depth = 4;
  io.on_color_trans_done = rxLcdOnDone;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &rxLcdIo) != ESP_OK) return false;
  esp_lcd_panel_dev_config_t pc = {};
  pc.reset_gpio_num = RXLCD_PIN_RST;
  pc.rgb_ele_order = (opt & 2) ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB;
  pc.bits_per_pixel = 16;
  if (esp_lcd_new_panel_st7789(rxLcdIo, &pc, &rxLcdPanel) != ESP_OK) return false;
  esp_lcd_panel_reset(rxLcdPanel);
  esp_lcd_panel_init(rxLcdPanel);
  esp_lcd_panel_invert_color(rxLcdPanel, true);   // у IPS-панелей ST7789 цвета без этого «в негативе»
  esp_lcd_panel_swap_xy(rxLcdPanel, true);        // горизонтально: 320 по ширине
  bool flip = opt & 1;
  esp_lcd_panel_mirror(rxLcdPanel, !flip, flip);
  esp_lcd_panel_set_gap(rxLcdPanel, 0, 35);       // 170 строк стоят посередине 240-строчной памяти контроллера
  // кадр и второй кадр (переход) — во внешней памяти (с 3.06, см. rxstrip.h); две полосы, через которые куски кадра
  // уходят по DMA, — во внутренней
  uint16_t *f0 = (uint16_t *)heap_caps_malloc(rxc::W * rxc::H * 2, MALLOC_CAP_SPIRAM);
  uint16_t *f1 = (uint16_t *)heap_caps_malloc(rxc::W * rxc::H * 2, MALLOC_CAP_SPIRAM);
  uint16_t *a0 = (uint16_t *)heap_caps_malloc(rxc::W * rxc::SH * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  uint16_t *a1 = (uint16_t *)heap_caps_malloc(rxc::W * rxc::SH * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  rxLcdStrips = (rxc::Strips *)heap_caps_calloc(1, sizeof(rxc::Strips), MALLOC_CAP_SPIRAM);
  if (!f0 || !a0 || !a1 || !rxLcdStrips) return false;
  new (rxLcdStrips) rxc::Strips();
  rxLcdStrips->begin(f0, f1, a0, a1);   // без второго кадра переходы просто сменяют экран
  esp_lcd_panel_disp_on_off(rxLcdPanel, true);
  rxLcdCap = (opt & 16) ? GPIO_DRIVE_CAP_DEFAULT : GPIO_DRIVE_CAP_0;
  rxLcdDrive(-1);
  ledcAttach(RXLCD_PIN_BLK, 30000, 10);
  rxLcdBrightNow = 255;
  return true;
}
