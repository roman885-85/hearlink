// Міст набору «Слух»: Raspberry Pi Pico (RP2040) між пультом і передавачем.
//   • вхід AES/EBU (трансформатор або дільник → 74HC04 → GP15) — бібліотека pico_spdif_rx (блок PIO0);
//   • вхід PCM1808: міст дає йому всі такти (GP0 MCLK 12,5 МГц, GP1 BCK, GP2 LRCK), дані — GP3;
//   • перерахунок частоти в 32 кГц тактом передавача (asrc.c) і вихід I2S до передавача: такти від нього
//     (GP6 ← вивід 1, GP7 ← вивід 2), дані GP4 → вивід 40;
//   • команди — UART з виводу 43 передавача (GP5); стан — у молодших 8 бітах слів I2S (hearlink/bridge_proto.h).
// Ядро 0: команди, стан, вибір джерела, сторож, світлодіод. Ядро 1: увесь звук — і переривання прийому AES
// (бібліотека береже свої дані лише від переривань свого ядра, тож читати її чергу з іншого ядра не можна).
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pico/stdio_usb.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "spdif_rx.h"
#include "bridge.pio.h"
#include "asrc.h"
#include "bridge_proto.h"
#include "layout.h"
#include "version.h"

#define PIN_MCLK 0
#define PIN_PCM_BCK 1   // LRCK — наступний (GP2): бокові біти PIO
#define PIN_PCM_DOUT 3
#define PIN_LINK_DATA 4
#define PIN_CMD_RX 5
#define PIN_LINK_BCK 6  // LRCK — наступний (GP7)
#define PIN_LINK_LRCK 7
#define PIN_AES 15
#define PIN_LED 25

#define FS_OUT 32000
#define FS_PCM 32552     // 200 МГц / 6144 = 32 552,08 Гц (решту виправляє регулятор)

// ---------------------------------------------------------------- останнє падіння
// panic() SDK (PICO_PANIC_FUNCTION=bridge_panic у CMakeLists.txt) і апаратна помилка не «висять», а пишуть, що сталося,
// у scratch[2..3] сторожа (переживають перезапуск) і перезапускають Pico; після запуску це видно в порту USB.
// scratch[0] — прохання до завантажувача, scratch[1] — його лічильник падінь, 4…7 — зайняті SDK.
#define FAULT_PANIC 0x50414E00u   // «PAN»
#define FAULT_HARD 0x48524400u    // «HRD»
static uint32_t lastFault, lastFaultWhat;

static void __attribute__((noreturn)) faultReboot(uint32_t kind, uint32_t what, uint32_t arg) {
  watchdog_hw->scratch[2] = what;
  watchdog_hw->scratch[3] = kind | ((arg & 0x7F) << 1) | get_core_num();
  watchdog_reboot(0, 0, 0);
  for (;;) {
  }
}

void bridge_panic(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  uint32_t a = va_arg(ap, uint32_t);
  va_end(ap);
  faultReboot(FAULT_PANIC, (uint32_t)fmt, a);
}

void hardfaultPc(uint32_t pc) { faultReboot(FAULT_HARD, pc, 0); }

// HardFault: адреса команди, на якій упало, — зі збереженого стеку (MSP чи PSP — за LR)
void __attribute__((naked)) isr_hardfault(void) {
  __asm volatile("movs r0, #4\n"
                 "mov r1, lr\n"
                 "tst r0, r1\n"
                 "beq 1f\n"
                 "mrs r0, psp\n"
                 "b 2f\n"
                 "1: mrs r0, msp\n"
                 "2: ldr r0, [r0, #24]\n"
                 "ldr r1, =hardfaultPc\n"
                 "bx r1\n");
}

// ---------------------------------------------------------------- спільне для ядер
// Ядро 0 → ядро 1: що грати. Ядро 1 бере новий запит, коли wantGen ≠ тому, що вже виконано.
static volatile uint32_t wantGen;
static volatile uint8_t wantAct, wantFmt;
static volatile uint32_t wantRate;
// Ядро 1 → звіт
static volatile uint8_t actNow = BP_ACT_SWITCH;
static volatile uint32_t hb1;                 // «серце» ядра 1 — сторож дивиться, чи воно б'ється
static volatile int32_t peakL, peakR;         // пік за проміжок звіту (24 біти)
static volatile uint16_t cpu1;                // зайнятість ядра 1, ‰
static volatile uint16_t nsProd, nsIn;        // на один кадр: вихід (перерахунок і все навколо) і вхід, нс
static volatile uint32_t aesBlkT;             // коли прийшла остання пачка AES (мкс) — з переривання
static volatile int16_t stLevel, stPpm10;     // запас і поправка перерахунку — ядро 1 кладе після регулятора
static volatile uint16_t stUnder, stOver;
static volatile uint32_t stOutRd;             // скільки слів забрав передавач (чи є його такти)
static asrc_t A;

// Зворотний канал (ядро 0 пише, ядро 1 видає по два байти на кадр I2S)
#define BQ 1024
static uint8_t bq[BQ];
static volatile uint32_t bqHead, bqTail;

static bool bqPush(const uint8_t *p, uint32_t n) {
  if (BQ - (bqHead - bqTail) < n) return false;
  uint32_t h = bqHead;
  for (uint32_t i = 0; i < n; i++) bq[(h + i) % BQ] = p[i];
  __dmb();
  bqHead = h + n;
  return true;
}

static inline uint8_t __not_in_flash_func(bqPop)(void) {
  uint32_t t = bqTail;
  if (t == bqHead) return 0;
  uint8_t b = bq[t % BQ];
  __dmb();
  bqTail = t + 1;
  return b;
}

// ---------------------------------------------------------------- звук (ядро 1)
static const PIO pio = pio1;
static uint smPcm, smLink, offPcm, offLink;
static volatile uint32_t stResync, stResyncAt;   // скільки разів вихід ловив такт передавача наново, і коли (мкс)

