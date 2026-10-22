// USB мосту (з 22.10): описувачі (звукова карта + порт + перезапуск для picotool), прийом звуку з комп'ютера і запис
// на комп'ютер того, що йде в ефір. Звукова частина повторює приклад TinyUSB cdc_uac2
// (pico-sdk/lib/tinyusb/examples/device/cdc_uac2), лише без регулятора гучності: міст передає звук як є (гучність — у
// комп'ютері й у пульті), і в кожного шляху своя частота й свій годинник: 48 кГц з комп'ютера, 32 кГц — запис.
// Кінцева точка «адаптивна»: комп'ютер шле 48 кадрів на кадр USB за своїм годинником, а різницю з тактом передавача
// (десятки ppm) прибирає перерахунок частоти моста, як і для AES.
#include <string.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/usb_reset_interface.h"
#include "pico/usb_reset_tusb.h"
#include "hardware/structs/usb.h"
#include "tusb.h"
#include "usb_audio.h"

enum { ITF_AC = 0, ITF_AS_OUT, ITF_AS_IN, ITF_CDC, ITF_CDC_DATA, ITF_RESET, ITF_TOTAL };
enum { STR_LANG = 0, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_AUDIO, STR_CDC, STR_RESET, STR_REC, STR_N };
#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_AUDIO_OUT 0x03
#define EP_AUDIO_IN 0x84
#define ENT_IN_TERM 0x01      // вхідний термінал: потік із комп'ютера
#define ENT_OUT_TERM 0x03     // вихідний: «в ефір»
#define ENT_CLOCK 0x04        // годинник 48 кГц
#define ENT_REC_IN 0x11       // запис: вхідний термінал — лінія до передавача
#define ENT_REC_OUT 0x13      // вихідний — потік до комп'ютера
#define ENT_CLOCK_REC 0x14    // годинник 32 кГц (такт передавача)
#define TERM_LINE 0x0603      // тип термінала «лінійне з'єднання» (USB Audio Terminal Types 2.0, 2.4 — зовнішні)
#define CFG_LEN (TUD_CONFIG_DESC_LEN + USBA_DESC_LEN + TUD_CDC_DESC_LEN + TUD_RPI_RESET_DESC_LEN)
#define CH_LR (AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT)

int16_t usbaRing[USBA_RING][2];
volatile uint32_t usbaWr, usbaPktT, usbaPkts, usbaGapMax, usbaLate, usbaFrMin = 999, usbaFrMax, usbaZeroRuns, usbaZeroPkts;
volatile bool usbaOn, usbaHost;
int16_t usbrRing[USBR_RING][2];
volatile uint32_t usbrWr, usbrPkts;
volatile bool usbrOn;
static uint32_t usbrRd;

static const tusb_desc_device_t devDesc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x2E8A,    // Raspberry Pi
    .idProduct = 0x000A,   // як у порту SDK: picotool знаходить міст так само
    .bcdDevice = 0x0200,   // склад інтерфейсів інший, ніж у порту SDK (0x0100), — щоб Windows їх не плутала
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t cfgDesc[CFG_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CFG_LEN, 0, 100),
    // звукова функція: годинник → вхідний термінал (USB) → вихідний («в ефір»); потік — інтерфейс 1, варіант 1
    TUD_AUDIO_DESC_IAD(ITF_AC, 3, 0),
    TUD_AUDIO_DESC_STD_AC(ITF_AC, 0, STR_AUDIO),
    TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_IO_BOX, USBA_AC_UNITS_LEN, 0),
    TUD_AUDIO_DESC_CLK_SRC(ENT_CLOCK, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,
                           (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) | (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_VAL_POS), 0, 0),
    TUD_AUDIO_DESC_INPUT_TERM(ENT_IN_TERM, AUDIO_TERM_TYPE_USB_STREAMING, 0, ENT_CLOCK, 2, CH_LR, 0, 0, 0),
    TUD_AUDIO_DESC_OUTPUT_TERM(ENT_OUT_TERM, AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER, 0, ENT_IN_TERM, ENT_CLOCK, 0, 0),
    TUD_AUDIO_DESC_CLK_SRC(ENT_CLOCK_REC, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,
                           (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) | (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_VAL_POS), 0, 0),
    TUD_AUDIO_DESC_INPUT_TERM(ENT_REC_IN, TERM_LINE, 0, ENT_CLOCK_REC, 2, CH_LR, 0, 0, STR_REC),
    TUD_AUDIO_DESC_OUTPUT_TERM(ENT_REC_OUT, AUDIO_TERM_TYPE_USB_STREAMING, 0, ENT_REC_IN, ENT_CLOCK_REC, 0, 0),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_AS_OUT, 0, 0, STR_AUDIO),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_AS_OUT, 1, 1, STR_AUDIO),
    TUD_AUDIO_DESC_CS_AS_INT(ENT_IN_TERM, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM, 2, CH_LR, 0),
    TUD_AUDIO_DESC_TYPE_I_FORMAT(2, 16),
    TUD_AUDIO_DESC_STD_AS_ISO_EP(EP_AUDIO_OUT, (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ADAPTIVE | TUSB_ISO_EP_ATT_DATA),
                                 USBA_EP_OUT_SZ, 1),
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE,
                                AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 1),
    // запис: інтерфейс 2, варіант 1 — асинхронна кінцева точка (пакет — скільки кадрів міст видав за цю мс)
    TUD_AUDIO_DESC_STD_AS_INT(ITF_AS_IN, 0, 0, STR_REC),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_AS_IN, 1, 1, STR_REC),
    TUD_AUDIO_DESC_CS_AS_INT(ENT_REC_OUT, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM, 2, CH_LR, 0),
    TUD_AUDIO_DESC_TYPE_I_FORMAT(2, 16),
    TUD_AUDIO_DESC_STD_AS_ISO_EP(EP_AUDIO_IN, (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS | TUSB_ISO_EP_ATT_DATA),
                                 USBR_EP_IN_SZ, 1),
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE,
                                AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0),
    // порт (stdio)
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    // перезапуск у режим запису (picotool load -f) — драйвер SDK, pico_usb_reset
    TUD_RPI_RESET_DESCRIPTOR(ITF_RESET, STR_RESET),
};

