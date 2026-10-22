// Перевірка плати моста без запису у флеш: picotool кладе цю програму в ОЗУ і запускає її
// (picotool load -x -f build/board_probe.uf2). Програма моста у флеші лишається як була — після перезапуску
// (буква «r» у порту USB або сама через 5 хвилин) працює знову вона.
// Що дивимось:
//   • GP24 — кнопка USR (YD-RP2040: до землі) чи датчик VBUS (Raspberry Pi Pico: «1», поки є живлення USB);
//   • GP23 — кольоровий світлодіод WS2812 (YD-RP2040) чи вивід перемикання живлення (Pico); кольори — по колу;
//   • GP29 — у Pico тут VSYS/3 (≈ 1,6 В від USB), у YD-RP2040 — вільний вивід;
//   • GP25 — звичайний світлодіод (блимає раз на секунду).
// Порт USB: «p» — виводи ще раз, «r» — перезапуск у програму моста, «b» — режим запису (без диска RPI-RP2).
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#define PIN_RGB 23
#define PIN_USR 24
#define PIN_LED 25

// WS2812: біт — 1,25 мкс; «1» — ~0,75 мкс високого рівня, «0» — ~0,4; кольори в порядку G, R, B, старшим бітом уперед.
// Увесь кадр (24 біти, 30 мкс) — з вимкненими перериваннями і з ОЗУ, щоб нічого не розтягнуло імпульс.
static void __not_in_flash_func(wsPut)(uint32_t grb) {
  uint32_t mhz = clock_get_hz(clk_sys) / 1000000u;
  uint32_t t0h = mhz * 35 / 100, t1h = mhz * 70 / 100, tb = mhz * 125 / 100;
  uint32_t save = save_and_disable_interrupts();
  for (int i = 23; i >= 0; i--) {
    uint32_t h = (grb >> i) & 1u ? t1h : t0h;
    sio_hw->gpio_set = 1u << PIN_RGB;
    busy_wait_at_least_cycles(h);
    sio_hw->gpio_clr = 1u << PIN_RGB;
    busy_wait_at_least_cycles(tb - h);
  }
  restore_interrupts(save);
}
static uint32_t grb(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)g << 16) | ((uint32_t)r << 8) | b; }

// стан виводу без підтяжки, з підтяжкою вгору і вниз: якщо він іде за підтяжкою — ззовні його ніщо не тримає
static void pullReport(uint pin, const char *what) {
  int v[3];
  for (int k = 0; k < 3; k++) {
    gpio_set_pulls(pin, k == 1, k == 2);
    sleep_us(500);
    v[k] = gpio_get(pin);
  }
  printf("  GP%u (%s): без підтяжки %d, угору %d, униз %d — %s\n", pin, what, v[0], v[1], v[2],
         v[1] && !v[2] ? "іде за підтяжкою (ззовні не тримається)" : v[1] && v[2] ? "тримається «1» ззовні"
         : !v[1] && !v[2] ? "тримається «0» ззовні" : "дивно");
}

static void pinsReport(void) {
  printf("Виводи плати:\n");
  gpio_set_dir(PIN_RGB, GPIO_IN);
  pullReport(PIN_RGB, "WS2812 чи живлення");
  gpio_set_pulls(PIN_RGB, false, false);
  gpio_set_dir(PIN_RGB, GPIO_OUT);
  pullReport(PIN_USR, "кнопка USR чи VBUS");
  gpio_set_pulls(PIN_USR, true, false);
  uint16_t raw = adc_read();
  printf("  GP29 (АЦП 3): %.2f В — у Raspberry Pi Pico тут VSYS/3, тобто ≈ 1,6 В від USB\n", raw * 3.3f / 4096.0f);
  printf("  частота ядра %lu МГц\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000u));
}

int main(void) {
  stdio_usb_init();
  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, GPIO_OUT);
  gpio_init(PIN_RGB);
  gpio_put(PIN_RGB, 0);
  gpio_set_dir(PIN_RGB, GPIO_OUT);
  gpio_init(PIN_USR);
  gpio_set_dir(PIN_USR, GPIO_IN);
  gpio_set_pulls(PIN_USR, true, false);
  adc_init();
  adc_gpio_init(29);
  adc_select_input(3);

  static const struct { uint8_t r, g, b; const char *name; } C[] = {
    { 32, 0, 0, "червоний" }, { 0, 32, 0, "зелений" }, { 0, 0, 32, "синій" }, { 20, 20, 20, "білий" }, { 0, 0, 0, "вимкнено" },
  };
  uint32_t t0 = time_us_32(), colorT = t0 - 2000000u, ledT = t0, btnT = t0;
  int color = -1;
  bool wasConnected = false, usrWas = gpio_get(PIN_USR);
  wsPut(0);
  for (;;) {
    uint32_t now = time_us_32();
    bool con = stdio_usb_connected();
    if (con && !wasConnected) {
      sleep_ms(200);
      printf("\nПеревірка плати моста (програма в ОЗУ; флеш не змінено)\n");
      pinsReport();
      printf("Кольоровий світлодіод: кольори по колу кожні 2 с. Кнопка USR — натискайте, тут буде видно.\n");
      printf("«p» — виводи ще раз, «r» — повернутися до програми моста, «b» — режим запису.\n");
    }
    wasConnected = con;
    if (now - colorT >= 2000000u) {
      colorT = now;
      color = (color + 1) % 5;
      wsPut(grb(C[color].r, C[color].g, C[color].b));
      if (con) printf("світлодіод: %s\n", C[color].name);
    }
    if (now - ledT >= 500000u) {
      ledT = now;
      gpio_xor_mask(1u << PIN_LED);
    }
    if (now - btnT >= 10000u) {   // кнопка: опитування раз на 10 мс
      btnT = now;
      bool usr = gpio_get(PIN_USR);
      if (usr != usrWas) {
        usrWas = usr;
        if (con) printf("кнопка USR (GP24): %s\n", usr ? "відпущена («1»)" : "НАТИСНУТА («0»)");
      }
    }
    if (con) {
      int ch = getchar_timeout_us(0);
      if (ch == 'p') pinsReport();
      else if (ch == 'r' || ch == 'b') {
        printf(ch == 'r' ? "перезапуск у програму моста\n" : "режим запису\n");
        wsPut(0);
        sleep_ms(100);
        if (ch == 'r') watchdog_reboot(0, 0, 0);
        else reset_usb_boot(0, 1);
        for (;;) tight_loop_contents();
      }
    }
    if (now - t0 > 300000000u) {   // 5 хвилин — і назад до програми моста, навіть якщо про перевірку забули
      wsPut(0);
      watchdog_reboot(0, 0, 0);
      for (;;) tight_loop_contents();
    }
  }
}
