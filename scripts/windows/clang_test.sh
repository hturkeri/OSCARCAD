#!/usr/bin/env bash
cd scripts/windows
{
echo "=== repo opencascade surumleri (ucrt64) ==="
curl -s https://repo.msys2.org/mingw/ucrt64/ | grep -oE 'mingw-w64-ucrt-x86_64-opencascade-[0-9][^"<]*\.pkg\.tar\.zst' | sort -u
echo "=== clang64 kurulum ==="
pacman -S --needed --noconfirm mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-opencascade mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-qt6-base mingw-w64-clang-x86_64-nlohmann-json mingw-w64-clang-x86_64-gtest mingw-w64-clang-x86_64-eigen3 mingw-w64-clang-x86_64-tbb mingw-w64-clang-x86_64-ntldd 2>&1 | tail -5
pacman -Q mingw-w64-clang-x86_64-opencascade mingw-w64-clang-x86_64-clang
export PATH=/clang64/bin:$PATH
clang++ -O1 -std=gnu++17 -I/clang64/include/opencascade occt_test.cpp -o /tmp/occt_test_clang.exe -lTKernel -lTKMath -lTKG3d -lTKGeomBase -lTKBRep -lTKTopAlgo -lTKPrim -lTKBO -lTKFillet -lTKG2d -lTKGeomAlgo && echo built
/tmp/occt_test_clang.exe; echo "exit=$?"
} > ../../logs/clang_test.log 2>&1
