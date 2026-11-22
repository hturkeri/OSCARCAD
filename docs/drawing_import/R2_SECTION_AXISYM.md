# R2 — Eksenel simetrik KESİT görünüşleri (SECTION A-A) → çok gövdeli döndürme

Durum: 16.11.2026, r2 kopyası. Sahip olunan dosyalar aşağıda; sözleşme başlıkları (`pc/drawing/*.h`) değişmedi.

## Sorun
`tests/drawing/real/` altındaki iki çizim (swirl cup SWC-RR-001, simplex atomizer ATM-PS-001) taralı, yatay eksenli
(kesik çizgi) yarım-çap ölçülü kesitlerdir. Mevcut hat bunları **dikdörtgen çekme** (ExtrudeSingleView, puan 0.37/0.40)
olarak yorumluyordu: S2 tarama tespiti çizgileri yalnız sınır kutusuna çeviriyor, kalın/ince ayrımı ve parça ayrımı yok.

## Yaklaşım (`src/drawing/interp/SectionAxisym.cpp`)
1. **Tarama analizi.** Çizgi kalınlık sınıfları: en ince sınıf (≤1.1×min) tarama/ölçü, kalınlar görünür kenar.
   Görünür kenarlar (çizgi+yay) düzlemsel düzenlemeye (`interp/Arrangement`) girer. Aday tarama çizgisi: ince, eksenlere
   15°–75° eğik, kesiksiz; S2'nin `HatchRegion.mergedFrom` ile tükettiği ham çizgiler de `ir.raw`'dan geri okunur.
   Bir çizgi, orta noktası bir yüzün içinde ve **iki ucu da o yüzün sınırında** (≤0.15 mm) ise o yüzün taramasıdır
   (kılavuz/ölçü çizgileri böylece elenir). ≥2 çizgili yüz = malzeme; tek çizgili yüz yalnız aynı açılı taralı yüze
   komşuysa (ince cidar). Aile = (açı, aralık) — çift açı ortalaması, ofset farklarının medyanı.
   Tarama çizgileri profile hiç girmez (`hatchLineIds ∩ profilePrimIds = ∅`, test edilir).
2. **Eksen.** Aday: yatay/dikey kesik çizgi. Görünüşü = eksen boyunca uzanan taralı yüzler. Puan = ayna IoU
   (90×90 ızgara) × (1 − büyük yay oranı) — B-B/C-C enine kesitleri (yaylarla sınırlı) böylece elenir; puan ≥0.5.
   İkinci düzenleme eksen doğrusuyla kurulur → yüzler eksende bölünür; her yüzün parçası, içindeki noktayı kapsayan
   taralı yüzün ailesinden gelir. **Parça = aile**; aynı açılı fakat farklı aralıklı ve *bitişik* aileler tek parça
   sayılır (not düşülür), farklı açılı bitişik aileler ayrı parçadır (ISO 128-50).
3. **Yarım profil.** Her parça için iki yarının alanı ölçülür, **daha dolu yarı** döndürülür (atomizer: insert'in üst
   yarısında besleme oluğu kesilmiş → alt yarı). Yarılar arası ayna IoU raporlanır; <0.97 ise "eksenel simetrik olmayan
   kanal/oluk ayrı özellik olarak eklenmeli" notu. Sınır kenarları zincirlenir, eş doğrusal parçalar birleştirilir,
   parça koordinatına (x = yarıçap, y = eksenel; x=0 ön yüz) çevrilir. Parça adı "(n) AD" etiketinin kılavuz ucundan.
4. **Ölçek.** Ø metinleri × ölçülen yarıçaplar oylaması (±%1.5 pencere, ≥2 ölçü) → parça/kağıt. Yoksa antet, yoksa
   S3 `scaleFromDims`. Antet çelişirse not (her iki PDF A3 değil 172 mm sayfaya basılmış: antet 10:1/2:1 yanlış ölçek verir).
5. **Ölçüler → kısıtlar** (sketch XZ düzlemi, Revolve ekseni V): Ø → `DistanceX(orijin, p) = D_xx/2` (yarıçapta
   eksenel çizgi, yoksa nokta; aynı Ø iki parçada → ortak parametre); `a x 45°` → pah bacakları `C_xx`; eksenel uzunluk →
   metne en yakın ve değeri tutan **uzatma çizgisi çifti** (eksene dik çizgiler, metne ≤3 mm) → `DistanceY = L_xx`;
   "90°", "… 90° INCL." → yarı açısı 45° olan, metin/kılavuz ucuna en yakın koni çizgisi:
   `Angle(eksenel ref, koni) = ±A_90/2 / 180−A_90/2`, paralel cidar (flare) `Parallel`. Her kısıt yalnız DOF düşürürse
   eklenir; geometride karşılığı olup DOF düşürmeyen ölçü **referans** ([T], `Derived`) olur. Kalan DOF ölçülen
   değerlerle kapatılır (`M_R..`, `M_X..` [Ö]). Sonra `sketch::solve`.
