// Міст набору «Слух»: Raspberry Pi Pico (RP2040) між пультом і передавачем.
//   • вхід AES/EBU (трансформатор або дільник → 74HC04 → GP15) — бібліотека pico_spdif_rx (блок PIO0);
//   • вхід PCM1808: міст дає йому всі такти (GP0 MCLK 12,5 МГц, GP1 BCK, GP2 LRCK), дані — GP3;
//   • перерахунок частоти в 32 кГц тактом передавача (asrc.c) і вихід I2S до передавача: такти від нього
//     (GP6 ← вивід 1, GP7 ← вивід 2), дані GP4 → вивід 40;
//   • команди — UART з виводу 43 передавача (GP5); стан — у молодших 8 бітах слів I2S (hearlink/bridge_proto.h).
//   • з 22.10: журнал подій (переживає перезапуски моста; передавач забирає його у свій журнал на карті), кнопка USR
//     (GP24) — перемкнути вхід, кольоровий світлодіод (WS2812, GP23) — що звучить і рівень (плата YD-RP2040).
// Ядро 0: команди, стан, вибір джерела, журнал, сторож, світлодіоди, кнопка. Ядро 1: увесь звук — і переривання
// прийому AES (бібліотека береже свої дані лише від переривань свого ядра, тож читати її чергу з іншого ядра не можна).
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
#define HUM_FN(f) __not_in_flash_func(f)
#include "hum.h"
#include "bridge_proto.h"
#include "layout.h"
#include "version.h"
#include "usb_audio.h"

#define PIN_MCLK 0
#define PIN_PCM_BCK 1   // LRCK — наступний (GP2): бокові біти PIO
#define PIN_PCM_DOUT 3
#define PIN_LINK_DATA 4
#define PIN_CMD_RX 5
#define PIN_LINK_BCK 6  // LRCK — наступний (GP7)
#define PIN_LINK_LRCK 7
#define PIN_AES 15
#define PIN_RGB 23   // кольоровий світлодіод WS2812 (YD-RP2040; у Raspberry Pi Pico тут перемикач живлення — не шкодить)
#define PIN_USR 24   // кнопка USR до землі (YD-RP2040; у Raspberry Pi Pico — датчик живлення USB, див. panelTick)
#define PIN_LED 25

#define FS_OUT 32000         // вихід до передавача, як було; з 23.10 буває й 48 кГц (BP_OUT48, «Монітор») — fsOut
static volatile uint32_t fsOut = FS_OUT, wantFsOut = FS_OUT;   // нинішня (ядро 1) і задана передавачем (ядро 0)
// PCM1808 — 48 828 Гц (з 23.10; було 32 552): головний такт той самий 12,5 МГц = 256 × fs, такт бітів — 200 МГц / 64.
// Власник: «крайне низкая частота дискретизации, из-за чего звук глухой» — АЦП на 32,5 кГц пропускав до 14,8 кГц
// (0,454 fs — фільтр самої PCM1808); тепер до 22 кГц, і в «Моніторі» (48 кГц) аналог не вужчий за цифру.
#define FS_PCM 48828     // 200 МГц / 4096 = 48 828,1 Гц (решту виправляє регулятор)

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

// ---------------------------------------------------------------- журнал подій (з 22.10)
// Записи — у пам'яті, яку запуск не чистить: журнал переживає перезапуск сторожем, падіння, завантажувач і оновлення
// з передавача (але не вимкнення живлення — тоді починається наново). Місце — фіксоване, вгорі ОЗУ: звичайне
// __uninitialized_ram лягло б одразу за таблицею переривань, а там дані завантажувача, який працює при кожному запуску
// (його ОЗУ — до ~0x20002000, програми — до ~0x2001F500; купа — лише пул будильників SDK, сотні байтів).
// Передавач забирає записи командою BP_LOG у свій журнал на карті; на столі — буква «j» у порту USB.
#define BLOG_N BP_LOG_KEEP
#define BLOG_MAGIC 0x4A4F5552u
typedef struct {
  uint32_t magic, magic2;   // magic2 = ~magic: після вимкнення живлення тут сміття — журнал починається наново
  uint32_t head;            // записів зроблено за весь час (номер наступного)
  uint16_t boot, pad;       // номер поточного запуску
  bp_log_rec_t r[BLOG_N];
} blog_t;
#define BLOG_ADDR 0x2003E000u
#define BLOG ((blog_t *)BLOG_ADDR)
_Static_assert(BLOG_ADDR + sizeof(blog_t) <= 0x20040000u, "журнал моста не вміщається під кінцем ОЗУ");

static void blogPut(uint8_t kind, uint8_t a, uint32_t b) {
  blog_t *L = BLOG;
  bp_log_rec_t *r = &L->r[L->head % BLOG_N];
  r->ms = (uint32_t)(time_us_64() / 1000u);
  r->boot = L->boot;
  r->kind = kind;
  r->a = a;
  r->b = b;
  __dmb();
  L->head = L->head + 1;
}

// Часті події (AES, такти, перескоки, провали, що звучить) — не більше 30 на хвилину, щоб погана лінія не витерла
// журнал за хвилини; пропущені лічаться (BL_DROP). Рідкісні (запуск, падіння, кнопка, завантажувач) — завжди.
static uint32_t blogWinT, blogDropped, blogWinN;
static void blog(uint8_t kind, uint8_t a, uint32_t b, bool limited) {
  if (limited) {
    if (blogWinN >= 30) {
      blogDropped++;
      return;
    }
    blogWinN++;
  }
  blogPut(kind, a, b);
}
static void blogTick(uint32_t now) {
  if (now - blogWinT < 60000000u) return;
  blogWinT = now;
  blogWinN = 0;
  if (blogDropped) blogPut(BL_DROP, 0, blogDropped);
  blogDropped = 0;
}

// Запуск: чи журнал живий (живлення не зникало), новий номер запуску і причина (BL_WHY_*).
static uint8_t blogStart(void) {
  blog_t *L = BLOG;
  bool alive = L->magic == BLOG_MAGIC && L->magic2 == ~BLOG_MAGIC;
  if (!alive) {
    memset(L, 0, sizeof *L);
    L->magic = BLOG_MAGIC;
    L->magic2 = ~BLOG_MAGIC;
  }
  L->boot = (uint16_t)(L->boot + 1);
  if (!L->boot) L->boot = 1;
  // Причина — від сторожа (завантажувач перед програмою чип не перезапускає — вона та сама). Справжнє зависання — лише
  // коли спрацював сторож, якого вмикала ця програма (watchdog_enable лишає мітку в scratch[4]): ПЗУ (picotool, режим
  // запису USB) перезапускає тим самим сторожем, але без мітки (22.10 запис picotool записався як «зависав»).
  // Нуль — апаратний запуск: живлення (журнал тоді порожній) чи кнопка RST (журнал живий).
  return watchdog_enable_caused_reboot() ? BL_WHY_WATCHDOG : watchdog_hw->reason ? BL_WHY_ASKED
         : alive ? BL_WHY_PIN : BL_WHY_POWER;
}

