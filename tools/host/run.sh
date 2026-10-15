#!/bin/sh
# Проверки на компьютере, без плат. Берут настоящий hearlink/proto.h.
#   codec  — моно: звук проходит «передатчик → пакет → приёмник» для каждого качества при потерях 0, 5 и 20 %;
#   stereo — то же для двух каналов: сигнал/шум по каналам и проникание одного канала в другой;
#   dbl    — «найвища» одиночными пакетами и двойными (два кадра в пакете, с 2.57) при потерях 0, 5 и 20 %;
#   otascan — раздача обновления: следующий нужный блок находится из любого места (до 2.67 — не из любого);
#   nsfx   — обработка «для слуха» (hearlink/nsfx.h): шумозаглушение на речи и музыке, сжатие вместо «усиление +
#            ограничитель», шум в паузах; записи для прослушивания — в build/host/ns;
#   rxchain — «зернистость» (14.10): ступени выходного тракта приёмника по отдельности (интермодуляция ограничителя,
#            подгонка темпа на синусах, перевод в целое); с записью музыки — ещё и по полосам частот, до и после 2.86;
#   nsstereo — шумозаглушение в стерео (2.89): Л и П с одинаковой обработкой, музыка из одного канала не протекает во второй;
#   rsdot  — подгонка темпа приёмника (2.97): быстрая свёртка hearlink/rsdot.h против прежней — расхождение в пределах
#            округления float, и на окнах через конец кольца тоже;
#   subc   — подполосный кодек дальнего режима (hearlink/subc.h): набор фильтров восстанавливает звук, быстрый счёт
#            совпадает с прежним «в лоб» (tools/host/subc_ref.h) до бита, сигнал/шум на записях против прежних сжатий,
#            пакет «кадр + три копии» при потерях; записи для прослушивания — в build/host/subc-wav;
#   sec_test — запасной программный шифр (tools/host/seccore.h, в прошивку не входит) по эталонам;
#   sec_kat.py — эталон для аппаратного AES набора (сверяется самопроверкой платы, порт: B).
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/host
sed 's/#include "config.h"//' hearlink/proto.h > build/host/proto_host.h
c++ -std=c++17 -O1 -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink tools/host/codec.cpp -o build/host/codec
c++ -std=c++17 -O1 -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink tools/host/stereo.cpp -o build/host/stereo
c++ -std=c++17 -O1 -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink tools/host/dbl.cpp -o build/host/dbl
c++ -std=c++17 -O1 -Wall tools/host/sec_test.cpp -o build/host/sec_test
c++ -std=c++17 -O1 -Wall tools/host/otascan.cpp -o build/host/otascan
./build/host/otascan
./build/host/sec_test
python3 tools/host/sec_kat.py
./build/host/codec
./build/host/stereo
./build/host/dbl
c++ -std=c++17 -O2 -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink tools/host/rxchain.cpp -o build/host/rxchain
./build/host/rxchain
mkdir -p build/host/ns
c++ -std=c++17 -O2 -Wall -Wno-unused-function -Ihearlink tools/host/nsfx.cpp -o build/host/nsfx
./build/host/nsfx assets build/host/ns
c++ -std=c++17 -O2 -Wall -Wno-unused-function -Ihearlink tools/host/nsstereo.cpp -o build/host/nsstereo
[ -f build/host/muzyka3.pcm ] || /usr/local/bin/ffmpeg -v error -i assets-src/muzyka3.mp3 -f s16le -ac 2 -ar 32000 build/host/muzyka3.pcm
./build/host/nsstereo build/host/muzyka3.pcm
c++ -std=c++17 -O2 -Wall -Ihearlink tools/host/rsdot.cpp -o build/host/rsdot
./build/host/rsdot build/host/muzyka3.pcm
./build/host/rsdot
mkdir -p build/host/subc-wav
c++ -std=c++17 -O2 -Wall -Wno-unused-function -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink -Itools/host tools/host/subc.cpp -o build/host/subc
./build/host/subc assets build/host/subc-wav $( [ -f build/sermon/a.pcm ] && echo build/sermon/a.pcm )
