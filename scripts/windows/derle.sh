#!/usr/bin/env bash
set -eo pipefail
export PATH=$MINGW_PREFIX/bin:$PATH
echo "[$(date)] cmake configure"
export CC=${CC:-$( [ $MSYSTEM = CLANG64 ] && echo clang || echo gcc )} CXX=${CXX:-$( [ $MSYSTEM = CLANG64 ] && echo clang++ || echo g++ )}
cmake -S . -B build-$(echo $MSYSTEM | tr A-Z a-z) -G Ninja -DCMAKE_BUILD_TYPE=Release
echo "[$(date)] ninja ($(nproc) cekirdek)"
ninja -C build-$(echo $MSYSTEM | tr A-Z a-z) -j"$(nproc)" -k 0
echo "[$(date)] bitti"
