// Завантажувач моста (перші 28 КБ флешу; передавач його не переписує ніколи — лише програму за ним).
// Запускає програму з 0x10008000, якщо вона ціла (відомості в 0x10007000 + CRC-32). Інакше, на прохання програми
// (STAY_MAGIC у watchdog scratch[0]) або після трьох поспіль перезапусків сторожем — чекає команд передавача:
// UART 115200, RX — GP5 (вивід 43 передавача), TX — GP4 (той самий дріт, що дані I2S: передавач на час оновлення
// слухає свій вивід 40 як UART). Кадри — hearlink/bridge_proto.h. Немає команд 60 с, а програма ціла — запускає її.
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/regs/m0plus.h"
#include "layout.h"
#include "bridge_proto.h"
#include "version.h"

#define PIN_TX 4
#define PIN_RX 5
#define PIN_LED 25
#define CRASH_MAGIC 0x43524153u   // у scratch[1]: старші 24 біти — знак, молодший байт — скільки разів поспіль

static const app_info_t *const info = (const app_info_t *)(FLASH_BASE_ADDR + INFO_OFF);

static bool appValid(void) {
  if (info->magic != INFO_MAGIC || info->magic2 != ~INFO_MAGIC) return false;
  if (info->size == 0 || info->size > APP_MAX) return false;
  return bp_crc32((const uint8_t *)APP_ADDR, info->size, 0) == info->crc32;
}

static void jumpToApp(void) {
  uart_deinit(uart1);
  gpio_init(PIN_TX);
  gpio_init(PIN_RX);
  gpio_init(PIN_LED);
  __asm volatile("cpsid i");
  systick_hw->csr = 0;
  *(volatile uint32_t *)(PPB_BASE + M0PLUS_NVIC_ICER_OFFSET) = 0xFFFFFFFFu;
  *(volatile uint32_t *)(PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET) = 0xFFFFFFFFu;
  const uint32_t *vt = (const uint32_t *)APP_ADDR;
  scb_hw->vtor = APP_ADDR;
  __asm volatile("msr msp, %0\n"
                 "cpsie i\n"
                 "bx %1\n" ::"r"(vt[0]),
                 "r"(vt[1]));
  for (;;) {
  }
}

static void reply(uint8_t cmd, const uint8_t *d, size_t n) {
  uint8_t out[32];
  size_t m = bp_frame(cmd, d, n, out);
  uart_write_blocking(uart1, out, m);
}

