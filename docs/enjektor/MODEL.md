# Simplex atomizer 09-DB01 — parametrik PersistCAD modeli

Kaynak şartname: `SPEC.md` / `spec.json` (vektör çizimden ölçüm). Bu belge modelin nasıl kurulduğunu, hangi ölçünün
ölçüm, hangisinin varsayım olduğunu ve doğrulama sonuçlarını verir.

| Dosya | İçerik |
|---|---|
| `samples/tools/gen_enjektor.py` | Üreteç (üç `.pcad` dosyasını yazar) |
| `samples/enjektor_09DB01.pcad` | **Donanım**: 5 katı gövde (27 özellik, 87 parametre). Akış hacmi burada YOKTUR (hızlı yenileme ≈ 0.3 s, sade kesit) |
| `samples/enjektor_09DB01_kesit.pcad` | Yalnız katılar, datasheet'teki gibi çeyrek kesit (swirler kesilmemiş) |
| `samples/enjektor_09DB01_akis.pcad` | Yalnız akış hacmi `AkisHacmi` (katılar tüketilir; imalat denetimi kapalı, parametre kuralları açık) — CFD için |
| `samples/enjektor_09DB01.step`, `…_akis.step` | STEP dışa aktarımları (montaj / akış hacmi) |
| `tests/test_enjektor.cpp` | 12 test; aşağıdaki tüm tabloları yazdırır |
| `tests/test_enjektor_dogrulama.cpp`, `tests/gui/gui_harness_v5.cpp` | Bağımsız inceleme testleri (8 + GUI): pencere azimutu, ölçülen boğaz kesitleri, tasarım bağıntıları, düzlem diyaloğu |
| `dogrulama/DOGRULAMA.md` | **Bağımsız doğrulama raporu**: model kesiti ↔ çizim vektörleri bindirmesi, sapma tablosu, açık riskler |

Görseller (hepsi bu sürümden üretilmiştir): `model_iso.png`, `model_kesit_xz.png`, `model_ceyrek_kesit.png`, `kesit_bas_swirler_on.png`,
`kesit_uc_orifis_girdap_odasi.png`, `swirler_slot_iso.png` / `_ust.png` / `_on.png` / `_yakin.png`, `filtre_pencereler.png`,
`akis_hacmi.png`, `akis_hacmi_kesit_xz.png`, `akis_hacmi_kesit_on.png`, `app_varsayilan.png`, `app_kesit.png`, `app_ihlal.png`, `app_akis.png`,
`sketch_Ck_*.png` (12 çizim). Reddedilen eski modele ait görseller silinmiştir.

Etiketler: **[Ö]** çizimden ölçüldü, **[S]** standart/nominal, **[V]** VARSAYIM, **[T]** türetilen.

## 1. Kuruluş adımları (önce sketch, sonra model)

Eksen Z = nozul ekseni, z = 0 altıgen alt yüzü, +z orifise doğru. Döner profiller XZ düzleminde (u = r, v = z) çizilir ve
V ekseni etrafında döndürülür. Her çizimde her köşe, orijinden (eksenden) verilen **ordinat ölçüleriyle** (DistanceX /
DistanceY, parametre ifadeleri) sabitlenir; yayların yarıçapı sabitlenmiş merkez + başlangıç noktasından gelir.
12 çizimin tamamı DOF 0'dır.

