# 09-DB01 simplex atomizör modeli — bağımsız doğrulama raporu

Bu rapor modeli kuran ekipten bağımsız bir inceleme sonucudur. Modelin kendi testlerine ve `SPEC.md`'deki köşe
tablolarına güvenilmemiş; kaynak çizimin vektörleri yeniden çıkarılmış, model kesiti OCCT ile hesaplanıp çizimin üzerine
bindirilmiştir.

## 1. Yöntem

1. **Çizim**: `ef502211-09-DB01-4A-E.pdf` sayfa 1, `pdftocairo -svg` ile vektöre çevrildi (4079 yol; kenar çizgileri
   kalınlık 8.37, ince çizgiler 5.98, tarama 1.79 birim). Birim 1/600 inç.
2. **Kalibrasyon (yeniden ölçüldü)**: ölçü uzatma çizgileri y = 3249 / 4242 / 4773 / 5694 px ↔ z = 14 / 0 / −7.5 / −20.5 mm
   → 70.93 / 70.80 / 70.83 px/mm; nominal 3:1 = 70.866 px/mm kabul edildi (kalıntı ≤ 0.02 mm). Eksen x = 1884 px.
   Ø16 uzatma çizgisi 15.69 mm verir (tolerans ortası). `SPEC.md` §1 kalibrasyonu doğrulandı.
3. **Model kesiti**: `pc_kesit samples/enjektor_09DB01.pcad xz.json --edges` — her gövde için `BRepAlgoAPI_Section`
   (y = 0 düzlemi) + tüm kenarların 3B çoklu doğruları (dış görünüş izdüşümü için), 0.002 mm sapma ile.
4. **Bindirme ve ölçüm**: `bindirme.py` çizim vektörlerini siyah, model kesitini gövde başına renkli çizer; iki yönde
   en yakın uzaklık istatistiği verir (model → çizim ve çizim → model). Görseller tek tek incelendi.
5. **3B kontroller** (`tests/test_enjektor_dogrulama.cpp`): slot yüzeylerinin türü/konumu, akış hacmi boğaz kesitleri
   (katı ∩ düzlem yaması alanı), pencere azimutu (nokta sınıflandırma).

Yeniden üretmek için:

```
pdftocairo -svg -f 1 -l 1 ef502211-09-DB01-4A-E.pdf p1.svg
pdftoppm -r 600 -f 1 -l 1 -png ef502211-09-DB01-4A-E.pdf p600
build/bin/pc_kesit samples/enjektor_09DB01.pcad xz.json --edges
python3 docs/enjektor/dogrulama/bindirme.py p1.svg p600-1.png xz.json docs/enjektor/dogrulama
```

## 2. Sapma tablosu

**Model → çizim** (model kesitinin 0.05 mm aralıklı her noktasının en yakın çizim çizgisine uzaklığı, sağ yarı):

| Gövde | Nokta | Ort. [mm] | %95 [mm] | En büyük [mm] | En büyük sapmanın yeri (r, z) |
|---|---:|---:|---:|---:|---|
| Govde (baş) | 1150 | 0.003 | 0.005 | 0.009 | (6.17, 11.60) kubbe yayı |
| Swirler | 282 | 0.004 | 0.007 | 0.053 | (3.72, 8.17) R0.2 köşe → pah |
| TutmaVidasi | 529 | 0.002 | 0.004 | 0.042 | (2.00, 8.00) delik ağzı kırması |
| Filtre | 703 | 0.002 | 0.004 | 0.007 | (4.95, −18.71) |
| Elek | 370 | 0.001 | 0.001 | 0.003 | (4.74, −16.71) |

**Slot izdüşümü** (çizimdeki "Slot" detayının 201 vektör noktası ↔ modelin izleyici tarafındaki swirler kenarlarının
izdüşümü): ort. 0.021, %95 0.031, **en büyük 0.035 mm** (çizgi kalınlığı 0.12 mm).

**Çizim → model** (çizimin kalın kenar çizgilerinden 4603 nokta): %93.8'i modele ≤ 0.04 mm. Kalan 284 noktanın tamamı
aşağıdaki bilinen kalemlerdir; modelde karşılığı olmayan başka çizgi **yoktur**:

| Yer (r, z) | Sapma | Açıklama |
|---|---:|---|
| (0.2, 6.3…7.3) | ≤ 0.63 | R8 merkez işareti (ölçü geometrisi, parça değil) |
| (4.42, −0.9…−5.6) | 0.32 | Baş deliğinin diş dibi çizgisi: modelde diş kozmetik, delik kavrama boyunca erkek diş üstüne (Ø9.48) açılmış |
| (4.76, 3.65) | 0.17 | İç diş bitiş işareti (z = 3.65…4.25) — modellenmedi |
| (0…2.65, −2.7…−3.05) | ≤ 0.20 | Alyan yuvası ağız havşasının eğrileri — modellenmedi |
| (4.52, −6.81) | 0.064 | Baş deliği ağız pahı / filtre konisi teması (H20) |
| (3.77, 8.14) | 0.056 | Swirler etek köşesi R0.2 (modelde 0.2 × 45° pah) |
| (0.3…1.9, 13.91) | 0.044 | Çıkış havşası köşe yuvarlatmasının (R0.08) siluet çizgisi |

Hüküm: döner profillerin tamamı çizimle **≤ 0.01 mm (%95)**, en kötü noktada 0.05 mm içinde çakışıyor; slot 0.035 mm
içinde. Bu, çizimin okunabilirlik sınırıdır (vektör çözünürlüğü 0.014 mm).

## 3. Görseller

| Dosya | İçerik | Gözlem |
|---|---|---|
| `01_tum_parca.png` | Tüm kesit, 5 gövde | Görünür fark yok |
| `02_bas_orifis_girdap_odasi.png` | Orifis Ø0.36 × 0.28, trompet R0.48, havşa, 69° girdap odası, uç düzlüğü (10×) | Trompet yayı ve havşa köşesi çizgi içinde |
| `03_swirler_koni_slot.png` | 110°/60° koni, yuva teması, slot izdüşümü (turuncu) | Slotun iki yüzey kenarı ve görünen taban kenarı çizimle çakışıyor; en soldaki turuncu çizgi çizimde **gizli** olan diğer taban kenarıdır (gizli çizgi ayıklaması yapılmadı) |
| `04_tutucu_vida_capraz_delik_halka.png` | Tutucu vida, Ø2.40 çapraz deliğin arakesit eğrileri, halka boşluk, alyan yuvası | Arakesit eğrileri çakışıyor; alyan ağız havşası yok |
| `05_filtre_pencere_elek.png` | Filtre gövdesi, kanallar, pencereler, elek kovanı | Profil çakışıyor; pencere hizasında sağ cidar açık |
| `06_model_ceyrek_kesit_on_gorunus.png` | Modelin çeyrek kesitli ön görünüşü (çizimle aynı bakış) | Elek, pencere bantlarında eksenden cidara kadar kesintisiz görünüyor (çizimdeki gibi); slot aynı yerde |

## 4. Bulunan ve düzeltilen kusurlar

