# Simplex atomizer 09-DB01-4A-E — geometrik şartname (çizimden ölçüm)

Kaynak: `ef502211-09-DB01-4A-E.pdf` (Fluidics Instruments BV, "Simplex atomizer 09-DB01-4A-E", EN 293/299, 13-02-15, 4 sayfa).
Makine-okur eşdeğeri: `spec.json`. Doğrulama görselleri: `olcum/*.png` (üretici betik: `olcum/olcum_overlay.py`).

Her bilginin dayanağı üç etiketten biriyle verilmiştir: **[Ö]** çizimden ölçüldü, **[L]** literatürden, **[V]** mühendislik varsayımı.

## 0. Özet — çizim gerçekte ne gösteriyor

1. **PDF'deki kesit raster değil vektördür.** Gölgelendirme 250 ppi bitmap karolar, ancak tüm kenar çizgileri 600 dpi birimli vektör doğru parçalarıdır; ölçek tam 3:1 (70.866 birim/mm). Bu nedenle köşe koordinatları ±0.02 mm ile okunmuştur.
2. **İç geometri şematiktir:** sayfa 1 (0.50–0.85), 2 (1.00–1.65), 3 (1.75–11.0) ve 4 (12–60 USgal/h) kesitlerinde baş, konik, slot, orifis ve vida çizgileri birebir aynıdır (orifis her sayfada Ø0.36). Yani çizim tek bir temsilî iç geometri verir; kapasiteye göre değişen orifis/slot ölçüleri datasheet'te yoktur. Sayfalar arası tek fark: filtre notu (75 µm/200 mesh ↔ 140 µm/120 mesh) ve sayfa 4'te filtrenin hiç olmaması.
3. **Girdap elemanı (Swirler)** çift konili, dolu, saplı bir parçadır: 110° uç konisi baş içindeki 110° yuvaya metal-metal oturur; arkasında 60° koni, Ø7.60 etek ve tutucu vidanın deliğine giren Ø3.70 sap vardır. **Slot**, 110° koninin yüzeyine açılmış, uç düzlüğünden başlayıp 60° koniye taşarak biten **düz, dar, teğetsel (eksenden kaçık) bir kanaldır**: genişlik ≈0.16 mm, derinlik ≈0.19 mm, eksen kaçıklığı ≈0.20 mm. Çizimde görünen çeyrekte yalnız 1 slot vardır; adet çizimden belirlenemez.
4. **Tutucu vida (Ms58)** boydan boya deliktir: altta 3/16" alyan yuvası → Ø3.90 eksenel delik → **Ø2.40 radyal çapraz delik** → vidanın inceltilmiş Ø7.48 gövdesi ile başın Ø8.84 deliği arasındaki **halka boşluk**. Yakıt konik eteğinin dışından dolaşıp slotlara ulaşır. Vida, koniği yuvasına bastırır; alt ucundaki kısa 3/8"-40 dişle başa bağlıdır.
5. **Filtre** düz bir boru değildir: 3/8"-40 dişle başa vidalanan, ucu kör (118° matkap konisi), dışında iki çevresel kanal ve bu kanalların dibinde iki sıra pencere bulunan bir gövde ve üzerine geçirilmiş ince elek kovanıdır (r 4.74→4.84, boy 9.10 mm). Akış dıştan içe: elek → kanal → pencere → gövde deliği.

## 1. Koordinat sistemi ve kalibrasyon [Ö]

- Eksen: `r` yarıçap, `z` eksenel; **z = 0 altıgen/flanş alt yüzü** (14 ve 7.5 ölçülerinin ortak düzlemi), **+z orifise doğru**.
- 600 dpi sayfa pikselinde: sayfa 1–2 eksen x = 1884, z0 y = 4242; sayfa 3 (1867, 3300); sayfa 4 (1867, 3502). `r = (x−x_eksen)/70.8661`, `z = (y_0−y)/70.8661`.
- Ölçek, 14 / 7.25 / 7.5 / 20.5 ölçü uzatma çizgilerinden: 70.87 ± 0.07 px/mm → 3:1 kabul edildi.

| Ölçü | Nominal | Çizimden ölçülen | Fark |
|---|---:|---:|---:|
| 14 +-0.2 (tepe duzlugu - z0) | 14.0 | 14.01 | +0.01 |
| 7.25 +0/-0.1 (tepe - R8 merkezi) | 7.25 | 7.25 | +0.00 |
| 7.5 +-0.5 (z0 - govde alt yuzu) | 7.5 | 7.49 | -0.01 |
| 20.5 max (z0 - filtre ucu siniri) | 20.5 | 20.49 | -0.01 |
| R8 +0/-0.2 (kubbe yayi) | 8.0 | 7.98 | -0.02 |
| O16 +0/-0.5 (flans) | 16.0 | 15.7 | -0.30 |
| 16 +0/-0.25 A/F (ust gorunus) | 16.0 | 15.85 | -0.15 |
| 9/16-24 UNEF-2A dis cap | 14.29 | 14.16 | -0.13 |
| 3/8-40 UNS dis cap (uzatma cizgisi) | 9.525 | 9.51 | -0.02 |
| 3/8-40 ic dis dibi (govde deligi) | 8.84 | 8.84 | +0.00 |
| O11.8 max (eksen isaretleri) | 11.8 | 11.8 | +0.00 |
| SW 3/16 (kose-kose/2 = 2.749) | 2.749 | 2.75 | +0.00 |