// ---------------------------------------------------------------- спільне для ядер
// Скільки мкс минуло від мітки t. Знаково: мітка «з майбутнього» (її поставило переривання чи друге ядро вже після того,
// як тут узяли now, або «now | 1») дає ≤ 0, а не «дуже давно». 22.10 беззнакове «now − мітка пакета USB» раз у раз
// давало «комп'ютер не грає» — вибір входу смикався USB ↔ тиша, і ядро 1 без кінця перераховувало фільтр.
static inline int32_t usSince(uint32_t t, uint32_t now) { return (int32_t)(now - t); }
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
// Віднімання гулу радіо передавача з аналогового входу (hum.h): шаблон веде ядро 1 на виході перерахунку, лише коли
// звучить PCM1808. Вмикає передавач (BP_PCM_HUM; без нього, як у старого передавача, — вимкнено), буква r у порту USB
// перемикає навпаки (на столі, до перезапуску). Шаблон стирає саме ядро 1: перерахунок з початку (новий вхід, частота
// виходу) і збій такту виходу (linkRecover) зсувають фазу гулу відносно кадрів.
static hum_t hum;
static volatile bool humTx, humLocal;

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
static volatile uint32_t stResync;   // скільки разів вихід ловив такт передавача наново

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
  sm_config_set_clkdiv_int_frac(&c, 16, 0);   // 200 МГц / 16 / 4 такти = BCK 3,125 МГц (64 × 48 828 Гц)
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

// Прийом AES (бібліотека — на перериваннях ядра 1, тож пускає й зупиняє його лише ядро 1). Канали DMA бібліотека займає
// САМА (dma_channel_claim): тут лише знаходимо вільні й одразу відпускаємо — інакше її claim падає в panic (так і було
// з першою версією: ядро 1 ставало, сторож перезапускав Pico).
static alarm_pool_t *aesAlarms;
static void aesStart(void) {
  int ch0 = dma_claim_unused_channel(true), ch1 = dma_claim_unused_channel(true);
  dma_channel_unclaim((uint)ch0);
  dma_channel_unclaim((uint)ch1);
  spdif_rx_config_t sc = {
      .data_pin = PIN_AES,
      .pio_sm = 0,
      .dma_channel0 = (uint)ch0,
      .dma_channel1 = (uint)ch1,
      .alarm_pool = aesAlarms,
      .flags = SPDIF_RX_FLAG_CALLBACKS | SPDIF_RX_FLAG_CHECK_PARITY,
  };
  spdif_rx_start(&sc);
}

static void audioSetup(void) {
  // MCLK 12,5 МГц — ШІМ (зріз 0, канал A на GP0): 200 МГц / 16. Той самий такт, що й у PIO (BCK = 200 МГц / 64), —
  // частоти кратні (×4), тож для PCM1808 усі три такти синхронні (з 23.10: 256 fs, 48,8 кГц; до того 384 fs, 32,5 кГц).
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
  // AES — переривання цього ядра, свій пул будильників теж на цьому ядрі (див. aesStart)
  aesAlarms = alarm_pool_create_with_unused_hardware_alarm(16);
  aesStart();
}

static inline void __not_in_flash_func(outPoll)(void) {
  uint32_t idx = (((uint32_t)dma_hw->ch[dmaOut].read_addr - (uint32_t)outRing) / 4u) & (OUT_WORDS - 1);
  outRd += (idx - outRdIdx) & (OUT_WORDS - 1);
  outRdIdx = idx;
}

// Вирівнювання PCM1808 (вимір на платі 21.10, запис слів через передавач): PCM1808 видає дані на такт BCK ПІЗНІШЕ,
// ніж міст їх читає (програма pcm_lj — «лівовирівняна»): біт 31 слова — «хвіст» попереднього (0), D23…D0 — біти 30…7,
// і слабкий мінус (−1480) читався як майже повна шкала плюс (0x7FFD1C). Причина — формат I2S: у наборі FMT на землі
// (власник 23.10: «fmy, md0, md1 — подключены на gnd»), а не на 3,3 В, як я вважав. Признак: біт 7 (у рівному слові
// там уже нулі доповнення) часто 1, а біт 31 — ніколи; тоді слово зсувається на біт ліворуч. Рахується на ходу (по 4096
// ненульових слів), рішення — лише за явних ознак, щоб тиша чи постійний зсув нуля його не перемикали.
// В I2S лівий канал іде при LRCK = 0, тож у пів-кадрі LRCK = 1 (перше слово пари) — ПРАВИЙ: зі зсувом міст міняє
// канали місцями. До 23.10 цього не було — аналог ішов з переставленими Л і П (одночасний знімок: слово «Л» PCM1808
// збігається з правим AES, когерентність 0,93–0,98).
static uint32_t pcmN, pcmB7, pcmB31;
static volatile uint8_t pcmShift = 1;   // I2S (FMT на землі) — так у наборі; перевіряється на ходу
// Симетричний аналоговий вхід (BP_PCM_DIFF, bridge_proto.h): у перерахунок іде (Л − П) / 2 в обидва канали. Ставить
// передавач (BP_SET) або буква d у порту USB (на столі, до перезапуску).
static volatile bool pcmDiffTx, pcmDiffLocal;
// Знімок обох входів одночасно (з 23.10, буква c у порту USB): сирі 24-бітні відліки AES і PCM1808 (PCM — до
// симетричного режиму) по CAP_N кадрів з одного й того самого моменту — порівняти аналог із цифрою того самого звуку.
#define CAP_N 1024
static int32_t capA[CAP_N][2], capP[CAP_N][2];
static volatile uint32_t capAn, capPn;
static volatile bool capOn;

// 32-бітне слово зі звуком у старших 24 бітах → 24 біти зі знаком, без округлень: перерахунок частоти бере всі 24
// (asrc.c). До 22.10 тут ставало 16 біт: спершу відкиданням молодших (крихітний сигнал пульта біля нуля ставав
// стрибками 0 ↔ −1, −96 дБ — власник: «на цифре откуда шим»), потім округленням — тиша стала тишею, але тихий звук
// (пульт −87 дБ) ішов лише кількома ступенями 16-бітної шкали: шипіння й тріск після підсилень передавача (+15 дБ) і
// приймача (+24 дБ) (власник: «при низком уровне звука на цифре слышно фоновое шипение и немного треска»).
static inline int32_t __not_in_flash_func(to24)(uint32_t w) {
  return (int32_t)w >> 8;
}

