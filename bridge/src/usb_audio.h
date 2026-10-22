// Міст як звукова карта комп'ютера (USB Audio Class 2, з 22.10): «Звук в ефір» — 48 кГц, стерео, 16 біт; це третій
// вхід моста (BP_SRC_USB). На тому самому USB лишаються порт (рядок стану й команди-букви, stdio) і інтерфейс
// перезапуску, через який picotool переводить міст у режим запису без диска.
// Дані: пакет USB (1 мс, ~48 кадрів) кладе в кільце задача USB на ядрі 0 (tud_task у фоновому перериванні SDK),
// забирає звукове ядро 1 (usbPull у main.c) у перерахунок частоти — так само, як вхід AES.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define USBA_RATE 48000
#define USBA_EP_OUT_SZ ((USBA_RATE / 1000 + 1) * 2 * 2)   // 196 байт: 49 кадрів (одним більше — запас хосту)
#define USBA_RING 1024                                    // кадрів у кільці (21 мс), степінь двійки

// Описувач звукової функції: керування (годинник 48 кГц → вхідний термінал «USB» → вихідний «в ефір») і потік.
#define USBA_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN + TUD_AUDIO_DESC_STD_AC_LEN + TUD_AUDIO_DESC_CS_AC_LEN + TUD_AUDIO_DESC_CLK_SRC_LEN \
                       + TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN + TUD_AUDIO_DESC_STD_AS_INT_LEN          \
                       + TUD_AUDIO_DESC_STD_AS_INT_LEN + TUD_AUDIO_DESC_CS_AS_INT_LEN + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN         \
                       + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)

extern int16_t usbaRing[USBA_RING][2];   // кадри L, R
extern volatile uint32_t usbaWr;         // кадрів записано всього (ядро 0)
extern volatile uint32_t usbaPktT;       // коли прийшов останній пакет, мкс
extern volatile bool usbaOn;             // комп'ютер відкрив потік (грає в міст)
extern volatile bool usbaHost;           // міст підключено до комп'ютера (USB налаштовано)
extern volatile uint32_t usbaPkts;       // пакетів прийнято всього