| # | Özellik | Tür | Açıklama |
|---|---|---|---|
| 1 | `Ck_Govde` → `Govde_Don` | Sketch XZ → Revolve (yeni gövde **Govde**) | H1…H24: dış profil + GERÇEK iç kavite (Ø8.84 delik, Ø8.58 kademe, 110° yuva, 69° girdap odası, trompet R0.48, havşa Ø3.98). İki yay: trompet ve kubbe (R7.98, merkez (−0.19, 6.77)) |
| 2 | `Ck_Altigen` → `Govde_Altigen` | Sketch XY → Extrude (intersect) | 16 A/F altıgen (çizimde 15.85), köşeler kesit düzleminde |
| 3 | `Ck_Orifis` → `Orifis` | Sketch XY (z = z_2) → Hole | Orifis Ø`d0` × `Lo`, trompet tarafından girdap odasına; delik kuralları buna bağlı |
| 4 | `Ck_Swirler` → `Swirler_Don` | Sketch XZ → Revolve (yeni gövde **Swirler**) | S1…S12: uç düzlüğü, 110° koni (yuva konisinin ÜZERİNDE), 60° arka koni, etek Ø7.60, sap Ø3.70 |
| 5 | `Dz_Slot` | Plane `axisAngle` | Ekseni içeren meridyen düzlemi, azimut `slot_az` (−75°: çizimdeki slot konumu) |
| 6 | `Ck_Slot` → `Slot` | Sketch (Dz_Slot, ofset −`e_slot`) → Extrude simetrik `w_slot`, cut | Bkz. §2 |
| 7 | `Slot_Cogalt` | Pattern dairesel, `n_slot` | Z ekseni etrafında eşit aralıklı |
| 8 | `Ck_Vida` → `Vida_Don` | Sketch XZ → Revolve (yeni gövde **TutmaVidasi**) | K1…K10: Ø7.48 gövde, dişli flanş, Ø3.90 delik, 45° geçiş |
| 9 | `Ck_Alyan` → `Vida_Alyan` | Sketch XY → Extrude cut | 3/16" alyan yuvası (A/F 4.76, derinlik 2.49), deliğe açık |
| 10 | `Ck_Capraz` → `Vida_Capraz` | Sketch YZ → Extrude cut | Ø2.40 radyal delik, z = 3.06; `n_capraz` = 2 → boydan boya |
| 11 | `Ck_Filtre` → `Filtre_Don` | Sketch XZ → Revolve (yeni gövde **Filtre**) | F1…F22: dişli, kör (118° matkap ucu), iki çevresel kanal, flanş |
| 12 | `Dz_Pencere`, `Ck_Pencere` → `Filtre_Pencere`, `Pencere_Cogalt` | Plane `axisAngle` → Sketch → Revolve `pen_aci` cut → Pattern `n_pen` | Kanal tabanlarında iki sıra pencere; merkezler 45° ve 225° (çizimde açık görünen 0°–90° çeyreği kapsar; köprüler 135° ve 315°) |
| 13 | `Ck_Elek` → `Elek_Don` | Sketch XZ → Revolve (yeni gövde **Elek**) | M1…M4: ince kovan r 4.74→4.84 |
| 14 | `Govde_DisYuvasi` | Combine cut, keepTools | Baş deliği, vidalanan parçaların erkek diş üstü çapına açılır (kozmetik diş: gövdeler örtüşmez, temas eder) |
| 15 | `Ck_Zarf` → `Zarf_Don` → `AkisHacmi_Kes` | Sketch XZ → Revolve (yeni gövde **AkisHacmi**, `mfg=0`) → Combine cut, largestOnly | **Yalnız `_akis` dosyasında.** Bkz. §3 |

Eksenel zincir yukarıdan aşağı kurulur: baş tepesi standartla sabittir (`H_bas`), sırasıyla havşa tabanı `z_f`, orifis
sonu `z_2`, orifis girişi `z_1 = z_2 − Lo`, girdap odası başlangıcı `z_24 = z_1 − (Ds−d0)/2/tan(alfa_s/2)`, yuva konisi
`z_yuva`. Swirler ve tutucu vidanın üst yüzü yuvayı izler (`z_uc`, `z_S3`, `z_S4`, `z_alt`); sap ucu sabittir. Böylece
`d0`, `Lo`, `Ds`, `alfa_s`, koni açıları değişince parçalar birbirine oturmaya devam eder.

## 2. Teğetsel slotlar

SPEC §3.2'deki "CAD tarifi" birebir uygulanmıştır:

1. `Dz_Slot`: nozul eksenini içeren düzlem, azimut `slot_az`.
2. `Ck_Slot` bu düzlemden `e_slot` kadar paralel kaydırılmış düzlemdedir (slotun orta düzlemi; eksenden kaçık).
3. Profil bir şerittir: taban doğrusu 110° koninin anadoğrusuna paralel ve yüzeyden `h_slot` içeride; dış doğru koninin
   0.25 mm dışında; şerit uç düzlüğünün 0.15 mm üstünden başlar, arka konide tükenme noktasını 0.3 mm geçer
   (`slot_s0`, `slot_s1`).
4. Şerit iki yana toplam `w_slot` uzatılıp Swirler'dan kesilir (disk testere kesimi); `n_slot` adet çoğaltılır.