Yorum: toleranslı ölçüler çizimde tolerans ortasına çizilmiş (Ø16 −0.5 → 15.70; 16 A/F −0.25 → 15.85; R8 −0.2 → 7.98; 9/16" diş 2A sınıfı → 14.16). Eksenel ölçülerde kalıntı ≤0.01 mm. "Ø11.8 max" ve "20.5 max" fiziksel kenar değil **azami zarf** ölçüleridir: çizilen filtre flanşı Ø10.30, çizilen filtre ucu z = −18.71 (zarf z = −20.49).

**Ölçüm belirsizliği:** vektör köşeleri ±0.02 mm; yay/teğet geçiş noktaları ±0.05 mm; slot ölçüleri ±0.03–0.05 mm (izdüşümden geri çözüm). Bindirme kontrolünde (`olcum/01…06`) tüm profiller çizgi kalınlığı (0.12 mm) içinde kalmıştır (<0.05 mm).

## 2. Parça profilleri (revolve eskizleri için kapalı (r,z) çokgenleri) [Ö]

Tablolardaki açıklama sütunu `spec.json`dan üretildiği için ASCII yazımlıdır. Köşeler sırayla birleştirilir, son köşe ilke bağlanır. Aksi belirtilmedikçe kenarlar doğrudur.

### 2.1 Baş (Head, 1.4005) — dış + iç kavite tek kapalı profil

| No | r [mm] | z [mm] | Açıklama |
|---|---:|---:|---|
| H1 | 0.18 | 13.24 | orifis girisi (girdap odasi konisi sonu) |
| H2 | 0.18 | 13.52 | orifis silindirik boyu sonu |
| H3 | 0.27 | 13.80 | cikis trompeti sonu; H2-H3 yay R0.48 merkez (0.66,13.52) |
| H4 | 1.91 | 13.80 | cikis havsasi tabani |
| H5 | 1.99 | 13.87 | havsa kose radyusu R0.08 |
| H6 | 1.99 | 13.96 | havsa duvari |
| H7 | 2.05 | 14.01 | 0.05 pah, tepe duzlugu |
| H8 | 3.17 | 14.01 | tepe duzlugu kenari; H8-H9 yay R7.98 merkez (-0.19,6.77) |
| H9 | 7.79 | 7.01 | kubbe sonu / altigen ust pahi baslangici |
| H10 | 9.14 | 5.87 | altigen kose (ust pah 40 derece konisi) |
| H11 | 9.14 | 1.59 | altigen kose alt |
| H12 | 7.85 | 0.51 | alt pah sonu = flans ust |
| H13 | 7.85 | 0.00 | flans alt (O15.70, conta yuzu) |
| H14 | 6.15 | 0.00 | boyun |
| H15 | 6.15 | -1.50 | boyun sonu |
| H16 | 7.08 | -2.43 | 45 derece pah, 9/16-24 dis baslangici |
| H17 | 7.08 | -6.49 | dis sonu |
| H18 | 6.08 | -7.49 | 45 derece pah |
| H19 | 5.19 | -7.49 | alt yuz |
| H20 | 4.42 | -6.70 | ic pah, delik agzi |
| H21 | 4.42 | 9.41 | delik O8.84 (3/8-40 ic dis dibi capi) sonu |
| H22 | 4.29 | 9.48 | kademe |
| H23 | 4.29 | 9.93 | O8.58 kisa silindir sonu = 110 derece yuva konisi baslangici |
| H24 | 0.76 | 12.40 | yuva konisi sonu = girdap odasi konisi (69 derece) baslangici |

- Yay H2→H3: R0.48, merkez (0.66, 13.52) — orifis çıkış trompeti.
- Yay H8→H9: R7.98, merkez (−0.19, 6.77) (çizimde yay merkezi eksenin 0.19 mm karşı tarafında). Sadeleştirme: eksen merkezli R7.77, merkez (0, 6.89), hata <0.03 mm.
- Türetilen açılar: yuva konisi H23–H24 yarı açı 55.0° (**110° tam açı**); girdap odası konisi H24–H1 yarı açı 34.6° (**69° tam açı**), Ø1.52 → Ø0.36, yükseklik 0.84; orifis Ø0.36 × 0.28 (L/d = 0.78); çıkış havşası Ø3.98 × 0.21 derin (kubbe tepesindeki "siyah çubuk"); altıgen üst/alt pah konileri yatayla ≈40°.
- H9–H12 arası döndürme profili altıgenin köşe zarfıdır (köşe r = 9.14); altıgen ayrıca kesilir (bkz. §3.1).

### 2.2 Girdap elemanı / konik (Swirler, 1.4005) — dolu döner gövde

| No | r [mm] | z [mm] | Açıklama |
|---|---:|---:|---|
| S1 | 0.00 | 12.66 | uc duzlugu merkezi |
| S2 | 0.40 | 12.66 | uc duzlugu kenari O0.80 |
| S3 | 2.79 | 10.98 | 110 derece ust koni sonu (yuva temas bandi r 0.76..2.79) |
| S4 | 3.78 | 9.27 | 60 derece alt koni sonu |
| S5 | 3.80 | 9.21 | etek ust kosesi |
| S6 | 3.80 | 8.26 | etek O7.60 |
| S7 | 3.60 | 8.06 | R0.2 kose, alt yuz |
| S8 | 2.05 | 8.06 | alt yuz ic kenari |
| S9 | 1.85 | 7.86 | R0.2 dip radyusu, sap baslangici |
| S10 | 1.85 | 4.85 | sap O3.70 |
| S11 | 1.44 | 4.16 | sap ucu pahi |
| S12 | 0.00 | 4.16 | sap ucu |

- S2–S3: yarı açı 54.9° (110° koni; baş yuvasıyla r = 0.76…2.79 bandında çakışık, temas boyu yanal 2.48 mm). S3–S4: yarı açı 30.1° (60° koni). S6–S7 ve S8–S9: R≈0.2 köşe yuvarlatmaları (doğru parçasıyla modellenebilir).
- Konik çizimde kesilmemiş (dış görünüş) verilmiştir; içinde delik yoktur [V: dolu parça].
- Uç düzlüğü (z = 12.66) girdap odası konisinin içine 0.26 mm girer; bu seviyede oda yarıçapı 0.58 → uç çevresinde 0.18 mm radyal halka açıklık.

### 2.3 Tutucu vida (Holding screw, Ms58)

| No | r [mm] | z [mm] | Açıklama |
|---|---:|---:|---|
| K1 | 1.95 | 8.06 | ust yuz ic kenari (delik O3.90) |
| K2 | 3.74 | 8.06 | ust yuz dis kenari |
| K3 | 3.74 | -0.73 | govde O7.48 sonu |
| K4 | 4.54 | -0.73 | disli flans ust yuzu |
| K5 | 4.74 | -0.95 | pah, 3/8-40 dis baslangici |
| K6 | 4.74 | -2.94 | dis sonu |
| K7 | 4.54 | -3.13 | pah, alt yuz |
| K8 | 2.38 | -3.13 | alyan yuvasi agzi (A/F/2) |
| K9 | 2.38 | -0.64 | alyan yuvasi dibi |
| K10 | 1.95 | -0.21 | 45 derece gecis, eksenel delik |

- Üst yüz z = 8.06 koniğin alt yüzüne (S7–S8) basar. Sap/delik radyal boşluğu 0.10 mm (Ø3.70 / Ø3.90).
- K8–K9 alyan yuvasının **düzlük** yarıçapıdır (2.38); altıgen cep ayrıca kesilir (§3.3).

### 2.4 Filtre gövdesi

| No | r [mm] | z [mm] | Açıklama |
|---|---:|---:|---|
| F1 | 3.70 | -3.71 | ust yuz ic kenari (delik O7.40) |
| F2 | 4.54 | -3.71 | ust yuz |
| F3 | 4.74 | -3.91 | pah, 3/8-40 dis baslangici |
| F4 | 4.74 | -5.46 | dis sonu |
| F5 | 4.29 | -5.90 | dis cikis pahi |
| F6 | 4.29 | -6.53 | boyun O8.58 |
| F7 | 4.74 | -6.91 | konik gecis |
| F8 | 4.74 | -8.10 | elek oturma bandi 1 (O9.48) |
| F9 | 4.09 | -8.65 | kanal pahi |
| F10 | 4.09 | -10.77 | kanal 1 tabani O8.18 (pencere 1: z -8.90..-10.51) |
| F11 | 4.74 | -11.30 | kanal pahi |
| F12 | 4.74 | -12.30 | elek oturma bandi 2 |
| F13 | 4.09 | -12.86 | kanal pahi |
| F14 | 4.09 | -14.96 | kanal 2 tabani (pencere 2: z -13.11..-14.70) |
| F15 | 4.74 | -15.51 | kanal pahi |
| F16 | 4.74 | -16.71 | elek oturma bandi 3 sonu |
| F17 | 5.15 | -16.71 | flans O10.30 |
| F18 | 5.15 | -18.51 | flans |
| F19 | 4.95 | -18.71 | pah |
| F20 | 0.00 | -18.71 | alt yuz merkezi |
| F21 | 0.00 | -17.92 | kor delik ucu (118 derece matkap konisi) |
| F22 | 3.70 | -15.71 | delik O7.40 silindirik sonu |

- Malzeme çizimde yok (yalnız "Strainer AISI 316" = elek). Renk koduna göre pirinç [V]; Delavan elek tipi süzgeçlerde gövde pirinçtir [L-1].
- Filtre üst yüzü (z = −3.71) ile vida alt yüzü (z = −3.13) arasında 0.58 mm boşluk vardır; conta/segman **yoktur**.

### 2.5 Elek kovanı (AISI 316)

| No | r [mm] | z [mm] | Açıklama |
|---|---:|---:|---|
| M1 | 4.74 | -7.61 |  |
| M2 | 4.84 | -7.61 |  |
| M3 | 4.84 | -16.71 |  |
| M4 | 4.74 | -16.71 |  |

- İç Ø9.48, dış Ø9.68 (çizilen kalınlık 0.10), boy 9.10 mm; üst ucu baş alt yüzünün 0.12 mm altında, alt ucu flanşa dayalı. Göz: 0.50–0.85 USgal/h için 75 µm / 200 mesh; 1.00–11.0 için 140 µm / 120 mesh; ≥12 USgal/h filtresiz [Ö: sayfa notları]. Açık alan oranı (kare örgü hesabı): 200 mesh %34.9, 120 mesh %43.7 [V].

## 3. Dönel olmayan unsurlar

### 3.1 Altıgen (baş) [Ö]
A/F = 15.85 (nominal 16 +0/−0.25), köşe-köşe 18.28; z = 0.51…7.01 aralığında, döner gövde ile **kesişim** alınır. Köşeler kesit düzleminde (0°, 60°, …). Üst görünüşte "slot işareti" yoktur; yalnız kubbe/altıgen kesişim dairesi, tepe düzlüğü, havşa ve orifis daireleri görünür. Altıgen yüzlerinde markalama (firma logosu, debi, açı) vardır — modellenmesi gerekmez.

### 3.2 Slot(lar) (konik)
| Büyüklük | Değer | Dayanak |
|---|---:|---|
| Genişlik w | 0.16 ± 0.03 mm | [Ö] kenar A/B arası, 3B geri çözüm (uçta 0.15, koni kırığında 0.17) |
| Derinlik h (110° koni yüzeyine dik) | 0.19 ± 0.04 mm | [Ö] slot taban çizgisinin 60° koniden çıktığı nokta (r≈3.02, z≈10.59) |
| Eksen kaçıklığı e | 0.20 ± 0.05 mm | [Ö] planda slot ekseninin nozul eksenine dik uzaklığı |
| Başlangıç / bitiş | uç düzlüğü z = 12.66 → 60° konide z ≈ 10.59 | [Ö] |
| Adet n | **2** (aralık 1–4) | **[V]** — görünen çeyrekte 1 slot; n ≤ 4 için hepsi bununla tutarlı |
| Açısal konum | 1. slot ≈ 75° (kesit düzleminden, izleyiciye doğru), diğerleri eşit aralıklı | [Ö]/[V] |

Çizimdeki izdüşüm noktaları (r≡x, z): kenar A (0.23,12.66)-(0.51,11.94)-(0.86,10.98)-(0.90,10.60); kenar B (0.35,12.66)-(0.64,11.98)-(1.02,10.98)-(1.06,10.58); taban çizgisi (0.31,12.66)→(1.06,10.58) düz.

**CAD tarifi:** azimutu φ olan meridyen düzleminde 110° koninin anadoğrusunu al; buna paralel, yüzeyden h kadar içeride bir taban doğrusu çiz; w genişlikli dikdörtgen kesitli takımı bu doğru boyunca süpür ve tüm kesimi meridyen düzlemine dik yönde **e kadar paralel kaydır** (disk freze ile açılmış kaçık slot). Kanal uç düzlüğünü keser, arkada 60° koni yüzeyinden kendiliğinden çıkar. n adet, 360°/n dairesel çoğaltma. Dönüş yönü: orifis tarafından bakınca saat yönünün tersi (düşük güven; tek izdüşümden).

Slotun işlevi: temas bandı (r 0.76…2.79) boyunca kapalı kanal, r < 0.76'da girdap odasına açılır. Girişte teğetsel hız oranı ≈ e/r.

### 3.3 Tutucu vida: alyan yuvası ve çapraz delik
- Alyan yuvası [Ö]: A/F 4.76 (3/16"), köşe-köşe 5.50, ağız z = −3.13, dip z = −0.64 (derinlik **2.49**), köşeler kesit düzleminde; ağızda ≈0.35×45° havşa. Dipten Ø3.90 deliğe 45° konik geçiş (K9–K10). Yuva deliğe **açıktır** (kör değil).
- Çapraz delik [Ö]: Ø2.40, ekseni radyal ve kesit düzleminde (azimut 0°), merkez z = 3.06 (z 1.86…4.26). Çapın doğrulaması: iç/dış arakesit eğrilerinin orta noktaları r = 1.54 ve 3.56 = √(R²−1.2²) ile birebir uyuşuyor. Adet **2 (0° ve 180°, boydan boya delinmiş) [V]** — kesitte yalnız sağ yarı görünür.
- Koniğin sap ucu z = 4.16'dadır; çapraz deliğin üst kenarının 0.10 mm altında kalır, yani delik ağzını kapatmaz.

### 3.4 Filtre pencereleri
- [Ö] İki sıra: z = −8.90…−10.51 (yükseklik 1.61) ve z = −13.11…−14.70 (1.59); cidar r 3.70→4.09. Pencereler çevresel kanalların (taban Ø8.18, 50° pahlı yanaklar) dibindedir; kanal sayesinde eleğin pencere dışındaki bölgesi de çalışır.
- Azimut kuralı (model eksenleri): +X = kesit düzleminin sağ yarısı (0°); izleyici −Y tarafındadır (slot −75°'de, izleyiciye doğru); arkadaki sağ çeyrek 0°–90°'dir.
- [Ö] Pencere hizasında kesit düzleminin sağ cidarı (0°) ve **arkadaki çeyrek (0°–90°) tamamen açık**: elek, eksenden r = 3.7'ye kadar kesintisiz görünür (`dogrulama/05_filtre_pencere_elek.png`). Dış görünüşte ön-sol (180°–270°) açık.
- **[V]** Sıra başına 2 pencere × 150°, merkezleri **45° ve 225°**; 30°'lik iki köprü 135° (dış görünüşün arkası) ve 315°'de (çıkarılan çeyrek) — çizimin gösteremediği bölgeler. Köprü sayısı/konumu çizimden doğrulanamaz. *(Bağımsız inceleme düzeltmesi: önceki sürümdeki 315°/135° merkezli yerleşim, çizimde açık görünen 30°–60° bölgesinde köprü bırakıyordu.)*

## 4. Dişler [Ö]

| Parça | Diş | z aralığı | Çizimdeki çap | Not |
|---|---|---|---:|---|
| Baş (dış) | 9/16"-24 UNEF-2A | −2.43 … −6.49 | Ø14.16 | üstte Ø12.30 boyun (z 0…−1.50) + 45° pah; altta 1×45° pah |
| Baş (iç) | 3/8"-40 UNS | −6.70 … +3.65 (çıkış 4.25'e kadar) | diş dibi Ø8.84, diş üstü Ø9.52 | üstü z = 9.41'e kadar düz Ø8.84 delik |
| Tutucu vida (dış) | 3/8"-40 UNS | −0.95 … −2.94 | Ø9.48 | ≈3 diş |
| Filtre gövdesi (dış) | 3/8"-40 UNS | −3.91 … −5.46 | Ø9.48 | ≈2.5 diş; altında Ø8.58 boşaltma |

## 5. Akış yolu (0.50 USgal/h = 1.90 kg/h, 10 bar, 817 kg/m³ → Q = 646.0 mm³/s)

Sıra: brülör adaptörü hacmi → elek (dıştan içe) → çevresel kanallar → pencereler → filtre gövde deliği → alyan yuvası → vidanın eksenel deliği → çapraz delik(ler) → vida/baş halka boşluğu (yukarı) → konik eteği çevresi → 60° koni ile 110° yuva arasındaki kama hacim → slot(lar) → girdap odası → orifis → çıkış havşası.

| # | Geçit | Alan [mm²] | Ort. hız [m/s] | Dayanak |
|---|---|---:|---:|---|
| 1 | Elek (200 mesh, 75 um) - kanal ustu serbest elek alani — elek acik alan orani 0.349; 120 mesh/140 um icin 0.437 | 66.58 | 0.0097 | olculen+hesap |
| 2 | Filtre pencereleri (2 sira) — 2 sira x 2 pencere x 150 derece | 65.35 | 0.0099 | VARSAYIM (pencere acisi) |
| 3 | Filtre govdesi deligi O7.40  | 43.01 | 0.015 | olculen |
| 4 | Filtre ustu - vida alti araligi (0.58 mm)  — alyan agzi cevresinde silindirik gecis; delik zaten dogrudan alyan yuvasina bakar | 9.111 | 0.0709 | olculen |
| 5 | Alyan yuvasi 3/16" (4.76 A/F)  | 19.62 | 0.0329 | olculen |
| 6 | Vida eksenel deligi O3.90 — z=-0.21..4.16 arasi bos; ustunde konik sapi (O3.70) | 11.95 | 0.0541 | olculen |
| 7 | Capraz delik(ler) O2.40 x2 — tek delik ise 4.52 | 9.048 | 0.0714 | cap olculen, adet VARSAYIM |
| 8 | Halka bosluk: vida O7.48 / govde deligi O8.84 — z=-0.73..8.06; alt kismi dis acilmis (dis dibi 8.84 alindi) | 17.43 | 0.0371 | olculen |
| 9 | Halka bosluk: konik etegi O7.60 / delik O8.84 — z=8.26..9.21 | 16.01 | 0.0403 | olculen |
| 10 | Kademe O8.58 / 60 derece koni (z=9.48) — sonra konik kama boslugu r=2.79 temas cizgisine kadar daralir | 15.73 | 0.0411 | olculen |
| 11 | Slotlar: n x w x h = 2 x 0.16 x 0.19 — tek slot 0.0304 mm2; EN DAR KESIT (olcum kesiti) | 0.0608 | 10.6 | w,h olculen; n VARSAYIM |
| 12 | Girdap odasi girisi O1.52 (koni tabani) — konik uc (O0.80 duzluk) ile 69 derece koni arasi halka, z=12.66 | 0.7578 | 0.853 | olculen |
| 13 | Orifis O0.36, L=0.28 — L/d=0.78; cikis trompeti O0.54, havsa O3.98 x 0.21 | 0.1018 | 6.35 | olculen (sematik) |

Sonuç: slotlar dışındaki tüm geçitler orifis alanının ≥ 90 katıdır (basınç kaybı ihmal edilir); **ölçüm kesitleri slot ve orifistir**. Bu sıra Danfoss'un tarifiyle aynıdır: filtre → yan delikli alt vida → konik kenarı → konik slotları → girdap odası → orifis [L-2].

**Kapasiteyle ölçekleme:** 1–10 numaralı geçitler ve tüm dış ölçüler sabittir (standart gövde). Yalnız d_o, L_o, D_s, n·w·h (ve gerekirse e) değişir; ilk yaklaşım A_o ∝ ṁ, A_p ∝ ṁ (sabit C_d ve K):

| USgal/h | kg/h | d_o [mm] (sabit C_d=0.128) | A_p [mm²] (A_p ∝ ṁ, 0.50 için n·w·h = 0.0608) |
|---:|---:|---:|---:|
| 0.5 | 1.9 | 0.36 | 0.0608 |
| 0.55 | 2.09 | 0.378 | 0.0669 |
| 0.6 | 2.28 | 0.394 | 0.073 |
| 0.65 | 2.47 | 0.41 | 0.079 |
| 0.75 | 2.82 | 0.439 | 0.0902 |
| 0.85 | 3.2 | 0.467 | 0.1024 |
| 1.0 | 3.77 | 0.507 | 0.1206 |
| 1.65 | 6.18 | 0.649 | 0.1978 |
| 2.0 | 7.49 | 0.715 | 0.2397 |
| 5.0 | 18.7 | 1.129 | 0.5984 |
| 11.0 | 41.2 | 1.676 | 1.3184 |

(Not: 5 USgal/h üzerinde d_o çizilen D_s = 1.52'yi geçer; büyük kapasitelerde girdap odası ve konik uç da büyütülmelidir — çizim bunu göstermez.)

## 6. Boyutlandırma bağıntıları

- Kütle debisi: ṁ = C_d·(π d_o²/4)·√(2ρΔP). 0.50 USgal/h için C_d·A_o = 0.01306 mm²; çizilen d_o = 0.36 ile **C_d = 0.1283**. Akış sayısı FN = ṁ/√(ΔP·ρ) = 1.85×10⁻⁸ m².
- Rizk–Lefebvre: C_d = 0.35·K^0.5·(D_s/d_o)^0.25, K = A_p/(D_s·d_o), A_p = n·w·h [L-3].
- Giffen–Muraszew (viskoz olmayan): K² = (π²/32)(1−X)³/X², C_d = 1.17·√((1−X)³/(1+X)), X = hava çekirdeği alan oranı; sin θ = (π/2)·C_d / (K(1+√X)) [L-3].
- Rizk–Lefebvre püskürtme açısı: 2θ = 6·K^−0.15·(ΔP·d_o²·ρ/μ²)^0.11; film kalınlığı t = 2.7·[d_o·FN·μ/√(ΔP·ρ)]^0.25 [L-3].
- Tipik oranlar [L-3]: L_o/d_o ≈ 0.5–1 (çizim 0.78), D_s/d_o ≈ 2.7–5 (çizim 4.2), A_p/(D_s·d_o) ≈ 0.05–0.3.

Çizilen geometri (d_o 0.36, D_s 1.52, w 0.16, h 0.19) ile Rizk–Lefebvre tutarlılık kontrolü:

| n_slot | A_p [mm²] | K=A_p/(D_s·d_o) | C_d (R–L) | ṁ @10 bar [kg/h] | USgal/h | 2θ (R–L) [°] |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.0304 | 0.0556 | 0.118 | 1.75 | 0.46 | 56.1 |
| 2 | 0.0608 | 0.1111 | 0.167 | 2.48 | 0.65 | 50.5 |
| 3 | 0.0912 | 0.1667 | 0.205 | 3.03 | 0.8 | 47.6 |
| 4 | 0.1216 | 0.2222 | 0.237 | 3.5 | 0.92 | 45.5 |

Yorum: çizilen ölçüler 0.5–0.9 USgal/h sınıfıyla tutarlıdır (yani uydurma değil, mertebesi doğru bir geometri); 0.50 değeri n = 1 ile −8 %, n = 2 ile +30 % çıkar. Korelasyon bu kadar küçük ve viskoz nozullar için doğrulanmış değildir, bu yüzden slot adedi buradan **kanıtlanamaz**. Öneri: varsayılan n = 2, w = 0.16, h = 0.19 (çizime sadık); debi hedefi parametrik tutturulacaksa h = A_p/(n·w), A_p = D_s·d_o·[C_d/(0.35 (D_s/d_o)^0.25)]² (0.50 için A_p = 0.036 mm² → n = 2'de h = 0.11). 2θ için R–L formülü K'ya çok zayıf bağlıdır; 45°/60°/80° varyantları pratikte e/D_s ve oda geometrisiyle ayarlanır — püskürtme açısı parametre olarak açılacaksa "türetilen/gösterge" olarak verilmesi önerilir.

## 7. Önerilen parametre listesi

| Parametre | Varsayılan | Aralık | Tür | Not |
|---|---:|---|---|---|
| `m_dot_kg_h` | 1.9 | [1.9, 41.2] | tasarim girdisi | EN 293 tablosu; 12 USgal/h ustu filtresiz (sayfa 4) |
| `dP_bar` | 10 | [7, 20] | tasarim girdisi |  |
| `rho_kg_m3` | 817 | — | sabit (test yagi ISO 4113) |  |
| `nu_cSt` | 3.5 | — | sabit (test yagi) |  |
| `d_o` | 0.36 | [0.2, 2.0] | tasarim degiskeni | orifis capi |
| `L_o` | 0.28 | [0.15, 1.0] | tasarim degiskeni | L_o/d_o ~0.8 |
| `D_s` | 1.52 | [1.0, 4.0] | tasarim degiskeni | girdap odasi giris capi (H24) |
| `alpha_s_deg` | 69.2 | [60, 120] | tasarim degiskeni | girdap odasi koni tam acisi |
| `n_slot` | 2 | [1, 4] | tasarim degiskeni (VARSAYIM) |  |
| `w_slot` | 0.16 | [0.1, 0.8] | tasarim degiskeni |  |
| `h_slot` | 0.19 | [0.1, 0.8] | tasarim degiskeni |  |
| `e_slot` | 0.2 | [0.0, 0.6] | tasarim degiskeni | teğetsel kaciklik; e <= D_s/2 - w/2 |
| `seat_cone_deg` | 110 | — | sabit (cizim) |  |
| `back_cone_deg` | 60 | — | sabit (cizim) |  |
| `d_tip` | 0.8 | [0.5, 2.0] | turetilen/tasarim | konik uc duzlugu capi |
| `d_recess` | 3.98 | — | sabit (cizim) |  |
| `h_recess` | 0.21 | — | sabit (cizim) |  |
| `AF_hex` | 15.85 | — | standart (EN 293: 16 +0/-0.25) |  |
| `H_head` | 14.0 | — | standart (14 +-0.2) |  |
| `R_dome` | 7.98 | — | standart (R8 +0/-0.2) |  |
| `z_dome_c` | 6.75 | — | standart (H_head-7.25) |  |
| `D_flange` | 15.7 | — | standart (O16 +0/-0.5) |  |
| `L_thread_end` | 7.5 | — | standart (7.5 +-0.5) |  |
| `thread_ext` | 9/16-24 UNEF-2A | — | standart |  |
| `thread_int` | 3/8-40 UNS | — | standart |  |
| `D_bore` | 8.84 | — | sabit (dis dibi) |  |
| `D_screw_body` | 7.48 | — | sabit (cizim) |  |
| `d_screw_bore` | 3.9 | — | sabit (cizim) |  |
| `d_cross` | 2.4 | — | sabit (cizim) |  |
| `n_cross` | 2 | — | VARSAYIM |  |
| `AF_socket` | 4.76 | — | standart (SW 3/16") |  |
| `filter_mesh_um` | 75 | [75, 140] | secim: 75 um/200 mesh (0.50-0.85), 140 um/120 mesh (1.00-11.0), yok (>=12) |  |
| `L_filter_max` | 20.5 | — | standart (max zarf; cizilen uc z=-18.71) |  |
| `D_filter_max` | 11.8 | — | standart (max zarf; cizilen flans O10.30) |  |

## 8. Literatür

- [L-1] Delavan, *A Total Look at Oil Burner Nozzles*: yakıt süzgeçten geçer, distribütördeki teğetsel slotlardan girdap odasına girer, orifisten içi boş koni film olarak çıkar; küçük debilerde slotlar daha küçüktür; elek süzgeçler pirinç gövde + monel elek; 0.50–2.00 GPH sinter, 2.25–15 GPH 120 mesh, ≥16 GPH süzgeçsiz. https://delavan.com/wp-content/uploads/2022/06/Delavan-Total-Look-Book.pdf
- [L-2] Danfoss, *Facts worth knowing about oil nozzles* (AG045286452015): filtre → yan delikli alt vida → konik kenarı → konik slotları → girdap odası → orifis; 1.50–11.0 USgal/h 140 µm monel elek, ≥12 USgal/h filtresiz (Fluidics sayfa 3–4 ile aynı sınırlar). https://assets.danfoss.com/documents/latest/100682/AG045286452015en-010101.pdf
- [L-3] A. H. Lefebvre & V. G. McDonell, *Atomization and Sprays*, 2. baskı, böl. 5 ve 7 (Rizk–Lefebvre, Giffen–Muraszew bağıntıları) — formüller bellekten yazılmıştır, katsayılar kullanılmadan önce kaynaktan teyit edilmelidir.
- Slot adedini veren bir Fluidics/Danfoss/Delavan kaynağı bulunamadı.

## 9. Açık belirsizlikler

1. **Slot adedi** (1–4) ve diğer slotların açısal konumu — çizimde tek slot görünür. [V: n = 2]
2. **İç geometrinin kapasiteye bağlılığı** — dört sayfada aynı çizim; d_o = 0.36, w, h değerleri "0.50 USgal/h'e ait" olarak değil "temsilî" olarak okunmalıdır. Gerçek parçada ölçüm (mikroskop/pin gauge) gerekir.
3. Slot derinliği ve kaçıklığı tek izdüşümden çözülmüştür (±0.04–0.05 mm); slot kesiti dikdörtgen varsayılmıştır (V veya yuvarlak dipli olabilir).
4. Çapraz delik adedi (1 veya 2) ve filtre pencere/köprü düzeni (sayı, açı) — kesit düzlemi dışı bilgi yok.
5. Filtre gövdesi malzemesi; eleğin gövdeye tespit şekli (lehim/sıkı geçme) belirtilmemiş. Elek kalınlığı (0.10) şematik.
6. Koniğin dolu olduğu ve sapla tek parça olduğu varsayılmıştır (kesilmeden çizilmiş).
7. Diş profilleri çizimde yalnız diş üstü/dibi çizgisiyle verilmiş; modelde kozmetik diş önerilir.
8. SA (dolu koni) / HA (içi boş koni) ve 45°/60°/80° varyantları için ayrı geometri verilmemiştir.
9. 20.5 max / Ø11.8 max zarf ölçüleridir; çizilen filtre daha kısadır (uç z = −18.71, flanş Ø10.30).

## 10. Doğrulama görselleri (`olcum/`)

| Dosya | İçerik |
|---|---|
| 01_genel_kesit_overlay.png | Tüm profiller raster üzerinde; sol yarıda ayna görüntü (dış siluet kontrolü) |
| 02_bas_ust_bolge.png | Baş + konik köşeleri, koordinat etiketli |
| 03_orifis_girdap_odasi.png | Orifis, trompet, havşa, girdap odası (7× büyütme) |
| 04_konik_slot.png | Konik, yuva teması, slot kenarları (turuncu) ve taban çizgisi (mor) |
| 05_tutucu_vida.png | Vida, alyan yuvası, çapraz delik, halka boşluk |
| 06_filtre.png | Filtre gövdesi, pencereler (turuncu), elek kovanı |
| 07_vektor_kesit.png, 08_vektor_slot_orifis.png | PDF'ten çıkarılan ham vektör çizgileri (siyah: kenar, kırmızı: ölçü/kılavuz, yeşil: tarama) |
| 09_ust_gorunus.png, 10_filtre_kontrast.png | Üst görünüş ve filtre bölgesi ham kırpımları |
