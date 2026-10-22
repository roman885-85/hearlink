# pico_spdif_rx — копія для мосту hearlink

Бібліотека прийому S/PDIF/AES для RP2040: https://github.com/elehobica/pico_spdif_rx
(© 2023 Elehobica, BSD-2-Clause — `LICENSE.txt`). Взято з коміту, записаного в `UPSTREAM.txt`.

Зміни для мосту (позначені `[hearlink]`):
- `spdif_rx_read_fine()`, `spdif_rx_get_fine_count()` — читання підкадрів одразу, як їх записав DMA, без чекання
  цілої пачки 384 підкадри (4 мс при 48 кГц): так затримка моста — ~1,5 мс замість ~6 мс.
- `CMakeLists.txt` — без залежності від `pico_audio_32b` (у самому `spdif_rx.c` вона не потрібна).