Sonuç: kanal uç düzlüğünü keser, temas bandı boyunca (r 0.76…2.79) yuva konisiyle kapalı bir kanaldır, r < Ds/2'de girdap
odasına açılır, arkada 60° koniden kendiliğinden çıkar (taban çıkışı r ≈ 3.02, z ≈ 10.59 — çizimle aynı).
Kaçıklık yönü orifis tarafından bakınca saat yönünün tersine dönüş verir (düşük güven, tek izdüşüm).

Not: kesim meridyen düzlemine paralel kaydırıldığı için küçük yarıçaplarda yüzeye dik gerçek derinlik `h_slot`'tan biraz
azdır. Akış hacminde OCCT ile **ölçülen** tek slot kesiti: r = 2.7'de 0.0297, r = 0.9'da 0.0283, girdap odası ağzında (r = 0.79)
0.0280 mm² (anma w·h = 0.0304). Yani boğaz girdap odası ağzındadır ve n·w·h'den %8 küçüktür. Türetilen parametre
`A_slot_etkin = n·w·(h − (e² + w²/12)/Ds·cos(koni_on/2))` bu ölçümü %0.5 içinde verir (0.0557 ↔ ölçülen 0.0559 mm²); `A_slot` (n·w·h)
anma değeri olarak kalır.

## 3. Akış hacmi (AkisHacmi)

Akış hacmi ayrı dosyadadır (`enjektor_09DB01_akis.pcad`, uygulama: `Enjektor_09DB01_Akis_App`). Önceki sürümde donanımla aynı
dosyada duruyor, katılarla birlikte görünüp kesitleri karıştırıyor ve her parametre değişiminde yenileme süresini ikiye
katlıyordu. İki dosya aynı üreteçten, aynı parametre tablosuyla yazılır.

Zarf: filtre azami zarfı Ø11.8 × 20.5 (brülör adaptörü deliği [V]) ve başın içinde kalan bir tapa (Ø9.8, yuvaya paralel
koni, Ø5.2) tepe düzlüğüne kadar. `AkisHacmi_Kes`: zarf − (Govde, Swirler, TutmaVidasi, Filtre). **Elek çıkarılmaz**: elek
kovanının yeri (r 4.74…4.84, donanım dosyasındaki `Elek` gövdesi) akış hacminin içinde kalır ve CFD'de gözenekli bölge /
porous-jump olarak tanımlanır; göz
açıklığı ve açık alan oranı parametredir (`elek_goz`, `elek_tel`, `elek_acik`, `A_elek`). Tek tek delinmiş bir elek deseni
modellenmedi (200 mesh → ≈ 10⁵ göz; yeniden üretim süresini patlatır).

`largestOnly=1` yalnız en büyük katıyı tutar: filtre diş boşaltmasındaki kapalı cep (2.66 mm³, r 4.29…4.42,
z −6.64…−5.77) atılır. Gövde `mfg=0` olduğundan imalat denetimine girmez.

## 4. Parametreler

Açılan (exposed) tasarım değişkenleri:

| Grup | Parametre | Varsayılan | Aralık | Dayanak |
|---|---|---:|---|---|
| Akış | `Q` debi [kg/h] | 1.90 | 1.9 – 7.5 | [S] EN 293 |
| Akış | `dP` [bar] | 10 | 7 – 20 | [S] |
| Akış | `Cd` | 0.1283 | 0.08 – 0.45 | [T] çizilen Ø0.36 ile kalibre |
| Akış | `d0_mod` | 0 | 0 / 1 | 0: Ø debiden; 1: `d0_el` |
| Akış | `d0_el` orifis Ø | 0.36 | 0.2 – 1.0 | [Ö] |
| Akış | `Lo` orifis boyu | 0.28 | 0.15 – 1.0 | [Ö] |
| Akış | `Ds` girdap odası Ø | 1.52 | 1.2 – 4.0 | [Ö] |
| Akış | `alfa_s` oda koni açısı | 69.2° | 60 – 120 | [Ö] |
| Swirler | `n_slot` | 2 | 1 – 4 | **[V]** |
| Swirler | `w_slot` | 0.16 | 0.1 – 0.5 | [Ö] ±0.03 |
| Swirler | `h_slot` | 0.19 | 0.1 – 0.3 | [Ö] ±0.04 |
| Swirler | `e_slot` | 0.20 | 0.05 – 0.5 | [Ö] ±0.05 |
| Swirler | `koni_on` / `koni_arka` | 110° / 60° | 100–120 / 50–70 | [Ö] |
| Swirler | `d_uc` uç düzlüğü Ø | 0.80 | 0.5 – 1.1 | [Ö] |
| Filtre | `elek_goz` [µm] | 75 | 75 – 140 | [S] sayfa notları |
| Filtre | `elek_tel` [µm] | 52 | 30 – 80 | **[V]** kare örgü |
| Filtre | `L_elek` | 9.10 | 7.5 – 10.8 | [Ö] |
| Filtre | `pen_aci` | 150° | 100 – 160 | **[V]** (çizim: 0°–90° açık) |
| Tutucu vida | `n_capraz` | 2 | 1 – 2 | **[V]** |

