#!/usr/bin/env bash
# dist/: MSYS2 olmadan çalışan klasör. DLL'ler ntldd ile özyinelemeli toplanır.
set -eo pipefail
export PATH=$MINGW_PREFIX/bin:$PATH
B=build-$(echo $MSYSTEM | tr A-Z a-z)/bin
D=${PC_DIST:-dist}
rm -rf "$D"; mkdir -p "$D"/platforms "$D"/styles "$D"/imageformats "$D"/samples "$D"/docs
cp "$B"/persistcad.exe "$B"/persistcad_runner.exe "$B"/pc_mkapp.exe "$D"/
# Teknik resimden içe aktarma: komut satırı aracı + OCR dil verisi (exe'nin yanında share/persistcad/tessdata;
# Ocr/sihirbaz önce <exe>/../share, sonra <exe>/share/persistcad/tessdata'ya bakar)
[ -f "$B"/pc_drawing_cli.exe ] && cp "$B"/pc_drawing_cli.exe "$D"/
mkdir -p "$D"/share/persistcad/tessdata
cp share/persistcad/tessdata/*.traineddata share/persistcad/tessdata/README.md "$D"/share/persistcad/tessdata/ 2>/dev/null || true
P=$MINGW_PREFIX/share/qt6/plugins
cp "$P"/platforms/qwindows.dll "$D"/platforms/
cp "$P"/styles/*.dll "$D"/styles/ 2>/dev/null || true
cp "$P"/imageformats/qjpeg.dll "$P"/imageformats/qico.dll "$P"/imageformats/qsvg.dll "$D"/imageformats/ 2>/dev/null || true
deps() { ntldd -R "$@" 2>/dev/null | grep -iE 'ucrt64|clang64' | sed -E 's/.*=> (.*) \(0x.*/\1/' | tr '\\' '/' ; }
for f in "$D"/*.exe "$D"/platforms/*.dll "$D"/styles/*.dll "$D"/imageformats/*.dll; do deps "$f"; done | sort -u > /tmp/pc_deps.txt
while read -r w; do u=$(cygpath -u "$w"); [ -f "$u" ] && cp -n "$u" "$D"/; done < /tmp/pc_deps.txt
echo "DLL sayisi: $(ls "$D"/*.dll | wc -l)"
cp samples/*.pcad "$D"/samples/
cp README.md "$D"/; cp docs/KULLANIM_KILAVUZU.md "$D"/docs/; cp -r docs/enjektor "$D"/docs/ 2>/dev/null || true; cp -r docs/screenshots "$D"/docs/ 2>/dev/null || true
# Örnek uygulamalar (Save as App'in komut satırı karşılığı)
"$D"/pc_mkapp.exe samples/bracket.pcad "$D"/persistcad_runner.exe "$D"/FlansBraket_App --name "Flanş Braket" --version 1.0
"$D"/pc_mkapp.exe samples/v2_multibody.pcad "$D"/persistcad_runner.exe "$D"/BraketKulak_App --name "Braket + Kulak" --version 2.0
"$D"/pc_mkapp.exe samples/enjektor_09DB01.pcad "$D"/persistcad_runner.exe "$D"/Enjektor_09DB01_App --name "Simplex Enjektör 09-DB01" --version 1.1 --author "MTAG" --desc "EN 293/299 simplex basınç-girdap atomizörü (donanım: baş, swirler, tutucu vida, filtre, elek): debi, basınç ve Cd'den orifis çapı; swirler kanalları; filtre."
# Akış hacmi (CFD için ıslak hacim) ayrı uygulamadır: donanım uygulaması hızlı ve kesitleri sade kalır
"$D"/pc_mkapp.exe samples/enjektor_09DB01_akis.pcad "$D"/persistcad_runner.exe "$D"/Enjektor_09DB01_Akis_App --name "Simplex Enjektör 09-DB01 — Akış Hacmi" --version 1.1 --author "MTAG" --desc "09-DB01 atomizörünün ıslak (akış) hacmi: filtre dışı → elek → pencereler → tutucu vida → slotlar → girdap odası → orifis. STEP olarak dışa aktarılıp CFD ağına verilir."
echo "--- dogrulama ---"
"$D"/FlansBraket_App.exe --validate-only --text | tail -3
set +e
"$D"/FlansBraket_App.exe --validate-only --text --set T=0.2 | tail -2; echo "ihlal cikis kodu: $? (2 bekleniyor)"
set -e
"$D"/BraketKulak_App.exe --validate-only --text | tail -2
"$D"/Enjektor_09DB01_App.exe --validate-only --text | tail -4
set +e
# Gerçek bir tasarım ihlali: iki değer de kendi aralığında, ama birlikte slot tabanı uçta ekseni geçer
# (slot_uc_payi < 0) ve iç duvar eksen düzlemini aşar (slot_ic_payi < 0) -> PARAM.RANGE x2, çıkış kodu 2
"$D"/Enjektor_09DB01_App.exe --validate-only --text --set h_slot=0.3 --set w_slot=0.5 | grep -E "Hata|Sonuç"; echo "enjektor ihlal cikis kodu: ${PIPESTATUS[0]} (2 bekleniyor)"
set -e
"$D"/Enjektor_09DB01_Akis_App.exe --validate-only --text | tail -2
set +e
"$D"/Enjektor_09DB01_Akis_App.exe --validate-only --text --set h_slot=0.3 --set w_slot=0.5 | grep -E "Hata|Sonuç"; echo "enjektor akis ihlal cikis kodu: ${PIPESTATUS[0]} (2 bekleniyor)"
set -e
du -sh "$D"
