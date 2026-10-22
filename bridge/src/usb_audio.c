// USB мосту (з 22.10): описувачі (звукова карта + порт + перезапуск для picotool) і прийом звуку з комп'ютера.
// Звукова частина повторює приклад TinyUSB cdc_uac2 (pico-sdk/lib/tinyusb/examples/device/cdc_uac2), лише без
// мікрофона й регулятора гучності: міст передає звук як є (гучність — у комп'ютері й у пульті), частота одна — 48 кГц.
// Кінцева точка «адаптивна»: комп'ютер шле 48 кадрів на кадр USB за своїм годинником, а різницю з тактом передавача
// (десятки ppm) прибирає перерахунок частоти моста, як і для AES.
#include <string.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/usb_reset_interface.h"
#include "pico/usb_reset_tusb.h"
#include "tusb.h"
#include "usb_audio.h"

enum { ITF_AC = 0, ITF_AS_OUT, ITF_CDC, ITF_CDC_DATA, ITF_RESET, ITF_TOTAL };
enum { STR_LANG = 0, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_AUDIO, STR_CDC, STR_RESET, STR_N };
#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_AUDIO_OUT 0x03
#define ENT_IN_TERM 0x01    // вхідний термінал: потік із комп'ютера
#define ENT_OUT_TERM 0x03   // вихідний: «в ефір»
#define ENT_CLOCK 0x04      // годинник 48 кГц
#define CFG_LEN (TUD_CONFIG_DESC_LEN + USBA_DESC_LEN + TUD_CDC_DESC_LEN + TUD_RPI_RESET_DESC_LEN)
#define CH_LR (AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT)

int16_t usbaRing[USBA_RING][2];
volatile uint32_t usbaWr, usbaPktT, usbaPkts;
volatile bool usbaOn, usbaHost;

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
    TUD_AUDIO_DESC_IAD(ITF_AC, 2, 0),
    TUD_AUDIO_DESC_STD_AC(ITF_AC, 0, STR_AUDIO),
    TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_DESKTOP_SPEAKER,
                         TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN, 0),
    TUD_AUDIO_DESC_CLK_SRC(ENT_CLOCK, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,
                           (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) | (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_VAL_POS), 0, 0),
    TUD_AUDIO_DESC_INPUT_TERM(ENT_IN_TERM, AUDIO_TERM_TYPE_USB_STREAMING, 0, ENT_CLOCK, 2, CH_LR, 0, 0, 0),
    TUD_AUDIO_DESC_OUTPUT_TERM(ENT_OUT_TERM, AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER, 0, ENT_IN_TERM, ENT_CLOCK, 0, 0),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_AS_OUT, 0, 0, STR_AUDIO),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_AS_OUT, 1, 1, STR_AUDIO),
    TUD_AUDIO_DESC_CS_AS_INT(ENT_IN_TERM, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM, 2, CH_LR, 0),
    TUD_AUDIO_DESC_TYPE_I_FORMAT(2, 16),
    TUD_AUDIO_DESC_STD_AS_ISO_EP(EP_AUDIO_OUT, (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ADAPTIVE | TUSB_ISO_EP_ATT_DATA),
                                 USBA_EP_OUT_SZ, 1),
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE,
                                AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 1),
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

// Годинник: частота 48 кГц і «годинник справний»; інших запитів (гучність тощо) немає.
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  audio_control_request_t const *r = (audio_control_request_t const *)p_request;
  if (r->bEntityID != ENT_CLOCK) return false;
  if (r->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
    if (r->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_4_t f = { (int32_t)tu_htole32(USBA_RATE) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &f, sizeof f);
    }
    if (r->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_4_n_t(1) rg = { .wNumSubRanges = tu_htole16(1) };
      rg.subrange[0].bMin = USBA_RATE;
      rg.subrange[0].bMax = USBA_RATE;
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
  // частота одна: «поставити» 48 кГц можна, іншу — ні
  if (r->bEntityID == ENT_CLOCK && r->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ && r->bRequest == AUDIO_CS_REQ_CUR &&
      r->wLength == sizeof(audio_control_cur_4_t))
    return ((audio_control_cur_4_t const *)buf)->bCur == USBA_RATE;
  return false;
}

// Потік відкрили (варіант 1) чи закрили (варіант 0): комп'ютер почав або перестав грати в міст.
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  uint8_t itf = tu_u16_low(tu_le16toh(p_request->wIndex)), alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_AS_OUT) usbaOn = alt != 0;
  return true;
}
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  uint8_t itf = tu_u16_low(tu_le16toh(p_request->wIndex)), alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_AS_OUT && alt == 0) usbaOn = false;
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
  for (uint32_t i = 0; i < fr; i++) {
    usbaRing[(w + i) & (USBA_RING - 1)][0] = buf[2 * i];
    usbaRing[(w + i) & (USBA_RING - 1)][1] = buf[2 * i + 1];
  }
  __dmb();
  usbaWr = w + fr;
  usbaPktT = time_us_32();
  usbaPkts = usbaPkts + 1;
  return true;
}