**Tasarım bağıntıları** (türetilen, alt sınırı 0 olan parametreler; ihlalde `PARAM.RANGE` hatası verilir ve bağlı bir GİRDİ
parametresi için değer önerilir — türetilen büyüklük elle yazılamaz):

| Parametre | İfade | Anlamı | Varsayılan |
|---|---|---|---:|
| `slot_uc_payi` | `d_uc/2 − h_slot/cos(koni_on/2)` ≥ 0 | Slot tabanı uç düzlüğünde eksene ulaşmamalı (karşılıklı slotlar uçta birleşir) | 0.069 |
| `slot_ic_payi` | `e_slot − w_slot/2` ≥ 0 | Slot iç duvarı eksen düzlemini geçmemeli (akışın bir kısmı ters döner) | 0.12 |
| `slot_dis_payi` | `Ds/2 − e_slot − w_slot/2` ≥ 0 | Slot dış duvarı girdap odası yarıçapının içinde kalmalı | 0.48 |

Uygulamada (runner) etiketli türetilen parametreler **"Hesaplanan değerler"** kutusunda salt-okunur gösterilir (d0, alanlar, K,
Cd_RL, 2θ, L/d, Ds/d0, paylar); aralık dışı olan kırmızıdır.

Açılmayan varsayım parametreleri: `n_pen` = 2 [V], `pen_merkez` = 45° ([Ö] 0°–90° açık; köprü konumu [V]); `slot_az` = −75° ([Ö] 1. slot; diğerlerinin eşit
aralıklı olması [V]); `D_zarf`, `L_zarf` (akış hacmi dış sınırı [V]). Standart/sabit ölçüler (`H_bas`, `AF_alt`,
`AF_soket`, `D_delik`, `D_dis`, `D_vida`, …) açılmaz. Aralıklar tek tek uçlarda geometri kurulacak şekilde seçilmiştir;
şartnamedeki daha geniş aralıklar (d0 ≤ 2.0, w/h ≤ 0.8) çizilen Ø0.80 uç ve Ø1.52 oda ile birlikte geçerli değildir.

Bağıntılar:

- Orifis: ṁ = Cd·(π d0²/4)·√(2ρΔP) → `d0_hes`. Cd = 0.1283 çizilen geometriden geri hesaplanmıştır.
- Rizk–Lefebvre boşaltma katsayısı `Cd_RL` = 0.35·K^0.5·(Ds/d0)^0.25, K = A_slot/(Ds·d0), ve püskürtme açısı
  `aci_RL` = 6·K^−0.15·(ΔP·d0²·ρ/μ²)^0.11: iki bağıntının biçimi ve katsayıları iki açık yayından teyit edildi (yayınların
  metin dökümünde denklemler bozuk dizildiği için üs/gruplama okuması dolaylıdır; Lefebvre'nin kitabından son bir
  kontrol önerilir) ve **gösterge** olarak verilir (bu kadar küçük ve viskoz nozul için doğrulanmış değildir; n = 2 ile +%30 debi verir, SPEC §6).
- Film kalınlığı (2.7·[…]^0.25) ve Giffen–Muraszew bağıntıları güvenilir bir kaynaktan teyit **edilemedi**; bu yüzden
  modele alınmadı. `FN` yalnızca tanımıdır.

Akış yolu alan tablosu (varsayılan, Q = 646.0 mm³/s):

