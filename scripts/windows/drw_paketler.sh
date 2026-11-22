#!/usr/bin/env bash
# Teknik resim icin istege bagli paketler (tam sistem guncellemesi YAPMAZ)
P=$MINGW_PACKAGE_PREFIX
opt=()
for k in $P-qpdf $P-opencv $P-tesseract-ocr $P-leptonica $P-qt6-pdf; do
  if pacman -Si "$k" >/dev/null 2>&1; then opt+=("$k"); else echo "(depoda yok: $k)"; fi
done
echo "Kurulacak: ${opt[*]}"
pacman -S --needed --noconfirm "${opt[@]}" || echo "UYARI: bazi paketler kurulamadi"
pacman -Q $P-qpdf $P-opencv $P-tesseract-ocr $P-leptonica $P-qt6-pdf 2>&1
exit 0