| # | Kusur | Düzeltme |
|---|---|---|
| 1 | **Filtre pencereleri yanlış azimutta**: merkezler 315°/135° idi; çizimde eleğin kesintisiz göründüğü arka çeyrekte (0°–90°) 30°–60° arasında köprü bırakıyordu. Neden: `SPEC.md` §3.4 azimutu izleyiciyi +Y'de varsayarak, slot ise −Y'de varsayarak yazılmıştı | `pen_merkez` = 45° (pencereler −30…120° ve 150…300°; köprüler 135° ve 315°, çizimin gösteremediği yerler). `SPEC.md`, `spec.json` düzeltildi; test: `FilterWindowsOpenWhereTheDrawingShowsThem` |
| 2 | `A_slot = n·w·h` gerçek boğazı %8 fazla veriyordu (ölçülen 0.0559, tablo 0.0608 mm²) | `A_slot_etkin` eklendi ve ölçümle doğrulandı (%0.5) |
| 3 | Aralık içi değerlerle fiziksel olarak anlamsız slotlar kurulabiliyordu (h = 0.5 → taban ekseni geçer; e = 0 → girdap yok; n = 4, w = h = 0.5 → swirler ucu 3 parçaya bölünüyordu) | `h_slot` ≤ 0.3, `e_slot` ≥ 0.05; üç tasarım bağıntısı (`slot_uc_payi`, `slot_ic_payi`, `slot_dis_payi`) hata + girdi önerisi verir |
| 4 | Akış hacmi katılarla aynı dosyada görünüyor, kesitleri karıştırıyordu | Donanım / akış hacmi ayrı dosya ve ayrı uygulama |
| 5 | Yardımcı düzlemler ekran görüntülerini kirletiyordu | `Plane show=0` |
| 6 | Düzlem diyaloğu `axisAngle` türünü tanımıyordu (Dz_Slot düzenlenince kenar düzlemine dönüyor / kenar istiyordu) | Diyaloğa tür, eksen ve açı eklendi; GUI testi `gui_harness_v5` |
| 7 | Tasarımcıda yenileme ≈ 1.8 s (konsolda 0.65 s) | Neden: GUI kitaplıklarının bıraktığı kirli AVX üst yarı durumu OCCT'nin SSE kodunu 2.5–3× yavaşlatıyordu. `VZEROUPPER` (`pc/SimdState.h`) → 0.3 s (donanım), 0.6 s (akış) |
| 8 | Görüntü ağı kaba (daireler 18 kenarlı), sığdırma gevşek (model görünümün ~%45'i) | 7°/yüzey; görünür köşelere sıkı sığdırma |
| 9 | Uygulamada hesaplanan büyüklükler (d0, K, Cd, açı) görünmüyordu | "Hesaplanan değerler" kutusu |

## 5. Slotun 3B denetimi

- Slot yüzeyleri **düzlemdir** (2 duvar + taban): düz kanal. Duvarlar slot azimutundaki meridyen düzlemine paralel,
  eksenden 0.120 ve 0.280 mm uzakta → w = 0.160, e = 0.200 (ölçülen). Taban 110° koninin anadoğrusuna paralel.
- Kanal uç düzlüğünü keser; temas bandında (r 0.76…2.79) yuva konisiyle kapalıdır; r < 0.76'da girdap odasına açılır;
  arkada 60° koniden çıkarak (r ≈ 3.02) kama hacminden beslenir. Slotlar bastırılınca akış hacmi ikiye ayrılır
  (`FluidVolumeIsOneConnectedSolidThroughTheSlots`): halka boşluk ile girdap odası arasındaki **tek** bağlantı slotlardır.
- İki slot noktaya göre simetriktir, aynı dönüş yönünü verir. Dönüş yönü (orifisten bakınca saat yönünün tersi) tek
  izdüşümden okunmuştur: düşük güven.
- Danfoss'un kendi tarifiyle uyumlu: yağ filtreden girer, alt vidadan ve yan deliklerinden geçer, koniğin kenarından
  konik slotlarına, oradan girdap odasına ve orifise gider ("Facts worth knowing about oil nozzles"). "Yan delikler"
  çoğuldur: çapraz delik adedi için 2 varsayımını destekler, kanıtlamaz.

Akış hacminde **ölçülen** kesitler (varsayılan):

| Geçit | Ölçülen [mm²] | Tablo [mm²] |
|---|---:|---:|
| Tek slot, r = 2.70 / 1.80 / 0.90 / 0.79 (ağız) | 0.0297 / 0.0293 / 0.0283 / 0.0280 | w·h = 0.0304 |
| Slotlar toplam (boğaz, n = 2) | 0.0559 | `A_slot_etkin` 0.0557; anma 0.0608 |
| Orifis | 0.1018 | 0.1018 |
| Vida/baş halka boşluğu (z = 6) | 17.43 (+1.19 sap boşluğu) | 17.43 |
| Vida eksenel deliği | 11.95 | 11.95 |
| Filtre gövde deliği | 43.01 | 43.01 |

En dar iki kesit slotlar ve orifistir (A_slot/A_orifis ≈ 0.55); sıradaki en dar geçit swirler ucu ile girdap odası konisi
arasındaki halkadır (0.58 mm², orifisin 5.7 katı — `SPEC.md` §5'teki "tüm diğer geçitler ≥ 90 kat" ifadesi bu halka
için doğru değildir); kalanlar ≥ 88 kat.

## 6. Ölçülen / varsayılan

**Çizimden ölçülen (doğrulandı):** tüm döner profiller (baş, swirler, tutucu vida, filtre gövdesi, elek kovanı), orifis
Ø0.36 × 0.28, girdap odası Ø1.52 / 69°, koni açıları 110°/60°, uç düzlüğü Ø0.80, slot w = 0.16, h = 0.19, e = 0.20 ve
1. slotun azimutu, çapraz delik Ø2.40 ve kotu, alyan yuvası, pencere sıralarının z aralıkları ve 0°–90° arasının açık
olduğu.

**Varsayım (çizim göstermiyor):**

- Slot adedi (n = 2; 1–4 hepsi çizimle tutarlı) ve diğer slotların eşit aralıklı olduğu.
- Slot kesitinin dikdörtgen olduğu (V veya yuvarlak dipli olabilir).
- Çapraz delik adedi (2, boydan boya).
- Filtrede sıra başına pencere adedi (2), yay açısı (150°) ve köprülerin yeri (135°, 315°).
- Elek tel çapı (52 µm, kare örgü); elek kalınlığı 0.10 şematik.
- Swirler'ın dolu ve sapla tek parça olduğu; filtre gövdesi malzemesi.
- Akış hacminin dış sınırı (Ø11.8 × 20.5 azami zarf = brülör adaptörü deliği).
- `Cd = 0.1283`: ölçülmüş bir katsayı değil; çizilen Ø0.36'nın 1.90 kg/h @ 10 bar vermesi için geri hesaplanan kalibrasyon.

**Gösterge (doğrulanmamış bağıntı):** `Cd_RL`, `Q_RL`, `aci_RL` (Rizk–Lefebvre). Biçim ve katsayılar Lefebvre &
McDonell'deki bağıntılarla aynıdır; ancak bu kadar küçük (FN ≈ 2×10⁻⁸ m²) ve viskoz nozullar için geçerliliği
gösterilmemiştir (n = 2 ile +%30 debi verir). Tasarım kararı için kullanılmamalıdır.

## 7. Açık riskler

1. **Çizim şematiktir**: dört sayfada (0.50…60 USgal/h) iç geometri birebir aynı çizilmiş. Ø0.36, w, h, e "0.50 USgal/h
   nozulunun" ölçüleri olarak değil, temsilî geometri olarak okunmalı. Model çizime sadıktır; gerçek parçaya sadakati
   ancak parça ölçümüyle (mikroskop, pin mastar, kesit alma) kanıtlanır.
2. Slot derinliği ±0.04 mm (±%20) belirsizdir; alan ve K aynı oranda belirsizdir. Slot adedi bilinmiyor: K ve debi
   tahmini n ile doğrudan ölçeklenir.
3. Köprü/pencere düzeni ve çapraz delik adedi akış kaybını etkilemez (alanlar orifisin ≥ 88 katı) ama parça üretimi için
   doğrulanmalıdır.
4. Dişler kozmetiktir (3/8"-40 UNS, 9/16"-24 UNEF): modelden diş açılmaz, STEP'te diş profili yoktur.
5. Köşe yuvarlatmaları (R0.2, R0.08) pah / keskin köşe olarak, alyan ağız havşası modellenmemiştir (≤ 0.2 mm, akışa
   etkisiz).
6. Çizim ekranları (`sketch_Ck_Slot.png`, `sketch_Ck_Orifis.png`) ordinat ölçüleri eksenden verildiği için küçük
   görünür; geometri doğrudur, sunum zayıftır.
7. `VZEROUPPER` düzeltmesi GCC/Clang + x86 içindir (MSYS2 CLANG64 dahil); MSVC ile derlenirse etkisizdir (yavaşlık geri
   gelir, doğruluk etkilenmez). Windows'ta gerçek sürücüde ölçülmedi.