// Нові кадри PCM1808 → перерахунок (feed) або в нікуди. Повертає, скільки кадрів забрано.
static uint32_t __not_in_flash_func(pcmPull)(bool feed) {
  uint32_t idx = ((((uint32_t)dma_hw->ch[dmaPcm].write_addr - (uint32_t)pcmRing) / 4u) & (PCM_WORDS - 1)) & ~1u;
  int32_t tmp[2 * 64];
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
        tmp[2 * n + (k ^ pcmShift)] = to24(pcmShift ? w << 1 : w);   // I2S: перше слово пари — правий
      }
      if (capOn && capPn < CAP_N) {
        capP[capPn][0] = tmp[2 * n];
        capP[capPn][1] = tmp[2 * n + 1];
        capPn = capPn + 1;
      }
      if (pcmDiffTx || pcmDiffLocal) {   // симетричний вхід: різниця — сигнал, однакове в обох проводах (гул) — геть
        int32_t m = (tmp[2 * n] - tmp[2 * n + 1]) >> 1;
        tmp[2 * n] = tmp[2 * n + 1] = m;
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
  int32_t tmp[2 * 64];
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
        tmp[2 * n] = to24(p[i] << 4);
        tmp[2 * n + 1] = to24(p[i + 1] << 4);
        if (capOn && capAn < CAP_N) {
          capA[capAn][0] = tmp[2 * n];
          capA[capAn][1] = tmp[2 * n + 1];
          capAn = capAn + 1;
        }
        i += 2;
        n++;
      }
      if (feed && n) asrc_write(&A, tmp, n);
      total += n;
    }
  }
}

// Нові кадри з комп'ютера (USB, 48 кГц): кільце пише задача USB на ядрі 0 (usb_audio.c) пачками по ~48 кадрів раз
// на 1 мс. Забрати все, що є; відстали на ціле кільце (ядро 1 стояло) — пропустити зайве.
static uint32_t usbaRd;
static volatile uint32_t usbIdleN;   // скільки разів USB «замовкав» понад 3 мс (видано тишу)
static uint32_t __not_in_flash_func(usbPull)(bool feed) {
  uint32_t wr = usbaWr, total = 0;
  int32_t tmp[2 * 64];
  __dmb();
  if (wr - usbaRd > USBA_RING) usbaRd = wr - USBA_RING / 2;
  while (usbaRd != wr) {
    uint32_t i = usbaRd & (USBA_RING - 1), n = wr - usbaRd;
    if (n > USBA_RING - i) n = USBA_RING - i;
    if (n > 64) n = 64;
    if (feed) {   // комп'ютер шле 16 біт — у шкалу 24 біт перерахунку
      for (uint32_t k = 0; k < n; k++) {
        tmp[2 * k] = (int32_t)usbaRing[i + k][0] << 8;
        tmp[2 * k + 1] = (int32_t)usbaRing[i + k][1] << 8;
      }
      asrc_write(&A, tmp, n);
    }
    usbaRd += n;
    total += n;
  }
  return total;
}

// Синус 1 кГц −12 дБ (перевірочний): 32 відліки на період при 32 кГц, з повною 24-бітною точністю. Не «16 біт << 8»: тоді
// молодші біти слова — нулі, і передавач 3.29 вирішував, що дані PCM1808 зсунуті на біт (власник: «тон с постоянными
// щелчками как трещотка»).
static int32_t tone48[48];   // той самий тон 1 кГц −12 дБ для виходу 48 кГц (рахується при запуску, 24 біти)
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
  for (int i = 0; i < 48; i++) tone48[i] = (int32_t)lrintf(2097152.0f * sinf(6.2831853f * (float)i / 48.0f));
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
  const uint32_t fs = fsOut, RUN = fs * 10, GAP = fs;
  if (sweepN == 0) sweepInc = 20.0f / (float)fs * 4294967296.0f;
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
  if (fsOut == 48000) {
    uint32_t i = (*ph)++ % 48;
    return tone48[i];
  }
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
  hum_reset(&hum, fsOut);   // кадри виходу зсунулись відносно пакетів передавача
}