static const char *const strs[STR_N] = {
    [STR_MANUF] = "hearlink",
    [STR_PRODUCT] = "Міст «Слух»",
    [STR_AUDIO] = "Звук в ефір",
    [STR_CDC] = "Порт моста",
    [STR_RESET] = "Reset",
    [STR_REC] = "Що йде в ефір",
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&devDesc; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return cfgDesc;
}
// Рядки — з UTF-8 у UTF-16 (у SDK — побайтно, кирилиця так не виходить).
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t d[32];
  static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  if (index == STR_LANG) {
    d[1] = 0x0409;
    d[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
    return d;
  }
  const char *s;
  if (index == STR_SERIAL) {
    if (!serial[0]) pico_get_unique_board_id_string(serial, sizeof serial);
    s = serial;
  } else if (index < STR_N && strs[index]) s = strs[index];
  else return NULL;
  int n = 0;
  for (const uint8_t *p = (const uint8_t *)s; *p && n < 31;) {
    uint32_t c = *p++;
    if (c >= 0xC0 && c < 0xE0 && p[0]) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
    else if (c >= 0xE0 && c < 0xF0 && p[0] && p[1]) {
      c = ((c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
      p += 2;
    }
    d[1 + n++] = (uint16_t)c;
  }
  d[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return d;
}

void tud_mount_cb(void) { usbaHost = true; }
void tud_umount_cb(void) {
  usbaHost = false;
  usbaOn = false;
}
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  usbaHost = false;
  usbaOn = false;
}
void tud_resume_cb(void) { usbaHost = tud_mounted(); }

// Годинники: частота (48 кГц — звук з комп'ютера, 32 кГц — запис) і «годинник справний»; інших запитів (гучність тощо)
// немає.
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  audio_control_request_t const *r = (audio_control_request_t const *)p_request;
  if (r->bEntityID != ENT_CLOCK && r->bEntityID != ENT_CLOCK_REC) return false;
  int32_t rate = r->bEntityID == ENT_CLOCK ? USBA_RATE : USBR_RATE;
  if (r->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
    if (r->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_4_t f = { (int32_t)tu_htole32(rate) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &f, sizeof f);
    }
    if (r->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_4_n_t(1) rg = { .wNumSubRanges = tu_htole16(1) };
      rg.subrange[0].bMin = rate;
      rg.subrange[0].bMax = rate;
      rg.subrange[0].bRes = 0;
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &rg, sizeof rg);
    }
  } else if (r->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && r->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t v = { .bCur = 1 };
    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &v, sizeof v);
  }
  return false;
}
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  (void)rhport;
  audio_control_request_t const *r = (audio_control_request_t const *)p_request;
  // частота в кожного годинника одна: «поставити» її можна, іншу — ні
  if ((r->bEntityID == ENT_CLOCK || r->bEntityID == ENT_CLOCK_REC) && r->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
      r->bRequest == AUDIO_CS_REQ_CUR && r->wLength == sizeof(audio_control_cur_4_t))
    return ((audio_control_cur_4_t const *)buf)->bCur == (r->bEntityID == ENT_CLOCK ? USBA_RATE : USBR_RATE);
  return false;
}