static void ack(uint8_t of, uint8_t status, uint32_t off) {
  uint8_t d[6] = {of, status, (uint8_t)off, (uint8_t)(off >> 8), (uint8_t)(off >> 16), (uint8_t)(off >> 24)};
  reply(BP_R_ACK, d, sizeof d);
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static bp_rx_t rx;
static bool erasedInfo;

// Записати шматок програми. Сектор (4 КБ) стирається, коли запис починається з його початку.
static uint8_t writeChunk(uint32_t off, const uint8_t *data, uint32_t n) {
  if (off % 256 || n % 256 || n == 0 || n > 4096 || off + n > APP_MAX) return BP_ACK_BAD;
  uint32_t ints = save_and_disable_interrupts();
  if (!erasedInfo) {   // перший запис — програма вже не ціла: спершу стерти відомості
    flash_range_erase(INFO_OFF, FLASH_SECTOR_SIZE);
    erasedInfo = true;
  }
  if (off % FLASH_SECTOR_SIZE == 0) flash_range_erase(APP_OFF + off, FLASH_SECTOR_SIZE);
  flash_range_program(APP_OFF + off, data, n);
  restore_interrupts(ints);
  return memcmp((const void *)(APP_ADDR + off), data, n) ? BP_ACK_FLASH : BP_ACK_OK;
}

static uint8_t finish(uint32_t size, uint32_t crc, uint32_t ver) {
  if (size == 0 || size > APP_MAX) return BP_ACK_BAD;
  if (bp_crc32((const uint8_t *)APP_ADDR, size, 0) != crc) return BP_ACK_CRC;
  static uint8_t page[FLASH_PAGE_SIZE];
  memset(page, 0xFF, sizeof page);
  app_info_t inf = {INFO_MAGIC, size, crc, ver, ~INFO_MAGIC};
  memcpy(page, &inf, sizeof inf);
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(INFO_OFF, FLASH_SECTOR_SIZE);
  flash_range_program(INFO_OFF, page, FLASH_PAGE_SIZE);
  restore_interrupts(ints);
  erasedInfo = false;
  return appValid() ? BP_ACK_OK : BP_ACK_FLASH;
}

int main(void) {
  bool stay = watchdog_hw->scratch[0] == STAY_MAGIC;
  watchdog_hw->scratch[0] = 0;
  // Перезапуски сторожем поспіль (програма падає): лічить програма — скидає, коли пропрацювала 10 с.
  uint32_t cr = watchdog_hw->scratch[1];
  uint32_t crashes = (cr >> 8) == (CRASH_MAGIC >> 8) ? (cr & 0xFF) : 0;
  if (watchdog_caused_reboot() && !stay) crashes++;
  else if (!watchdog_caused_reboot()) crashes = 0;
  watchdog_hw->scratch[1] = (CRASH_MAGIC & ~0xFFu) | (crashes > 255 ? 255 : crashes);
  bool valid = appValid();
  if (valid && !stay && crashes < 3) jumpToApp();

  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, GPIO_OUT);
  uart_init(uart1, 115200);
  gpio_set_function(PIN_RX, GPIO_FUNC_UART);
  gpio_set_function(PIN_TX, GPIO_FUNC_UART);
  gpio_pull_up(PIN_RX);
  memset(&rx, 0, sizeof rx);
  absolute_time_t until = make_timeout_time_ms(60000);
  uint32_t ledT = 0;
  for (;;) {
    if (valid && time_reached(until)) {   // ніхто не прийшов — працювати далі зі старою програмою
      watchdog_hw->scratch[1] = 0;
      jumpToApp();
    }
    uint32_t now = time_us_32();
    if (now - ledT > 60000) {   // часте блимання — завантажувач чекає
      ledT = now;
      gpio_xor_mask(1u << PIN_LED);
    }
    if (!uart_is_readable(uart1)) continue;
    if (!bp_rx_feed(&rx, (uint8_t)uart_get_hw(uart1)->dr)) continue;
    until = make_timeout_time_ms(60000);
    const uint8_t *d = bp_rx_data(&rx);
    uint16_t n = bp_rx_len(&rx);
    switch (bp_rx_cmd(&rx)) {
      case BP_HELLO: {
        uint8_t h[1 + 1 + 4 + 4 + 4];
        uint32_t ver = valid ? info->ver : 0, size = valid ? info->size : 0, crc = valid ? info->crc32 : 0;
        h[0] = LOADER_VERSION;
        h[1] = valid;
        memcpy(h + 2, &ver, 4);
        memcpy(h + 6, &size, 4);
        memcpy(h + 10, &crc, 4);
        reply(BP_R_HELLO, h, sizeof h);
        break;
      }
      case BP_BAUD:
        if (n >= 4) {
          uint32_t b = rd32(d);
          ack(BP_BAUD, BP_ACK_OK, b);
          uart_tx_wait_blocking(uart1);
          uart_set_baudrate(uart1, b);
        }
        break;
      case BP_WRITE:
        if (n >= 4 + 256) {
          uint32_t off = rd32(d);
          valid = false;
          ack(BP_WRITE, writeChunk(off, d + 4, n - 4u), off);
        } else {
          ack(BP_WRITE, BP_ACK_BAD, 0);
        }
        break;
      case BP_FINISH:
        if (n >= 10) {
          uint8_t s = finish(rd32(d), rd32(d + 4), (uint32_t)(d[8] | (d[9] << 8)));
          valid = s == BP_ACK_OK;
          ack(BP_FINISH, s, 0);
        } else {
          ack(BP_FINISH, BP_ACK_BAD, 0);
        }
        break;
      case BP_BOOT:
        ack(BP_BOOT, valid ? BP_ACK_OK : BP_ACK_BAD, 0);
        uart_tx_wait_blocking(uart1);
        if (valid) {
          watchdog_hw->scratch[1] = 0;
          jumpToApp();
        }
        break;
    }
  }
}
