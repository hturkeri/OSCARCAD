#!/usr/bin/env bash
# PersistCAD derleme bağımlılıkları (MSYS2 UCRT64)
set -euo pipefail
# Varsayılan ortam CLANG64: MSYS2'nin GCC 16 ile derlenmiş OCCT 7.9.3 (UCRT64) paketi
# BRepClass3d_SolidClassifier'da koni geometrisinde çöküyor; Clang derlemesi sorunsuz.
case "${MSYSTEM:-CLANG64}" in
  CLANG64) P=mingw-w64-clang-x86_64; CC=clang ;;
  *)       P=mingw-w64-ucrt-x86_64;  CC=gcc ;;
esac
pkgs=($P-$CC $P-cmake $P-ninja $P-qt6-base $P-opencascade $P-nlohmann-json $P-gtest $P-eigen3 $P-tbb $P-ntldd $P-gdb)
echo "Kuruluyor: ${pkgs[*]}"
pacman -S --needed --noconfirm "${pkgs[@]}"
export PATH=$MINGW_PREFIX/bin:$PATH

# --- İSTEĞE BAĞLI: teknik resimden içe aktarma (PDF vektör, tarama/OCR, PDF önizleme) ---------------------------
# qpdf -> vektör PDF, opencv -> taranmış resim, tesseract-ocr + leptonica -> OCR, qt6-pdf -> PDF sayfa görüntüsü.
# Bu adım başarısız olsa da kurulum durmaz: CMake bulamadığı kütüphane için "nodeps" yolunu derler (DXF ve
# DrawingIR her zaman çalışır; eksik olan girdi türü sihirbazda "bu derlemede yok" diye bildirilir).
# Atlamak için: PC_DRAWING_OPTIONAL=0 bash scripts/windows/kurulum.sh
if [ "${PC_DRAWING_OPTIONAL:-1}" = "1" ]; then
  opt=()
  for k in $P-qpdf $P-opencv $P-tesseract-ocr $P-leptonica $P-qt6-pdf; do
    if pacman -Si "$k" >/dev/null 2>&1; then opt+=("$k"); else echo "  (depoda yok, atlandı: $k)"; fi
  done
  if [ ${#opt[@]} -gt 0 ]; then
    echo "İsteğe bağlı (teknik resim): ${opt[*]}"
    pacman -S --needed --noconfirm "${opt[@]}" || echo "UYARI: isteğe bağlı paketler kurulamadı; teknik resim içe aktarma kısıtlı derlenecek."
  fi
  # OCR dil verisi (eng/tur) depodan gelir: share/persistcad/tessdata; sistem tessdata paketi gerekmez.
fi

echo "--- surumler ---"
$CC --version | head -1; cmake --version | head -1
pacman -Q $P-qt6-base $P-opencascade
pacman -Q $P-qpdf $P-opencv $P-tesseract-ocr $P-qt6-pdf 2>/dev/null || true   # isteğe bağlı (yoksa sorun değil)
