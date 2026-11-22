# Teknik resimden içe aktar — spesifikasyon ve fizibilite planı

Durum: TASLAK v0.1 (Kasım 2026). Bu belge ürünün kendisini değil, spesifikasyonunu ve fizibilite prototipi planını verir.
Arayüz sözleşmesi: `INTERFACE.h-draft` (tarihsel taslak). **P0 tamamlandı (15.11.2026): sözleşme "DrawingIR 1.0" olarak
`src/drawing/include/pc/drawing/*.h` altında dondu** (konu başına bir başlık, şemsiye `Drawing.h`). Dosya sahipliği, saplamalar ve
designer kancası: `OWNERSHIP.md`. Bu belgedeki dosya düzeni (§4) ile çelişki olursa `OWNERSHIP.md` geçerlidir.
Spike kaynakları: `spike/` (§9).

Kullanıcı isteği: "Teknik resmi yükleyeyim, program sketch'ini ve 3B modelini oluştursun."
Kullanıcının verdiği kararlar:
- Girdiler vektör PDF ve DXF, ayrıca taranmış ya da fotoğraflanmış görüntülerdir (PNG/JPG/taranmış PDF).
- Program tamamen **çevrimdışı** çalışır. Bulut tabanlı yapay zekâ kullanılmaz.
- Hedef platformlar Linux ve Windows'tur (MSYS2 CLANG64).

Ürün ilkesi: Program "tek tıkla doğru model" vaat etmez. Her aşamada kullanıcıya gösterilen ve düzeltilebilen bir
**sihirbaz** vardır. Otomatik sonuç yalnızca bir öneridir. Ölçülerden gelen her değer adlandırılmış bir **parametre**
olur. Böylece içe aktarılan parça doğrudan PersistCAD'in parametrik ve imalat denetimi hattına girer.

---

## 0. Özet

| Konu | Karar |
|---|---|
| Vektör PDF | **libqpdf (Apache-2.0)** ile nesne ve akış ayrıştırma. İçerik akışı yorumlayıcısını kendimiz yazarız (yol, metin, yazı tipi, Form XObject). MuPDF (AGPL) ve Poppler (GPL) bağlanmaz. |
| PDF'i rastere çevirme | Taranmış PDF ve sihirbaz arka planı için **QtPdf** (Qt6::Pdf, LGPLv3, içinde PDFium/BSD). Ayrı `pc_drawing_qt` hedefinde durur. |
| DXF | **Kendi okuyucumuz** (ASCII DXF R12–R2018 grup kodları). libdxfrw ve dxflib GPL olduğu için kullanılmaz. DWG desteklenmez; kullanıcı DXF'e dönüştürür. |
| Raster | **OpenCV** (Apache-2.0: core, imgproc, imgcodecs) + **Tesseract 5** (Apache-2.0) + **Leptonica** (BSD-2). tessdata eng/tur (Apache-2.0) uygulamayla birlikte dağıtılır. |
| Ara veri | `DrawingIR` (JSON, şema `pcdrw/1`). Yedi aşamanın her biri saf fonksiyondur ve ayrı test edilir. |
| 3B | Sıralı stratejiler: Revolve (eksenel simetrik) > iki görünüşlü Extrude > kesitten Revolve > çok özellikli (sonraki faz) > yalnız sketch. |
| Test | Kendi modellerimizden **sentetik derlem** üretilir: vektör PDF, DXF, temiz PNG ve bozulmuş tarama. Doğru sonuç (ground truth) bilinir. Ek olarak gerçek enjektör çizimi (`docs/enjektor/spec.json` doğru sonuç). |
| İş paketleri | WP-1 vektör girişi, normalizasyon ve derlem; WP-2 raster ve OCR, ölçü dilbilgisi ve bindirme görünümü; WP-3 yorumlama, sketch, 3B ve sihirbaz. |

---

## 1. Kapsam

### 1.1 Girdiler
| Tür | Örnek | Faz |
|---|---|---|
| CAD çıkışı vektör PDF (gerçek metin) | Inventor/SolidWorks/NX "PDF'e yazdır", PScript5 + Ghostscript | 1 |
| DXF (model space ve paper space) | AutoCAD, Inventor DXF çıkışı | 1 |
| Metnin çizgi olarak verildiği vektör PDF (SHX) | AutoCAD "SHX metni geometri" | 2 (metin için OCR'a düşer) |
| Temiz tarama (≥300 dpi, düz) | Arşiv taramaları | 1 (yalnız sketch + ölçü önerisi, kullanıcı onaylı) |
| Kötü tarama (eğik, lekeli, 150–200 dpi, JPEG) | | 2 |
| Fotoğraf (perspektif, gölge) | Telefonla çekilmiş çizim | 3 (en iyi çaba) |
| Elle çizim, DWG, 3B PDF (PRC/U3D), izometrik görünüşten 3B | | Kapsam dışı |

### 1.2 Çıktılar
- Mevcut belgeye ya da yeni belgeye eklenen özellikler: Sketch, Revolve/Extrude/Hole/Chamfer/Fillet/Pattern ve parametreler.
- Bu ekleme tek bir geri al adımıdır: "Teknik resimden içe aktar".
- Yan dosya: `<ad>.pcdrw.json`. Tüm IR ve kullanıcı düzeltmeleri burada saklanır. Dosya tekrar üretilebilirlik, hata raporu ve
  regresyon testi için kullanılır.
- Rapor: tanınan ve tanınmayan ölçüler, varsayımlar, DOF, çizime göre sapma.

### 1.3 Sketch modülü kısıtları (tasarımı belirler)
`pc::Sketch` yalnızca Line, Circle ve Arc varlıklarını destekler. Elips ve spline yoktur.
- Çizimdeki spline ve Bezier eğrileri yay zincirine uydurulur (tolerans 0.02 mm vektör, 0.25 mm raster).
- Elipsler (eğik görünüşteki daireler) yalnızca görünüş tanımada kullanılır, sketch'e girmez.
- Ölçü türleri (`SkConstraintType`) şunlardır: Distance, DistanceX/Y, PointLineDistance, Length, Radius, Diameter ve Angle.
  Çizimdeki her ölçü türü bunlardan birine eşlenir (§2.9).

---

## 2. İşlem hattı

```
 dosya ──S0 sniff──► S1 ingest ──► S2 normalize ──► S3 layout ──► S4 annotate ──► S5 views ──► S6 sketch ──► S7 3D
          (tür)     (WP-1/WP-2)      (WP-1)          (WP-3)          (WP-3)         (WP-3)        (WP-3)        (WP-3)
                     PrimitiveSheet(raw) → PrimitiveSheet(norm) → SheetLayout → Annotation → ViewGraph → SketchCandidate[] → SynthesisCandidate[]
                                                  ▲ her aşama sonrası sihirbaz adımı: kullanıcı düzeltir → edits[] → aşamadan itibaren yeniden çalıştır
```

### 2.0 S0 — Tür tespiti (`sniff`)
- **PDF:** Sayfa başına çizilen yol sayısı, metin sayısı ve görüntü alanı oranı sayılır.
  - Sayfanın en az %80'ini kaplayan tek bir görüntü varsa ve yol sayısı 50'den azsa sayfa **taranmış PDF** kabul edilir.
  - Bunun dışındaki PDF **vektör** kabul edilir. Enjektör PDF'inde 260 küçük görüntü karosu vardır (tarama dokusu) ve sayfa yine
    vektördür. Bu yüzden karar ölçütü görüntü sayısı değil, görüntünün kapladığı alandır.
- **DXF:** İlk 2 KB'ta `SECTION` ve `HEADER` aranır. İkili DXF imzası (`AutoCAD Binary DXF`) faz 2'dedir.
- **Görüntü:** EXIF ve DPI okunur. Belirgin perspektif bozulması varsa (§2.4, sayfa dörtgeni dikdörtgen değilse) "fotoğraf" önerilir.

### 2.1 S1a — Vektör PDF girişi (WP-1)
Akış şöyledir: libqpdf belgeyi açar, filtreleri çözer (Flate/LZW/ASCII85/RunLength) ve `parsePageContents` ile içerik akışını
dağıtır. İşleçleri kendi yorumlayıcımız işler. Spike'ta 150 satırlık bir sürüm çalıştı (§9).
- **Grafik durumu:** `q Q cm w d J j M RG rg K k G g CS cs SC SCN sc scn gs` (ExtGState: LW, D, CA).
- **Yollar:** `m l c v y h re` ve boyama işleçleri `S s f F f* B B* b b* n`. Kırpma (`W W*`) kaydedilir ama uygulanmaz.
  Kırpılmış ayrıntı görünüşleri faz 2'dedir.
- **Metin:** `BT ET Tf Tm Td TD T* Tj TJ ' " Tc Tw Tz TL Ts Tr`. Glif ilerlemesi `/Widths` ya da CIDFont `/W` ile hesaplanır.
  Unicode eşlemesi şu sırayla denenir: ToUnicode CMap, ardından `/Encoding` + `/Differences` glif adları (AGL), ardından
  Standard/WinAnsi/MacRoman. `Tr 3` (görünmez metin) OCR'lı PDF katmanı demektir. Bu metin aday olarak alınır, bayrağı kaydedilir.
- **Form XObject:** Özyinelemeli işlenir (`/Matrix`, `/Resources`), derinlik 8 ile sınırlıdır. Görüntü XObject'leri yalnızca dörtgen
  olarak kaydedilir.
- **İsteğe bağlı içerik (OCG) adları** `LineStyle::layer` alanına yazılır. Inventor ve AutoCAD katmanları burada görünür ve çizgi
  rolü için çok güçlü bir ipucudur.
- **Ek açıklamalar** (Annotation, FreeText) faz 2'dedir.
- Birim: 1 pt = 25.4/72 mm. Sayfa kutusu `/CropBox`, yoksa `/MediaBox`. `/Rotate` uygulanır.

### 2.2 S1b — DXF girişi (WP-1)
- Okuyucu grup kodu tabanlıdır. Okunan bölümler: HEADER (`$INSUNITS`, `$DIMSCALE`, `$LTSCALE`, `$MEASUREMENT`), TABLES (LAYER,
  LTYPE, STYLE, DIMSTYLE), BLOCKS, ENTITIES ve OBJECTS (yalnızca LAYOUT).
- Varlıklar: LINE, LWPOLYLINE/POLYLINE (bulge → yay), CIRCLE, ARC, ELLIPSE, SPLINE (yaya uydurmak için örneklenir), TEXT, MTEXT
  (biçim kodları temizlenir), INSERT (blok patlatma; ölçek, dönme ve dizi uygulanır), HATCH (sınır + desen açısı → HatchRegion),
  DIMENSION, LEADER/MULTILEADER, SOLID (ok ucu).
- **DIMENSION:** Tür `70 & 7` alanından okunur. Tanım noktaları 10/13/14/15/16, metin 1 numaralı gruptan gelir (`<>` ölçülen değer
  demektir) ve blok `*Dn` adıyla verilir.
  - **Ölçülen değer tanım noktalarından hesaplanır.** Grup 42 isteğe bağlıdır ve spike'ta ezdxf çıktısında hiç yoktu.
  - Blok patlatılır; içinden gelen ilkellere `fromDimBlock=true` yazılır. Böylece S4 bu ilkelleri geometriye karıştırmaz.
- Özel metin kodları dönüştürülür: `%%c`→Ø, `%%d`→°, `%%p`→±, `\U+2300`→Ø.
- Çizgi türü ByLayer/ByBlock olarak çözülür, `LineStyle::linetype` alanına yazılır (CENTER, HIDDEN, DASHED, PHANTOM ...).
- Paper space'teki görünüş pencereleri (VIEWPORT) faz 2'dedir. Faz 1'de model space alınır; ölçek 1:1 kabul edilir.

### 2.3 S1c — Raster girişi (WP-2)
1. **Yükleme:** PNG/JPG/TIFF OpenCV imgcodecs ile açılır. Taranmış PDF'te gömülü görüntü (DCT/Flate) libqpdf ile çıkarılır.
   CCITT ya da JBIG2 kodlu görüntüler ve karma içerik QtPdf ile render edilir (`pc_drawing_qt`).
2. **Ön işleme:** Gri tonlamaya çevrilir, arka plan düzeltilir (büyük çekirdekli morfolojik kapanış ile bölme), Sauvola ya da
   uyarlamalı eşik ile ikilenir ve gürültü (küçük bağlı bileşenler) temizlenir.
3. **Eğiklik düzeltme:** LSD ile bulunan uzun ve yatay/dikeye yakın çizgilerin uzunlukla ağırlıklı açı ortalaması alınır.
   Spike'ta eğiklik 1.5° verildi, -1.499° ölçüldü. Fotoğrafta çerçeve dörtgeni (en büyük dörtgen kontur) bulunur ve homografi ile
   düzeltilir. Ardından aydınlatma düzeltilir.
4. **DPI ve ölçek:** Önce dosya metaverisi okunur. Yoksa çerçeve boyutu standart kâğıt boyutlarına (A4–A0, yatay/dikey) oturtulur.
5. **Metin ve grafik ayrımı:** Bağlı bileşen istatistiği (boyut, en-boy oranı, yoğunluk) kullanılır. İsteğe bağlı olarak OpenCV
   EAST/DB metin dedektörü eklenebilir (ONNX, çevrimdışı; faz 2). Metin bölgeleri grafikten maskelenir.
6. **Çizgiler:** LSD (`cv::createLineSegmentDetector`, OpenCV ≥4.5.1'de imgproc içindedir; MSYS2'deki OpenCV 5.0'da varlığı P0'da doğrulanacak, yoksa `cv::ximgproc::FastLineDetector` ya da kendi LSD uygulamamız kullanılır). İnceltme ve vektörleştirme seçeneği
   faz 2'dedir. Segmentler eş doğrusallığa göre birleştirilir. Kalınlık, çizginin dik profili boyunca ölçülür (görünür ~0.5 mm,
   ince ~0.25 mm).
