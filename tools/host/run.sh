#!/bin/sh
# Проверки на компьютере, без плат. Берут настоящий hearlink/proto.h.
#   codec  — моно: звук проходит «передатчик → пакет → приёмник» для каждого качества при потерях 0, 5 и 20 %;
#   stereo — то же для двух каналов: сигнал/шум по каналам и проникание одного канала в другой;
#   dbl    — «найвища» одиночными пакетами и двойными (два кадра в пакете, с 2.57) при потерях 0, 5 и 20 %;
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
./build/host/sec_test
python3 tools/host/sec_kat.py
./build/host/codec
./build/host/stereo
./build/host/dbl