// Увесь «гарячий» шлях ядра 1 — з ОЗП (__not_in_flash_func): з флешу через спільний з ядром 0 і USB кеш на 16 КБ
// кадр коштував 17,5 + 6,2 мкс із 31,25 (вимір на платі 21.10).
static void __not_in_flash_func(core1Main)(void) {
  audioSetup();
  uint32_t haveGen = 0, steerN = 0, tonePh = 0, busy = 0, winT = time_us_32();
  bool usbWasIdle = false, humOn = false;
  uint32_t prodUs = 0, prodFr = 0, inUs = 0, inFr = 0;
  bool restartPending = false;
  uint32_t pullT = 0;
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
      got = pcmPull(feed && act == BP_ACT_PCM) + aesPull(feed && act == BP_ACT_AES) + usbPull(feed && act == BP_ACT_USB);
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
    // Комп'ютер перестав слати пакети (кінець відтворення, хост пригальмував) — тиша, а не «провали» перерахунку
    // (22.10 кінець тону з Mac дав 361 провал, поки macOS не закрила потік); пакети пішли знову — запас наново.
    bool usbIdle = act == BP_ACT_USB && usSince(usbaPktT, time_us_32()) > 3000;
    if (usbIdle && !usbWasIdle) usbIdleN = usbIdleN + 1;
    if (usbIdle) usbWasIdle = true;
    else if (usbWasIdle) {
      usbWasIdle = false;
      restartPending = true;
    }
    while ((int32_t)(outWr - outRd) < lead) {
      int32_t l = 0, r = 0;
      bool snd = (act == BP_ACT_PCM || act == BP_ACT_AES || act == BP_ACT_USB) && !usbIdle;
      if (cs != CS_BUILD && snd) {
        // Перший кадр після нового фільтра: черга тиші, накопичена за розрахунок ваг (до 4 мс), уже пішла — лише тепер
        // ставимо точку читання (інакше запас підскакував до 47 кадрів замість 16, і регулятор зганяв його секунди).
        if (restartPending) {
          asrc_restart(&A);
          restartPending = false;
          hum_reset(&hum, fsOut);
        }
        asrc_read(&A, &l, &r);
        bool hOn = act == BP_ACT_PCM && humTx != humLocal;
        if (hOn != humOn) {
          hum_reset(&hum, fsOut);
          humOn = hOn;
        }
        if (hOn) hum_step(&hum, &l, &r);
        if (++steerN == 32) {
          steerN = 0;
          // AES і PCM1808 читаються по кадру — запас рівний, «кадрів у дорозі» нема. USB — пачками раз на 1 мс: скільки
          // кадрів «уже йде», але ще не прийшло, — з часу від останнього пакета (див. asrc_steer).
          float frac = 0;
          if (act == BP_ACT_USB) {
            int32_t dt = usSince(usbaPktT, time_us_32());
            frac = dt <= 0 ? 0.0f : dt >= 1000 ? (float)(USBA_RATE / 1000) : (float)dt * (USBA_RATE / 1000) / 1000.0f;
          }
          asrc_steer(&A, 32, frac);
          stLevel = (int16_t)asrc_level(&A);
          stPpm10 = (int16_t)(A.ppm * 10.0f);
          stUnder = (uint16_t)A.under;
          stOver = (uint16_t)A.over;
        }
      } else if (cs != CS_BUILD && act == BP_ACT_TONE) {
        l = r = genNext(genKind, &tonePh);   // однаково в обидва канали: міряємо тембр (середину), не стерео
      }
      // Дизера (шуму ±1 молодшого біта 16 біт) більше немає (22.10): тиша мусить бути тишею. Його додали 21.10 проти
      // «підпорогових» чисел у фільтрах еквалайзера передавача на точних нулях AES (губились кадри I2S), але після
      // підсилення входу передавача (+15 дБ) і приймача (+24 дБ) він чувся в навушниках шипінням на всіх входах
      // (власник: «если на цифре нет звука, то шипению неоткуда взяться»). Підпорогові числа тепер прибирають самі
      // фільтри (hearlink/eq.h).
      // плавно: уходимо до нуля перед зміною, входимо після
      if (cs == CS_FADE) gain = gain > gStep ? gain - gStep : 0;
      else if (cs == CS_PLAY) gain = gain < 32768 - gStep ? gain + gStep : 32768;
      else gain = 0;
      if (gain != 32768) {   // лише під час входу/уходу звуку (без 64-бітного множення: точність тут не важлива)
        l = (l >> 8) * gain >> 7;
        r = (r >> 8) * gain >> 7;
      }
      // У 24 біти: «l << 8» нижче за межею міняв би знак слова — гучний щиглик замість простого обмеження (22.10:
      // так бувало з дизером на повній шкалі; перерахунок свій вихід обмежує, тут — на будь-який випадок).
      l = l > 8388607 ? 8388607 : l < -8388608 ? -8388608 : l;
      r = r > 8388607 ? 8388607 : r < -8388608 ? -8388608 : r;
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
      if (usbrOn && fsOut == FS_OUT) {   // запис на комп'ютер описано як 32 кГц — на виході 48 кГц не пишемо   // запис на комп'ютер (USB): те саме, що пішло передавачу, 16 біт (з округленням); забирає задача USB
        uint32_t w = usbrWr;
        int32_t ql = (l + 128) >> 8, qr = (r + 128) >> 8;
        usbrRing[w & (USBR_RING - 1)][0] = (int16_t)(ql > 32767 ? 32767 : ql);
        usbrRing[w & (USBR_RING - 1)][1] = (int16_t)(qr > 32767 ? 32767 : qr);
        __dmb();
        usbrWr = w + 1;
      }
      made++;
      if (cs == CS_FADE && gain == 0) {   // тиша — можна міняти
        haveGen = wantGen;
        fsOut = wantFsOut;   // і частоту виходу (32 / 48 кГц) — теж лише в тиші
        uint8_t want = wantAct;
        uint32_t rate = wantRate;
        act = want;
        actNow = BP_ACT_SWITCH;
        if (act == BP_ACT_PCM || act == BP_ACT_AES || act == BP_ACT_USB) {
          // Затримка моста: запас + пів фільтра + вихід наперед. AES 48 кГц: 24 + 32 кадри (0,5 + 0,67 мс) + 0,4 мс;
          // PCM1808: 16 + 16 (0,5 + 0,5 мс) + 0,4 мс. Перевірено на Mac (bridge/test/asrc_test.c): 98 і 97 дБ.
          // USB: пачка 48 кадрів (1 мс) — запас 72 (пачка і пів пачки поля): 1,5 + 0,67 мс + 0,4 мс.
          // Вихід 48 кГц («Монітор», з 23.10): фільтр — 32 відводи (зріз 22 кГц; на Mac 48 → 48: 89–91 дБ), бо відліків
          // виходу в півтора раза більше, а частоти входу й виходу близькі — довгий фільтр не потрібен.
          uint32_t fo = fsOut;
          int tAes = fo == 48000 ? 32 : 64;
          if (act == BP_ACT_AES) asrc_begin(&A, rate, fo, tAes, 24, 8);
          else if (act == BP_ACT_USB) asrc_begin(&A, USBA_RATE, fo, tAes, 72, USBA_RATE / 1000);
          else asrc_begin(&A, FS_PCM, fo, fo == 48000 ? 32 : 64, 16, 8);   // 48,8 → 32 кГц — як AES 48 → 32
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
      usbPull(false);
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
static uint32_t logSend, logSendEnd;   // записи журналу для передавача: від logSend до logSendEnd (ставить BP_LOG)
// Вхід, обраний буквою в порту USB (a, p, u, m, w), тримається хвилину: передавач щосекунди нагадує свій (BP_SET) і
// інакше перебивав би його одразу — на столі так можна послухати будь-який вхід при підключеному передавачі.
static uint32_t localSrcT;   // коли обрали з порту (мкс; 0 — не обирали)
static bool localSrcHeld(void) {
  if (localSrcT && usSince(localSrcT, time_us_32()) >= 60000000) localSrcT = 0;
  return localSrcT != 0;
}
// Розбір команд: буфер на 4 КБ — НЕ на стеку (стек ядра 0 — 2 КБ: перша версія мосту падала саме від цього — буфер
// вилазив за стек і псував пам'ять ядра 1, сторож перезапускав Pico, а після трьох падінь завантажувач чекав команд).
static bp_rx_t rx;

static uint8_t aesPin = 0;      // підтяжка виводу 15: 0 — немає, 1 — угору, 2 — униз (так після запуску RP2040)
static bool aesSchmitt = true;

static void enterLoader(uint8_t why) {   // why — для журналу: 0 команда передавача, 1 «розрив» лінії, 2 USB
  blog(BL_LOADER, why, 0, false);
  watchdog_hw->scratch[0] = STAY_MAGIC;
  watchdog_reboot(0, 0, 10);
  for (;;) tight_loop_contents();
}

// Порт USB (лише на столі: міст підключено до комп'ютера) — рядок стану раз на секунду і команди однією буквою.
// У ділі USB не підключено, і цей код нічого не робить.
static const char *const actName[] = {"нічого", "AES", "PCM1808", "тиша", "перевірочний сигнал", "перемикання", "USB (комп'ютер)"};
static const char *const aesName[] = {"немає", "ловлю", "є", "частота не та"};

static void usbHelp(void) {
  printf("міст %d.%02d: a — AES, p — PCM1808, w — USB (комп'ютер), u — авто, m — тиша (вхід з порту тримається хвилину), "
         "t — перевірочний сигнал по колу, "
         "l — у завантажувач, b — у режим запису USB (без диска; писати picotool), g — що на вході AES, "
         "k — чи живі виводи (такти й дані PCM1808, такти передавача, AES), j — журнал моста, "
         "d — аналоговий вхід симетричний (різниця каналів, моно) / стерео, "
         "r — гул радіо з аналогового входу: прибирати / ні (навпаки до передавача), "
         "c — знімок обох входів\n", BRIDGE_VER_MAJOR, BRIDGE_VER_MINOR);
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
        uint8_t src = d[0] <= BP_SRC_USB ? d[0] : BP_SRC_AUTO;
        // d[1]: біт 0 (формат PCM1808) не читається — міст розпізнає I2S сам (pcmShift); біт 1 — симетричний аналоговий
        // вхід (BP_PCM_DIFF); біт 3 — прибирати гул радіо з аналогового входу (BP_PCM_HUM)
        pcmDiffTx = (d[1] & BP_PCM_DIFF) != 0;
        humTx = (d[1] & BP_PCM_HUM) != 0;
        wantFsOut = (d[1] & BP_OUT48) ? 48000 : FS_OUT;
        uint8_t gen = d[2] <= BP_GEN_SWEEP ? d[2] : BP_GEN_TONE;   // 0 — немає, 1 — тон, 2 — рожевий шум, 3 — розгортка
        if (localSrcHeld()) src = reqSrc;   // хвилину після вибору з порту USB — його
        if (src != reqSrc || gen != reqTone) blog(BL_SET, src, gen, true);
        reqSrc = src;
        reqTone = gen;
        if (reqTone) genKind = reqTone;
        lastSeq = d[3];
      }
      break;
    case BP_ENTER_LOADER:
      if (n >= 4 && d[0] == 'L' && d[1] == 'O' && d[2] == 'A' && d[3] == 'D') enterLoader(0);
      break;
    case BP_LOG:   // записи журналу від номера from (до count штук) — кадрами BP_ST_LOG, скільки влізе (головний цикл)
      if (n >= 4) {
        uint32_t from = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
        uint32_t head = BLOG->head, count = n >= 6 ? (uint32_t)(d[4] | (d[5] << 8)) : BLOG_N;
        if (from > head) from = head;
        if (head - from > BLOG_N) from = head - BLOG_N;
        logSend = from;
        logSendEnd = from + count;
      }
      break;
  }
}

static uint8_t peakDb2(int32_t p) {   // 24-бітний пік → −дБ ×2 (255 — тиша)
  if (p <= 0) return 255;
  float db = -20.0f * log10f((float)p / 8388608.0f) * 2.0f;
  return db < 0 ? 0 : db > 254 ? 254 : (uint8_t)db;
}

// ---------------------------------------------------------------- панель на платі (з 22.10; YD-RP2040: GP23, GP24)
// Кнопка USR — перемкнути вхід (цифровий ↔ аналоговий). Міст сам не перемикається: просить передавача (btnSeq/btnSrc
// у стані), а той робить те саме, що кнопка входу на його екрані, — і на екрані це видно. Клацання — на відпусканні,
// натиск 30 мс – 1,5 с, і лише після того, як кнопку бачили відпущеною секунду: у Raspberry Pi Pico на GP24 датчик
// живлення USB, і відключення USB (вивід надовго в «0») за кнопку так не сприймається.
// Кольоровий світлодіод: колір — що звучить, яскравість — рівень:
//   зелений — цифровий вхід (AES); синій — аналоговий (PCM1808); фіолетовий — USB (комп'ютер); жовтий — перевірочний
//   сигнал; тьмяний білий — тиша; помаранчевий блимає — обрано цифровий вхід, а сигналу AES немає; фіолетовий
//   блимає — обрано USB, а комп'ютер не грає; червоний блимає — немає тактів передавача
//   (вимкнений, перезапускається, обрив); червоний спалах — перевантаження (пік у межах 0,5 дБ від повної шкали);
//   білий спалах — натиснули USR; три червоні спалахи — передавач за 2 с вхід не змінив (не та версія, не той вхід).
static uint8_t btnSeq, btnSrc;
static uint8_t panelLvl = 255;     // пік останнього стану, −дБ ×2 (255 — тиша)
static uint32_t panelClipT;        // коли був останній перевантажений проміжок (мкс; 0 — давно)
static uint32_t clipMin;           // перевантажених проміжків за поточну хвилину (для журналу)

// WS2812: біт 1,25 мкс, «1» — ~0,7 мкс високого рівня, «0» — ~0,35 мкс; кольори в порядку G, R, B, старшим бітом
// уперед. Кадр (24 біти, 30 мкс) — з ОЗУ і з вимкненими перериваннями ядра 0. PIO для цього немає: PIO0 бібліотека AES
// чистить повністю при кожному новому захопленні сигналу, у PIO1 вільна одна команда. Ядро 1 (звук) не зупиняється.
static void __no_inline_not_in_flash_func(rgbPut)(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t mhz = SYS_CLK_HZ / 1000000u, T0 = mhz * 35 / 100, T1 = mhz * 70 / 100, TB = mhz * 125 / 100;
  uint32_t v = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  uint32_t save = save_and_disable_interrupts();
  for (int i = 23; i >= 0; i--) {
    uint32_t h = (v >> i) & 1u ? T1 : T0;
    sio_hw->gpio_set = 1u << PIN_RGB;
    busy_wait_at_least_cycles(h);   // вбудовується (цикл з трьох тактів) — уся функція лишається в ОЗУ
    sio_hw->gpio_clr = 1u << PIN_RGB;
    busy_wait_at_least_cycles(TB - h);
  }
  restore_interrupts(save);
}

static void panelInit(void) {
  gpio_init(PIN_USR);
  gpio_set_dir(PIN_USR, GPIO_IN);
  gpio_pull_up(PIN_USR);
  gpio_init(PIN_RGB);
  gpio_put(PIN_RGB, 0);
  gpio_set_dir(PIN_RGB, GPIO_OUT);
  rgbPut(0, 0, 0);
}

static void panelTick(uint32_t now, bool linkOk, uint8_t aes, bool usbPlaying) {
  static uint32_t sampT, rawT, downT, flashT, waitT, rgbT, errT;
  static bool raw = true, down, armed, waiting;
  static uint8_t errN, lastR = 255, lastG = 255, lastB = 255;
  static float lvl;
  // кнопка — раз на 5 мс; стан приймається, коли вивід 30 мс не змінювався
  if (now - sampT >= 5000) {
    sampT = now;
    bool v = gpio_get(PIN_USR);   // 1 — відпущена
    if (v != raw) {
      raw = v;
      rawT = now;
    }
    if (now - rawT >= 30000 && down == raw) {
      down = !raw;
      if (down) downT = now;
      else if (armed && now - downT <= 1500000u) {   // клацання: попросити передавача інший вхід
        bool onAes = reqSrc == BP_SRC_AES || (reqSrc == BP_SRC_AUTO && actNow == BP_ACT_AES);
        btnSrc = onAes ? BP_SRC_PCM : BP_SRC_AES;
        btnSeq++;
        blog(BL_BTN, btnSrc, 0, false);
        flashT = now | 1;
        waitT = now;
        waiting = true;
      }
    }
    if (!down && raw && now - rawT >= 1000000u) armed = true;
  }
  if (waiting && reqSrc == btnSrc) waiting = false;
  if (waiting && now - waitT > 2000000u) {   // передавач вхід не змінив
    waiting = false;
    errN = 6;
    errT = now;
  }
  // світлодіод — раз на 20 мс (і лише коли колір змінився)
  if (now - rgbT < 20000) return;
  rgbT = now;
  float want = panelLvl >= 120 ? 0.0f : 1.0f - panelLvl / 120.0f;   // 0 дБ → 1, −60 дБ і тихіше → 0
  if (want > lvl) lvl = want;
  else lvl = lvl > 0.02f ? lvl - 0.02f : 0.0f;   // спад — секунду, як у стрілочного індикатора
  float k = 0.15f + 0.85f * lvl;
  uint32_t ms = now / 1000u;
  uint8_t act = actNow, r = 0, g = 0, b = 0;
  if (flashT && usSince(flashT, now) >= 150000) flashT = 0;
  if (panelClipT && usSince(panelClipT, now) >= 250000) panelClipT = 0;
  if (flashT) r = g = b = 28;
  else if (errN) {
    if (now - errT >= 150000u) {
      errT = now;
      errN--;
    }
    r = errN & 1 ? 40 : 0;
  } else if (!linkOk) r = (ms / 250) & 1 ? 40 : 0;
  else if (panelClipT) r = 48;
  else if (act == BP_ACT_TONE) {
    r = (uint8_t)(36 * k);
    g = (uint8_t)(24 * k);
  } else if (reqSrc == BP_SRC_AES && aes != BP_AES_OK) {
    if ((ms / 500) & 1) {
      r = 40;
      g = 12;
    }
  } else if (reqSrc == BP_SRC_USB && !usbPlaying) {
    if ((ms / 500) & 1) {
      r = 24;
      b = 40;
    }
  } else if (act == BP_ACT_AES) g = (uint8_t)(40 * k);
  else if (act == BP_ACT_PCM) b = (uint8_t)(44 * k);
  else if (act == BP_ACT_USB) {
    r = (uint8_t)(24 * k);
    b = (uint8_t)(40 * k);
  } else r = g = b = 4;
  if (r != lastR || g != lastG || b != lastB) {
    lastR = r;
    lastG = g;
    lastB = b;
    rgbPut(r, g, b);
  }
}

int main(void) {
  uint32_t s3 = watchdog_hw->scratch[3];
  if ((s3 & 0xFFFFFF00u) == FAULT_PANIC || (s3 & 0xFFFFFF00u) == FAULT_HARD) {
    lastFault = s3;
    lastFaultWhat = watchdog_hw->scratch[2];
  }
  watchdog_hw->scratch[2] = 0;
  watchdog_hw->scratch[3] = 0;
  blog(BL_BOOT, blogStart(), (BRIDGE_VER_MAJOR << 8) | BRIDGE_VER_MINOR, false);
  if (lastFault)
    blog(BL_FAULT, (uint8_t)(((lastFault & 0xFFFFFF00u) == FAULT_HARD ? 1u : 0u) | ((lastFault & 1u) << 7)), lastFaultWhat, false);
  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, GPIO_OUT);
  panelInit();
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
  uint32_t resync = 0, resyncAt = 0, uptime0 = time_us_32(), lastOutRd = 0, outRdT = uptime0 - 1000000u, usbT = 0;
  uint32_t aesSeenT = 0, clipT = uptime0;
  uint16_t underSeen = 0, overSeen = 0;
  uint8_t pkSecL = 255, pkSecR = 255;   // найбільший пік виходу за секунду рядка стану (−дБ ×2; 255 — тиша)
  uint8_t aesSeen = 0xFF, aesLogged = 0xFF, actLogged = 0xFF;   // що вже записано в журнал (0xFF — ще нічого)
  uint8_t usbLogged = BP_USB_NONE;
  bool usbRecLogged = false;
  int8_t clkLogged = -1;
  bool lineWasHigh = false, crashCleared = false;
  logSend = logSendEnd = BLOG->head;
  // CRC-32 своєї програми — з відомостей, які записав завантажувач (або перша запис з комп'ютера, bridge-full.uf2).
  // Пробна збірка в ОЗУ (bridge_ram, picotool load -x) флеш не читає — називає CRC програми, що лежить у флеші
  // (BRIDGE_RAM_CRC): інакше передавач вирішив би, що міст «не той», і переписав би його своїм образом.
#ifdef BRIDGE_RAM_CRC
  uint32_t myCrc = BRIDGE_RAM_CRC;
#else
  const app_info_t *info = (const app_info_t *)(FLASH_BASE_ADDR + INFO_OFF);
  uint32_t myCrc = info->magic == INFO_MAGIC && info->magic2 == ~INFO_MAGIC ? info->crc32 : 0;
#endif
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
    // «Розрив» — у завантажувач (запасний шлях: передавач міг би так оновити міст, навіть якщо ця програма вже не
    // розуміє команд; нинішній передавач його не вживає — шле команду LOAD). Розрив — це імпульс: лінія була в спокої
    // «1», упала в нуль на 80 мс … 1 с і ПІДНЯЛАСЯ знову, і весь цей час ішли такти передавача (він живий).
    // До 22.10 вистачало «нуля ≥ 80 мс»: при скиданні чи вимиканні передавача його вивід теж падає в нуль, і міст
    // сам ішов у завантажувач і чекав там, доки передавач після запуску не спитає — 15–75 с без звуку (власник:
    // «если сбросить передатчик, то мост не работает»). Тепер без тактів лінія нічого не означає, а нуль довше 1 с
    // (лінія залипла, передавача немає) — теж.
    bool cmdHi = gpio_get(PIN_CMD_RX), clkNow = now - outRdT < 20000;
    if (!clkNow) {
      lineWasHigh = cmdHi;
      lowSince = 0;
    } else if (cmdHi) {
      if (lowSince) {
        int32_t lowUs = usSince(lowSince, now);
        if (lowUs > 80000 && lowUs < 1000000) enterLoader(1);
      }
      lineWasHigh = true;
      lowSince = 0;
    } else if (lineWasHigh && !lowSince) lowSince = now | 1;
    // перескоки синхронізації виходу (PIO IRQ 0)
    if (stResync != resync) {
      resync = stResync;
      resyncAt = (uint32_t)(time_us_64() / 1000000u);
      blog(BL_RESYNC, 0, resync, true);
    }
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
    bool aesSteady = aesOkSince && usSince(aesOkSince, now) > 300000;   // «авто» бере AES, коли він тримається 0,3 с
    uint8_t want;
    // USB «грає», поки потік відкрито і пакети йдуть (macOS закриває потік не одразу після кінця відтворення)
    bool usbPlaying = usbaOn && usSince(usbaPktT, now) < 50000;
    if (reqTone) want = BP_ACT_TONE;
    else if (reqSrc == BP_SRC_PCM) want = BP_ACT_PCM;
    else if (reqSrc == BP_SRC_MUTE) want = BP_ACT_MUTE;
    else if (reqSrc == BP_SRC_AES) want = aes == BP_AES_OK ? BP_ACT_AES : BP_ACT_MUTE;
    else if (reqSrc == BP_SRC_USB) want = usbPlaying ? BP_ACT_USB : BP_ACT_MUTE;
    else want = aesSteady ? BP_ACT_AES : BP_ACT_PCM;
    if (want != wantAct || reqFmt != wantFmt || (want == BP_ACT_AES && rate != wantRate) || wantFsOut != fsOut) {
      wantAct = want;
      wantFmt = reqFmt;
      wantRate = rate;
      __dmb();
      wantGen = wantGen + 1;
    }
    // такти передавача: слова з виходу забираються (лічильник ядра 1 росте)
    if (stOutRd != lastOutRd) {
      lastOutRd = stOutRd;
      outRdT = now;
    }
    bool linkOk = now - outRdT < 200000;
    // журнал. AES: втрата і поява — одразу, решта змін — коли стан тримається секунду (без сигналу бібліотека
    // перебирає «немає» ↔ «ловлю»). Далі — що звучить, такти передавача, провали перерахунку, перевантаження.
    if (aes != aesSeen) {
      aesSeen = aes;
      aesSeenT = now;
    }
    if (aes != aesLogged && ((aes == BP_AES_OK) != (aesLogged == BP_AES_OK) || now - aesSeenT > 1000000u)) {
      blog(BL_AES, aes, aes == BP_AES_OK ? rate : aes == BP_AES_RATE ? (uint32_t)spdif_rx_get_samp_freq() : 0, true);
      aesLogged = aes;
    }
    uint8_t actN = actNow;
    if (actN != BP_ACT_SWITCH && actN != actLogged) {
      blog(BL_ACT, actN, 0, true);
      actLogged = actN;
    }
    if ((int8_t)linkOk != clkLogged && now - uptime0 > 500000u) {   // перші 0,5 с — такти ще не встигли з'явитися
      blog(BL_CLK, linkOk ? 1 : clkLogged < 0 ? 2 : 0, 0, true);
      clkLogged = (int8_t)linkOk;
    }
    uint16_t un = stUnder, ov = stOver;   // лічильники перерахунку; з новим входом починаються з нуля
    if (un != underSeen) {
      if (un > underSeen) blog(BL_UNDER, 0, un, true);
      underSeen = un;
    }
    if (ov != overSeen) {
      if (ov > overSeen) blog(BL_OVER, 0, ov, true);
      overSeen = ov;
    }
    uint8_t usbNow = usbPlaying ? BP_USB_PLAY : usbaHost ? BP_USB_HOST : BP_USB_NONE;
    if (usbNow != usbLogged) {
      blog(BL_USB, usbNow, 0, true);
      usbLogged = usbNow;
    }
    if (usbrOn != usbRecLogged) {
      blog(BL_USBREC, usbrOn, 0, true);
      usbRecLogged = usbrOn;
    }
    if (now - clipT >= 60000000u) {
      clipT = now;
      if (clipMin) blog(BL_CLIP, 0, clipMin, false);
      clipMin = 0;
    }
    blogTick(now);
    panelTick(now, linkOk, aes, usbPlaying);
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
      panelLvl = st.peakL < st.peakR ? st.peakL : st.peakR;
      if (panelLvl <= 1 && (actNow == BP_ACT_AES || actNow == BP_ACT_PCM)) {   // вхід у межах 0,5 дБ від повної шкали
        panelClipT = now | 1;
        clipMin++;
      }
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
      st.uptime = (uint32_t)(time_us_64() / 1000000u);   // не (now − початок): 32-бітні мікросекунди — лише 71 хвилина
      st.appCrc = myCrc;
      st.logHead = BLOG->head;
      st.boot = BLOG->boot;
      st.btnSeq = btnSeq;
      st.btnSrc = btnSrc;
      st.usb = usbPlaying ? BP_USB_PLAY : usbaHost ? BP_USB_HOST : BP_USB_NONE;
      st.usbRec = usbrOn;
      const size_t SL = sizeof st;   // передавач з c6bb44f бере й довший кадр (відоме — собі)
      uint8_t raw[sizeof st + 2], enc[sizeof st + 8];
      memcpy(raw, &st, SL);
      uint16_t c = bp_crc16(raw, SL, 0xFFFF);
      raw[SL] = (uint8_t)c;
      raw[SL + 1] = (uint8_t)(c >> 8);
      size_t m = bp_cobs_enc(raw, SL + 2, enc);
      enc[m++] = 0;
      bqPush(enc, (uint32_t)m);
    }
    // записи журналу, які попросив передавач (BP_LOG), — скільки влізе в чергу зворотного каналу, лишаючи місце стану
    while (logSend != logSendEnd && logSend != BLOG->head && BQ - (bqHead - bqTail) >= 256) {
      uint32_t head = BLOG->head;
      if (head - logSend > BLOG_N) logSend = head - BLOG_N;   // найстаріші вже переписані новими
      uint32_t n = head - logSend < BP_LOG_PER ? head - logSend : BP_LOG_PER;
      if (n > logSendEnd - logSend) n = logSendEnd - logSend;
      uint8_t raw[sizeof(bp_log_hdr_t) + BP_LOG_PER * sizeof(bp_log_rec_t) + 2], enc[sizeof raw + 4];
      bp_log_hdr_t h = { BP_ST_LOG, (uint8_t)n, logSend, head };
      memcpy(raw, &h, sizeof h);
      for (uint32_t i = 0; i < n; i++) memcpy(raw + sizeof h + i * sizeof(bp_log_rec_t), &BLOG->r[(logSend + i) % BLOG_N], sizeof(bp_log_rec_t));
      size_t len = sizeof h + n * sizeof(bp_log_rec_t);
      uint16_t c = bp_crc16(raw, len, 0xFFFF);
      raw[len] = (uint8_t)c;
      raw[len + 1] = (uint8_t)(c >> 8);
      size_t m = bp_cobs_enc(raw, len + 2, enc);
      enc[m++] = 0;
      if (!bqPush(enc, (uint32_t)m)) break;
      logSend += n;
    }
    // знімок готовий — у порт: «@@CAPA i hex» / «@@CAPP i hex», по 8 кадрів у рядку (Л, П — int32 LE)
    if (capOn && capAn >= CAP_N && capPn >= CAP_N) {
      capOn = false;
      printf("@@CAP begin %u %u\n", (unsigned)CAP_N, (unsigned)FS_PCM);
      for (int src = 0; src < 2; src++)
        for (int i = 0; i < CAP_N; i += 8) {
          const uint8_t *b = (const uint8_t *)(src ? &capP[i][0] : &capA[i][0]);
          printf("@@CAP%c %d ", src ? 'P' : 'A', i);
          for (int k = 0; k < 64; k++) printf("%02x", b[k]);
          printf("\n");
          watchdog_update();
        }
      printf("@@CAP end\n");
    }
    // USB: команди однією буквою і рядок стану раз на секунду
    if (stdio_usb_connected()) {
      int ch = getchar_timeout_us(0);
      if (ch == 'a' || ch == 'p' || ch == 'u' || ch == 'm' || ch == 'w') {
        reqSrc = ch == 'a' ? BP_SRC_AES : ch == 'p' ? BP_SRC_PCM : ch == 'm' ? BP_SRC_MUTE : ch == 'w' ? BP_SRC_USB : BP_SRC_AUTO;
        localSrcT = now | 1;
        printf("вхід з порту: %s — передавач не перебиває хвилину\n",
               ch == 'a' ? "AES" : ch == 'p' ? "PCM1808" : ch == 'm' ? "тиша" : ch == 'w' ? "USB (комп'ютер)" : "авто");
      }
      else if (ch == 't') {   // перевірочний сигнал по колу: немає → тон → рожевий шум → розгортка → немає
        reqTone = (uint8_t)((reqTone + 1) % 4);
        if (reqTone) genKind = reqTone;
      }
      else if (ch == 'l') enterLoader(2);
      else if (ch == 'c') {   // знімок обох входів (див. capA / capP)
        capAn = capPn = 0;
        __dmb();
        capOn = true;
      }
      else if (ch == 'd') {   // симетричний аналоговий вхід (різниця каналів) — увімкнути / вимкнути, до перезапуску
        pcmDiffLocal = !pcmDiffLocal;
        printf("аналоговий вхід: %s\n", pcmDiffLocal ? "СИМЕТРИЧНИЙ — (Л − П) / 2 в обидва канали (з порту)"
                                                       : pcmDiffTx ? "симетричний (так велить передавач)" : "стерео, як є");
      }
      else if (ch == 'r') {   // гул радіо з аналогового входу — навпаки до передавача, до перезапуску
        humLocal = !humLocal;
        printf("гул радіо з аналогового входу: %s%s\n", humTx != humLocal ? "ПРИБИРАЮ" : "не прибираю",
               humLocal ? " (з порту, навпаки до передавача)" : " (як велить передавач)");
      }
      else if (ch == 'b') {   // режим запису без «диска» RPI-RP2 (див. CMakeLists.txt)
        blog(BL_LOADER, 2, 1, false);
        reset_usb_boot(0, 1);
      } else if (ch == 'j') {   // журнал моста: усе, що є в пам'яті
        blog_t *L = BLOG;
        uint32_t head = L->head, i = head > BLOG_N ? head - BLOG_N : 0, t0 = time_us_32();
        printf("журнал моста: записів за весь час %lu (у пам'яті — останні %lu), зараз запуск №%u\n", (unsigned long)head,
               (unsigned long)(head - i), L->boot);
        for (; i < head && time_us_32() - t0 < 3000000u; i++) {
          bp_log_rec_t r = L->r[i % BLOG_N];
          char t[120];
          bp_log_text(&r, t, sizeof t);
          unsigned long sec = r.ms / 1000u;
          printf("  #%lu [запуск %u, %02lu:%02lu:%02lu.%03lu] %s\n", (unsigned long)i, r.boot, sec / 3600, sec / 60 % 60, sec % 60,
                 (unsigned long)(r.ms % 1000u), t);
          if (hb1 != lastHb) {   // друк довгий: сторожа годувати й тут (поки ядро 1 живе)
            lastHb = hb1;
            watchdog_update();
          }
        }
      }
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
        uint8_t a = actNow > BP_ACT_USB ? BP_ACT_SWITCH : actNow;
        // пік виходу (з підмішаним шумом ±1 молодшого біта 16-бітного звуку: «рівний нуль» на вході дає тут близько −90 дБ)
        char pk[48];
        if (pkSecL >= 180 && pkSecR >= 180) snprintf(pk, sizeof pk, "тиша (≤ −90 дБ)");
        else snprintf(pk, sizeof pk, "Л −%.1f П −%.1f дБ", pkSecL / 2.0, pkSecR / 2.0);
        pkSecL = pkSecR = 255;
        char hm[176] = "";   // гул радіо (hum.h): рівень шаблону — скільки віднімається; залишок — що лишилось у циклі
        if (a == BP_ACT_PCM && humTx != humLocal) {
          float lv = hum_level(&hum), er = (float)hum.eLast * (256.0f / (8388608.0f * 8388608.0f));
          snprintf(hm, sizeof hm, "; гул радіо: прибираю (шаблон %.1f дБ, залишок %.1f дБ, тихих циклів %lu)",
                   lv > 0 ? 10 * log10f(lv) : -200.0f, er > 0 ? 10 * log10f(er) : -200.0f, (unsigned long)hum.n);
        } else if (a == BP_ACT_PCM) snprintf(hm, sizeof hm, "; гул радіо: не прибираю");
        printf("[%lu с] звучить: %s, пік %s; AES: %s %lu Гц, помилок парності %lu; передавач: %s; запас %d, поправка %+.1f ppm, "
               "провали %u/%u; перескоки %lu (останній на %lu с); ядро 1 зайняте %u‰ (на кадр: вихід %u нс, вхід %u нс); "
               "USB: %s, пакетів %lu (перерва найбільша %lu мкс, понад 2 мс — %lu, тиша без пакетів — %lu; кадрів у пакеті %lu…%lu; "
               "нулі посеред звуку: %lu разів, %lu пакетів); "
               "запис на комп'ютер: %s (пакетів %lu)%s%s%s\n",
               (unsigned long)(time_us_64() / 1000000u), actName[a], pk, aesName[aes > 3 ? 0 : aes],
               (unsigned long)rate, (unsigned long)spdif_rx_get_parity_err_count(),
               now - outRdT < 200000 ? "такти є" : "НЕМАЄ тактів", stLevel, stPpm10 / 10.0, stUnder, stOver,
               (unsigned long)resync, (unsigned long)resyncAt, cpu1, nsProd, nsIn,
               usbPlaying ? "комп'ютер грає" : usbaHost ? "комп'ютер підключено" : "немає", (unsigned long)usbaPkts,
               (unsigned long)usbaGapMax, (unsigned long)usbaLate, (unsigned long)usbIdleN, (unsigned long)(usbaFrMin == 999 ? 0 : usbaFrMin),
               (unsigned long)usbaFrMax, (unsigned long)usbaZeroRuns, (unsigned long)usbaZeroPkts,
               usbrOn ? "іде" : "ні", (unsigned long)usbrPkts,
               localSrcT ? "; вхід обрано з порту" : "", pcmShift ? "; PCM1808: формат I2S — вирівнюю на біт, Л і П на місці" : "",
               hm);
        usbaGapMax = 0;
        usbaFrMin = 999;
        usbaFrMax = 0;
      }
    }
    // світлодіод: спалах раз на секунду — працює; два — грає AES; часто — немає тактів передавача
    if (now - ledT >= 50000) {
      ledT = now;
      uint32_t ph = (now / 50000) % 20;
      bool on = actNow == BP_ACT_AES ? (ph == 0 || ph == 3) : ph == 0;
      if (!linkOk) on = ph & 1;
      gpio_put(PIN_LED, on);
    }
  }
}