6. **Sentez** (`src/drawing/synth/RevolveMulti.cpp`): her parça `SketchK` + `RevolveK {axis V, 360, op new, bodyName}`
   → BODIES sözleşmesine göre ayrı gövde. Doğrulama: (a) model XZ düzleminde kesilip HLR ile izdüşürülür, çizimin kalın
   kenarlarıyla iki yönlü ortalama uzaklık (model→çizim yönünde ince çizgiler de sayılır: arka plan daire kenarları
   çizimde uzatma çizgileriyle çakışıyor); (b) gövde hacmi ↔ çizilen yarım profilin Pappus hacmi (OCCT'den bağımsız).
   Çok parçalıda aynı görünüşün tek profilli Revolve/SectionRevolve adayları ×0.5 düşürülür; tek parçada genel aday
   zaten iyi izdüşüyorsa (<0.2 mm) R2 adayı onun hemen altına konur (mevcut E2E beklentileri korunur).
   Koruma: >150 kenarlı "profil" (dönel parça değil) sketch'e çevrilmez (DOF rank analizi O(n³)); yaylar gerçek yay olarak girer.

## Metrikler (fixture'lar, ctest `drawing.R2Section.*`)
| | Atomizer ATM-PS-001 | Swirl cup SWC-RR-001 |
|---|---|---|
| Önceki sonuç | ExtrudeSingleView 0.37 (yanlış) | ExtrudeSingleView 0.40 (yanlış) |
| Yeni en iyi aday | SectionRevolve 0.92, **2 gövde** NOZZLE_TIP / SWIRLER_INSERT | SectionRevolve 1.00, **1 gövde** |
| Tarama aileleri | 45°/0.381 mm (48 çizgi), 135°/0.381 mm (19) | 45°/1.75 mm (50; flare dahil) |
| Ayna IoU (kesit / parça) | 0.92 / 0.9995, 0.81 (oluk) | 1.00 |
| Ölçek (ölçülerden) | 0.24688 parça/kağıt (5 Ø); antet 10:1 → çelişki notu | 1.28937 (9 Ø); antet 2:1 → not |
| Ölçü: bağlı / referans / bağlanamayan | 12 / 2 (L_10, 2. 90°) / 1 ("0.5" oluk genişliği, kesilmemiş yarıda) | 20 / 4 (22, 30, üst 6, flare 90°) / 0 |
| DOF / max sapma | 0 / 0.008 mm | 0 / 0.015 mm |
| HLR yeniden izdüşüm | 0.047 mm (çizimde 8 % örnek: insert oluğu) | 0.0006 mm |
| Hacim (model / Pappus) | 364.27 / 113.88 mm³ (fark 0.08 %) | 30075.5 mm³ (0.04 %) |

### Elle hacim tahmini (testte ±%1)
* **Nozzle tip:** Ø8×10 silindir 502.65 − pah halkası (0.125 mm² × 2π×3.833) 3.01 − Ø5×5.8 delik 113.88 − Ø3.2×2 hazne 16.08
  − çıkış konisi (r 0.4→1.6, 1.2 boy) 4.22 − orifis Ø0.8×0.4 0.20 − giriş konisi (r 1→0.4, 0.6 boy) 0.98 = **364.28 mm³**.
* **Swirler insert:** π·2.5²·5.8 = **113.88 mm³** (teğetsel kanallar/oluklar hariç — R3 kesim özellikleri).
* **Swirl cup:** nominal yarım profil (x, r): (0,25)(19,25)(19,16)(22.5,16)(30,23.5)(30,21.5)(22,13.5)(16,13.5)(16,17)
  (10,17)(10,11)(21,9.5)(21,8.5)(15,7)(13,7)(8,12)(8,14.14)(3,14.14)(3,5)(0,5); alan 275.17 mm², V = 2π∬r dA =
  **30075.5 mm³**. 14.14 ölçüsüz (ölçülen [Ö]). Kesit düzlemi birincil/ikincil kanalları kestiği için bu profil kanal
  bölgelerinde halka oluk içerir (not 6: kanallar ayrı extrude-cut, R3).

### R4 referans modelleriyle karşılaştırma (kanal kesimi ÖNCESİ döndürme profili)
Kaynak: `r4/samples/ref_*.pcad`, `r4/docs/drawing_import/R4_REFERENCE.md` (referans hacimler kanallar dahil).
| | R2 döndürme | R4 profili (Pappus, kesimsiz) | R4 son model | Yorum |
|---|---|---|---|---|
| NozzleTip | 364.27 mm³ | 364.27 (aynı 10 nokta) | 364.27 | birebir |
| SwirlerInsert | 113.88 | 113.88 (Ø5×5.8) | 107.57 | fark = 3 yuva + 3 oluk (R3) |
| SwirlCup | 30075.5 | 30954.2 | 26288.4 | 19 noktanın 18'i aynı; fark x 3..8 birincil hazne: R2 A-A'daki çizgiyi (r=14.14, kanal duvarının kesit düzlemiyle kesişimi) alıyor, R4 B-B/not 5'teki Ø24'ü (r=12). Halka 5×2.14 mm², 879 mm³. Kanal kesimleri (R3) bu halkayı zaten boşaltır; Ø24 için B-B'deki "Ø24" kılavuzunun kesite taşınması gerekir (açık iş). |
Gövde sayıları (1 / 2), Ø50×30 ve Ø8×10 zarfları referansla aynı.

## Fixture'lar
`tests/drawing/fixtures/r2/{atomizer_ATM-PS-001,swirlcup_SWC-RR-001}.pcdrw.json` = gerçek PDF'in S1 ingest + S2 normalize
(`arrowMinMm=0.5`: ok uçları 0.9 mm), glif konturları atıldı, `ir.raw` yalnız S2 taramasının tükettiği çizgiler.
Ölçü/etiket TextRun'ları PNG'den elle, glif kümesi kutularına yerleştirildi: `<ad>.texts.json` (r1 glif-OCR yok).
Yeniden üretim: `pc_drawing_tests --gtest_also_run_disabled_tests --gtest_filter=R2Tool.DISABLED_MakeFixtures`
(`R2Tool.DISABLED_Dump` tanılama dökümü; `R2_DEBUG=1`, `R2_DUMP_REPROJ=<dosya>` geliştirici çıktıları).

## Dosyalar
Yeni: `src/drawing/interp/SectionAxisym.{h,cpp}`, `src/drawing/synth/RevolveMulti.{h,cpp}`,
`tests/drawing/test_section_axisym.cpp`, `tests/drawing/fixtures/r2/*` (2 fixture + 2 texts.json), bu belge.
Değişen (kanca): `src/drawing/interp/Pipeline.cpp`, `src/drawing/synth/Synthesize.cpp`.

### Kanca satırları
`src/drawing/interp/Pipeline.cpp`:
```cpp
#include "interp/SectionAxisym.h"   // R2 hook
...
      case Stage::Sketch: {
        r = reconstructSketches(ir, opts.interp, prog);
        // R2 hook (docs/drawing_import/R2_SECTION_AXISYM.md): axisymmetric SECTION view (hatch + centreline) ->
        // one half-profile sketch per hatch family (part); runs even if the generic S6 found nothing.
        StageResult r2s = StageResult::success("Eksenel simetrik kesit: parça profilleri");
        if (r2::addAxisymSectionSketches(ir, opts.interp, r2s) > 0 && !r.ok()) r = r2s;
        else r.diagnostics.insert(r.diagnostics.end(), r2s.diagnostics.begin(), r2s.diagnostics.end());
        break;
      }
```
`src/drawing/synth/Synthesize.cpp` (sıralamadan hemen önce):
```cpp
#include "synth/RevolveMulti.h"   // R2 hook
...
  // R2 hook (docs/drawing_import/R2_SECTION_AXISYM.md): part profiles of an axisymmetric section -> one body per part
  r2::addRevolveMultiCandidate(ir, opt, res);
```
IR çıktıları: `SketchCandidate::purpose = "revolve-half-profile-part"` (+ `r2.part`, `r2.partName`, `r2.side`,
`r2.mirrorIoU`, `r2.hatch`, `r2.pappusVolume`, `r2.constraintText.<cid>`), `ir.attrs["r2.section.*"]`,
`SynthesisCandidate.attrs["r2.bodies"|"r2.body.<i>.*"|"r2.reproj.*"]`.

## Test durumu
`ctest -j2` (282 test): tümü geçti (R2Section 4 yeni test; E2ECorpus süreleri temel değerde, ~13 s).

## Sınırlar
* Kesitte kalan kanal/oluk boşlukları döndürülünce halka oluk olur; kanal özellikleri (R3) bunları kapatmalı/kesmeli.
* Bir sayfada yalnız en iyi eksenel kesit işlenir. Dikey eksen desteklenir ama gerçek çizimle denenmedi.
* "WALL 2" gibi cidar kalınlığı notu kısıta çevrilmez (paralellik + Ø43/Ø47 ile tanımlı).

## Integration (16.11.2026)
* Cross-view diameters: Ø texts around the transverse views (circle of that view) and named note diameters
  (`Ø14 THROAT`) bind profile radii that the section does not dimension (`D_24`, `D_34`; redundant ones -> [T] reference,
  e.g. `D_14`), so no [Ö] `M_R..` remains for them.
* Slot bands: an axial section line inside a through-wall slot band of a transverse view (x0..x1, rIn < r < rOut,
  rIn dimensioned in that view) is the slot wall crossing the cutting plane -> the revolve takes the dimensioned bore
  (SWC-RR-001: r 14.14 -> Ø24/2, note in the warnings). Cut model reproduces the A-A line (drawing->model 0.011 mm).
* Section caption = nearest SECTION text that is not a transverse view's caption (was: last SECTION text on the sheet).
* Axial dims equal to an R3 slot depth (atomizer "0.5", x 4.2..4.7) are recorded as [T] references of the slot.
* Reference texts are recorded (`r2.referenceText.<param>`) so the wizard does not list them twice.