#define OUT_WORDS 512   // 256 кадрів = 8 мс
static uint32_t outRing[OUT_WORDS] __attribute__((aligned(OUT_WORDS * 4)));
static int dmaOut;
static uint32_t outWr, outRd, outRdIdx;
#define LEAD_RUN 16     // слів наперед: 8 кадрів = 0,25 мс (владелец: «минимальная задержка звука, в идеале без задержки»)
#define LEAD_BUILD 256  // поки рахуються ваги — 4 мс (одна фаза ваг рахується ~1,2 мс)

#define PCM_WORDS 512
static uint32_t pcmRing[PCM_WORDS] __attribute__((aligned(PCM_WORDS * 4)));
static int dmaPcm;
static uint32_t pcmIdx;

static void __isr dmaIrq1(void) {
  // Канали йдуть по колу з лічильником 2^32 слів; раз на ~18 год він закінчується — одразу знову
  // (FIFO PIO на 8 слів дає на це сотню мікросекунд).
  if (dma_channel_get_irq1_status(dmaOut)) {
    dma_channel_acknowledge_irq1(dmaOut);
    dma_channel_set_trans_count(dmaOut, 0xFFFFFFFFu, true);
  }
  if (dma_channel_get_irq1_status(dmaPcm)) {
    dma_channel_acknowledge_irq1(dmaPcm);
    dma_channel_set_trans_count(dmaPcm, 0xFFFFFFFFu, true);
  }
}

// Бібліотека AES кличе це з переривання на кожну цілу пачку (192 кадри) — запам'ятовуємо час.
void spdif_rx_callback_func(uint32_t *buff, uint32_t n, uint8_t c_bits[SPDIF_BLOCK_SIZE / 16], bool parity_err) {
  (void)buff;
  (void)n;
  (void)c_bits;
  (void)parity_err;
  aesBlkT = time_us_32();
}

