// TinyUSB для мосту (з 22.10): порт CDC (stdio — рядок стану й команди) і звукова карта USB Audio Class 2
// (usb_audio.h). Описувачі — свої (usb_audio.c), бо в SDK — лише порт; ініціалізацію і фонову задачу USB лишаємо SDK
// (PICO_STDIO_USB_ENABLE_TINYUSB_INIT, PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK у CMakeLists.txt).
#pragma once
#include "usb_audio.h"

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE)
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 256   // рядок стану — ~300 байт: з 64 він ішов п'ятьма обертами tud_task
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_VENDOR 0             // інтерфейс перезапуску (picotool) — драйвер SDK (pico_usb_reset), не цей клас

#define CFG_TUD_AUDIO 1
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN USBA_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 2
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ 64
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 1
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX USBA_EP_OUT_SZ
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (4 * USBA_EP_OUT_SZ)
#define CFG_TUD_AUDIO_ENABLE_EP_IN 1
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX USBR_EP_IN_SZ
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (4 * USBR_EP_IN_SZ)