| Parametre | Geçit | A [mm²] | SPEC | v [m/s] |
|---|---|---:|---:|---:|
| `A_elek` | Elek açık alanı (kanal üstü) | 66.58 | 66.58 | 0.0097 |
| `A_pencere` | Filtre pencereleri [V] | 65.26 | 65.35 | 0.0099 |
| `A_filtre_delik` | Filtre gövde deliği | 43.01 | 43.01 | 0.015 |
| `A_aralik` | Filtre/vida aralığı | 9.107 | 9.111 | 0.071 |
| `A_soket` | Alyan yuvası | 19.62 | 19.62 | 0.033 |
| `A_vida_delik` | Vida eksenel deliği | 11.95 | 11.95 | 0.054 |
| `A_capraz` | Çapraz delikler [V: adet] | 9.048 | 9.048 | 0.071 |
| `A_halka` | Vida/gövde halka boşluğu | 17.43 | 17.43 | 0.037 |
| `A_etek` | Etek/delik halkası | 16.01 | 16.01 | 0.040 |
| `A_kama` | Kademe / arka koni | 15.82 | 15.73 | 0.041 |
| `A_slot` | Slotlar n·w·h [V: adet] | 0.0608 | 0.0608 | 10.62 |
| `A_oda_giris` | Swirler ucu / oda konisi halkası | 0.5765 | (0.7578) | 1.12 |
| `A_orifis` | Orifis | 0.1018 | 0.1018 | 6.35 |

`A_oda_giris` şartnameden farklı tanımlıdır (uç düzlüğü seviyesinde oda yarıçapı 0.58 ile uç Ø0.80 arası halka;
şartnamedeki 0.7578 değeri Ø1.52 ile r 0.58 arasını alır). Oranlar: A_slot/A_orifis = 0.597, slot ve orifis dışındaki en
dar geçit / A_orifis = 5.67 (`A_oda_giris`; diğerleri ≥ 88), K = 0.1111, Cd_RL = 0.167, Q_RL = 2.48 kg/h,
aci_RL = 50.5°, Lo/d0 = 0.78, Ds/d0 = 4.22, v_slot = 10.6 m/s, elek açık alan oranı 0.349 (200 mesh).

## 5. Ölçüm / varsayım / bilinçli sapmalar

Ölçülen her köşe `spec.json` değeriyle çizilmiştir. Bilinçli farklar:

- **K1** (vida deliği üst köşesi, sapma 0.076 mm): çizimde vida köşesi swirler'ın sap dibi yuvarlatmasının (S8–S9) içine
  girer; modelde vida deliği ağzına S8–S9 ile çakışan 0.1 × 45° kırma verildi [V], aksi halde iki gövde örtüşür.
- **H20** (baş delik ağzı, 0.046 mm): filtrenin konik geçişi (F6–F7) çizimde baş pahına 0.05 mm girer; `Govde_DisYuvasi`
  bu teması keser.
- Dişler kozmetiktir: erkek dişler çizimdeki diş üstü çapında (Ø9.48), baş deliği diş dibi çapında (Ø8.84);
  kavrama boyunca baş deliği Ø9.48'e açılmıştır. 9/16"-24 dış diş Ø14.16 silindirdir.