// Потік відкрили (варіант 1) чи закрили (варіант 0): комп'ютер почав або перестав грати в міст.
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  uint8_t itf = tu_u16_low(tu_le16toh(p_request->wIndex)), alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_AS_OUT) usbaOn = alt != 0;
  if (itf == ITF_AS_IN) {
    usbrRd = usbrWr;   // запис — з цієї миті, без старого в кільці
    usbrOn = alt != 0;
  }
  return true;
}
// RP2040 (TinyUSB 0.18): ізохронну кінцеву точку при закритті потоку ніхто не закриває — пакет, поставлений перед тим,
// лишається «готовим» (AVAIL у керуванні буфером), а наступне відкриття ставить ще один → panic «ep 84 was already
// available» (22.10: друге відкриття запису на Mac — міст упав і перезапустився). Зняти пакет апаратно: EP_ABORT,
// дочекатися EP_ABORT_DONE (кінцева точка вільна), очистити керування буфером.
static void epRevoke(uint8_t num, bool in) {
  uint32_t bit = 1u << (2 * num + (in ? 0 : 1)), t0 = time_us_32();
  hw_set_bits(&usb_hw->abort, bit);
  while (!(usb_hw->abort_done & bit) && time_us_32() - t0 < 2000) tight_loop_contents();
  if (in) usb_dpram->ep_buf_ctrl[num].in = 0;
  else usb_dpram->ep_buf_ctrl[num].out = 0;
  hw_clear_bits(&usb_hw->abort, bit);
  usb_hw->abort_done = bit;   // «готово» скидається записом одиниці
}

// Потік закривають (або перевідкривають іншим варіантом): що грав чи писав — уже ні; поставлений пакет — зняти.
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  uint8_t itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  if (itf == ITF_AS_OUT) {
    usbaOn = false;
    epRevoke(EP_AUDIO_OUT & 0x0F, false);
  }
  if (itf == ITF_AS_IN) {
    usbrOn = false;
    epRevoke(EP_AUDIO_IN & 0x0F, true);
  }
  return true;
}

// Запис: перед кожним пакетом (раз на 1 мс) — у FIFO TinyUSB усе, що ядро 1 видало відтоді (до 33 кадрів; решта —
// наступним). Так пакет і несе стільки, скільки міст видав за такт передавача: 31…33 кадри.
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t func_id, uint8_t ep_in, uint8_t cur_alt_setting) {
  (void)rhport;
  (void)func_id;
  (void)ep_in;
  (void)cur_alt_setting;
  static int16_t buf[USBR_EP_IN_SZ / 2];
  uint32_t wr = usbrWr;
  __dmb();
  if (wr - usbrRd > USBR_RING) usbrRd = wr - USBR_RATE / 1000;   // відстали більше, ніж на кільце, — від свіжого
  uint32_t n = wr - usbrRd;
  if (n > USBR_EP_IN_SZ / 4) n = USBR_EP_IN_SZ / 4;
  for (uint32_t i = 0; i < n; i++) {
    buf[2 * i] = usbrRing[(usbrRd + i) & (USBR_RING - 1)][0];
    buf[2 * i + 1] = usbrRing[(usbrRd + i) & (USBR_RING - 1)][1];
  }
  usbrRd += n;
  if (n) tud_audio_write(buf, (uint16_t)(n * 4));
  usbrPkts = usbrPkts + 1;
  return true;
}

// Пакет звуку прийшов (задача USB, ядро 0): кадри — у кільце, звідки їх забирає ядро 1.
bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
  (void)rhport;
  (void)func_id;
  (void)ep_out;
  (void)cur_alt_setting;
  static int16_t buf[USBA_EP_OUT_SZ / 2];
  uint16_t n = tud_audio_read(buf, n_bytes_received < sizeof buf ? n_bytes_received : sizeof buf);
  uint32_t fr = n / 4u, w = usbaWr;
  bool zero = fr != 0;
  for (uint32_t i = 0; i < fr; i++) {
    usbaRing[(w + i) & (USBA_RING - 1)][0] = buf[2 * i];
    usbaRing[(w + i) & (USBA_RING - 1)][1] = buf[2 * i + 1];
    if (buf[2 * i] | buf[2 * i + 1]) zero = false;
  }
  // пакет із самих нулів після звуку — у комп'ютера скінчився звук для відправки (його недобір, не наш)
  static bool lastZero = true;
  if (zero) {
    usbaZeroPkts = usbaZeroPkts + 1;
    if (!lastZero) usbaZeroRuns = usbaZeroRuns + 1;
  }
  lastZero = zero;
  __dmb();
  usbaWr = w + fr;
  uint32_t now = time_us_32(), gap = now - usbaPktT;
  if (usbaOn && usbaPkts && gap < 1000000u) {
    if (gap > usbaGapMax) usbaGapMax = gap;
    if (gap > 2000) usbaLate = usbaLate + 1;
  }
  if (fr < usbaFrMin) usbaFrMin = fr;
  if (fr > usbaFrMax) usbaFrMax = fr;
  usbaPktT = now;
  usbaPkts = usbaPkts + 1;
  return true;
}
