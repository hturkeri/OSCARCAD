#!/usr/bin/env bash
# UCRT64: önceki OCCT paket derlemelerini koni sınıflandırma testiyle dener.
cd scripts/windows
export PATH=/ucrt64/bin:$PATH
{
for v in 7.9.3-2 7.9.2-2 7.8.1-2; do
  echo "=== opencascade $v ==="
  pacman -U --noconfirm "https://repo.msys2.org/mingw/ucrt64/mingw-w64-ucrt-x86_64-opencascade-$v-any.pkg.tar.zst" 2>&1 | tail -2
  pacman -Q mingw-w64-ucrt-x86_64-opencascade
  g++ -O1 -std=gnu++17 -I/ucrt64/include/opencascade occt_test.cpp -o /tmp/occt_test_$v.exe -lTKernel -lTKMath -lTKG3d -lTKGeomBase -lTKBRep -lTKTopAlgo -lTKPrim -lTKBO -lTKFillet -lTKG2d -lTKGeomAlgo && echo built
  /tmp/occt_test_$v.exe; rc=$?; echo "exit=$rc"
  if [ $rc -eq 0 ]; then echo "CALISAN SURUM: $v"; break; fi
done
} > ../../logs/occt_eski_test.log 2>&1