- Alyan yuvası altıgenden 0.02 mm küçük ön delikle açılır (K8/K9: 2.36 yerine altıgen düzlüğü 2.38'dedir); ağız havşası
  (≈0.35 × 45°) modellenmedi.
- Köşe yuvarlatmaları (S6–S7, S8–S9, H4–H5) şartnamedeki gibi doğru parçasıdır.
- Varsayımlar (§4'te [V]): slot adedi ve diğer slotların açısı, çapraz delik adedi, pencere adedi/açısı/köprü konumu,
  elek tel çapı, swirler'ın dolu ve tek parça olması, filtre gövdesi malzemesi, akış hacmi dış sınırı.

## 6. Doğrulama sonuçları (`build-m/bin/test_enjektor`)

| Kontrol | Sonuç |
|---|---|
| Çizimler | 12 çizim, hepsi DOF 0, çözücü durumu Ok |
| Gövdeler | Donanım dosyası: 5 gövde; akış dosyası: 1 gövde; hepsi tek ve geçerli katı (BRepCheck) |
| Girişim | Tüm çiftlerde ortak hacim 0; temas (mesafe 0): Govde–Swirler (konik yuva), Swirler–TutmaVidasi, Govde–TutmaVidasi ve Govde–Filtre (diş), Filtre–Elek. Boşluklar: TutmaVidasi–Filtre 0.58, Govde–Elek 0.33 |
| Akış hacmi | TEK katı, V = 1643.0 mm³, 83 yüz; 15 kontrol noktası (filtre dışı → elek → kanal → pencere → delik → alyan → eksenel delik → çapraz delik → halka → etek → kama → girdap odası → orifis → havşa) ve slot kanalının ortası aynı katının içinde |
| Slotların açıklığı | `largestOnly=0` iken 2 parça (ıslak hacim + 2.66 mm³ kapalı diş cebi). Slotlar bastırılınca girdap odası + orifis ayrı bir parçaya düşer; yani halka boşluğu ile girdap odası arasındaki tek bağlantı slotlardır |
| Kesit – şartname köşeleri (XZ ve YZ kesitleri) | Govde 24 köşe, en büyük 0.046 (H20); Swirler 12 köşe, 0.009; TutmaVidasi 10 köşe, 0.007 (+ K1 0.076, bilinçli); Filtre 22 köşe, 0.000; Elek 4 köşe, 0.000. Hedef ≤ 0.05 mm |
| Slot kenarları – çizim izdüşümü | 12 nokta (kenar A/B), en büyük sapma 0.034 mm (ölçüm belirsizliği ±0.03–0.05) |
| Sağlamlık | 86 yeniden üretim (20 açık parametrenin min/max'ı, donanım + akış dosyası; 6 birleşik durum) + inceleme sırasında 220 rastgele birleşim (150 donanım, 70 akış): 0 hata, tüm çizimler DOF 0 |
| Süre | Donanım ≈ 0.3 s, akış hacmi ≈ 0.6 s. Tasarımcıda eskiden görülen ≈ 1.8 s, GUI kitaplıklarının bıraktığı kirli AVX durumundan kaynaklanıyordu (`pc/SimdState.h`); şimdi GUI içinde de 0.3–0.6 s |
| İmalat (tornalama, 1.4005) | Varsayılanda 0 hata, 5 uyarı (altıgen, slot, alyan, çapraz delik: canlı takım/frezeleme gerekir) |

İmalat ayarları: en küçük takım 0.10 mm (disk testere bıçağı / tel erozyon), en küçük delik Ø0.15, delik L/d ≤ 3.
Slot genişliği ve orifis çapı kurallara bağlıdır: sınırlar 0.20 mm / Ø0.40'a çekildiğinde `CNC.SLOT_WIDTH` →
"w_slot ≥ 0.20" ve `CNC.MIN_HOLE` → "Q ≥ 2.35" önerilir; öneriler uygulanınca iki hata da kalkar.

## 7. Motor (engine) eklemeleri

- `Plane kind=axisAngle`: dünya eksenini içeren, açıyla döndürülmüş yardımcı düzlem (referanssız). Tasarımcının Düzlem
  diyaloğu bu türü tanır (eksen + açı; kenar/yüz seçimi istemez).
- `Plane show=0`: yapı düzlemi; tasarımcıda yalnız seçiliyken/çizimi açıkken çizilir, runner'da hiç çizilmez.
- `PARAM.RANGE` türetilen parametrelerde bağlı girdi parametresi için değer önerir (tarama + ikiye bölme).
- Runner: "Hesaplanan değerler" kutusu; görüntüleyici: görünür yüzlere sıkı sığdırma, yeniden boyutlamada otomatik sığdırma,
  daha ince görüntü ağı (7°/yüzey); `pc_kesit` aracı (gövde başına düzlem kesiti → JSON).
- `Pattern`: `count` = 1 kabul edilir (yalnız kaynak örnek).
- `Combine largestOnly=1`: sonucun yalnız en büyük katısı tutulur.
- Gövde oluşturan özellikte `mfg=0`: gövde referans geometridir, imalat denetimine girmez.
- İmalat: döndürülmüş (torna) gövdelerdeki dar kesimler ve kör delikler için analitik kurallar.

Ayrıntı: `src/core/pc/Feature.h`, `src/model/STATUS.md`, `src/mfg/STATUS.md`.
