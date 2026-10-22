// Міст як звукова карта комп'ютера (USB Audio Class 2, з 22.10): «Звук в ефір» — 48 кГц, стерео, 16 біт; це третій
// вхід моста (BP_SRC_USB). Назад — «Що йде в ефір»: запис на комп'ютер того, що міст віддає передавачу (32 кГц, такт
// передавача — тому свій годинник і асинхронна кінцева точка: міст шле стільки кадрів, скільки видав, 31…33 на мс).
// На тому самому USB лишаються порт (рядок стану й команди-букви, stdio) і інтерфейс перезапуску, через який picotool
// переводить міст у режим запису без диска.
// Дані: пакет USB (1 мс, ~48 кадрів) кладе в кільце задача USB на ядрі 0 (tud_task у фоновому перериванні SDK),
// забирає звукове ядро 1 (usbPull у main.c) у перерахунок частоти — так само, як вхід AES.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define USBA_RATE 48000
#define USBA_EP_OUT_SZ ((USBA_RATE / 1000 + 1) * 2 * 2)   // 196 байт: 49 кадрів (одним більше — запас хосту)
#define USBA_RING 1024                                    // кадрів у кільці (21 мс), степінь двійки
#define USBR_RATE 32000                                   // запис «що йде в ефір»
#define USBR_EP_IN_SZ ((USBR_RATE / 1000 + 1) * 2 * 2)   // 132 байти: до 33 кадрів у пакеті
#define USBR_RING 256                                     // кадрів у кільці запису (8 мс), степінь двійки

// Описувач звукової функції: керування — два годинники (48 кГц — звук з комп'ютера, 32 кГц — запис) і по два термінали
// на кожен шлях; два потоки (інтерфейси 1 і 2, варіант 1 — з кінцевою точкою).
#define USBA_AS_LEN (2 * TUD_AUDIO_DESC_STD_AS_INT_LEN + TUD_AUDIO_DESC_CS_AS_INT_LEN + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN \
                     + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)
#define USBA_AC_UNITS_LEN (2 * (TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN))
#define USBA_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN + TUD_AUDIO_DESC_STD_AC_LEN + TUD_AUDIO_DESC_CS_AC_LEN + USBA_AC_UNITS_LEN + 2 * USBA_AS_LEN)

extern int16_t usbaRing[USBA_RING][2];   // кадри L, R
extern volatile uint32_t usbaWr;         // кадрів записано всього (ядро 0)
extern volatile uint32_t usbaPktT;       // коли прийшов останній пакет, мкс
extern volatile bool usbaOn;             // комп'ютер відкрив потік (грає в міст)
extern volatile bool usbaHost;           // міст підключено до комп'ютера (USB налаштовано)
extern volatile uint32_t usbaPkts;       // пакетів прийнято всього
extern volatile uint32_t usbaGapMax;     // найбільша перерва між пакетами з останнього звіту, мкс (обнуляє звіт)
extern volatile uint32_t usbaLate;       // перерв понад 2 мс усього
extern volatile uint32_t usbaFrMin, usbaFrMax;   // кадрів у пакеті: найменше й найбільше з останнього звіту
extern volatile uint32_t usbaZeroRuns;   // скільки разів посеред звуку комп'ютер прислав пакети з одних нулів (недобір у нього)
extern volatile uint32_t usbaZeroPkts;   // таких пакетів усього
extern int16_t usbrRing[USBR_RING][2];   // запис: кадри L, R (пише ядро 1)
extern volatile uint32_t usbrWr;         // кадрів записано всього
extern volatile bool usbrOn;             // комп'ютер записує (потік запису відкрито)
extern volatile uint32_t usbrPkts;       // пакетів запису відправлено