7. **Daireler ve yaylar:** `HoughCircles(HOUGH_GRADIENT_ALT)` aday üretir. Spike'ta enjektör sayfasında 146 aday çıktı ve çoğu
   metin ya da taramaydı. Bu yüzden her aday **kenar desteği** ile doğrulanır: çevrenin ≥%70'i siyah piksel üzerinde olmalıdır.
   Yaylar için birleştirilmiş segment zincirlerine en küçük kareler ile daire uydurulur.
8. **Kesikli çizgiler:** Aynı doğru üzerindeki kısa segmentler zincirlenir. Desen (uzun–boşluk–nokta) eksen ya da gizli çizgi diye
   sınıflandırılır.
9. **Ok uçları:** Dolu üçgen bloblar aranır (alan, dışbükeylik, üçgen uyumu). Açık okta 15–30° açılı iki kısa segment aranır.
   Çıktı `PrimKind::Arrowhead` olur.
10. **OCR (Tesseract 5 LSTM):**
    - Metin bölgeleri satır satır kırpılır ve dönüklüğe göre çevrilir (0/90/180/270 ve satırın kendi açısı).
    - `PSM_SINGLE_LINE` kullanılır, ölçü için karakter beyaz listesi uygulanır.
    - n-best hipotezler (`ChoiceIterator`) `TextRun::alternatives` alanına yazılır.
    - Antet ve notlar için `eng+tur`, ölçüler için `eng` (faz 2'de `pcdim` ince ayarlı model) kullanılır.
    - Tam sayfa `PSM_SPARSE_TEXT` yalnızca aday bulmak için kullanılır. Spike'ta bu tek başına yetersiz kaldı (§9).
11. **Ölçü dilbilgisi ile yeniden sıralama:** `rankDimTextHypotheses` çağrılır. Karışıklık kümeleri şunlardır:
    `Ø↔0/O/o/2/@`, `±↔+/+-/t`, `°↔o/0/9`, `×↔x/X`, `,↔.`, `1↔l/I`, `5↔S`, `8↔B`.
    Düzeltme yalnızca dilbilgisi ve geometrik bağlam birlikte destekliyorsa uygulanır. Örneğin "020 H7" daire yakınında okunduysa
    "Ø20 H7" olur. Yapılan düzeltme `DimText::corrections` alanına yazılır ve güven puanı düşürülür.

### 2.4 S2 — Normalizasyon (WP-1, tüm kaynaklar için ortak)
Spike'ta gerçek PDF'te görülen sorunlar bu aşamanın gerekliliğini gösterir:
- **Kesikli çizgi zincirleme.** PScript5 sürücüsü kesikli çizgileri ayrı ayrı kısa segmentler olarak yazıyor (`d` işleci 0 kez;
  0.5–1.7 pt uzunlukta binlerce segment). Eş doğrusal ve aralıklı segmentler tek çizgide birleştirilir. Desen `LineStyle::dash`
  alanına yazılır.
- **Yay ve daire geri uydurma.** Aynı sürücü yayları doğru parçalarına bölüyor (0 adet `c` işleci). Polyline'a özyinelemeli yay
  uydurulur (Douglas–Peucker benzeri bölme ve cebirsel daire uydurma). Tam çember tespit edilir. Bezier zincirleri de yaya
  çevrilir; 4 kontrol noktalı çeyrek daireler kesin olarak tanınır.
- **Yakalama ve birleştirme.** Uç noktalar yakalanır (`snapTol`). Eş doğrusal ve üst üste binen çizgiler birleştirilir.
  Dolgu+kontur kopyaları ve sıfır uzunluklu segmentler silinir.
- **Glif birleştirme.** Döndürülmüş metinler glif glif yazılmış ("1","4","±","0",".2"). Aynı taban çizgisinde ve aynı açıdaki
  glifler, boşluk 1.2×yükseklikten küçükse kelimelerde birleştirilir. Üst üste yazılmış tolerans satırları (+0 / -0.5) ayrı kalır;
  S4 bunları ölçüye bağlar.
- **Sembol yazı tipi onarımı.** ToUnicode olmayan TrueType sembol fontları `/Differences` glif adlarıyla eşlenir. Tablo yazı tipi
  ailesine göre seçilir ve tabloya yeni girdi eklenebilir. Enjektörde gözlenen eşleme şudur: `grave`→±, `n`→Ø, `asciitilde`→°.
  Bilinen diğer aileler: AIGDT, GDT, isocp, Symbol. Glif adı yoksa glif konturu render edilip OCR'a verilir (faz 2).
- **Vektör ok ucu tespiti.** Küçük, dolu ve kapalı üçgenler ok ucu olarak işaretlenir. Kesitte dolu tarama da olabileceği için
  boyut sınırı kullanılır (2–6 mm).
- **Tarama bölgeleri.** Paralel, eşit aralıklı ve 30–60° açılı ince çizgi kümeleri ile görüntü karosu kümeleri HatchRegion olur.

### 2.5 S3 — Sayfa düzeni (WP-3)
- **Çerçeve:** Sayfaya paralel en büyük dikdörtgen.
- **Antet:** Sağ alt köşede ızgara yapısında bir tablo aranır. Anahtar kelime sözlüğü (TR/EN) kullanılır: ÖLÇEK/SCALE, MALZEME/MATERIAL,
  RESİM NO/DWG NO, ADI/TITLE, BİRİM, TOLERANS (ISO 2768-m), PROJEKSİYON. Proje yöntemi sembolü (kesik koni) ayrıca tanınır.
- **Ölçek:** Önce antetteki "1:2" ya da "2:1" okunur. Sonra ölçü değerlerinin ölçülen uzunluklara oranlarının dayanıklı medyanı
  alınır. Enjektörde bu yöntemle 70.87±0.07 px/mm bulundu, yani 3:1. Ölçüm doğrusal ve çap ölçüleriyle yapılır; "max", "min" ve
  referans ölçüleri dışarıda tutulur.
  - İki kaynak çelişirse uyarı verilir ve ölçülerden gelen ölçek tercih edilir. "Ölçeksiz" (NTS) durumu desteklenir: her ölçü kendi
    değerini verir, geometri yalnızca topolojiyi sağlar.
- **Görünüşler:** Çerçeve, antet ve not blokları dışında kalan "Visible" adayları kümelenir (bağlı bileşenler ve boşluk eşiği).
  Görünüş etiketleri ("A-A", "KESİT A-A", "DETAY B (5:1)", "GÖRÜNÜŞ C") eşlenir. Kesit düzlemi çizgileri (kalın uçlu eksen çizgisi
  ve oklar) bulunur.

### 2.6 S4 — Çizgi rolleri ve ölçüler (WP-3)
**Çizgi rolü sınıflandırması** kurallara dayanır ve her kural bir puan üretir.
- DXF katman ve çizgi türü adı en güçlü ipucudur.
- Vektör PDF'te kalınlık sınıfları vardır. Enjektörde 5 sınıf ölçüldü: 1.435/1.005/0.718/0.502/0.215 pt. Kalın olanlar görünür
  kenar, ince olanlar ölçü/uzatma/tarama çizgisidir.
- Diğer ipuçları: kesik desen (gizli ya da eksen), renk ve geometrik bağlam (ok ucuna değen ince çizgi ölçü çizgisidir).

**Ölçü çözümleme** ISO 129-1 ve ASME Y14.5 ilkelerine göre yapılır:
1. Metin ayrıştırılır (`parseDimText`). Dilbilgisi EBNF ile §2.10'da verilir.
2. Metne en yakın ölçü çizgisi aranır. Metin genellikle çizginin üstünde ve ona paraleldir; döndürülmüş metin dik ölçüdür.
3. Ölçü çizgisinin uçlarındaki ok uçları ya da eğik çizgiler bulunur. Uzatma çizgileri ok uçlarından geometriye doğru izlenir ve
   aradaki boşluk (≈1 mm) hesaba katılır.
4. **Geometriye bağlama:** Uzatma çizgisinin ucu bir kenar ya da köşeyle hizalanmalıdır. Çap ölçüsü bir daireye bağlanır ya da yan
   görünüşte iki paralel kenara bağlanır (silindir). Yarıçap ölçüsünde lider çizgisi yayın merkezinden geçer. Açı ölçüsünde yay ve
   iki doğru kullanılır.
5. **Tolerans yığını:** Üst üste duran "+0" ve "-0.5" metinleri ana metnin sağına hizalı ise ona bağlanır.
6. **Tutarlılık:** `ölçülen (geometri/ölçek) − nominal` farkı kabul bandı içinde olmalıdır. Kabul bandı şudur:
   max(|tol bandı|, 0.5% nominal, yakalama toleransı) × `tolMatchFactor`.
   - Bant dışında kalan ölçü çizim hatası ya da yanlış bağlama olarak işaretlenir (kırmızı).
   - Ölçü yine **sürücü** kalır: çözücü geometriyi ölçüye çeker.
7. Sürücü olmayan ölçüler: referans "(..)", max/min zarf ölçüleri ve fazlalık ölçüler (§2.9).
8. Raster kaynakta ölçü geometriden **değil** metinden gelir. Geometri yalnızca başlangıç tahmini ve topoloji sağlar.

### 2.7 S5 — Görünüşler (WP-3)
- Görünüşler arası yatay ve dikey hizalama aranır (sınır kutusu çıkıntıları ve köşe x/y değerleri eşleşmeli).
- **Projeksiyon yöntemi** önce antet sembolünden okunur. Sembol yoksa birinci ve üçüncü açı düzenleri ayrı ayrı puanlanır ve tutarlı
  olan seçilir. Varsayılan ISO E'dir (TS/ISO 128). Kullanıcı sihirbazda değiştirebilir.
- **Birincil görünüş:** En çok kapalı döngü ve ölçü içeren görünüş seçilir.
- **Simetri ekseni:** Görünüşteki eksen çizgisine göre ayna eşleşme puanı hesaplanır. Bu puan, segmentlerin aynadaki karşılığını
  ≤ snapTol içinde bulma oranıdır. Eksen çizgisine dik görünüşte eş merkezli daireler varsa ve puan ≥0.9 ise parça
  **döner simetrik** sayılır.
- Her görünüş için `sheetToView` dönüşümü hesaplanır (ölçek ve görünüş orijini). Orijin, döner parçada eksen ile en alt yüz
  (enjektördeki z=0 kuralı gibi), prizmatik parçada sol alt köşedir.

### 2.8 Enjektör çalışmasından alınan dersler (bağlayıcı)
Kaynaklar: `docs/enjektor/SPEC.md`, `MODEL.md`, `olcum/olcum_overlay.py`, `samples/tools/gen_enjektor.py`.
1. **Toleranslı ölçüler tolerans ortasına çizilir.** Ø16 −0.5 çizimde 15.70, 16 A/F −0.25 çizimde 15.85 çıktı.
   - Tutarlılık testi nominali **ve** tolerans ortasını dener; hangisine yakınsa onu kabul eder.
   - Parametrenin değeri varsayılan olarak nominaldir. `preferTolMid` seçeneği vardır, iki değer de açıklamada yazar.
2. **"max" ve "min" ölçüleri fiziksel kenar değil zarftır.** Örnek: Ø11.8 max, 20.5 max. Bunlar sürücü olmaz, parametre aralık
   kuralı olarak önerilir.
3. **Kesit vektördür, tarama ise bitmap karodur.** Taramalı bölge tespiti görüntü karolarını da kabul etmelidir.
4. **Çizim şematik olabilir.** Aynı çizim birden çok sayfada tekrar edebilir. Sayfa seçimi kullanıcıya bırakılır.
5. **Ordinat yaklaşımı.** Gen_enjektor her köşeyi eksenden DistanceX/DistanceY ile sabitledi ve tüm sketch'ler DOF 0 oldu.
   Çizimin kendi ölçüleri yetmediğinde eksik serbestlikler aynı yöntemle **ölçülen** değerlerle kapatılır (§2.9 adım 5).
6. **Etiket düzeni.** Her parametre açıklamasında kaynak etiketi bulunur: **[D]** çizim ölçüsü, **[Ö]** geometriden ölçüldü,
   **[V]** varsayım, **[T]** türetilmiş. Bu düzen enjektördeki [Ö]/[S]/[V]/[T] düzeninin uzantısıdır.
7. **Kalibrasyon önce doğrulanır.** Enjektörde 4 ölçü ile ölçek ±0.1% doğrulandı. S3 aynı şeyi otomatik yapar ve kalıntıları raporlar.
8. **Dişler.** Dış dişte büyük çap, iç dişte diş dibi çapı geometri olarak kullanılır. Diş tanımı açıklamada kalır; PersistCAD'de
   diş özelliği yoktur.

### 2.9 S6 — Sketch'i yeniden kurma (WP-3)
Her strateji için ilgili görünüşten bir `SketchCandidate` üretilir.
1. **Varlıklar.** Rolü Visible olan ilkeller (ve kesitteki malzeme sınırları) Line, Arc ya da Circle olur. Eksen çizgisi construction
   line olur. Döner parça için yalnızca eksenin bir tarafındaki **yarım profil** alınır ve eksen üzerinde kapatılır.
   Koordinatlar görünüş koordinatına (parça mm) çevrilir.
2. **Kapalı döngü.** Uç noktalar birleştirilir, T kesişimlerinde kenar bölünür ve yüz bulunur (`pc::sketch::buildProfile` ile aynı
   kural). Döngü kapanmıyorsa en küçük boşluk önerilir ve kırmızı gösterilir.
3. **Geometrik kısıtlar.** Toleranslar vektör ve raster için ayrıdır:

   | Kısıt | Koşul |
   |---|---|
   | Coincident | Ortak uç |
   | Horizontal / Vertical | Açı < 0.3° (raster 1°) |
   | Tangent | Yay ile doğru arasında teğet geçiş |
   | Concentric | Merkez farkı < snapTol |
   | Equal | Aynı yarıçap; "4x" ifadesi de eşit sayılır |
   | Symmetric | Eksen çizgisine göre |
   | PointOnEntity | Eksen üzerinde duran uç nokta |

   Gereğinden fazla kısıt üretilmez. Hangi kısıtın ekleneceğine DOF çözücüsü karar verir (aday kısıt eklenir, `degreesOfFreedom`
   düşmezse geri alınır).
4. **Sürücü ölçüler.** Her `Dimension` bir `SkConstraint`'e eşlenir:

   | Ölçü | Kısıt |
   |---|---|
   | Yatay/dikey doğrusal | DistanceX/DistanceY (iki köşe arasında) |
   | Eğik doğrusal | Distance ya da PointLineDistance |
   | Ø (daire) | Diameter |
   | Ø (yan görünüşte, döner profil) | Eksenden DistanceX = Ø/2. İfade parametre üzerinden kurulur: `D3/2` |
   | R | Radius |
   | Açı | Angle. Koni tam açısı yarıya bölünür: `alfa/2` |
   | Pah 2x45° | İki Length ya da Distance + Angle |
   | Adet (4x) | Pattern parametresi (S7) |

   Parametre değeri nominaldir. Tolerans `ParamProposal` alanına ve açıklamaya yazılır.
5. **DOF kapatma.** `pc::sketch::solve` çalıştırılır. DOF > 0 ise kalan serbestlikler eksen ya da datum noktasından ordinat
   ölçüleriyle kapatılır. Bu ölçüler **[Ö] ölçülen** parametre olur (`L_olc1`, ...). Ölçülen değer vektörde 0.01 mm'ye, rasterde
   0.1 mm'ye yuvarlanır. Kullanıcı sihirbazda isterse bunları Fixed kısıtına çevirebilir.
6. **Çelişki ve fazlalık.** `SolveResult::conflicting` raporlanır. Fazlalık ölçüler (zincir ölçü ile toplam ölçü birlikte verildiyse)
   referans ölçüye çevrilir. Bu durumda ilk ölçüler sürücü kalır, toplam ölçü `[T]` türetilmiş olarak doğrulanır.
7. **Parametre adları.** Değerden değil, rolden türetilir; değer değişince ad anlamsızlaşmamalıdır. Çakışmada `_2` eklenir.

   | Tür | Ad deseni |
   |---|---|
   | Döner parça çapları | `D1..Dn` (eksen boyunca alttan üste sıralı) |
   | Eksenel uzunluklar | `L1..` |
   | Toplam boy | `L_top` |
   | Prizmatik dış boyutlar | `W` (genişlik), `H` (yükseklik), `T` (derinlik/kalınlık) |
   | Delikler | `Dh1`, delik adedi `n_h1` |
   | Yarıçap, açı, pah | `R1`, `A1`, `C1` |
   | Diş | `M1_D` |
   | Ölçülenler | `*_olc` sonekli |

   Açıklama örneği: `[D] "Ø16 +0/-0.5" — görünüş A, sayfa 1 (tol. ortası 15.75)`. Kullanıcı adları sihirbazda değiştirebilir.
   Adlar `ParamTable::isValidName` kuralına uymalıdır; Türkçe karakter kullanılmaz.
8. **Doğrulama.** Çözülmüş sketch çizim geometrisiyle karşılaştırılır (`maxGeomDeviation`). Sapma > tolerans ise sapan varlıklar
   işaretlenir. Çizim ölçekli değilse ya da ölçü yanlış okunduysa sapma burada ortaya çıkar.

### 2.10 Ölçü metni dilbilgisi (WP-2 `parseDimText`)
```
dim      := [count] [prefix] number [fit] [tol] [suffix] | thread | chamfer | angle
count    := INT ("x"|"X"|"×") | INT ("adet"|"DELİK"|"HOLES")
prefix   := "Ø" | "R" | "SØ" | "SR" | "□" | "M"            (M -> thread)
number   := INT | DEC        (ondalık ayırıcı "." veya ",")
fit      := [A-Za-z]{1,2} INT          (H7, g6, js9)
tol      := "±" DEC | ("+" DEC) ("-" DEC | "/" "-" DEC)? | "+0" "-" DEC | stacked(two TextRuns)
suffix   := "max" | "maks" | "min" | "(ref)"  | reference: "(" dim ")"
thread   := "M" number ["x" number] ["-" fitclass] | frac "\"" "-" INT ("UNC"|"UNF"|"UNEF"|"UNS") ["-" INT ("A"|"B")] | "G" frac
chamfer  := number ("x"|"×") number "°" | "C" number
angle    := number "°" [tol]
```

### 2.11 S7 — 3B sentezi (WP-3)
Stratejiler puanlanır ve sıralanır. Her aday `pc::model::regenerate` ile üretilir. Sonuç HLR (`HLRBRep_Algo`) ile çizim görünüşüne
geri izdüşürülür ve çizime göre **yeniden izdüşüm hatası** hesaplanır. Bu, sentezin otomatik doğrulamasıdır.

| # | Strateji | Koşul | Üretilen özellikler |
|---|---|---|---|
| a | **Revolve** (döner parça: enjektör, mil, burç, nozul, flanş) | Görünüşte eksen çizgisi ve ayna puanı ≥0.9. Eksene dik görünüş varsa eş merkezli daireler. | XZ sketch (u=r, v=z) yarım profil → Revolve, eksen V, 360°. İç profil (kesitte görünür, değilse gizli çizgilerden) aynı döngüdedir. Radyal delikler, altıgen, düz yüz ve kama yuvası gibi döner olmayan ayrıntılar (b)'deki gibi ayrı kesimlerle eklenir; faz 1'de bunlar uyarıdır. |
| b | **İki görünüşlü Extrude** (prizmatik: braket, plaka, flanş) | Birincil görünüş + hizalı ikinci görünüş | Birincil profil → Extrude, derinlik = komşu görünüşte izdüşüm yönündeki genişlik (ölçü varsa ölçü). Dairelerin komşu görünüşteki karşılığına bakılır: boydan boya gizli çizgi varsa `Hole through=1`, kısmi ise kör delik (`depth`); karşılık yoksa çizim belirsizdir ve kullanıcıya sorulur. Kademeli bölgeler (farklı derinlikteki iç döngüler) için Extrude cut. "4x Ø6.6" ve dairesel ya da doğrusal düzen → Pattern ya da çoklu Hole noktası. Pah ve köşe yarıçapı → Chamfer/Fillet (kenar seçimi kalıcı adlarla yapılır). |
| b' | **Tek görünüş + kalınlık notu** | "t=5", "KALINLIK 5" notu | Extrude |
| c | **Kesitten Revolve** | Kesit A-A + tarama + eksen | Yarım profil malzeme sınırından alınır. Enjektör türü çizimlerde **en güvenilir yol** budur, çünkü iç geometri gizli çizgiyle değil görünür kenarla verilir. |
| d | **Çok özellikli tanıma** (genel 2–3 görünüşten B-rep: tel kafes → yüz → hacim, Wesley–Markowsky / Shin–Shin) | | Araştırma konusudur, faz 3+. Vaat edilmez. |
| e | **Yalnız sketch** | Her durumda | Sketch'ler düzlemlere yerleştirilir, kullanıcı işlemi kendisi seçer (Çek / Döndür). |

Puan şu bileşenlerden oluşur: geometrik koşul puanı, ölçü kapsamı (sürücü ölçülerle kapanan DOF oranı), yeniden izdüşüm hatası ve
çözülemeyen öğe sayısı. Açıklama metni Türkçedir.

### 2.12 Sihirbaz — "Dosya > Teknik resimden içe aktar…" (WP-3, bindirme görünümü WP-2)
Ribbon'da Ekle sekmesi > "Teknik resim" düğmesi bulunur. `QWizard` modsuz değil, modal çalışır. Sağ tarafta 3B önizleme vardır
(mevcut viewer ile hayalet şekil).

| Adım | İçerik | Kullanıcı düzeltmeleri |
|---|---|---|
| 1 Kaynak | Dosya, sayfa küçük resimleri, tespit edilen tür, DPI/ölçek önizleme, "fotoğraf" onay kutusu | Sayfa ve tür seçimi, DPI |
| 2 Sayfa düzeni | Bindirme: çerçeve, antet, not ve görünüş kutuları. Antet alanları tablosu. Ölçek iki kaynaktan gösterilir. Projeksiyon yöntemi. | Kutu sürükleme, ekleme ve silme; görünüş rolü (Ön/Üst/Yan/Kesit/Detay/Yok say); ölçek; ISO E veya A |
| 3 Geometri | Rol renkleri (görünür siyah, gizli mavi kesik, eksen kırmızı, ölçü yeşil, tarama gri) ve güven renkleri (≥0.9 yeşil, 0.6–0.9 sarı, <0.6 kırmızı kenar parlaması). Tanınmayan bölgeler taranır. | Sil, rol değiştir, birleştir, eksik çizgiyi çiz (çizgi/yay/daire), yakala |
| 4 Ölçüler | Tablo: metin (OCR alternatifleri açılır listede), ayrıştırılmış değer ve tolerans, tür, bağlı geometri (satıra tıklayınca vurgulanır), ölçülen değer, fark, parametre adı, sürücü/referans | Metni düzelt, bağlamayı yeniden seç (iki tık), parametre adı, sürücü bayrağı |
| 5 Sketch | Görünüş başına sketch önizlemesi. DOF, çelişkiler, sapma haritası. Düğmeler: "Kalan serbestlikleri ölçülen değerle kapat", "Kısıtları yeniden çıkar" | Sketch editörünü aç (mevcut SketchEditor, yalnız okuma değil), kısıt sil |
| 6 3B | Strateji listesi (puan ve açıklama), parametreleri (eksen, derinlik, hangi görünüş) ve hayalet önizleme | Strateji seçimi, "Yalnız sketch" |
| 7 Özet | Parametre listesi ([D]/[Ö]/[V]/[T] etiketleri), uyarılar, "Mevcut belgeye ekle / Yeni belge" | Bitir |

- Her adımda "Bu adımdan itibaren yeniden hesapla" düğmesi bulunur. Kullanıcı düzeltmeleri `ir.edits` listesinde tutulur ve yeniden
  hesaplamada tekrar uygulanır.
- Uzun aşamalar (OCR) iş parçacığında çalışır; ilerleme ve iptal desteklenir.

---

## 3. Veri modeli (DrawingIR)

Ayrıntı `INTERFACE.h-draft` dosyasındadır. Özeti:
- `raw` alanı S1 çıktısıdır ve sonradan değiştirilmez. `norm` S2'dir. Sonra `layout`, `annot`, `views`, `sketches[]` ve
  `synthesis[]` gelir. `edits[]` kullanıcı düzeltmeleridir.
- Koordinatlar sayfa mm'sidir (sol alt köşe orijin, y yukarı). S5'ten sonra görünüş koordinatları parça mm'sidir.
- Her öğede bir `id`, `conf` [0,1], `prov` (kaynak nesne ya da handle) ve `mergedFrom` (köken izi) bulunur.
- JSON şeması `pcdrw/1`'dir (nlohmann). Aynı girdi her zaman bayt bayt aynı JSON'u üretir. Böylece testlerde altın dosya (golden)
  karşılaştırması yapılabilir.
- Sketch ve parametreler doğrudan `pc::Sketch` ve `pc::Parameter` tipleriyle taşınır. Dönüşüm katmanı yoktur.

---

## 4. Modül ve dosya düzeni

> **P0 sonrası geçerli düzen `OWNERSHIP.md`'dedir.** Farklar: başlıklar `src/drawing/include/pc/drawing/` altında; `IRJson.cpp` →
> `ir/`; OCR `ocr/` dizininde (OpenCV + Tesseract'a bağlı); `text/` ölçü dilbilgisi; S7 `synth/`; CLI `cli/`; Qt arayüzü designer
> içinde değil `src/drawing_ui/{overlay,wizard}` (`pc_drawing_ui`) altında; `sources_wp*.cmake` yok, her dizin glob ile toplanır;
> `GroundTruth.h` kütüphanede (WP-1, yalnız ekleme). Bağımlılığı bulunmayan dizinin yerine `nodeps/` alt dizini derlenir.

```
src/drawing/                          pc_drawing (static lib; NO Qt)
  CMakeLists.txt                      [WP-3 in P0, then each WP appends its own sources via sources_*.cmake]
  pc/DrawingIR.h  pc/DrawingStages.h  [FROZEN, P0 — owner WP-3]
  IRJson.cpp                          [WP-1]
  sniff/Sniff.cpp                     [WP-1]
  pdf/PdfInterp.{h,cpp} pdf/PdfFonts.{h,cpp} pdf/Cmap.cpp pdf/Agl.cpp pdf/SymbolFonts.cpp pdf/PdfImage.cpp   [WP-1]
  dxf/DxfReader.{h,cpp} dxf/DxfEntities.cpp dxf/DxfDimension.cpp                                            [WP-1]
  normalize/Dashes.cpp normalize/ArcFit.cpp normalize/Snap.cpp normalize/TextMerge.cpp normalize/Arrows.cpp normalize/Hatch.cpp [WP-1]
  raster/Load.cpp raster/Preprocess.cpp raster/Deskew.cpp raster/Rectify.cpp raster/Lines.cpp raster/Circles.cpp
  raster/Arrows.cpp raster/TextDetect.cpp raster/Ocr.cpp                                                   [WP-2]
  text/DimGrammar.cpp text/Confusion.cpp                                                                    [WP-2]
  interp/Layout.cpp interp/TitleBlock.cpp interp/LineRoles.cpp interp/Dimensions.cpp interp/Views.cpp
  interp/SketchBuild.cpp interp/ParamNaming.cpp interp/Synth*.cpp interp/Reproject.cpp interp/Pipeline.cpp interp/Merge.cpp [WP-3]
  eval/Metrics.cpp                                                                                          [WP-1]
src/drawing_qt/                       pc_drawing_qt (Qt6::Gui + Qt6::Pdf)  RenderPdf.cpp  ImageConv.cpp      [WP-2]
src/designer/drawing_import/          (persistcad'e bağlanır; designer'ın sources.cmake'ine tek include satırı)
  OverlayView.{h,cpp} OverlayLayers.cpp                                                                     [WP-2]
  ImportWizard.{h,cpp} Page*.cpp ImportAction.cpp (installDrawingImportAction)                               [WP-3]
tools/drawing_corpus/                 pc_drawing_corpus (Qt6::Gui, OCCT HLR)  Gen*.cpp  Degrade.cpp DxfWriter.cpp [WP-1]
tests/drawing/
  corpus/<family>/<case>/{model.pcad, sheet.pdf, sheet_outline.pdf, sheet.dxf, clean.png, scan_*.jpg, photo_*.jpg, gt.json}
                                      [üretilir; yalnız küçük çekirdek set depoya girer, gerisi ctest öncesi üretilir]
  real/enjektor/                      (ef502211…pdf symlink + gt.json from docs/enjektor/spec.json)       [WP-1]
  GroundTruth.{h,cpp}                 [WP-1]
  test_drawing_ir.cpp test_ingest_pdf.cpp test_ingest_dxf.cpp test_normalize.cpp                      [WP-1]
  test_ingest_raster.cpp test_dimgrammar.cpp test_ocr.cpp                                            [WP-2]
  test_interp_layout.cpp test_interp_dims.cpp test_sketch_build.cpp test_synth.cpp test_e2e_corpus.cpp [WP-3]
  gui/drawing_wizard_harness.cpp      [WP-3]
share/persistcad/tessdata/            eng.traineddata, tur.traineddata (tessdata_fast or _best), pcdim.traineddata (faz 2)
```
Kök `CMakeLists.txt` içine `option(PC_BUILD_DRAWING "Drawing import" ON)` ve `add_subdirectory(src/drawing)` eklenir. Bu değişiklik
P0'da tek seferlik yapılır.

**Designer dokunuşu en az olmalıdır.** Başka bir ajan şu anda `src/designer` üzerinde çalışıyor. Bu iş tamamlanınca
`MainWindowShell.cpp` dosyasına yalnızca şu satır eklenir:
`installDrawingImportAction(this, menuFile_, …)`. Ribbon düğmesi de aynı işlevde `Ribbon` API'si üzerinden eklenir. Değişiklik
`docs/CONTRACT_REQUESTS.md` dosyasında "DRW:" önekiyle istenir ve designer sahibi uygular.

---

## 5. Kütüphaneler, lisanslar ve platformlar

| Kütüphane | Lisans | Ubuntu 24.04 (burada kuruldu) | MSYS2 CLANG64 paketi | Karar |
|---|---|---|---|---|
| qpdf (libqpdf) | Apache-2.0 | 11.9.0 `libqpdf-dev` | `mingw-w64-clang-x86_64-qpdf` 12.4.2 | **Kullan** (PDF ayrıştırma) |
| OpenCV | Apache-2.0 (≥4.5) | 4.6.0 `libopencv-dev` | `mingw-w64-clang-x86_64-opencv` **5.0.0** | **Kullan.** Yalnızca core, imgproc ve imgcodecs. 4.x/5.x farkları ince bir uyum katmanında tutulur. CI iki sürümde de çalışır. |
| Tesseract | Apache-2.0 | 5.3.4 `libtesseract-dev`, `tesseract-ocr-eng/-tur` | `mingw-w64-clang-x86_64-tesseract-ocr` 5.5.3, `…-tesseract-data-eng/-tur` | **Kullan** |
| Leptonica | BSD-2 | 1.82 | `…-leptonica` 1.87.0 | **Kullan** (Tesseract bağımlılığı) |
| QtPdf (Qt6::Pdf, içinde PDFium) | LGPLv3 / GPL / ticari (Qt ile aynı) | `qt6-pdf-dev` 6.4.2 (kuruldu, spike çalıştı) | qt6 paket ailesinde var (`qt6-pdf-debug` görüldü). **Ana paket adı doğrulanacak.** | **Kullan** (yalnızca render; `pc_drawing_qt`) |
| PDFium (doğrudan) | BSD-3 | yok | **yok** | Yedek plan B. Yol API'si (`FPDFPath_*`) var ama derlemesi ağır (gn). pdfium-binaries MSVC derlemelidir; C API'si olduğu için clang64'ten bağlanabilir ama **doğrulanmadı**. |
| MuPDF | **AGPL-3.0** | – | `mupdf` 1.28.5 | **Kullanma** |
| Poppler | **GPL-2/3** | 24.02 (yalnız referans araç olarak) | var (`poppler-qt6` vb.) | **Kullanma.** pdftoppm/pdftotext yalnızca geliştirici aracıdır, ürüne girmez. |
| PoDoFo | LGPL-2 + GPL-2 araçlar | – | 1.1.2 | Gerek yok (qpdf ile aynı işi yapar) |
| libdxfrw / dxflib | **GPL-2** | dxflib var, libdxfrw yok | MSYS2'de görülmedi | **Kullanma.** Kendi okuyucumuz yazılır (~1.5 bin satır). |
| ezdxf (Python) | MIT | pip | – | Yalnızca derlem üretimi ve referans testleri için |
| OCCT HLR (TKHLR) | LGPL-2.1 + istisna | 7.6.3 | mevcut OCCT | Derlem üretimi ve S7 yeniden izdüşüm |

**P0'da kurulan geliştirme paketleri (Ubuntu 24.04, apt):**
`sudo apt-get install -y --no-install-recommends libqpdf-dev libopencv-dev libtesseract-dev libleptonica-dev tesseract-ocr tesseract-ocr-eng tesseract-ocr-tur qt6-pdf-dev`
Sürümler: libqpdf-dev 11.9.0-1.1ubuntu0.1, libopencv-dev 4.6.0+dfsg-13.1ubuntu1, libtesseract-dev 5.3.4-1build5,
libleptonica-dev 1.82.0-3build4, tesseract-ocr-eng/-tur 1:4.1.0-2 (`/usr/share/tesseract-ocr/5/tessdata`), qt6-pdf-dev
6.4.2-final+dfsg-12ubuntu9; ayrıca mevcut libgtest-dev 1.14.0. CMake bunları isteğe bağlı bulur: qpdf `find_package(qpdf CONFIG)`
(yoksa pkg-config `libqpdf`), OpenCV `find_package(OpenCV COMPONENTS core imgproc imgcodecs)`, Tesseract `find_package(Tesseract
CONFIG)` + `Leptonica` (MSYS2) ya da pkg-config `tesseract` + `lept` (Ubuntu), QtPdf `find_package(Qt6 COMPONENTS Pdf)`. Bayraklar:
`PC_DRAWING_HAVE_QPDF/OPENCV/TESSERACT/QTPDF` (her zaman 0/1). `-DPC_DRAWING_DISABLE_OPTIONAL=ON` hiçbirini aramaz (Windows'ta
paketsiz derleme); tek tek kapatmak için `PC_DRAWING_WITH_<X>=OFF`.

**Lisans değerlendirmesi** (hukuk görüşü yerine geçmez, son karar hukuk biriminindir):
- PersistCAD kurum içi bir araçtır. Ancak ileride "Uygulama Olarak Kaydet" ile üretilen uygulamalar ya da kaynak kod başka
  kuruluşlara (program ortakları, alt yükleniciler) verilebilir.
  - GPL bir kütüphaneye bağlanmak bu dağıtımda PersistCAD'in tamamını GPL koşullarına bağlar.
  - AGPL buna ağ kullanımını da ekler ve savunma projelerinde kaynak açma yükümlülüğü kabul edilemez.
  - Aynı tüzel kişilik içinde kullanım çoğu yorumda "dağıtım" sayılmaz. Yine de bu bir **politika riski** olarak kalır.
- Bu nedenle bütün zincir izin verici lisanslarla (Apache/BSD/MIT) ve dinamik bağlı LGPL Qt ile kurulur.
- QtPdf'in LGPLv3 yükümlülükleri (dinamik bağlama, değiştirilebilirlik) mevcut Qt kullanımıyla aynıdır. Yeni bir yükümlülük
  getirmez.
- tessdata modelleri Apache-2.0'dır ve uygulamayla birlikte dağıtılabilir.

**Çevrimdışı çalışma:**
- Ağ çağrısı yoktur. Tesseract modelleri `share/persistcad/tessdata` altında gelir ve `TESSDATA_PREFIX` uygulama göreli yoldan atanır.
- İnce ayarlı `pcdim.traineddata` kurum içinde **tesstrain** ile çevrimdışı üretilir. Eğitim verisi sentetik derlem ve kurum çizim
  fontlarıdır (ISO 3098, Ø ± ° × □ semboller dahil).
- Faz 2'deki isteğe bağlı ONNX metin dedektörü (OpenCV dnn) de yerel dosyadan yüklenir.

**Windows dağıtımı:** clang64 DLL'leri `ldd`/`ntldd` ile toplanır: qpdf, opencv_core/imgproc/imgcodecs, tesseract, leptonica,
libjpeg/png/tiff/webp/zlib ve Qt6Pdf. Bu listeye `windeployqt` çıktısı eklenir. Runner bu modüle bağlanmaz; Save-as-App boyutu
değişmez.

---

## 6. Derlem ve doğruluk ölçütleri

### 6.1 Sentetik derlem (WP-1: `pc_drawing_corpus`)
1. **Model ailesi ve parametre varyasyonları.** Her aile için ≥10 rastgele ama geçerli parametre seti üretilir.

   | Aile | Kaynak |
   |---|---|
   | Braket | `samples/bracket.pcad` |
   | Flanş | `samples/model_flange.pcad` |
   | Plaka + delik düzeni | Yeni |
   | Kademeli mil (pah, kanal, diş) | Yeni |
   | Burç | Yeni |
   | Enjektör gövdesi | `enjektor_09DB01.pcad` içinden Govde |
   | L-köşebent | Yeni |

2. **Görünüşler.** `pc::model::regenerate` ile şekil üretilir, `HLRBRep_Algo` ile Ön/Üst/Yan görünüşler ve kesit A-A (BRepAlgoAPI_Section
   + tarama) çıkarılır. Görünür çizgiler 0.5 mm, gizli çizgiler 0.25 mm ve kesikli, eksen çizgileri silindir eksenlerinden ince
   noktalı çizgi olarak çizilir.
3. **Ölçüler doğru sonuçla birlikte yerleştirilir.** Sketch'lerin ölçü kısıtları (`SkConstraint` + valueExpr) görünüşe izdüşürülür.
   Böylece hangi ölçünün hangi geometriye bağlı olduğu kesin bilinir. Çap, R, açı, "4x", ± ve +/- toleransları, H7 ve diş
   ifadeleri metin şablonlarından üretilir.
4. **Çıktılar.**

   | Dosya | İçerik |
   |---|---|
   | `sheet.pdf` | QPdfWriter, gerçek metin, A4/A3, antet "ÖLÇEK 1:2" |
   | `sheet_outline.pdf` | Metin eğri olarak yazılır (SHX benzetimi) |
   | `sheet_pscript.pdf` | Yaylar parçalanmış, kesikler ayrı segment (PScript5 benzetimi) |
   | `sheet.dxf` | Kendi yazıcımız. DIMENSION + *D blokları, katmanlar, CENTER/HIDDEN çizgi türleri |
   | `clean.png` | 300 dpi |
   | `scan_{a,b,c}.jpg` | Eğiklik 0.5–3°, bulanıklık σ 0.8–1.5, gürültü, JPEG q40–70, 200 dpi, leke ve katlama izi |
   | `photo_*.jpg` | Perspektif ±15°, düzgün olmayan aydınlatma |
   | `gt.json` | İlkeller, ölçüler (değer, tür, bağ), görünüş rolleri ve kutuları, ölçek, beklenen parametreler ve hacim |

5. **Gerçek set.** `ef502211-09-DB01-4A-E.pdf` kullanılır; doğru sonuç `docs/enjektor/spec.json` (profil köşeleri ±0.02 mm) ve ölçü
   listesidir. Kurum arşivinden onaylı 10–20 çizim eklenir. Bu çizimler gizlilik gereği depoya girmez, yerel test dizininde kalır.

### 6.2 Ölçütler (`pc::drawing::evaluate`)
| Ölçüt | Tanım |
|---|---|
| `geom.precision/recall` | Görünür kenar eşleşmesi. Hausdorff < 0.05 mm (vektör) / 0.5 mm (raster), sayfa mm |
| `geom.endpointRms` | Eşleşen uç noktaların RMS hatası |
| `text.cer`, `dim.textExact` | Ölçü metni karakter hata oranı ve birebir doğru oranı |
| `dim.valueAcc` | Ayrıştırılmış nominal ve tolerans doğru |
| `dim.assocAcc` | Doğru geometriye bağlı (gt bağları ile) |
| `view.roleAcc`, `scale.exact` | Görünüş rolü ve ölçek doğru |
| `sketch.dof0`, `sketch.paramErr` | DOF 0 oranı ve parametre değer hatası |
| `solid.volErr`, `solid.hausdorff` | Hacim bağıl hatası ve örneklenmiş Hausdorff (mm) |
| `auto.success` | Hiç düzeltme gerektirmeden hacim hatası < 0.5% ve tüm sürücü parametreler doğru |
| `edits.toSuccess` | Doğru sonuçtan otomatik "kullanıcı düzeltmesi" uygulanır, kaç düzeltme gerektiği sayılır (sihirbaz benzetimi) |

ctest'te faz kapılarının (§7) altına düşen her ölçüt testi kırmızıya çevirir. `test_e2e_corpus` tablo halinde rapor yazar.

---

## 7. Gerçekçi doğruluk beklentileri ve faz kapsamı

### 7.1 Girdi türüne göre beklenti
| Girdi | Geometri | Ölçü metni | Ölçü bağlama | Uçtan uca otomatik (düzeltmesiz) | Not |
|---|---|---|---|---|---|
| DXF (CAD çıkışı, DIMENSION'lı) | Kesin | %100 (metin + ölçülen) | ≥%95 | Basit prizmatik ve döner parçada **≥%80**. Kalanı ≤3 düzeltme. | En kolay girdi |
| DXF (patlatılmış ölçüler) | Kesin | ≥%98 | ≥%85 | ≥%60 | |
| Vektör PDF (gerçek metin) | Baskı hassasiyetinde (enjektör ±0.02 mm @3:1). Yaylar yeniden uydurulur. | ≥%95 (sembol font onarımıyla) | ≥%85 | Basit parçada **≥%60–70**. Kalanı ≤5 düzeltme. | Asıl zorluk yazı tipleri ve kesik çizgiler |
| Vektör PDF (metin eğri) | Aynı | Temiz render'da OCR, ≈%80–90 (faz 2 ince ayar ile) | ≥%75 | ≈%40 | |
| Temiz tarama 300 dpi | ±1 px ≈ ±0.1 mm sayfa (1:1). Parametreler ölçüden gelir. | Kutudan çıktığı haliyle %50–70 (spike), dilbilgisi ve bağlamla %75–85, ince ayarlı modelle %85–95 | ≈%60–75 | Nadiren. Hedef: basit parçada **≤10 düzeltme**. | Kullanıcı her ölçüyü görür |
| Kötü tarama | ±2–3 px | %40–70 | ≈%50 | Yok | Yalnızca sketch ve ölçü önerisi |
| Fotoğraf | ±0.5–1% (homografi) | %30–60 | düşük | Yok | Faz 3, en iyi çaba |

Bir parça türünün otomasyonu zorlaştırdığı durumlar: 3+ görünüş gerektiren karmaşık prizmatik parçalar, eğik yüzler ve yardımcı
görünüşler, döküm/dövme (çok sayıda yarıçap ve eğim), kaynaklı montaj ve sac (açınım). Bunlar faz 1'de **yalnız sketch**
stratejisine düşer.

### 7.2 Faz 1 (ilk teslim) MUTLAKA şunları sağlar
1. S0–S7 hattı, DrawingIR JSON (kaydet, yükle, yeniden oynat) ve sihirbazın 7 adımı. Menü ve ribbon girişi dahildir.
2. **DXF:** LINE/ARC/CIRCLE/LWPOLYLINE/TEXT/MTEXT/INSERT/HATCH/DIMENSION (doğrusal, çap, yarıçap, açısal), katman ve çizgi türü
   rolleri.
3. **Vektör PDF:** Yol ve metin yorumlayıcısı, ToUnicode/Differences/AGL, sembol font onarımı (en az enjektördeki sürücü ve
   QPdfWriter), kesik zincirleme, yay geri uydurma, glif birleştirme, vektör ok ucu, tarama bölgesi (çizgi ve karo).
4. Antetten ölçek ve ölçülerden ölçek doğrulaması. Tek sayfa; tek ya da iki görünüş.
5. **Uçtan uca**, DXF ve vektör PDF girişinde:
   - (a) tek görünüşlü ya da kesitli **döner parça → yarım profil Revolve**, iç profil dahil;
   - (b) **iki görünüşlü prizmatik → Extrude**, boydan boya ve kör Hole ile "nx" delik düzeni;
   - (e) yalnız sketch.
   Ölçüler **adlandırılmış parametre** olur, DOF raporlanır ve ölçülen değerle DOF kapatma yapılır.
6. **Raster:** Yalnızca temiz tarama (≥300 dpi, eğiklik ≤5°). Eğiklik düzeltme, LSD, doğrulanmış daire, ok ucu, satır OCR ve
   dilbilgisi ile yeniden sıralama. Strateji (a) ve (b) **zorunlu sihirbaz onayıyla** kullanılır (strict mode).
7. Derlem üreticisi (4 aile: braket, flanş, mil, enjektör gövdesi) ve ölçüt raporu. ctest kapıları:
   - DXF ve vektör PDF: `dim.valueAcc` ≥0.95, `auto.success` ≥0.7 (DXF) / ≥0.6 (PDF).
   - Temiz PNG: `geom.recall` ≥0.9, `dim.textExact` ≥0.75.
   - Enjektör PDF: Govde profilinin 24 köşesi ±0.05 mm içinde ve 10 ölçünün tamamı okunur.
8. Linux ve MSYS2 CLANG64 derlemesi. tessdata uygulamayla dağıtılır; çevrimdışı çalıştığı test edilir (ağ kapalıyken ctest).

### 7.3 Sonra (faz 2/3)
- **Faz 2:**
  - Metin eğri PDF ve SHX için glif OCR; `pcdim` ince ayarlı Tesseract modeli; ONNX metin dedektörü.
  - Kötü tarama: Sauvola ayarı, inceltmeli vektörleştirme, leke ve katlama temizliği.
  - Detay görünüşü (ölçekli), kırpma yolları, yardımcı (eğik) görünüş, ikili DXF, paper space VIEWPORT.
  - Döner parçada döner olmayan ayrıntılar (radyal delik, altıgen A/F, düz yüz) otomatik; Chamfer/Fillet tanıma.
  - GD&T çerçeveleri yalnızca okunur ve not olarak saklanır; yüzey pürüzlülüğü notları.
  - Çok sayfalı çizim ve parça listesi.
- **Faz 3:** Fotoğraf (perspektif ve aydınlatma), 3 görünüşten genel B-rep yeniden kurma (strateji d), sac açınım, montaj
  çizimleri.

---

## 8. İş paketleri (3 paralel ajan, ayrık dosya sahipliği)

**P0 (2–3 gün, sırayla):**
1. WP-3 `pc/DrawingIR.h` ve `pc/DrawingStages.h` dosyalarını bu taslaktan oluşturur.
2. Tüm aşama fonksiyonları için derlenen saplama (stub) sürümleri yazılır; her saplama `StageResult{false,"uygulanmadı"}` döner.
3. WP-3 `src/drawing/CMakeLists.txt` dosyasını ve `sources_wp1/2/3.cmake` dosyalarını açar.
4. WP-1 `IRJson.cpp` dosyasını yazar ve `test_drawing_ir` gidiş-dönüş testini ekler.

**P0 durumu (15.11.2026): tamamlandı.** Başlıklar `src/drawing/include/pc/drawing/` altında "DrawingIR 1.0" olarak dondu; tüm
giriş noktaları için saplamalar (`P0Stubs.cpp`, `Status::NotImplemented`) ve bağımlılık yedekleri (`nodeps/`, `Status::Unavailable`)
bağlanıyor; `ctest -R drawing` geçiyor. Taslağa göre değişiklikler: ingest/normalize `int& nextId` alır; `StageResult` artık
`Status` + yapısal `Diagnostic` taşır; `UserEdit` işlem kataloğu, enum JSON adları, JSON kuralları, `Attrs` genişletme alanı,
`PdfRasterizer` geri çağrısı (pc_drawing Qt'siz kalır), `OverlayView` ve `DrawingImportHost` API'leri eklendi. Ayrıntı: `OWNERSHIP.md`.
P0 sonunda başlıklar **donar**. Değişiklik isteği "DRW:" önekiyle `docs/CONTRACT_REQUESTS.md` dosyasına yazılır; iki taraf onaylar,
sahip uygular.

| WP | Sahip olduğu dosyalar | Teslim | Zorluk ve önerilen model |
|---|---|---|---|
| **WP-1 Vektör girişi + normalizasyon + derlem** | `src/drawing/{IRJson,sniff,pdf,dxf,normalize,eval}/*`, `tools/drawing_corpus/*`, `tests/drawing/{GroundTruth.*,test_drawing_ir,test_ingest_pdf,test_ingest_dxf,test_normalize}.cpp`, `tests/drawing/real/*` | S0, S1a, S1b, S2, `extractPdfPageImage`, `evaluate`, derlem + gt.json, enjektör gerçek testi | **Orta–yüksek.** PDF font ve kodlama ayrıntıları, ArcFit dayanıklılığı ve HLR tabanlı çizim üretimi. Güçlü model (Opus sınıfı). Derlem üreticisinin rapor ve şablon kısmı daha küçük bir modele bırakılabilir. Süre: ~4–5 hafta. |
| **WP-2 Raster + OCR + ölçü dilbilgisi + bindirme görünümü** | `src/drawing/{raster,text}/*`, `src/drawing_qt/*`, `src/designer/drawing_import/Overlay*`, `share/persistcad/tessdata/*`, `tests/drawing/{test_ingest_raster,test_dimgrammar,test_ocr}.cpp` | S1c, `loadRasterFile`, `parseDimText`, `rankDimTextHypotheses`, `renderPdfPage`, OverlayView | **Yüksek, araştırma riski var.** OCR doğruluğu, sembol karışıklıkları, daire doğrulama ve OpenCV 4/5 uyumu. Güçlü model (Opus sınıfı). Faz 2'deki tesstrain işi ayrı bir alt pakettir. Süre: ~5–6 hafta. |
| **WP-3 Yorumlama + sketch + 3B + sihirbaz** | `src/drawing/pc/*` (P0), `src/drawing/interp/*`, `src/drawing/CMakeLists.txt`, `src/designer/drawing_import/{ImportWizard,Page*,ImportAction}.*`, `tests/drawing/{test_interp_*,test_sketch_build,test_synth,test_e2e_corpus}.cpp`, `tests/drawing/gui/*` | S3–S7, `runPipeline`, `mergeIntoDocument`, sihirbaz, menü/ribbon kancası (CONTRACT_REQUESTS ile) | **En yüksek, kritik yol.** Ölçü bağlama, görünüş ilişkileri, DOF/kısıt seçimi ve strateji puanlama. En güçlü model (Opus sınıfı) ve en deneyimli inceleyici. Süre: ~7–9 hafta. Ara teslim: P1'de DXF ile uçtan uca. |

**Paralellik ve kilitlenmeyi önleme:**
- WP-3, WP-1'i beklememek için ilk iki haftada **DXF'e benzer el yapımı IR JSON** dosyaları ve derlemin `gt.json` dosyalarından
  türetilmiş "mükemmel S2 çıktısı" ile çalışır. `gt → PrimitiveSheet` dönüştürücüsü WP-1'den P0+1 haftada istenir.
- WP-2 bindirme görünümünü IR JSON üzerinden bağımsız geliştirir. WP-3 sihirbaz sayfalarında bu görünümün yalnızca başlıktaki
  API'sini kullanır.
- Ortak dosyalar (kök CMake, designer sources.cmake, MainWindowShell'deki tek satır) yalnızca P0'da ya da CONTRACT_REQUESTS ile
  değiştirilir.

**Kilometre taşları:**

| Taş | Zaman | İçerik |
|---|---|---|
| M1 | Hafta 3 | DXF → Revolve/Extrude uçtan uca (CLI: `pc_drawing_cli in.dxf --out part.pcad --ir ir.json`) |
| M2 | Hafta 6 | Vektör PDF uçtan uca + sihirbaz 1–7. Enjektör Govde yarım profili otomatik çıkar. |
| M3 | Hafta 9 | Temiz tarama strict modda, faz kapıları yeşil, Windows paketi |

---

## 9. Fizibilite spike sonuçları (bu ortamda, 15.11.2026)

Ortam Ubuntu 24.04'tür. apt ile şunlar kuruldu: libopencv-dev 4.6.0, tesseract-ocr 5.3.4 (+eng, tur), libtesseract-dev,
libleptonica-dev 1.82, libqpdf-dev 11.9.0, libpoppler-cpp-dev (yalnız referans), libdxflib-dev (yalnız inceleme) ve
qt6-pdf-dev 6.4.2. ezdxf pip ile kuruldu. Kaynaklar `docs/drawing_import/spike/` altındadır.

**1. Vektör PDF (`pdfvec.cpp`, libqpdf, ~150 satır yorumlayıcı)** — `ef502211-09-DB01-4A-E.pdf` sayfa 1:
- **Çalıştı.** Sayfada 4256 segment var: 3994 çizgi, 262 dolgu, **0 Bezier**. PScript5 sürücüsü yayları doğru parçalarına bölüyor.
- **0 `d` işleci var.** Kesikli çizgiler ayrı kısa segmentler olarak yazılmış (0.5–1.7 pt).
- 260 görüntü XObject'i taramayı oluşturan karolardır.
- Çizgi kalınlıkları açıkça ayrışıyor: 1.435/1.005/0.718/0.502/0.215 pt. Rol sınıflaması bu sayede mümkün.
- 229 metin çalıştırması var. Döndürülmüş metin glif glif yazılmış.
- Semboller ±, Ø ve ° ToUnicode'u olmayan bir TrueType sembol fontunda (`/Differences`: `grave`, `n`, `asciitilde`) duruyor.
  pdftotext bile "14 `0.2" ve "45~" çıkarıyor.
- Sembol onarımı ve glif birleştirme ile (`merge_text.py`) **10 ölçü ifadesinin tamamı** birebir elde edildi: `14±0.2`,
  `7.5±0.5`, `20.5 max.`, `Ø16` (+0/-0.5), `R8` (+0/-0.2), `7.25` (+0/-0.1), `16` (+0/-0.25), `Ø…11.8 max`, `3/8"-40UNS`,
  `9/16"-24UNEF-2A`. Üst üste yazılmış toleranslar ayrı çalıştırmalar halinde geliyor; S4 bunları bağlamalıdır.
  "Ø max.11.8" sıralama hatası glif sıralamasının iyileştirilmesi gerektiğini gösteriyor.
- Sonuç: S2'nin (§2.4) her maddesi gerçek bir ihtiyaçtır. Yorumlayıcının sürekli bakımı gerekir, ama ilave kütüphane gerekmez.

**2. Raster ve OCR (`rasterocr.cpp`, OpenCV 4.6 + Tesseract 5.3.4 C++ API).** Aynı sayfa 300 dpi render edildi (QtPdf ve pdftoppm
ile ayrı ayrı; ikisi de çalıştı).
- LSD 6138 segment buldu. Eğiklik tahmini −0.013° çıktı (sayfa düz).
- HoughCircles 146 aday verdi. Sayfadaki gerçek daire sayısından çok fazla olduğu için çoğu büyük olasılıkla yanlış pozitiftir (tek tek sayılmadı); doğrulama gerekli.
- OCR (seyrek metin, 0° ve 90°, 9 s/sayfa) şunları buldu: R8, −0.2, +0, 7.25, 16, −0.25, 3/8"-40UNS, 9/16"-24UNEF-2A ve 45°/60°/80°.
- Ancak **14±0.2, 7.5±0.5, Ø16, Ø11.8 ve 20.5 kaçırıldı.** Ölçülerin yaklaşık %50'si okundu.

**3. Sentetik sayfa (`synth.py`, 12 etiket, DejaVu font):**

| Durum | Sonuç |
|---|---|
| Temiz sayfa, seyrek OCR | 6/12 bulundu; bunların biri `12±0.1` → `12+0.1` diye yanlış okundu |
| 1.5° eğik + bulanık + gürültülü + JPEG60 | Eğiklik **−1.499°** ölçüldü (gerçek 1.5°); yine 6/12 (`M8x1.25` → `M8x1.2`) |
| Ø'lü ifadeler | **Hiçbiri** okunmadı |
| Satır kırpma + `--psm 7` | `O20 H7`, `4x 06.6`, `240 +0.05/-0`, `OLCEK 1:2` (tur ile `ÖLCEK 1:2`). Hepsi §2.3'teki karışıklık kümesiyle düzeltilebilir. |

Sonuç: Raster tarafının iyileştirme yolu belli; satır tabanlı OCR, dilbilgisi ve bağlam kullanılır, faz 2'de ince ayarlı model
gelir. Kutudan çıktığı haliyle OCR ürün için yetersizdir.

**4. DXF (`dxfmini.cpp`, ~40 satır grup kodu okuyucu).**
- ezdxf ile üretilen R2010 dosyası okundu: LINE (CENTER katmanı), LWPOLYLINE, ARC, CIRCLE, TEXT `ÖLÇEK 1:1` (UTF-8 doğru) ve
  2 DIMENSION (tür 32/35, blok `*D1/*D2`, metin `<>`).
- **Grup 42 yoktu.** Ölçülen değer tanım noktalarından hesaplanmalıdır.

**5. QtPdf render (`qtpdfrender.cpp`).**
- Qt6::Pdf ile 300 dpi render çalıştı. QPA offscreen ile GUI gerektirmiyor.
- Saydam arka planın beyazla birleştirilmesi gerekiyor.
- Bu render ile OCR sonuçları pdftoppm ile aynı sayıda bulgu verdi.

**6. MSYS2 CLANG64 paketleri:** qpdf 12.4.2, tesseract-ocr 5.5.3, leptonica 1.87.0, opencv 5.0.0 ve tesseract-data-* paketleri var.
mupdf 1.28.5 (AGPL) ve podofo 1.1.2 (LGPL/GPL) de var. PDFium paketi bulunamadı. Doğrudan paket deposuna erişim bu ortamdan
kapalıydı, bilgiler packages.msys2.org sayfalarından alındı.

---

## 10. Riskler

| Risk | Etki | Azaltma |
|---|---|---|
| PDF üreticileri arasındaki farklar (font kodlaması, Type3 font, kırpma, Form XObject iç içeliği, metnin eğri olması) | Vektör PDF başarısı üreticiye göre çok değişir | Kurum içinde kullanılan CAD/PDF sürücülerinden örnek set toplanır (Inventor, NX, SolidWorks, AutoCAD, PScript5); sembol font tablosu genişletilebilir yapılır; glif OCR yedeği eklenir |
| OCR'ın sembolleri (Ø ± °) okuyamaması (spike'ta kanıtlandı) | Raster ölçülerin önemli kısmı yanlış okunur | Satır OCR + dilbilgisi + geometrik bağlam + n-best; faz 2'de tesstrain ince ayarı; sihirbazda zorunlu onay |
| Ölçü–geometri bağlamanın belirsizliği (yoğun çizimler, zincir/baz/ordinat ölçüleri, kırık uzatma çizgileri) | Yanlış parametre bağlanır | Tutarlılık testi (ölçülen ≈ nominal veya tol ortası), çoklu hipotez + puanlama, sihirbazda iki tıkla yeniden bağlama |
| İki görünüşten 3B'nin belirsizliği (gizli çizgi yokluğu, yetersiz görünüş) | Yanlış ya da eksik model | Strateji kapsamını dar tut (a, b, c); yeniden izdüşüm hatasıyla doğrula; belirsizlikte kullanıcıya sor; "yalnız sketch"e düş |
| Çizim ölçeğe uygun değil (NTS) ya da şematik (enjektör iç geometrisi) | Ölçülen değerler yanıltıcı | Ölçü sürücüdür; ölçülen [Ö] parametreler açıkça etiketlenir; ölçek çelişkisi uyarısı |
| Sketch modülünde spline/elips olmaması | Eğrisel profiller yalnızca yaklaşık | Yay zinciri uydurma + sapma raporu; gerekirse ayrı sketch genişletmesi (kapsam dışı, CONTRACT_REQUESTS) |
| OpenCV 4.6 (Ubuntu) ve 5.0 (MSYS2) API farkları; Tesseract 5.3 ve 5.5 farkları | Platformlar arasında farklı sonuç | Yalnızca core/imgproc/imgcodecs; ince adaptör katmanı; iki platformda CI; altın sonuç testlerinde tolerans kullanılır, birebir eşitlik aranmaz |
| QtPdf'in MSYS2 clang64'te ayrı paket olarak bulunmaması | Taranmış PDF render'ı Windows'ta eksik kalır | Plan B: qpdf ile gömülü görüntü çıkarma (taranmış PDF'lerin büyük kısmı tek DCT/CCITT görüntüdür); CCITT çözücüyü kendimiz yazarız ya da Leptonica'nın G4 desteği kullanılır |
| Lisans yanlışlıkla GPL'ye kayar (ör. poppler'ın geçişli bağımlılık olarak gelmesi) | Dağıtım yükümlülüğü doğar | CMake'te izin verilen kütüphane listesi tutulur; `ldd`/`ntldd` çıktısı lisans denetiminden geçer (CI adımı) |
| Designer üzerinde eşzamanlı çalışma | Birleştirme çakışması | Designer'a tek satırlık kanca; tüm UI kodu `src/designer/drawing_import/` altında |
| Gizli ve kontrollü teknik resimler | Bilgi güvenliği | Tamamen çevrimdışı; IR yan dosyası çizimin türevidir ve aynı gizlilik sınıfıyla saklanır; derleme ve test için gerçek çizimler depoya girmez |
| Performans (OCR 9 s/sayfa tek iş parçacığı) | Sihirbaz yavaş hisseder | Bölge başına paralel OCR (Tesseract örneği iş parçacığı başına), önbellek (dosya sha256 + aşama sürümü), ilerleme ve iptal |