// PCM1808 (FMT на 3,3 В — «лівовирівняний»): запуск із початку кільця, щоб слова йшли парами L,R.
static void pcmStart(void) {
  offPcm = pio_add_program(pio, &pcm_lj_program);
  pio_sm_config c = pcm_lj_program_get_default_config(offPcm);
  sm_config_set_sideset_pins(&c, PIN_PCM_BCK);
  sm_config_set_in_pins(&c, PIN_PCM_DOUT);
  sm_config_set_in_shift(&c, false, true, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv_int_frac(&c, 24, 0);   // 200 МГц / 24 / 4 такти = BCK 2,0833 МГц
  pio_sm_init(pio, smPcm, offPcm, &c);
  dma_channel_config d = dma_channel_get_default_config(dmaPcm);
  channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
  channel_config_set_read_increment(&d, false);
  channel_config_set_write_increment(&d, true);
  channel_config_set_ring(&d, true, 11);   // запис по колу 2048 байт
  channel_config_set_dreq(&d, pio_get_dreq(pio, smPcm, false));
  dma_channel_configure(dmaPcm, &d, pcmRing, &pio->rxf[smPcm], 0xFFFFFFFFu, true);
  pcmIdx = 0;
  pio_sm_set_enabled(pio, smPcm, true);
}

static void audioSetup(void) {
  // MCLK 12,5 МГц — ШІМ (зріз 0, канал A на GP0): 200 МГц / 16. Той самий такт, що й у PIO (BCK = 200 МГц / 96), —
  // частоти кратні (×6), тож для PCM1808 усі три такти синхронні.
  gpio_set_function(PIN_MCLK, GPIO_FUNC_PWM);
  uint sl = pwm_gpio_to_slice_num(PIN_MCLK);
  pwm_config pc = pwm_get_default_config();
  pwm_config_set_clkdiv_int(&pc, 1);
  pwm_config_set_wrap(&pc, 15);
  pwm_init(sl, &pc, false);
  pwm_set_chan_level(sl, pwm_gpio_to_channel(PIN_MCLK), 8);
  pwm_set_enabled(sl, true);
  // PCM1808 (виводи й канал DMA; програму ставить pcmStart)
  smPcm = (uint)pio_claim_unused_sm(pio, true);
  pio_gpio_init(pio, PIN_PCM_BCK);
  pio_gpio_init(pio, PIN_PCM_BCK + 1);
  pio_sm_set_consecutive_pindirs(pio, smPcm, PIN_PCM_BCK, 2, true);
  pio_sm_set_consecutive_pindirs(pio, smPcm, PIN_PCM_DOUT, 1, false);
  dmaPcm = dma_claim_unused_channel(true);
  // вихід до передавача
  offLink = pio_add_program(pio, &link_tx_program);
  smLink = (uint)pio_claim_unused_sm(pio, true);
  pio_sm_config c = link_tx_program_get_default_config(offLink);
  sm_config_set_out_pins(&c, PIN_LINK_DATA, 1);
  sm_config_set_in_pins(&c, PIN_LINK_BCK);
  sm_config_set_jmp_pin(&c, PIN_LINK_LRCK);
  sm_config_set_out_shift(&c, false, true, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  pio_gpio_init(pio, PIN_LINK_DATA);
  pio_sm_set_consecutive_pindirs(pio, smLink, PIN_LINK_DATA, 1, true);
  pio_sm_set_consecutive_pindirs(pio, smLink, PIN_LINK_BCK, 2, false);
  pio_sm_init(pio, smLink, offLink, &c);
  memset(outRing, 0, sizeof outRing);
  outWr = LEAD_RUN;   // перші LEAD слів — тиша, що вже «лежить» у кільці
  dmaOut = dma_claim_unused_channel(true);
  dma_channel_config d = dma_channel_get_default_config(dmaOut);
  channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
  channel_config_set_read_increment(&d, true);
  channel_config_set_write_increment(&d, false);
  channel_config_set_ring(&d, false, 11);  // читання по колу 2048 байт
  channel_config_set_dreq(&d, pio_get_dreq(pio, smLink, true));
  dma_channel_configure(dmaOut, &d, &pio->txf[smLink], outRing, 0xFFFFFFFFu, true);
  dma_channel_set_irq1_enabled(dmaOut, true);
  dma_channel_set_irq1_enabled(dmaPcm, true);
  irq_set_exclusive_handler(DMA_IRQ_1, dmaIrq1);
  irq_set_enabled(DMA_IRQ_1, true);
  pcmStart();
  pio_sm_set_enabled(pio, smLink, true);
  // AES — переривання цього ядра, свій пул будильників теж на цьому ядрі. Канали DMA бібліотека займає САМА
  // (dma_channel_claim): тут лише знаходимо вільні й одразу відпускаємо — інакше її claim падає в panic (так і було
  // з першою версією: ядро 1 ставало, сторож перезапускав Pico).
  int ch0 = dma_claim_unused_channel(true), ch1 = dma_claim_unused_channel(true);
  dma_channel_unclaim((uint)ch0);
  dma_channel_unclaim((uint)ch1);
  spdif_rx_config_t sc = {
      .data_pin = PIN_AES,
      .pio_sm = 0,
      .dma_channel0 = (uint)ch0,
      .dma_channel1 = (uint)ch1,
      .alarm_pool = alarm_pool_create_with_unused_hardware_alarm(16),
      .flags = SPDIF_RX_FLAG_CALLBACKS | SPDIF_RX_FLAG_CHECK_PARITY,
  };
  spdif_rx_start(&sc);
}

static inline void __not_in_flash_func(outPoll)(void) {
  uint32_t idx = (((uint32_t)dma_hw->ch[dmaOut].read_addr - (uint32_t)outRing) / 4u) & (OUT_WORDS - 1);
  outRd += (idx - outRdIdx) & (OUT_WORDS - 1);
  outRdIdx = idx;
}

// Вирівнювання PCM1808 (вимір на платі 21.10, запис слів через передавач): з тактами від моста PCM1808 (FMT на 3,3 В)
// видає дані на такт BCK ПІЗНІШЕ, ніж міст їх читає: біт 31 слова — «хвіст» попереднього (0), D23…D0 — біти 30…7, і
// слабкий мінус (−1480) читався як майже повна шкала плюс (0x7FFD1C). Признак: біт 7 (у рівному слові там уже нулі
// доповнення) часто 1, а біт 31 — ніколи; тоді слово зсувається на біт ліворуч. Рахується на ходу (по 4096 ненульових
// слів), рішення — лише за явних ознак, щоб тиша чи постійний зсув нуля його не перемикали.
static uint32_t pcmN, pcmB7, pcmB31;
static volatile uint8_t pcmShift = 1;   // так виміряно з FMT на 3,3 В; перевіряється на ходу

// Нові кадри PCM1808 → перерахунок (feed) або в нікуди. Повертає, скільки кадрів забрано.
static uint32_t __not_in_flash_func(pcmPull)(bool feed) {
  uint32_t idx = ((((uint32_t)dma_hw->ch[dmaPcm].write_addr - (uint32_t)pcmRing) / 4u) & (PCM_WORDS - 1)) & ~1u;
  int16_t tmp[2 * 64];
  uint32_t total = 0;
  while (pcmIdx != idx) {
    uint32_t n = 0;
    while (pcmIdx != idx && n < 64) {
      for (int k = 0; k < 2; k++) {
        uint32_t w = pcmRing[pcmIdx + k];
        if (w) {
          pcmN++;
          if (w & 0x80u) pcmB7++;
          if (w & 0x80000000u) pcmB31++;
        }
        tmp[2 * n + k] = (int16_t)((int32_t)(pcmShift ? w << 1 : w) >> 16);
      }
      pcmIdx = (pcmIdx + 2) & (PCM_WORDS - 1);
      n++;
    }
    if (feed) asrc_write(&A, tmp, n);
    total += n;
  }
  if (pcmN >= 4096) {
    if (pcmB7 > pcmN / 16 && pcmB31 == 0) pcmShift = 1;
    else if (pcmB7 == 0 && pcmB31 > pcmN / 64) pcmShift = 0;
    pcmN = pcmB7 = pcmB31 = 0;
  }
  return total;
}

// Нові кадри AES (слово бібліотеки: [27:4] — 24 біти звуку, [3:0] — синхро: B 1111, M 1011 — лівий, W 0111 — правий).
// Читається «по кадру» — щойно DMA записав (spdif_rx_read_fine), не чекаючи цілої пачки в 4 мс.
static uint32_t __not_in_flash_func(aesPull)(bool feed) {
  int16_t tmp[2 * 64];
  uint32_t total = 0;
  for (;;) {
    uint32_t avail = spdif_rx_get_fine_count() & ~1u;
    if (!avail) return total;
    uint32_t *p;
    uint32_t got = spdif_rx_read_fine(&p, avail), i = 0;
    if (!got) return total;
    while (i + 1 < got) {
      uint32_t n = 0;
      while (i + 1 < got && n < 64) {
        if ((p[i] & 0xF) == 0x7) {   // починається з правого — зсув на одне слово
          i++;
          continue;
        }
        tmp[2 * n] = (int16_t)((int32_t)(p[i] << 4) >> 16);
        tmp[2 * n + 1] = (int16_t)((int32_t)(p[i + 1] << 4) >> 16);
        i += 2;
        n++;
      }
      if (feed && n) asrc_write(&A, tmp, n);
      total += n;
    }
  }
}

// Синус 1 кГц −12 дБ (перевірочний): 32 відліки на період при 32 кГц, з повною 24-бітною точністю. Не «16 біт << 8»: тоді
// молодші біти слова — нулі, і передавач 3.29 вирішував, що дані PCM1808 зсунуті на біт (власник: «тон с постоянными
// щелчками как трещотка»).
static const int32_t tone32[32] = {0, 409134, 802545, 1165115, 1482910, 1743718, 1937516, 2056856, 2097152, 2056856,
                                   1937516, 1743718, 1482910, 1165115, 802545, 409134, 0, -409134, -802545, -1165115,
                                   -1482910, -1743718, -1937516, -2056856, -2097152, -2056856, -1937516, -1743718,
                                   -1482910, -1165115, -802545, -409134};

// Перевірочні сигнали (1.02; власник 22.10 обрав зі списку можливостей моста): іде на вихід замість входу — через увесь
// шлях передавача (його обробку, ефір, приймачі), щоб міряти тракт без пульта. 21.10 саме такого не вистачало, коли
// шукали «одні високі»: рожевий шум з пульта треба було просити в людини. Вид — BP_SET d[2] (bridge_proto.h, BP_GEN_*).
static int16_t sinTab[1025];   // синус на 1024 кроки (+1 — для лінійного проміжку), ±32767
static void genInit(void) {
  for (int i = 0; i <= 1024; i++) sinTab[i] = (int16_t)lrintf(32767.0f * sinf(6.2831853f * (float)i / 1024.0f));
}
static volatile uint8_t genKind = BP_GEN_TONE;   // який сигнал (ставить ядро 0 з BP_SET, читає ядро 1)
static uint32_t genRnd = 22222;
static inline int32_t genRand(void) {   // ±2^20 (21 біт зі знаком)
  genRnd = genRnd * 1664525u + 1013904223u;
  return (int32_t)(genRnd >> 11) - (1 << 20);
}
// Рожевий шум — алгоритм Восса — Маккартні: 16 рядів випадкових чисел, ряд k оновлюється раз на 2^k відліків (кожен
// новий відлік — той ряд, скільки нулів у кінці лічильника), сума рядів і білий шум. Рівень −20 дБ (СКВ) від повної шкали.
static int32_t pinkRows[16], pinkSum;
static uint32_t pinkN;
static int32_t __not_in_flash_func(genPink)(void) {
  pinkN++;
  int k = __builtin_ctz(pinkN);
  if (k < 16) {
    int32_t v = genRand() / 3;   // ±349 525 — 17 доданків (16 рядів + білий) дають СКВ ≈ 0,1 повної шкали
    pinkSum += v - pinkRows[k];
    pinkRows[k] = v;
  }
  return pinkSum + genRand() / 3;
}
// Розгортка: частота росте від 20 Гц до 16 кГц за 10 с (логарифмічно — кожна октава однаково довго), потім 1 с тиші й
// знову. −12 дБ. Крок фази множиться на k раз на відлік (число з рухомою комою — 1 % ядра).
static uint32_t sweepPh, sweepN;
static float sweepInc;
static int32_t __not_in_flash_func(genSweep)(void) {
  const uint32_t RUN = FS_OUT * 10, GAP = FS_OUT;
  if (sweepN == 0) sweepInc = 20.0f / (float)FS_OUT * 4294967296.0f;
  uint32_t n = sweepN++;
  if (sweepN >= RUN + GAP) sweepN = 0;
  if (n >= RUN) return 0;
  static const float K = 1.00002085f;   // (16000 / 20)^(1 / 320 000)
  sweepPh += (uint32_t)sweepInc;
  sweepInc *= K;
  uint32_t i = sweepPh >> 22, f = (sweepPh >> 7) & 0x7FFF;
  int32_t a = sinTab[i], b = sinTab[i + 1];
  return (a + (((b - a) * (int32_t)f) >> 15)) * 64;   // ±2 097 088 = −12 дБ у 24 бітах
}
static int32_t __not_in_flash_func(genNext)(uint8_t kind, uint32_t *ph) {
  if (kind == BP_GEN_PINK) return genPink();
  if (kind == BP_GEN_SWEEP) return genSweep();
  return tone32[(*ph)++ & 31];
}

enum { CS_PLAY, CS_FADE, CS_BUILD };

// Вихід збився з такту передавача (link_tx: IRQ 0 і чекає): зупинити, почистити, читати з межі кадру на LEAD позаду
// запису, пустити з пошуку початку кадру. Ніякого «хвоста» півслова — лівий і правий не переплутаються.
static void __not_in_flash_func(linkRecover)(void) {
  pio_sm_set_enabled(pio, smLink, false);
  dma_channel_set_irq1_enabled(dmaOut, false);
  dma_channel_abort(dmaOut);
  dma_channel_acknowledge_irq1(dmaOut);
  pio_sm_clear_fifos(pio, smLink);
  pio_sm_restart(pio, smLink);
  pio_sm_exec(pio, smLink, pio_encode_jmp(offLink + link_tx_offset_sync));
  uint32_t idx = (outWr - LEAD_RUN) & (OUT_WORDS - 1);
  outRd = outWr - LEAD_RUN;
  outRdIdx = idx;
  dma_channel_set_read_addr(dmaOut, &outRing[idx], false);
  dma_channel_set_trans_count(dmaOut, 0xFFFFFFFFu, true);
  dma_channel_set_irq1_enabled(dmaOut, true);
  pio_interrupt_clear(pio, 0);
  pio_sm_set_enabled(pio, smLink, true);
  stResync = stResync + 1;
  stResyncAt = time_us_32();
}

// Увесь «гарячий» шлях ядра 1 — з ОЗП (__not_in_flash_func): з флешу через спільний з ядром 0 і USB кеш на 16 КБ
// кадр коштував 17,5 + 6,2 мкс із 31,25 (вимір на платі 21.10).
static void __not_in_flash_func(core1Main)(void) {
  audioSetup();
  uint32_t haveGen = 0, steerN = 0, tonePh = 0, busy = 0, winT = time_us_32();
  uint32_t prodUs = 0, prodFr = 0, inUs = 0, inFr = 0;
  bool restartPending = false;
  uint32_t pullT = 0, ditherSeed = 12345;
  uint8_t act = BP_ACT_NONE;   // що звучить
  int cs = CS_PLAY;
  int32_t gain = 0;            // Q15
  const int32_t gStep = 32768 / 320;   // 10 мс уходу/входу
  for (;;) {
    hb1 = hb1 + 1;
    // зайнятість — лише час самої роботи (обходи з опитуванням не рахуються)
    uint32_t t0 = time_us_32();
    bool feed = cs == CS_PLAY || cs == CS_FADE;   // і під час уходу звуку (інакше перерахунку бракує входу)
    // Вхід забирається раз на 100 мкс (4–5 кадрів), а не на кожному обході: виклик бібліотеки AES (із вимиканням
    // переривань і діленнями) коштував ~4 мкс, і на кадр виходило 4,8 мкс лише на опитування (ядро 1 — 79 %).
    uint32_t got = 0;
    if (t0 - pullT >= 100) {
      pullT = t0;
      got = pcmPull(feed && act == BP_ACT_PCM) + aesPull(feed && act == BP_ACT_AES);
    }
    uint32_t tIn = time_us_32();
    if (got) {
      busy += tIn - t0;
      inUs += tIn - t0;
      inFr += got;
    }
    uint32_t made = 0;
    if (cs == CS_PLAY && wantGen != haveGen) cs = CS_FADE;
    if (pio_interrupt_get(pio, 0)) linkRecover();
    outPoll();
    stOutRd = outRd;
    int lead = cs == CS_BUILD ? LEAD_BUILD : LEAD_RUN;
    while ((int32_t)(outWr - outRd) < lead) {
      int32_t l = 0, r = 0;
      bool snd = act == BP_ACT_PCM || act == BP_ACT_AES;
      if (cs != CS_BUILD && snd) {
        // Перший кадр після нового фільтра: черга тиші, накопичена за розрахунок ваг (до 4 мс), уже пішла — лише тепер
        // ставимо точку читання (інакше запас підскакував до 47 кадрів замість 16, і регулятор зганяв його секунди).
        if (restartPending) {
          asrc_restart(&A);
          restartPending = false;
        }
        asrc_read(&A, &l, &r);
        if (++steerN == 32) {
          steerN = 0;
          asrc_steer(&A, 32, 0);   // обидва входи читаються по кадру — запас рівний, «кадрів у дорозі» нема
          stLevel = (int16_t)asrc_level(&A);
          stPpm10 = (int16_t)(A.ppm * 10.0f);
          stUnder = (uint16_t)A.under;
          stOver = (uint16_t)A.over;
        }
      } else if (cs != CS_BUILD && act == BP_ACT_TONE) {
        l = r = genNext(genKind, &tonePh);   // однаково в обидва канали: міряємо тембр (середину), не стерео
      }
      // Дизер: трикутний шум ±1 молодшого біта 16-бітного звуку (−96 дБ — не чути). Передавач працює в 16 бітах, і
      // цифрова тиша з AES доходила до нього рівно нулями: фільтри еквалайзера в паузах загасали до «підпорогових»
      // чисел, процесор рахував їх у рази довше — задача звуку не встигала, губились кадри I2S (190 за 21 с лише
      // на AES; на PCM1808 і тоні — 0), у навушниках «прерывистый» звук і тріск. Шум не дає тиші стати рівно нулем.
      ditherSeed = ditherSeed * 1664525u + 1013904223u;
      int32_t dl = (int32_t)((ditherSeed >> 24) & 0xFF) - (int32_t)((ditherSeed >> 16) & 0xFF);
      int32_t dr = (int32_t)((ditherSeed >> 8) & 0xFF) - (int32_t)(ditherSeed & 0xFF);
      // плавно: уходимо до нуля перед зміною, входимо після
      if (cs == CS_FADE) gain = gain > gStep ? gain - gStep : 0;
      else if (cs == CS_PLAY) gain = gain < 32768 - gStep ? gain + gStep : 32768;
      else gain = 0;
      if (gain != 32768) {   // лише під час входу/уходу звуку (без 64-бітного множення: точність тут не важлива)
        l = (l >> 8) * gain >> 7;
        r = (r >> 8) * gain >> 7;
      }
      l += dl;
      r += dr;
      int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
      if (al > peakL) peakL = al;
      if (ar > peakR) peakR = ar;
      // Байт зворотного каналу — по напівбайту в біти 7…4 лівого і правого слова; біти 3…0 — завжди нулі: передавач 3.29
      // «узнає зсув PCM1808» саме за бітом 0 (біт 0 іноді 1, а біт 8 — ніколи) і тоді псує звук.
      uint32_t i = outWr & (OUT_WORDS - 1);
      uint8_t bb = bqPop();
      outRing[i] = ((uint32_t)l << 8) | (bb & 0xF0u);
      outRing[i + 1] = ((uint32_t)r << 8) | ((uint32_t)(bb & 0x0Fu) << 4);
      outWr += 2;
      made++;
      if (cs == CS_FADE && gain == 0) {   // тиша — можна міняти
        haveGen = wantGen;
        uint8_t want = wantAct;
        uint32_t rate = wantRate;
        act = want;
        actNow = BP_ACT_SWITCH;
        if (act == BP_ACT_PCM || act == BP_ACT_AES) {
          // Затримка моста: запас + пів фільтра + вихід наперед. AES 48 кГц: 24 + 32 кадри (0,5 + 0,67 мс) + 0,4 мс;
          // PCM1808: 16 + 16 (0,5 + 0,5 мс) + 0,4 мс. Перевірено на Mac (bridge/test/asrc_test.c): 98 і 97 дБ.
          if (act == BP_ACT_AES) asrc_begin(&A, rate, FS_OUT, 64, 24, 8);
          else asrc_begin(&A, FS_PCM, FS_OUT, 32, 16, 8);
          cs = CS_BUILD;
        } else {
          actNow = act;
          cs = CS_PLAY;
        }
        break;
      }
    }
    uint32_t tOut = time_us_32();
    if (made) {
      busy += tOut - tIn;
      prodUs += tOut - tIn;
      prodFr += made;
    }
    if (cs == CS_BUILD && asrc_build(&A, 1)) {
      // свіжий вхід: старі кадри викинути, почати з нуля
      pcmPull(false);
      aesPull(false);
      asrc_finish(&A);
      restartPending = true;
      steerN = 0;
      actNow = act;
      cs = CS_PLAY;
    }
    uint32_t t1 = time_us_32();
    if (cs == CS_BUILD) busy += t1 - tOut;
    if (t1 - winT >= 100000) {
      cpu1 = (uint16_t)(busy * 1000u / (t1 - winT));
      nsProd = (uint16_t)(prodFr ? prodUs * 1000u / prodFr : 0);
      nsIn = (uint16_t)(inFr ? inUs * 1000u / inFr : 0);
      prodUs = prodFr = inUs = inFr = 0;
      busy = 0;
      winT = t1;
    }
  }
}

// ---------------------------------------------------------------- команди і стан (ядро 0)
static uint8_t reqSrc = BP_SRC_AUTO, reqFmt = BP_FMT_LJ, reqTone, lastSeq;
// Розбір команд: буфер на 4 КБ — НЕ на стеку (стек ядра 0 — 2 КБ: перша версія мосту падала саме від цього — буфер
// вилазив за стек і псував пам'ять ядра 1, сторож перезапускав Pico, а після трьох падінь завантажувач чекав команд).
static bp_rx_t rx;

static uint8_t aesPin = 0;      // підтяжка виводу 15: 0 — немає, 1 — угору, 2 — униз (так після запуску RP2040)
static bool aesSchmitt = true;

static void enterLoader(void) {
  watchdog_hw->scratch[0] = STAY_MAGIC;
  watchdog_reboot(0, 0, 10);
  for (;;) tight_loop_contents();
}

// Порт USB (лише на столі: міст підключено до комп'ютера) — рядок стану раз на секунду і команди однією буквою.
// У ділі USB не підключено, і цей код нічого не робить.
static const char *const actName[] = {"нічого", "AES", "PCM1808", "тиша", "перевірочний сигнал", "перемикання"};
static const char *const aesName[] = {"немає", "ловлю", "є", "частота не та"};

static void usbHelp(void) {
  printf("міст %d.%02d: a — AES, p — PCM1808, u — авто, m — тиша, t — тон, "
         "l — у завантажувач, b — у режим запису USB (без диска; писати picotool), g — що на вході AES, "
         "k — чи живі виводи (такти й дані PCM1808, такти передавача, AES)\n", BRIDGE_VER_MAJOR, BRIDGE_VER_MINOR);
}

static void handleCmd(const bp_rx_t *rx) {
  const uint8_t *d = bp_rx_data(rx);
  uint16_t n = bp_rx_len(rx);
  switch (bp_rx_cmd(rx)) {
    case BP_PING:
      if (n >= 1) lastSeq = d[0];
      break;
    case BP_SET:
      if (n >= 4) {
        reqSrc = d[0] <= BP_SRC_MUTE ? d[0] : BP_SRC_AUTO;
        // d[1] — формат PCM1808: лише «лівовирівняний» (FMT на 3,3 В), інший прибрано (див. bridge.pio)
        reqTone = d[2] <= BP_GEN_SWEEP ? d[2] : BP_GEN_TONE;   // 0 — немає, 1 — тон, 2 — рожевий шум, 3 — розгортка
        if (reqTone) genKind = reqTone;
        lastSeq = d[3];
      }
      break;
    case BP_ENTER_LOADER:
      if (n >= 4 && d[0] == 'L' && d[1] == 'O' && d[2] == 'A' && d[3] == 'D') enterLoader();
      break;
  }
}

static uint8_t peakDb2(int32_t p) {   // 24-бітний пік → −дБ ×2 (255 — тиша)
  if (p <= 0) return 255;
  float db = -20.0f * log10f((float)p / 8388608.0f) * 2.0f;
  return db < 0 ? 0 : db > 254 ? 254 : (uint8_t)db;
}

int main(void) {
  uint32_t s3 = watchdog_hw->scratch[3];
  if ((s3 & 0xFFFFFF00u) == FAULT_PANIC || (s3 & 0xFFFFFF00u) == FAULT_HARD) {
    lastFault = s3;
    lastFaultWhat = watchdog_hw->scratch[2];
  }
  watchdog_hw->scratch[2] = 0;
  watchdog_hw->scratch[3] = 0;
  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, GPIO_OUT);
  uart_init(uart1, 115200);
  gpio_set_function(PIN_CMD_RX, GPIO_FUNC_UART);
  gpio_pull_up(PIN_CMD_RX);   // передавач вимкнено — лінія не «висить» у нулі
  // Вхід AES: без підтяжки. Після запуску RP2040 тягне вивід униз (~50 кОм), і зміщення 1,3 В від дільника на
  // вхідній схемі просідало до ~1,07 В: «1» на виводі трималась 36 % часу замість 50, і прийом не ловився (21.10).
  gpio_disable_pulls(PIN_AES);
  stdio_usb_init();
  genInit();
  multicore_launch_core1(core1Main);
  watchdog_enable(1000, true);

  uint32_t lastHb = 0, hbT = time_us_32(), statT = 0, lowSince = 0, aesOkSince = 0, ledT = 0;
  uint32_t resync = 0, resyncAt = 0, uptime0 = time_us_32(), lastOutRd = 0, outRdT = 0, usbT = 0;
  uint8_t pkSecL = 255, pkSecR = 255;   // найбільший пік виходу за секунду рядка стану (−дБ ×2; 255 — тиша)
  bool lineWasHigh = false, crashCleared = false;
  for (;;) {
    uint32_t now = time_us_32();
    // пропрацювали 10 с — лічильник «падінь поспіль» для завантажувача скинути (див. loader/main.c)
    if (!crashCleared && now - uptime0 > 10000000u) {
      watchdog_hw->scratch[1] = 0;
      crashCleared = true;
    }
    // сторож: годуємо, лише поки ядро 1 живе
    if (hb1 != lastHb) {
      lastHb = hb1;
      hbT = now;
    }
    if (now - hbT < 300000) watchdog_update();
    // команди передавача
    while (uart_is_readable(uart1)) {
      uint8_t b = (uint8_t)uart_get_hw(uart1)->dr;
      if (bp_rx_feed(&rx, b)) handleCmd(&rx);
    }
    // «Розрив» (лінія в нулі ≥ 80 мс після того, як була в спокої «1») — у завантажувач: так передавач оновлює
    // міст, навіть якщо ця програма вже не розуміє команд.
    if (gpio_get(PIN_CMD_RX)) {
      lineWasHigh = true;
      lowSince = 0;
    } else if (lineWasHigh) {
      if (!lowSince) lowSince = now | 1;
      else if (now - lowSince > 80000) enterLoader();
    }
    // перескоки синхронізації виходу (PIO IRQ 0)
    resync = stResync;
    if (resync) resyncAt = (stResyncAt - uptime0) / 1000000u;
    // AES і вибір джерела
    spdif_rx_state_t s = spdif_rx_get_state();
    uint32_t rate = s == SPDIF_RX_STATE_STABLE ? (uint32_t)spdif_rx_get_samp_freq() : 0;
    uint8_t aes = s == SPDIF_RX_STATE_STABLE ? (rate == 44100 || rate == 48000 ? BP_AES_OK : BP_AES_RATE)
                  : s == SPDIF_RX_STATE_WAITING_STABLE ? BP_AES_WAIT : BP_AES_NONE;
    if (aes == BP_AES_OK) {
      if (!aesOkSince) aesOkSince = now | 1;
    } else {
      aesOkSince = 0;
    }
    bool aesSteady = aesOkSince && now - aesOkSince > 300000;   // «авто» бере AES, коли він тримається 0,3 с
    uint8_t want;
    if (reqTone) want = BP_ACT_TONE;
    else if (reqSrc == BP_SRC_PCM) want = BP_ACT_PCM;
    else if (reqSrc == BP_SRC_MUTE) want = BP_ACT_MUTE;
    else if (reqSrc == BP_SRC_AES) want = aes == BP_AES_OK ? BP_ACT_AES : BP_ACT_MUTE;
    else want = aesSteady ? BP_ACT_AES : BP_ACT_PCM;
    if (want != wantAct || reqFmt != wantFmt || (want == BP_ACT_AES && rate != wantRate)) {
      wantAct = want;
      wantFmt = reqFmt;
      wantRate = rate;
      __dmb();
      wantGen = wantGen + 1;
    }
    // стан — 20 разів на секунду
    if (now - statT >= 50000) {
      statT = now;
      bp_status_t st;
      memset(&st, 0, sizeof st);
      st.type = BP_ST_STATUS;
      st.verMajor = BRIDGE_VER_MAJOR;
      st.verMinor = BRIDGE_VER_MINOR;
      st.seq = lastSeq;
      st.src = reqSrc;
      st.fmt = reqFmt;
      st.tone = reqTone;
      st.act = actNow;
      st.aes = aes;
      int32_t pl = peakL, pr = peakR;
      peakL = 0;
      peakR = 0;
      st.peakL = peakDb2(pl);
      st.peakR = peakDb2(pr);
      if (st.peakL < pkSecL) pkSecL = st.peakL;
      if (st.peakR < pkSecR) pkSecR = st.peakR;
      st.loaderVer = LOADER_VERSION;
      st.aesRate = rate ? rate : (s == SPDIF_RX_STATE_NO_SIGNAL ? 0 : (uint32_t)spdif_rx_get_samp_freq());
      st.aesParity = spdif_rx_get_parity_err_count();
      st.ppm10 = stPpm10;
      st.level = stLevel;
      st.under = stUnder;
      st.over = stOver;
      st.resync = (uint16_t)resync;
      st.cpu1 = cpu1;
      st.uptime = (now - uptime0) / 1000000u;
      uint8_t raw[sizeof st + 2], enc[sizeof st + 8];
      memcpy(raw, &st, sizeof st);
      uint16_t c = bp_crc16(raw, sizeof st, 0xFFFF);
      raw[sizeof st] = (uint8_t)c;
      raw[sizeof st + 1] = (uint8_t)(c >> 8);
      size_t m = bp_cobs_enc(raw, sizeof raw, enc);
      enc[m++] = 0;
      bqPush(enc, (uint32_t)m);
    }
    // USB: команди однією буквою і рядок стану раз на секунду
    if (stdio_usb_connected()) {
      int ch = getchar_timeout_us(0);
      if (ch == 'a') reqSrc = BP_SRC_AES;
      else if (ch == 'p') reqSrc = BP_SRC_PCM;
      else if (ch == 'u') reqSrc = BP_SRC_AUTO;
      else if (ch == 'm') reqSrc = BP_SRC_MUTE;
      else if (ch == 't') {   // перевірочний сигнал по колу: немає → тон → рожевий шум → розгортка → немає
        reqTone = (uint8_t)((reqTone + 1) % 4);
        if (reqTone) genKind = reqTone;
      }
      else if (ch == 'l') enterLoader();
      else if (ch == 'b') reset_usb_boot(0, 1);   // режим запису без «диска» RPI-RP2 (див. CMakeLists.txt)
      else if (ch == '?' || ch == 'h') usbHelp();
      else if (ch == 'x') {   // підтяжка виводу 15: вимк. → угору → униз → вимк. (за замовчуванням у RP2040 — униз)
        aesPin = (uint8_t)((aesPin + 1) % 3);
        gpio_set_pulls(PIN_AES, aesPin == 1, aesPin == 2);
        printf("вивід 15: підтяжка %s\n", aesPin == 0 ? "вимкнена" : aesPin == 1 ? "угору" : "униз");
      } else if (ch == 'y') {   // тригер Шмітта на виводі 15
        aesSchmitt = !aesSchmitt;
        gpio_set_input_hysteresis_enabled(PIN_AES, aesSchmitt);
        printf("вивід 15: тригер Шмітта %s\n", aesSchmitt ? "увімкнено" : "вимкнено");
      } else if (ch == 'g') {   // що на вході AES: 1 мс опитування виводу 15 (ядро 0, без переривань)
        uint32_t chg = 0, ones = 0, n = 0, t = time_us_32(), save = save_and_disable_interrupts();
        bool prev = gpio_get(PIN_AES);
        while (time_us_32() - t < 1000) {
          bool v = gpio_get(PIN_AES);
          chg += v != prev;
          ones += v;
          prev = v;
          n++;
        }
        restore_interrupts(save);
        printf("вхід AES (вивід 15) за 1 мс: опитувань %lu, змін %lu, «1» %lu%% часу%s\n", (unsigned long)n,
               (unsigned long)chg, (unsigned long)(n ? ones * 100 / n : 0),
               chg == 0 ? (ones ? " — стоїть «1»" : " — стоїть «0»") : chg < 200 ? " — змін мало (сигнал слабкий чи завада)" : " — сигнал є");
      } else if (ch == 'k') {   // чи живі виводи: 1 мс опитування всіх разом (ядро 0, без переривань)
        // Вхідний буфер виводу читає його стан і тоді, коли вивід — вихід ШІМ чи PIO: видно, чи йде такт з моста.
        // Опитування ~10 МГц — MCLK (12,5 МГц) лише «є/немає»; BCK ~4000 змін за мс, LRCK ~65, дані — як вийде.
        static const uint8_t P[] = { PIN_MCLK, PIN_PCM_BCK, PIN_PCM_BCK + 1, PIN_PCM_DOUT, PIN_LINK_BCK, PIN_LINK_LRCK, PIN_LINK_DATA, PIN_AES };
        static const char *const NM[] = { "MCLK до PCM1808 (GP0)", "BCK до PCM1808 (GP1)", "LRCK до PCM1808 (GP2)", "DOUT з PCM1808 (GP3)",
                                          "BCK від передавача (GP6)", "LRCK від передавача (GP7)", "дані до передавача (GP4)", "AES (GP15)" };
        uint32_t chg[8] = { 0 }, ones[8] = { 0 }, n = 0, t = time_us_32(), save = save_and_disable_interrupts();
        uint32_t prev = gpio_get_all();
        while (time_us_32() - t < 1000) {
          uint32_t v = gpio_get_all(), d = v ^ prev;
          for (int i = 0; i < 8; i++) {
            chg[i] += (d >> P[i]) & 1u;
            ones[i] += (v >> P[i]) & 1u;
          }
          prev = v;
          n++;
        }
        restore_interrupts(save);
        printf("виводи за 1 мс (опитувань %lu):\n", (unsigned long)n);
        for (int i = 0; i < 8; i++)
          printf("  %s: змін %lu, «1» %lu%% часу%s\n", NM[i], (unsigned long)chg[i], (unsigned long)(n ? ones[i] * 100 / n : 0),
                 chg[i] == 0 ? (ones[i] ? " — СТОЇТЬ «1»" : " — СТОЇТЬ «0»") : "");
      }
      if (now - usbT >= 1000000) {
        usbT = now;
        if (lastFault) {   // минулий запуск закінчився падінням — сказати один раз
          unsigned core = lastFault & 1, arg = (lastFault >> 1) & 0x7F;
          if ((lastFault & 0xFFFFFF00u) == FAULT_PANIC) {
            const char *m = (const char *)lastFaultWhat;
            bool ok = lastFaultWhat >= APP_ADDR && lastFaultWhat < APP_ADDR + 0x100000u;
            printf("МИНУЛИЙ ЗАПУСК УПАВ: panic на ядрі %u: «%s» (перший аргумент %u)\n", core, ok ? m : "?", arg);
          } else {
            printf("МИНУЛИЙ ЗАПУСК УПАВ: апаратна помилка на ядрі %u, адреса 0x%08lx\n", core, (unsigned long)lastFaultWhat);
          }
          lastFault = 0;
        }
        uint8_t a = actNow > BP_ACT_SWITCH ? BP_ACT_SWITCH : actNow;
        // пік виходу (з підмішаним шумом ±1 молодшого біта 16-бітного звуку: «рівний нуль» на вході дає тут близько −90 дБ)
        char pk[48];
        if (pkSecL >= 180 && pkSecR >= 180) snprintf(pk, sizeof pk, "тиша (≤ −90 дБ)");
        else snprintf(pk, sizeof pk, "Л −%.1f П −%.1f дБ", pkSecL / 2.0, pkSecR / 2.0);
        pkSecL = pkSecR = 255;
        printf("[%lu с] звучить: %s, пік %s; AES: %s %lu Гц, помилок парності %lu; передавач: %s; запас %d, поправка %+.1f ppm, "
               "провали %u/%u; перескоки %lu (останній на %lu с); ядро 1 зайняте %u‰ (на кадр: вихід %u нс, вхід %u нс)%s\n",
               (unsigned long)((now - uptime0) / 1000000u), actName[a], pk, aesName[aes > 3 ? 0 : aes],
               (unsigned long)rate, (unsigned long)spdif_rx_get_parity_err_count(),
               now - outRdT < 200000 ? "такти є" : "НЕМАЄ тактів", stLevel, stPpm10 / 10.0, stUnder, stOver,
               (unsigned long)resync, (unsigned long)resyncAt, cpu1, nsProd, nsIn,
               pcmShift ? "; PCM1808: дані на біт пізніше — вирівнюю" : "");
      }
    }
    // світлодіод: спалах раз на секунду — працює; два — грає AES; часто — немає тактів передавача
    if (stOutRd != lastOutRd) {
      lastOutRd = stOutRd;
      outRdT = now;
    }
    if (now - ledT >= 50000) {
      ledT = now;
      uint32_t ph = (now / 50000) % 20;
      bool linkOk = now - outRdT < 200000;
      bool on = actNow == BP_ACT_AES ? (ph == 0 || ph == 3) : ph == 0;
      if (!linkOk) on = ph & 1;
      gpio_put(PIN_LED, on);
    }
  }
}
