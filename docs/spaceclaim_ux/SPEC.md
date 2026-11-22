# PersistCAD — SpaceClaim kullanıcı deneyimi (UX) spesifikasyonu

Durum: TASLAK v1 (Kasım 2026). Yalnızca spesifikasyondur; ürün kodu bu belgeyle değişmez.
Hedef: SpaceClaim'e (Ansys SpaceClaim / Discovery SpaceClaim) alışmış bir kullanıcı PersistCAD tasarım
ortamında sketch çizip 3B gövde oluştururken **yabancılık hissetmemeli**: aynı yerlerde aynı düğmeler, aynı tek
tuş kısayolları, aynı fare hareketleri, aynı "seç → çek → sayı yaz → Enter" akışı.
Arka planda model **parametrik geçmiş ağacı** olarak kalır (Sketch, Extrude, Revolve, Fillet, …); doğrudan
modelleme hissi bu ağaca eşlenir. Kaydetme biçimi (`.pcad`), parametreler, "Uygulama Olarak Kaydet",
çoklu gövde, datum düzlemleri, kesit görünümü ve imalat denetimi aynen korunur.

Kurallar:
* Arayüz dili Türkçedir. Bu belgede her öğe "Türkçe etiket (SpaceClaim İngilizce terimi)" biçiminde yazılır,
  ör. **Çek (Pull)**. Araç ipuçlarında (tooltip) İngilizce terim parantez içinde gösterilir, böylece
  SpaceClaim kullanıcısı tanıdığı adı görür.
* Üçüncü parti ribbon kütüphanesi kullanılmaz: Qt6 Widgets (`QTabBar`, `QStackedWidget`, `QToolBar`,
  `QToolButton`, `QTabWidget`, `QDockWidget`) yeterlidir.
* Mevcut ekran görüntüleri: `current_3d.png`, `current_sketch.png` (bu dizinde, Kasım 2026 derlemesi).

Doğrulama işaretleri (SpaceClaim davranışı için):
* **[D]** Resmi Ansys SpaceClaim 2025 R2 çevrimiçi yardımında (ansyshelp.ansys.com, `.../spaceclaim/Discovery/user_manual/...`)
  doğrulandı. Aynı çekirdeği kullanan DesignSpark Mechanical'ın kısayol listesi de aynı değerleri veriyor.
* **[U]** Uygulayıcı deneyimi veya üçüncü taraf kaynak (PADT blog yazıları, eğitim notları). Resmi
  sayfada bulunamadı. Uygulamadan önce gerçek bir SpaceClaim kurulumunda kontrol edilmeli.
* **[PC]** SpaceClaim'de karşılığı yok, PersistCAD'e özgü (parametre, imalat, geçmiş ağacı).

---

## 0. Mevcut durumun özeti (kaynak kodundan)

| Alan | Bugün (src/designer, src/viewer) |
|---|---|
| Pencere | Klasik `QMenuBar` (Dosya, Düzen, Ekle, Görünüm, İmalat, Yardım) ve "Ana" araç çubuğu (Yeni, Aç, Kaydet, App olarak kaydet, Geri Al, Yinele, Sketch, Düzlem, Extrude, Revolve, Delik, Fillet, Chamfer, Shell, Pattern, İzo/Üst/Ön/Sağ, Sığdır, Kesit, Denetle, İmalat ayarları). |
| Dock'lar | Sol: "Özellik Ağacı" (`dockTree`, `tree_`) ve altında "Gövdeler" (`dockBodies`, `bodiesTree_`). Sağ: "Parametreler" (`dockParams`, `paramView_` QTableView + QStandardItemModel). Alt: "İmalat Denetimi" (`dockIssues`, `issueTable_`). |
| Sketch | `SketchEditor`, `QStackedWidget` içinde 3B görünümün **yerine** açılan ayrı bir 2B QPainter alanı (3B model altlığı yok). Araçlar: Seç, Nokta, Çizgi, Dikdörtgen, Çember, Yay, Yapı çizgisi, Sil, Sığdır, Düzlem ofseti (ExprEdit), Sketch'i Bitir, Vazgeç. İkinci satırda 11 kısıt ve 7 ölçü düğmesi. Ölçü = önce seç, sonra düğme, sonra `DimensionDialog`. Çizerken sayı yazılamaz. |
| 3B görünüm | `ShapeViewer` (ortografik izdüşüm, az/el turntable kamera). Sol sürükle = döndür, orta sürükle veya Shift+sol = kaydır, tekerlek = imlece doğru yakınlaştır. Sol tık = yüz seç, Ctrl+tık = kenar seç (kenar menüsü açılır), sağ tık = kenar/datum menüsü. |
| Özellik oluşturma | Her özellik için modsuz `FeatureDialog` (ExprEdit alanları, parametre seçici, "Yeni…"). Kenar ve yüz listeleri görünümden seçilerek doldurulur. |
| Commit hattı | `tryCommit` → `mfg::validateChange` → gerekirse `ChangeReviewDialog` → `pushCommit` (QUndoStack). Parametreler `installParameterCommitHook` ile anında işlenir. |
| Kısayollar | Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Shift+A (App olarak kaydet), Ctrl+Z/Undo, Redo, **F = Sığdır**, **K = Kesit**, F5 = Denetle, sketch'te Del, Esc, F. |

Çatışmalar: SpaceClaim'de **K = Sketch modu**, **F = Doldur (Fill)**, **X = Kesit modu (Section)**. Bugünkü K ve F
eşlemeleri değişecektir (bkz. §4).

---

## 1. (a) Hedef pencere düzeni

### 1.1 ASCII taslak (1600×1000, 3B modu, Çek aracı etkin, bir sketch bölgesi seçili)

```
+------------------------------------------------------------------------------------------------------------------+
|[Dosya v]|[Yeni][Ac][Kaydet][GeriAl][Yinele]|  Tasarim | Goruntu | Olcum | Imalat |          bracket.pcad - PersistCAD |
+------------------------------------------------------------------------------------------------------------------+
| Yonlendir            | Sketch                         | Mod        | Duzenle              | Kesisim | Olustur      |
| [Ana]  [Plan]        | [Cizgi][Dikdortgen][Cember]    | [Sketch K] | [Sec ][Cek ][Tasi]   |[Birles- | [Duzlem]     |
| [Sigdir][Gorunum v]  | [Yay ][Nokta][Yapi ciz.][Kirp] | [Kesit  X] | [  S ][  P ][  M ]   | tir  I] | [Desen v]    |
| [Cevir][Kaydir][Yak.]|                                | [3B     D] | [Doldur F]           |         | [Kabuk][Delik]|
|                      |                                |            |                      |         | [Klasik v]   |
|______Yonlendir_______|_____________Sketch_____________|____Mod_____|_______Duzenle________|_Kesisim_|___Olustur____|
+---------------------------+------------------------------------------------------------------------------------+
| Tasarim1 (bracket)        |  Cek (Pull): Bir yuz, kenar ya da sketch bolgesi secin; oku surukleyin ya da   [Sec] |
|  [x] Govdeler             |  bir deger yazin.                                                              [Yon] |
|   [x] # Extrude1  37818mm3|                                                                               [Dondur]|
|  [x] Duzlemler            |                        ^  oku (tutamac)                                       [Kadar]|
|   [ ] Plane1              |                        |                                                      [Cetvel]|
|  [x] Sketch'ler           |         +--------------|---------------+                                            |
|   [ ] Sketch1 (XY)        |        /   secili bolge (mavi)         /   +-----------+                            |
|   [x] Sketch5 (yuz)       |       +-------------------------------+    |  8 mm  [P]|  <- boyut kutusu (Tab)     |
|                           |                                            +-----------+                            |
|                           |       [+Ekle][-Kes][Ayri][Cetvel][Grup]   <- mini arac cubugu (imlec yaninda)     |
|                           |                                                                                     |
|[Yapi][Gecmis][Secim][Gruplar]                                                                                   |
+---------------------------+                                                                                     |
| Secenekler - Cek          |                                                                                     |
|  Birlestirme              |                                                                                     |
|   (o) Otomatik  ( ) Ekle  |                                                                                     |
|   ( ) Kes  ( ) Ayri govde |                                                                                     |
|  Kenar: (o) Yuvarlat      |                                                                                     |
|         ( ) Pah           |                                                                                     |
|  [ ] Iki yone cek         |                 Z                                                                   |
|  [ ] Cetvel               |                 |_ Y                                                              |
|  [Grup olustur (Ctrl+G)]  |                /X                                                                   |
|[Secenekler][Ozellikler]   |                                                                                     |
+---------------------------+-------------------------------------------------------------------------------------+
| Imalat Denetimi (daraltilabilir dock, varsayilan 140 px)                                                       |
+------------------------------------------------------------------------------------------------------------------+
| Cek: bolge Sketch5:r0 secili | 1 bolge | Hata:0 Uyari:1 | [Akilli v] | Yenileme 58 ms | Hacim 37818 mm3 | [<][>]|
+------------------------------------------------------------------------------------------------------------------+
```

Sketch modunda (K) aynı düzen kalır; görünümün içinde sketch ızgarası, ızgaranın köşesinde **sketch mini araç
çubuğu** ve ribbon'da bağlamsal **Kısıtlar**, **Ölçüler** ve **Sketch** (Bitir/Vazgeç/Düzlem ofseti) grupları görünür:

```
| ...ribbon Tasarim sekmesi (yukaridaki gibi) ... | Kisitlar (baglamsal)                    | Olculer (baglamsal)          | Sketch Oturumu           |
|                                                | [Cakisik][Yatay][Dikey][Paralel][Dik]   | [Mesafe][Yatay m.][Dikey m.] | [Sketch'i Bitir]          |
|                                                | [Teget][Esit][Sabit][Orta n.][Simetrik] | [Uzunluk][Yaricap][Cap][Aci] | [Vazgec]                  |
|                                                | [Es merkezli]                           |                              | Duzlem ofseti: [0    ][P] |
+-------------------------------------------------------------------------------------------------------------------------------------------+
|  (Yapi paneli)  |  +--- sketch mini arac cubugu: [3B'ye Don][Yeni sketch duzlemi][Izgarayi tasi][Plan gorunumu] ---+        [Sec]   |
|                 |  :  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  :   <- izgara (sketch duzleminde, 3B)        [Cizgi] |
|                 |  :  .  +-------------------------+  .  .  .  .  .  .  .  .  :                                          [Dikd.] |
|                 |  :  .  |                         |  .  .  .  .  .  .  .  .  :                                          [Cember]|
|                 |  :  .  |    +--------+           |  .  .  .  .  .  .  .  .  :                                                  |
|                 |  :  .  +----| 60  mm |-----------+  <- canli olcu alanlari (Tab ile gecis)                                     |
|                 |  :  .  .  . +--------+  .  .  . [40 mm]  .  .  .  .  .  .  .  :                                                  |
```

### 1.2 Bölgeler ve Qt yapısı

| # | Bölge (SpaceClaim) | Qt uygulaması | objectName |
|---|---|---|---|
| 1 | **Dosya menüsü (File menu)** [D] | Ribbon sekme çubuğunun solunda mavi `QToolButton` (`InstantPopup`) ve bir `QMenu` ("&Dosya"). İçerik bugünkü Dosya menüsüyle aynı: Yeni, Aç..., Kaydet, Farklı Kaydet..., Uygulama Olarak Kaydet... (Save as App), Dışa Aktar ▸ STEP/STL, Son dosyalar ▸, Ayarlar ▸ (İmalat ayarları...), Hakkında, Çıkış. | `ribbonFileButton`, menü `menuFile` |
| 2 | **Hızlı erişim araç çubuğu (Quick Access toolbar)** [D] | Dosya düğmesinin sağında küçük ikonlu `QToolBar` (16 px): Yeni, Aç, Kaydet, Geri Al, Yinele. Başlık çubuğuna gömülmez (Qt Widgets ile taşınabilir değil). | `quickAccessBar` |
| 3 | **Ribbon** [D] | `Ribbon` sınıfı: `QTabBar` (sekmeler) + `QStackedWidget` (sayfalar). Her sayfa yatay `RibbonGroup` listesidir. Her grup altta gri başlık etiketi olan bir `QFrame`'dir ve düğmeleri bir veya iki **gömülü `QToolBar`** içinde tutar (hareket ettirilemez, yüzdürülemez). Büyük düğme 32 px ikon + alt yazı (`Qt::ToolButtonTextUnderIcon`), küçük düğmeler 16 px ikon + yan yazı ile 3 satır halinde gösterilir. `QToolBar` kullanmak `widgetForAction()` ve test düzeneklerinin `QToolButton::defaultAction()` aramalarını korur. Ribbon yüksekliği sabit 96 px'tir. Çift tıkla daraltma (ribbon minimize) 2. fazdadır. | `ribbon`, sekmeler `ribbonTab.design` vb., gruplar `ribbonGroup.<anahtar>` |
| 4 | **Etkin araç göstergesi (Active tool indicator)** [D] | Etkin araç düğmesi turuncu arka planla vurgulanır (`QToolButton:checked { background:#f7c97a; border:1px solid #e09a2b; }`). | — |
| 5 | **Tasarım penceresi (Design window)** [D] | `ShapeViewer` merkezi widget olur. `QStackedWidget` kaldırılır ve `SketchEditor` görünümün üzerine şeffaf bir **kaplama (overlay)** olarak yerleşir (§6.1). | `viewer` |
| 6 | **Mini araç çubuğu (Mini-toolbar)** [D] | Görünümün çocuğu olan çerçevesiz yarı saydam `QWidget`. Son tıklama noktasının sağ altında çıkar, imleç uzaklaşınca saydamlaşır. İçeriği etkin aracın en sık kullanılan seçenekleridir. | `miniToolbar` |
| 7 | **Araç kılavuzları (Tool guides)** [D] | Görünümün sağ kenarında dikey ikon şeridi (`ToolGuideBar`, görünümün çocuğu). Tek seçimli `QToolButton` grubu. Tab ile döngü yapılır. | `toolGuideBar` |
| 8 | **Durum çubuğu (Status bar)** [D] | `QStatusBar`. Soldan sağa: mesaj alanı (`showMessage`, bugünkü gibi), hızlı ölçüm etiketi, seçim listesi etiketi, hata/uyarı ikonu, seçim filtresi açılır kutusu (`selectionFilter`), önceki/sonraki görünüm düğmeleri. Kalıcı PersistCAD etiketleri (Yenileme ms, Hacim, Sketch DOF, Hata/Uyarı) korunur. | `statusMessage`, `quickMeasure`, `selectionList`, `selectionFilter` |
| 9 | **Yapı paneli (Structure panel)** [D] | Sol `QDockWidget` (`dockStructure`). İçinde sekmeleri **altta** olan `QTabWidget` (`QTabWidget::South`): **Yapı (Structure)**, **Geçmiş (History)** [PC], **Seçim (Selection)**, **Gruplar (Groups)**. Katmanlar (Layers) ve Görünümler (Views) sekmeleri yoktur (§3.9). | `structureTabs` |
| 10 | **Seçenekler paneli (Options panel)** [D] | Sol altta ikinci `QDockWidget` (`dockOptions`). Sekmeleri altta olan `QTabWidget`: **Seçenekler (Options)** ve **Özellikler (Properties)**. Başlık "Seçenekler - <araç>" biçimindedir (SpaceClaim'in "Options - Selection" başlığına benzer). [U]: SpaceClaim'de Seçenekler ve Özellikler panellerinin varsayılan olarak sol altta sekme olarak durduğu deneyime dayanır. Resmi yardım yalnızca Özellikler panelinin "Yapı ağacı ve Seçenekler panelinin altında, solda" olduğunu söyler [D]. | `optionsTabs`, `optionsPanel`, `propertiesPanel` |
| 11 | **Sağ tık bağlam menüsü (context menu)** | Görünümde ve ağaçlarda `QMenu`. İçerik §3.10'dadır. | `viewContextMenu` |
| 12 | **Radyal menü (Radial menu), O** [D] | 2. faz: imleç etrafında 8 dilimli `QWidget` (Seç, Çek, Taşı, Doldur, Sketch modu, 3B modu, Plan görünümü, Ana görünüm). 1. fazda O tuşu yalnızca durum çubuğunda "Radyal menü henüz yok" mesajı verir. | `radialMenu` |
| 13 | İmalat Denetimi [PC] | Mevcut `dockIssues` korunur. Altta yerleşir, varsayılan yükseklik 140 px, kapatılabilir ve İmalat sekmesinden geri açılır. | `dockIssues` |

Ana pencere düzen kuralları:
* `menuBar()` gizlenir (`menuBar()->setVisible(false)`), menü nesneleri silinmez. Tüm `QMenu` ve `QAction` nesneleri
  `MainWindow` soyundan kalır, çünkü test düzenekleri `findChildren<QAction*>()` ve `findChildren<QMenu*>()` kullanır.
  Kısayolların ribbon sekmesi görünmezken de çalışması için her global eylem `MainWindow::addAction()` ile pencereye eklenir.
* Eski "Ana" araç çubuğu (`mainBar_`) kaldırılır. Onun yerine ribbon gelir.
* Varsayılan genişlikler: sol dock'lar 330 px, İmalat Denetimi 140 px yüksek. Dock durumu `QSettings` ile saklanır.
  "Görüntü > Düzeni sıfırla" eylemi varsayılana döndürür.
* Pencere başlığı `<dosya> - PersistCAD [*]` biçiminde kalır.

### 1.3 Ribbon sekmeleri ve grupları

**Tasarım (Design)** [D: sekme ve Edit grubunun içeriği, Pull/Move/Fill araçları. U: grupların sırası]

| Grup (SpaceClaim) | Düğmeler (Türkçe (İngilizce) — kısayol) | Not |
|---|---|---|
| Pano (Clipboard) | — | **Desteklenmez** (§3.9). Grup hiç gösterilmez. |
| Yönlendir (Orient) [D: grup adı] | **Ana görünüm (Home)** H · **Plan görünümü (Plan View)** V · **Sığdır (Zoom Extents)** Z / Alt+Z · **Görünümler ▾** (İzometrik, Üst, Alt, Ön, Arka, Sol, Sağ; menü başlığı "Görünüm") · **Çevir (Spin)**, **Kaydır (Pan)**, **Yakınlaştır (Zoom)** (sol sürükleme kipini değiştiren, birbirini dışlayan geçiş düğmeleri) | "Sığdır" eyleminin metni değişmez (test). |
| Sketch (Sketch) | **Çizgi (Line)** L · **Dikdörtgen (Rectangle)** R · **Çember (Circle)** C · **Yay (Sweep Arc)** · **Nokta (Point)** · **Yapı çizgisi (Construction Line)** · **Kırp (Trim Away)** T | Eylemleri WP-B'nin ToolManager'ı üretir. Metinler bugünküyle aynıdır: "Çizgi", "Dikdörtgen", "Çember", "Yay", "Nokta", "Yapı çizgisi". Yeni olan yalnızca "Kırp". |
| Mod (Mode) [D] | **Sketch modu (Sketch Mode)** K · **Kesit modu (Section Mode)** X · **3B modu (3D Mode)** D | Birbirini dışlayan `QActionGroup`. "Kesit modu" düğmesi `SectionControls::toggleAction()` nesnesinin kendisidir (metni "Kesit" kalır, ipucu "Kesit modu (Section Mode) — X"). |
| Düzenle (Edit) [D] | **Seç (Select)** S · **Çek (Pull)** P · **Taşı (Move)** M · **Doldur (Fill)** F | Değiştir (Replace) ve Yüzü esnet (Tweak Face) **desteklenmez**. "Seç" tek eylemdir ve hem 3B'de hem sketch'te kullanılır. |
| Kesişim (Intersect) [D: grup var. U: içerik] | **Birleştir (Combine)** I | Gövdeyi böl (Split Body), Böl (Split) ve İzdüşür (Project) **desteklenmez**. |
| Oluştur (Create) [U] | **Düzlem (Plane)** · **Desen ▾** (Doğrusal desen (Linear Pattern), Dairesel desen (Circular Pattern)) · **Kabuk (Shell)** · **Delik (Hole)** [PC] · **Klasik ▾** [PC] | "Klasik ▾" bugünkü diyalog eylemlerini aynı metinlerle tutar: "Sketch...", "Düzlem...", "Extrude...", "Revolve...", "Delik...", "Fillet...", "Chamfer...", "Shell...", "Pattern...", "Gövdeleri Birleştir (Combine)...". Eksen (Axis), Nokta (Point), Orijin (Origin), Ayna (Mirror), Ofset (Offset) **desteklenmez**. |
| Kısıtlar (Constraints) [PC, bağlamsal] | Çakışık, Yatay, Dikey, Paralel, Dik, Teğet, Eşit, Sabit, Orta nokta, Simetrik, Eş merkezli | Yalnızca Sketch modunda görünür. Metinler aynıdır. |
| Ölçüler (Dimensions) [PC, bağlamsal] | Mesafe, Yatay mesafe, Dikey mesafe, Uzunluk, Yarıçap, Çap, Açı | Yalnızca Sketch modunda görünür. Metinler aynıdır. |
| Sketch Oturumu [PC, bağlamsal] | **Sketch'i Bitir** · **Vazgeç** · Düzlem ofseti: [ExprEdit `sketchOffset`] | Yalnızca Sketch modunda görünür. "Sketch'i Bitir" = 3B'ye dön (commit). |

**Görüntü (Display)**: Kenarlar, Datum düzlemleri (`actShowPlanes`), Sketch ölçüleri (3B) (`actSketchDims3D`), Eksenler,
Kesit düzlemi ▸ (`menuSectionPlane`: XZ/YZ/XY/Çeyrek kesit/Ters çevir, objectName'ler aynı), Gövde renkleri, Düzeni sıfırla.
Grup adları: Görünürlük, Kesit, Pencere. Bu sekmedeki "Görünüm ▾" açılır düğmesi, başlığı **"Görünüm"** olan ve
`section_->toggleAction()` eylemini de içeren bir `QMenu` taşır. `gui_harness_v4` bu menüyü arar.

**Ölçüm (Measure)** [D: sekme var, U: içerik]: **Ölç (Measure)** E (2. faz) · **Kütle özellikleri (Mass)** (seçili gövdeler için
hacim, alan, ağırlık merkezi ve sınır kutusu. Kaynak: `pc::model::massProperties`).

**İmalat (Manufacturing)** [PC]: Ayarlar... · Şimdi Denetle (F5) · İmalat Denetimi paneli (aç/kapa) · **Uygulama Olarak Kaydet...**
(Ctrl+Shift+A). SpaceClaim'in Onar (Repair), Hazırla (Prepare), Detay (Detail), Sac metal (Sheet Metal) ve Faset (Facets)
sekmelerinin karşılığı **yoktur** (§3.9).

---

## 2. Etkileşim modeli (SpaceClaim davranışı → PersistCAD'deki karşılığı)

### 2.1 Modlar (Design Modes) [D]
SpaceClaim'de üç tasarım modu vardır: Sketch, Kesit (Section) ve 3B. Sketch modu 3B nesneleri değiştirmez, Kesit modu
3B nesneleri kesit üzerinden düzenler [D].

| Tuş | SpaceClaim | PersistCAD |
|---|---|---|
| **K** | Sketch modu: ızgara bir düzlemde görünür | Seçim düzlemsel bir yüz, datum düzlemi ya da sketch ise o düzlemde yeni sketch açılır (`OnFace` / `OnPlane`). Seçili bir sketch varsa o sketch düzenlenir. Seçim yoksa: belge boşsa XY'de sketch açılır; değilse **"Sketch düzlemi seç"** durumuna geçilir: imleç altındaki düzlemsel yüzde ve orijindeki XY/XZ/YZ önizleme karelerinde ızgara önizlemesi çıkar, tıklama düzlemi seçer. [U]: SpaceClaim'de 3B modunda bir sketch aracı (L/R/C) seçmek de sketch moduna geçirir. PersistCAD de aynısını yapar. |
| **X** | Kesit modu | Mevcut **görünüm-yalnız kesit** (`SectionControls`) açılır. Seçili bir datum düzlemi ya da düzlemsel yüz varsa kesit `setSectionPlane(origin, normal)` ile o düzlemden alınır. Yoksa XZ ön ayarı kullanılır. **Kesitte düzenleme desteklenmez** (§3.9). X'e tekrar basmak ya da D kesiti kapatır. |
| **D** | 3B modu | Açık sketch commit edilir (`finishSketch(true)` → `tryCommit`). Boş sketch atılır. Kesit kapatılır. Etkin araç **Seç** olur. |

Durum makinesi `MainWindow::setDesignMode(DesignMode)` içinde, sahibi WP-A'dır. Geçiş kuralı: etkin araç meşgulse
(`IActiveTool::busy()`, ör. boyut kutusu açık ya da sürükleme sürüyor) önce araç iptal edilir (Esc anlamı).

### 2.2 Sketch akışı [D/U]
* Kaynaklar: sketch mini araç çubuğu (4 düğme: **3B'ye Dön (Return to 3D Mode)**, **Yeni sketch düzlemi seç (Select New
  Sketch Plane)**, **Izgarayı taşı (Move Grid)**, **Plan görünümü (Plan View)**) [D]. 3B'ye Dön, Çek aracına geçer ve kapalı
  döngüler yüzey veya yüz olur [D]. Ctrl+→ ve Ctrl+← ızgarayı içeri ve dışarı taşır [D]. Dikdörtgen sketch ızgarası
  eksenleri boyunca çizilir [D].
* PersistCAD:
  - **3B'ye Dön**: sketch commit edilir, araç **Çek** olur ve az önce bitirilen sketch'in bölgeleri vurgulanır.
    Kullanıcı bir bölgeye tıklayıp hemen çekebilir.
  - **Yeni sketch düzlemi seç**: açık sketch commit edilir ve "Sketch düzlemi seç" durumuna geçilir (K ile aynı).
  - **Izgarayı taşı**: Taşı aracını sketch düzlemi üzerinde çalıştırır. Normal ok `offsetExpr` değerini değiştirir. Ctrl+→ ve Ctrl+←
    ofseti 1 ızgara adımı ileri veya geri alır. Ifade bir parametreye bağlıysa parametre değişir (§2.6).
  - **Plan görünümü (V)**: kamera sketch normaline bakar, sketch `v` ekseni yukarı gelir.
* Kapalı döngüler 3B'de **bölge (region)** olarak gösterilir: `pc::sketch::buildProfile` yüzleri yarı saydam açık mavi
  çizilir ve seçilebilir. Ad biçimi `sketch:<sid>:r<k>` olur (k = profil yüz sırası). Bir katı özellik tarafından tüketilmiş
  sketch'ler bugünkü gibi gizlidir. Tüketilmemiş sketch'in bölgeleri görünür.

### 2.3 Çek (Pull) [D]
Resmi tanım: Çek; yüzleri ötelemek, uzatmak, döndürmek, süpürmek ve eğim vermek, kenarları yuvarlatmak, pahlamak,
uzatmak, kopyalamak ve döndürmek için kullanılır [D]. Kılavuzlar sırasıyla Seç, Yön, Döndür (Revolve), Eğim (Draft),
Süpür (Sweep), Gövdeyi ölçekle (Scale Body), Kadar (Up To) ve Tam çekme (Full Pull)'dur ve **Tab ile dolaşılır** [D].
Alt+tık, döndürme ve süpürme için sürücü kenarı seçer [D]. Seçenekler: Ekle (Add), Kes (Cut), Birleştirme yok (No Merge),
Ofseti koru, İki yöne çek, Ölç, Cetvel (Ruler), Yuvarlat (Round), Pah (Chamfer), Kenarı uzat, Kenarı kopyala, Kenarı döndür [D].
Sürüklerken iki değer görünür (mevcut değer ve değişim) ve Tab ikisi arasında geçer [U, PADT]. Enter tamamlar,
Esc iptal eder [D].

PersistCAD'deki Çek durum makinesi (sahibi WP-B, `PullTool`):

```
Idle --(seçim: bölge|yüz|kenar)--> Armed (tutamaç oku + boyut kutusu kapalı)
Armed --(ok sürükle)--> Dragging (önizleme hayaleti, boyut kutusu canlı)
Armed --(rakam / Space)--> Typing (boyut kutusu odakta)
Dragging --(bırak)--> Typing (değer kutuda, Enter bekler)   [U: SpaceClaim bırakınca uygular; biz commit'i
                                                            Enter'a ya da 600 ms sonra otomatik commit'e bağlarız, §6.4]
Typing --(Enter)--> Commit --> Idle (sonuç seçili kalır, aynı yüz yeniden çekilebilir)
any --(Esc)--> önceki durum / Idle
Armed --(Alt+tık düz çizgi | eksen)--> Armed[Revolve]  (ok yerine dönme yayı tutamacı)
```

Seçimden özelliğe eşleme (**PullMapping**, saf mantık, GUI'siz test edilebilir):

| Seçim | Sonuç (geçmiş ağacında) | Değer alanı |
|---|---|---|
| Sketch bölgesi `sketch:S:r<k>` (S'nin tek bölgesi var ya da CR-1 hazır) | **Yeni Extrude**: `sketch=S`, `distance=<|değer|>`, `direction` = işaret > 0 ise `normal`, < 0 ise `reverse`. "İki yöne çek" açıksa `symmetric`. `op` Seçenekler'den gelir (§2.5). CR-1 hazırsa `regions=<u,v>` (bölgenin iç noktası) | "Mesafe": ifade kabul eder (`8`, `T`, `T*2`) |
| Bölge + Alt+tık ile sketch çizgisi, sketch U/V ekseni (Döndür kılavuzu) | **Yeni Revolve**: `axis=U|V|<entityId>`, `angle=<değer>` (varsayılan 360, Tam çekme), `op` aynı şekilde | "Açı" (°) |
| `<fid>.end` yüzü (fid = Extrude) | **fid düzenlenir**: `distance := distance + Δ`. Tab ile ikinci alan "Toplam" doğrudan yeni mesafeyi alır | Δ (birincil), Toplam (ikincil) |
| `<fid>.start` yüzü (Extrude, sketch başka bir özellikçe kullanılmıyorsa) | sketch `offsetExpr := ofset − Δ` ve `distance := distance + Δ` (taban düzlemi dışarı taşınır, üst yüz yerinde kalır) | Δ |
| `<fid>.f<k>` (Fillet yüzü) / `<fid>.c<k>` (Chamfer yüzü) | **fid düzenlenir**: `radius` / `distance` = yeni değer [D: yuvarlatma yüzünün yarıçapı Özellikler'de de düzenlenir] | Yarıçap / Mesafe |
| `<fid>.h<k>.wall` (Hole) | fid `diameter` düzenlenir (silindirik yüz çekmek yarıçapı değiştirir [U, PADT]) | Çap |
| `<fid>.side.e<id>` silindirik ve sketch'te `id` için Çap/Yarıçap ölçüsü varsa | O sketch ölçüsünün ifadesi düzenlenir | Çap |
| `<fid>.rev.e…`, `<fid>.end` (Revolve, açı < 360) | Revolve `angle` düzenlenir | Açı |
| Bir veya daha çok kenar (Ctrl ile çoklu) | **Yeni Fillet** (`refs` = kenarlar, `radius`) ya da Seçenekler'de "Pah" seçiliyse **yeni Chamfer** (`distance`). Kenar içeri sürüklenir [U] | Yarıçap / Mesafe |
| Diğer düzlemsel yüzler (ör. bir extrude'un yan yüzü, bir sketch'e bağlı olmayan yüz) | CR-2 hazırsa **Yüzden Extrude** (`face=<ad>`, + yönde join, − yönde cut). Değilse durum çubuğunda şu mesaj çıkar: "Bu yüz için Çek henüz desteklenmiyor: yüzde sketch açmak için K, mevcut özelliği düzenlemek için çift tıklayın." | Mesafe |
| Datum düzlemi | Taşı'ya yönlendirilir (§2.7) | — |

Değer kuralları:
* Değer bir **ifadedir**. Kutu `ExprEdit` ile aynı tamamlayıcıyı (completer) kullanır. Geçersiz ifade kırmızı çerçeveyle
  gösterilir ve Enter kabul etmez.
* Düzenlenen alan bir parametreye bağlıysa (`distance = T`) ve parametrenin ifadesi düz bir sayıysa yeni değer **parametreye**
  yazılır (`IDesignHost::changeParameter`, imalat denetimiyle). Durum çubuğunda "T = 10 (parametre güncellendi)" yazar.
  Parametre türetilmişse (ör. `Hb + T - 4`) Çek reddedilir ve şu mesaj çıkar: "Mesafe türetilmiş bir ifadeye bağlı
  (Lbore = Hb + T - 4); Gruplar panelinden düzenleyin." Alan düz bir sayı ya da serbest ifade ise özelliğin ifadesi değişir.
* **Grup oluştur (Create Group)**, Ctrl+G [D: Ctrl+G = grup oluştur]: boyut kutusu açıkken ya da son Çek işleminden sonra
  `NewParameterDialog` açılır (ad önerisi `<fid>_distance`, değer = mevcut değer, "Uygulamada göster" seçilebilir). Onaylanınca alan
  parametreye bağlanır. Bu, SpaceClaim'de cetvel ölçüsünü Gruplar paneline "sürücü ölçü (driving dimension)" olarak kaydetmenin
  karşılığıdır [D].
* Her commit **tek bir geri alma adımıdır** (`tryCommit` açıklaması: "Çek: Extrude3 = 8").
* Önizleme: sürükleme sırasında tam model yeniden üretilmez. Bölge prizması, dönüş veya yüz ofseti gibi yalnızca araç gövdesi
  `setGhost` ile gösterilir (en çok 12 Hz). Kenar yuvarlatmada önizleme yoktur, yalnızca ok ve sayı görünür. Commit tam
  `tryCommit` hattından geçer.

### 2.4 Tutamaçlar (handles) ve boyut kutusu
* **Çek oku**: seçimin ağırlık merkezinde, yön normal boyunca, 60 px uzunluğunda sarı ok (SpaceClaim'in sarı/mavi ok
  görünümü). Sürükleme yalnızca ok ekseni boyunca izdüşürülür. Ok başına tıklayınca yön tersine döner. Revolve kipinde ok
  yerine dönme yayı çizilir.
* **Taşı tutamacı (Move handle)**: 3 renkli eksen oku (X kırmızı, Y yeşil, Z mavi) ve 3 dönme yayı. Merkezdeki küre çapadır (anchor).
* **Boyut kutusu (DimensionBox)**: imlecin 18 px sağ altında, en çok 2 alan (birincil ve ikincil), birim etiketi, küçük
  "P" düğmesi (Grup oluştur). Tab alanlar arasında geçer, Enter commit eder, Esc kapatır. Shift+↑/↓ birincil değeri normal
  adımla artırıp azaltır. Alt+Shift küçük, Ctrl+Shift büyük adım kullanır. Shift+→/← ikincil alan için aynısını yapar [D: nudge tuşları].
  Adımlar: normal = ızgara adımı (varsayılan 1 mm / 1°), küçük = adım/10, büyük = adım×10.
* Tutamaçlar QPainter ile görünümün üstündeki kaplamaya çizilir. 3B konumlar `ShapeViewer::worldToScreen` ile ekrana
  dönüştürülür (§6.2). GL kodunda değişiklik gerekmez.

### 2.5 Ekle / Kes / Birleştirme yok ↔ BODIES işlemleri [D seçenekler, PC eşleme]

| Seçenek | Extrude/Revolve `op` | Not |
|---|---|---|
| (hiçbiri) Otomatik [PC varsayılanı] | gövde yoksa `new`. Değer > 0 ve sketch bir gövde yüzündeyse `join`, değer < 0 ise `cut` (yüzden içeri). Serbest düzlemde gövdeye değiyorsa `join`, değmiyorsa `new` | SpaceClaim'in "akıllı" davranışı [U] |
| **Ekle (Add)** | `join` | yalnızca malzeme ekler [D] |
| **Kes (Cut)** | `cut` (`through` yok; Kadar ile "tümünü kes" 2. fazda) | yalnızca malzeme çıkarır [D] |
| **Birleştirme yok (No Merge)** | `new` (+ isteğe bağlı gövde adı alanı) | kesiştiği nesnelerle birleştirmez [D] |
| Kesişim | `intersect` | [PC] SpaceClaim Çek'te yok. Seçenekler'de "Gelişmiş ▸ Kesişim" olarak durur |
| Hedef gövdeler | `targets` | [PC] Seçenekler'de "Hedef: Otomatik ▾" listesi (mevcut targetsList mantığı) |
| Kesiciyi koru | `keepTool` | [PC] yalnızca Kes ve Kesişim'de etkin |

### 2.6 Gruplar (Groups) = parametreler [D grup kavramı, PC eşleme]
* SpaceClaim'de Gruplar paneli, Çek ve Taşı'nın cetvel ölçülerini **sürücü ölçü** olarak saklar. Grup yeniden çağrıldığında araç,
  seçim, seçenekler ve cetvel geri yüklenir [D].
* PersistCAD'de **Gruplar** sekmesi bugünkü **parametre tablosudur** (`paramView_`, kolonlar aynıdır: Ad, İfade, Değer, Birim,
  Min, Maks, App'te göster, Etiket, …). "Uygulamada göster" kolonu, parametreyi "Uygulama Olarak Kaydet" çıktısına taşır.
* Panel düğmeleri: **Grup oluştur (Create Group)** (= "Parametre ekle", Ctrl+G), **Sil**, **Kullanıldığı yerler** (seçili
  parametreyi kullanan özelliklerin yüzlerini görünümde vurgular. Ters yönde `featureParameterSummary` kullanılır).
* Satıra çift tıklama (2. faz): parametre bir özellik alanına bağlıysa ilgili araç yeniden çağrılır (ör. Extrude mesafesi →
  Çek aracı, `<fid>.end` seçili, boyut kutusu açık). Bu, SpaceClaim'de grubu yeniden çağırmanın karşılığıdır [D].
* Değer kolonunda düzenleme bugünkü gibi `tryParameterChange` hattından geçer.

### 2.7 Taşı (Move) [D kılavuz ve seçenekler]
Resmi kılavuzlar: Seç, Bileşen seç, Taşıma yönü, Yörünge boyunca taşı, Çapa (Anchor), Dayanak (Fulcrum), Eksen etrafında radyal,
Kadar (Up To), Nesneye hizala [D]. Seçenekler: Izgarayı taşı, Simetrik taşı, Ölç, Cetvel, Yönelimi koru, **Desen oluştur
(Create Patterns)**, Önce ayır, Sketch bağlantısını koru, XYZ koordinat gir [D].

PersistCAD eşlemesi:

| Seçim | Taşı'nın sonucu | Faz |
|---|---|---|
| Datum düzlemi | Normal oku Plane `offset` değerini, düzlem içi dönme yayı `rotate` değerini değiştirir. `axisAngle` türünde eksen yayı `angle` değerini değiştirir | 2 |
| 3B'de bir sketch (Yapı ağacından ya da bölgeden seçili) | Normal oku `offsetExpr` değerini değiştirir (Izgarayı taşı ile aynı) | 2 |
| Sketch modunda sketch eğrileri / noktaları | Bugünkü sürükleyerek çözme. Ok ile eksene kilitli sürükleme ve yazılan mesafe, sketch'e sabit bir `DistanceX`/`DistanceY` kısıtı **eklemez**, yalnızca noktaları taşır | 2 |
| "Desen oluştur" seçili + Extrude(cut/join)/Hole özelliğinin bir yüzü | Eksen oku boyunca sürükleme yeni **Pattern** (`kind=linear`, `source=fid`, `count=2`, `spacing=<değer>`, `dirX/Y/Z` = ok yönü) oluşturur. Ardından Özellikler panelinde `count` düzenlenir. Dönme yayı `kind=circular`, `axis` = en yakın dünya ekseni | 2 |
| Gövde (öteleme / döndürme) | **CR-3 gerekir** (Transform özelliği). O zamana kadar durum çubuğunda şu mesaj çıkar: "Gövde taşıma henüz desteklenmiyor (parametrik Transform özelliği planlandı)." | 3 |

### 2.8 Doldur (Fill) [D]
SpaceClaim'de Doldur, seçili bölgeyi çevresindeki yüzey ya da katıyla doldurur, yuvarlatma ve pah izlerini kaldırır [D].
Parametrik karşılığı **özelliği geri almak**tır:

| Seçilen yüz | Sonuç |
|---|---|
| `<fid>.f<k>` / `<fid>.c<k>` | O kenar `refs` listesinden çıkarılır. Liste boşalırsa özellik silinir |
| `<fid>.h<k>.*` (Hole) | Tek konumluysa Hole silinir. Çok konumluysa şu mesaj çıkar: "Delik konumu sketch noktasıdır: sketch'i düzenleyin (çift tık)." |
| Extrude(op=cut) / Revolve(cut) yüzleri | Özellik **bastırılır** (silinmez. Geçmiş sekmesinden geri açılabilir) ve durum çubuğu bunu bildirir |
| `<fid>.inner.*` (Shell) | Shell'in `refs` listesinden yüz çıkarılır. Boşalırsa Shell silinir |
| Diğer | "Doldur bu yüz için desteklenmiyor" mesajı çıkar |

Geri alma bugünkü `deleteFeature` / `toggleSuppress` / feature düzenleme commit'leri üzerinden yapılır. Bağımlı özellik
uyarıları (`deleteFeature`'daki deps kontrolü) korunur.

### 2.9 Birleştir (Combine) [D: araç var. U: kılavuz akışı]
Kılavuzlar: **Hedef seç (Select Target)** → **Kesici seç (Select Cutter)** → (SpaceClaim: **Kaldırılacak bölgeleri seç (Select
Regions to Remove)**). PersistCAD: hedef ve kesici seçildikten sonra mini araç çubuğu üç işlem sunar: **Çıkar**
(`Combine op=cut`), **Kesişim** (`intersect`), **Birleştir** (`join`). Seçim sırasında hayalet önizleme görünür. Seçenek:
**Kesiciyi koru** (`keepTools`), **Yalnız en büyük parça** (`largestOnly`). Ctrl ile iki gövde seçilip Enter'a basmak **join**
yapar. Bölge seçerek kaldırma **desteklenmez**.

### 2.10 Düzlem (Plane) [U]
Oluştur > Düzlem: seçim düzlemsel yüzse `faceOffset`, tek düz kenarsa `edgeNormal (t=0.5)` (mevcut "Bu kenara dik düzlem"
davranışı), iki düz kenarsa `twoEdges`, seçim yoksa mevcut düzlem diyaloğu açılır. Oluşan düzlem seçili kalır ve **Taşı**
ile ötelenip döndürülür (§2.7). Sağ tık menüsündeki "Bu kenardan açılı düzlem" ve "Düzlem diyaloğu..." korunur.

### 2.11 Seçim modeli [D/U]
* Tık = seç. Ctrl+tık = ekle veya çıkar. Çift tık = kenar döngüsü (yeniden çift tık döngüler arasında geçer). Üçlü tık = katı gövde.
  Soldan sağa kutu tamamen içerdekileri, sağdan sola kutu değenleri seçer. Ctrl+A aynı türden tümünü seçer [U: üçüncü
  taraf eğitim notu ve resmi kısayol listesi (Ctrl+A)]. Kutu, kement ve boyama kipleri **1. fazda yoktur**.
* **Değişiklik**: bugün kenar yalnızca **Ctrl+tık** ile seçiliyor ve hemen bir menü açılıyor. Yeni davranışta düz tık,
  6 px içindeki kenarı yüze tercih eder. Kenar menüsü yalnızca **sağ tık** ile açılır. `edgePicked` sinyali düz tıkta da yayılır (§7.4).
* Önvurgulama (hover): imleç altındaki yüz veya kenar açık turuncu görünür (bugün yalnızca yüz).
* **Seçim filtresi (Selection filter)** [D]: durum çubuğunda Akıllı / Yüz / Kenar / Gövde / Düzlem / Sketch.
* Seçim **SelectionModel** nesnesinde tutulur (§7.2). Özellikler paneli, durum çubuğu "seçim listesi" ve araçlar bu nesneyi dinler.
  Seçimi geri al (Revert selection) düğmesi bir önceki seçimi geri yükler [D].

### 2.12 Fare ile gezinme [D]
| Hareket | SpaceClaim [D] | PersistCAD (yeni, tasarım ortamı) | Bugün |
|---|---|---|---|
| Orta düğme sürükle | Çevir (spin) | Çevir | Kaydır |
| Shift + orta sürükle | Kaydır (pan) | Kaydır | — |
| Ctrl + orta sürükle (yukarı/aşağı) | Yakınlaştır | Yakınlaştır (merkeze göre) | — |
| Shift + orta çift tık | Görünümü yüze hizala (snap view) | İmleç altındaki düzlemsel yüze dik bak (`setViewToFace`) | — |
| Tekerlek | [U] yakınlaştır | İmlece doğru yakınlaştır (bugünkü gibi) | aynı |
| Sol sürükle (boşlukta) | Seç: kutu seçimi. Çevir/Kaydır/Yakınlaştır düğmesi etkinse o işlem [D: durum çubuğundaki görünüm denetimleri] | 1. faz: Çevir/Kaydır/Yakınlaştır geçiş düğmesi etkinse o işlem, değilse işlem yok (kutu seçimi 2. faz) | Döndür |
| Shift + sol sürükle | — | **eski kaydırma kaldırılır** | Kaydır |
| Sağ tık | Bağlam menüsü | Bağlam menüsü (bugünkü kenar ve datum menüleri, §3.10) | aynı |
| Sağ sürükle | Hareket kısayolları (gesture) [D] | **Desteklenmez** (sağ sürükleme yok sayılır) | — |
| Ctrl+T / Ctrl+L / Ctrl+Q | Dönme merkezi koy / bul / temizle [D] | 2. faz: dönme merkezi = imleç altındaki yüz noktası | — |

`ShapeViewer` kütüphanesi çalıştırıcı (runner) ile ortaktır. Yeni şema **eklemeli** bir API ile açılır:
`setNavigationScheme(NavigationScheme::SpaceClaim)`. Varsayılan `Legacy` olduğu için runner'ın davranışı değişmez (§6.2).
Ana görünüm (Home, H) [D] = izometrik + sığdır. Plan görünümü (V) [D] = sketch modunda sketch düzlemine, 3B'de seçili
düzlemsel yüze ya da datum düzlemine dik bakış. Önceki ve sonraki görünüm (← / →) [D] = son 20 kameralık geçmiş.

---

## 3. (b) Eşleme tablosu: SpaceClaim öğesi → mevcut PersistCAD → yapılacak değişiklik

Sahip: **A** = WP-A (kabuk), **B** = WP-B (araçlar). Faz 1/2/3.

| # | SpaceClaim öğesi | Mevcut PersistCAD karşılığı (kod) | Yapılacak değişiklik | Sahip | Faz |
|---|---|---|---|---|---|
| 1 | Dosya menüsü | `buildMenus` → "&Dosya" QMenu | Menü ribbon'daki Dosya düğmesine taşınır, içerik aynı kalır. "İmalat > Ayarlar" girdisi de buraya kopyalanır | A | 1 |
| 2 | Hızlı erişim | `mainBar_` (Yeni/Aç/Kaydet/Geri/Yinele) | Yeni `quickAccessBar` eklenir. Aynı QAction nesneleri kullanılır (`actSave_`, `actUndo_`, `actRedo_`, Yeni, Aç) | A | 1 |
| 3 | Ribbon ve sekmeler | Menü çubuğu + `mainBar_` + `sketchBar_`/`sketchBar2_` | Yeni `Ribbon` ve `RibbonGroup` sınıfları. Menü çubuğu gizlenir. `buildToolbars`/`buildSketchToolbar` ribbon kurulumuna dönüşür | A | 1 |
| 4 | Yönlendir grubu | Görünüm menüsü (7 görünüm, Tümünü Sığdır=F) | H, V, Z/Alt+Z, Görünümler ▾, Çevir/Kaydır/Yakınlaştır geçiş düğmeleri, ← → görünüm geçmişi. F artık Doldur | A (düğme), B (viewer API) | 1 |
| 5 | Sketch grubu araçları | `SketchEditor::Tool` {Select, Point, Line, Rect, Circle, Arc} | Araçlar ToolManager eylemlerine taşınır. L/R/C kısayolları eklenir. **Kırp (Trim Away, T)** eklenir. Spline, Ofset, İzdüşür, 3 noktalı dikdörtgen/çember/yay, teğet yay desteklenmez | B | 1 (Kırp 2) |
| 6 | Sketch ızgarası | `ShapeViewer::setSketchPlane` (ızgara çizimi var) ve ayrı 2B `SketchEditor` | SketchEditor görünümün üstünde şeffaf kaplama olur. Düzlem→ekran afin dönüşümü viewer kamerasından alınır (§6.1) | B | 1 |
| 7 | Sketch mini araç çubuğu | Yok | `MiniToolbar` (Sketch kipi): 3B'ye Dön, Yeni sketch düzlemi, Izgarayı taşı, Plan görünümü | B | 1 |
| 8 | Çizerken ölçü girişi | Yok (ölçü yalnızca sonradan diyalogla) | DimensionBox: rakam/Space açar, Tab dolaşır, Enter onaylar. Yazılan değerler sürücü kısıt olur (§5) | B | 1 |
| 9 | Mod grubu K/X/D | Sketch girişi: "Sketch..." → QInputDialog. Kesit: K | `setDesignMode`. K=sketch, X=kesit (eski K), D=3B. "Sketch..." klasik eylemi aynen kalır | A | 1 |
| 10 | Seç (S) | Görünümde yüz tıklaması (`onFacePicked`) + sketch Seç aracı | Tek "Seç" eylemi. SelectTool; düz tıkla kenar; çift/üçlü tık; seçim filtresi | B (araç), A (filtre UI) | 1 |
| 11 | Çek (P) | Extrude/Revolve/Fillet/Chamfer diyalogları | PullTool + PullMapping (§2.3). Diyaloglar "Klasik ▾" altında kalır | B | 1 |
| 12 | Çek kılavuzları | Yok | ToolGuideBar: Seç, Yön, Döndür, Kadar (2. faz), Cetvel (2. faz). Eğim, Süpür, Ölçekle, Tam çekme desteklenmez (Tam çekme = Revolve 360 seçeneği) | B | 1/2 |
| 13 | Çek seçenekleri | `FeatureDialog::buildBodyOps` (op combo, targets, keepTool, bodyName) | ToolOption listesi → Seçenekler paneli (A çizer) ve mini araç çubuğu (B çizer) | A+B | 1 |
| 14 | Kenar çekerek yuvarlatma/pah | Fillet/Chamfer diyalogları + kenar listesi | PullMapping kenar satırı. Seçenek Yuvarlat/Pah | B | 1 |
| 15 | Alt+tık eksen ile döndürme | Revolve diyaloğu (eksen U/V/çizgi) | Döndür kılavuzu. Alt+tık sketch çizgisi / U / V eksen göstergesi | B | 1 |
| 16 | Kadar (Up To, U) | `through=1` (yalnızca cut) | Hedef düzlem veya yüze mesafe bir kez hesaplanır ve sayı yazılır (ilişkisel değil). Cut + "Tümü" = `through=1` | B | 2 |
| 17 | Cetvel (Ruler) | Yok | Cetvel çapası: seçilen kenar veya yüze olan mesafe birincil değer olur. Yazılan değer hedef mesafeden Δ hesaplar. Grup oluştur ile parametre olur | B | 2 |
| 18 | Taşı (M) | Plane offset/rotate alanları, sketch `offsetExpr`, Pattern | MoveTool (§2.7) | B | 2 |
| 19 | Doldur (F) | `deleteFeature`, `toggleSuppress`, refs düzenleme | FillTool (§2.8) | B | 2 |
| 20 | Birleştir (I) | Combine diyaloğu (target/tools/op/keepTools) | CombineTool (§2.9) | B | 2 |
| 21 | Gövdeyi böl, Böl, İzdüşür | Yok | **Desteklenmez** | — | — |
| 22 | Oluştur > Düzlem | Düzlem diyaloğu + kenar menüleri | PlaneTool (§2.10). Diyalog kalır | B | 2 |
| 23 | Oluştur > Eksen / Nokta / Orijin | Yok (eksen yalnızca Revolve `axis` ya da Plane `axisAngle`) | **Desteklenmez** (CR-5 ile 3. faz) | — | 3 |
| 24 | Oluştur > Desen | Pattern diyaloğu | "Doğrusal desen" ve "Dairesel desen" düğmeleri: seçili özelliğin yüzünü kaynak alarak Pattern diyaloğunu ön dolu açar. 2. fazda Taşı + "Desen oluştur" | A (düğme), B (Taşı) | 1/2 |
| 25 | Oluştur > Kabuk | Shell diyaloğu | Kabuk aracı: yüzlere tıkla (kaldırılacaklar), kalınlık yaz, Enter. Diyalog kalır | B | 2 |
| 26 | Yapı ağacı (Structure tree) | "Özellik Ağacı" + "Gövdeler" dock'ları | Yapı sekmesi: kök "Tasarım" ▸ Gövdeler (mevcut `bodiesTree_` ögeleri ve onay kutuları) ▸ Düzlemler ▸ Sketch'ler. Geçmiş sekmesi: mevcut `tree_` (geri sarma, bastırma vb.) | A | 1 |
| 27 | Katmanlar (Layers) | Yok | **Desteklenmez** | — | — |
| 28 | Seçim paneli (Power selection) | Yok | Seçim sekmesi: seçim listesi. 2. faz: "Benzerini seç" (aynı yarıçaplı fillet yüzleri, aynı çaplı delikler, aynı özelliğin yüzleri; ad kalıplarıyla) | A | 1/2 |
| 29 | Gruplar | Parametre tablosu (`dockParams`) | Gruplar sekmesi = aynı `paramView_` (kolonlar aynı). Grup oluştur = Parametre ekle (Ctrl+G) | A | 1 |
| 30 | Görünümler (Views) | Yok | **Desteklenmez** (1. faz). 2. faz isteğe bağlı: kayıtlı kameralar | — | — |
| 31 | Seçenekler paneli | FeatureDialog içindeki op/targets alanları | OptionsPanel: etkin aracın `ToolOption` listesini çizer | A | 1 |
| 32 | Özellikler paneli | Yok (bilgiler durum çubuğu ve ipuçlarında) | PropertiesPanel (§9): seçim türüne göre alanlar, düzenlenebilir ifadeler | A | 1 |
| 33 | Durum çubuğu | `lblHover_`, `lblRegen_`, `lblVol_`, `lblDof_`, `lblIssues_` | Mesaj alanı = araç istemi (`IActiveTool::prompt`). Seçim listesi, hızlı ölçüm, filtre, ← → düğmeleri eklenir. Mevcut etiketler kalır | A | 1 |
| 34 | Tutamaçlar ve oklar | Yok | `ViewportOverlay` (QPainter) + `worldToScreen` | B | 1 |
| 35 | Bağlam menüsü | Kenar menüsü (`edgeContextMenu`), datum menüsü, ağaç menüleri | Görünüm sağ tık menüsü, imleç altındaki öğeye göre genişler (§3.10) | A (menü), B (vurgu/isabet) | 1 |
| 36 | Radyal menü (O) | Yok | 2. faz | B | 2 |
| 37 | Ölçüm (E) | `massProperties` (yalnızca toplam hacim) | Ölçüm sekmesi: Kütle özellikleri. E aracı 2. fazda | A (sekme), B (E aracı) | 1/2 |
| 38 | Gizle/göster Ctrl+H, Alt+H, Ctrl+Shift+H | Gövdeler dock'unda onay kutusu ve bağlam menüsü (Gizle/Yalnız bunu göster/Tümünü göster) | Kısayollar seçime uygulanır (gövde) | A | 1 |
| 39 | Geri al / yinele Ctrl+Z / Ctrl+Y | `actUndo_`/`actRedo_` (QKeySequence::Undo/Redo) | Ctrl+Y açıkça eklenir (Linux'ta Redo varsayılanı Ctrl+Shift+Z'dir) | A | 1 |
| 40 | Geçmiş (yok, SpaceClaim doğrudan modelleyicidir) | Özellik ağacı, geri sarma, bastırma, düzenleme | **Korunur**: Geçmiş sekmesi [PC]. Yapı ağacındaki her nesnenin bağlam menüsünde "Geçmişte göster" vardır | A | 1 |
| 41 | Kesit modunda düzenleme | Görünüm-yalnız kesit | **Desteklenmez** | — | — |
| 42 | Sac metal, onarım, hazırlık, detay, faset, bileşen/montaj | Yok | **Desteklenmez** | — | — |

### 3.9 Açıkça DESTEKLENMEYENLER (1. ve 2. faz)
Pano (kopyala, yapıştır, yüzleri kopyala), Değiştir (Replace), Yüzü esnet (Tweak Face), Harmanla (Blend, B), Gövdeyi böl, Böl,
İzdüşür (Project), Eğim kılavuzu, Süpür kılavuzu, Gövdeyi ölçekle, Tam çekme kılavuzu (yalnızca Revolve "360°" seçeneği vardır),
İki yöne asimetrik çekme, Ofseti koru, Ayna (Mirror), Ofset ilişkisi, Spline, sketch'te Ofset, 3 noktalı araçlar, teğet yay,
Katmanlar, Görünümler, Bileşenler ve montaj (Ctrl+Shift+A SpaceClaim'de "bileşeni etkinleştir"dir; bizde "Uygulama Olarak
Kaydet" olarak kalır), kesit modunda düzenleme, Onar/Hazırla/Detay/Sac metal/Faset sekmeleri, sağ sürükleme hareketleri,
kutu dışı seçim kipleri (kement, boyama), çok pencere (Ctrl+Tab), yazdırma, Paylaş. Gövde taşıma ve döndürme CR-3'e,
herhangi bir düzlemsel yüzü çekmek CR-2'ye, aynı sketch'in bölgelerini ayrı ayrı çekmek CR-1'e bağlıdır (§11).
Desteklenmeyen bir SpaceClaim kısayoluna (B, Ctrl+C, Ctrl+D, …) basıldığında durum çubuğunda tek satırlık şu açıklama
çıkar: "Blend (B) PersistCAD'de yok." Böylece kullanıcı tuşun neden çalışmadığını anlar.

### 3.10 Bağlam menüsü (görünüm, sağ tık)
İmleç altındaki öğe önce SelectionModel'e eklenir (seçili değilse), sonra menü açılır:
* Her zaman: **Seç (Select)**, **Çek (Pull)**, **Taşı (Move)** · ayraç · **Ana görünüm**, **Plan görünümü**, **Sığdır** · ayraç · Kesit (X).
* Yüz: "Bu yüzde sketch aç (K)", "Bu yüzden düzlem", "Oluşturan özelliği düzenle..." (klasik diyalog), "Geçmişte göster",
  "Gövdeyi gizle (Ctrl+H)", "Yalnız bu gövde (Alt+H)".
* Kenar: mevcut `edgeContextMenu` girdileri ("Bu kenara dik düzlem", "Bu kenardan açılı düzlem", "İki kenardan düzlem",
  "Düzlem diyaloğu...") + "Yuvarlat (Çek)", "Pahla (Çek)".
* Datum düzlemi: mevcut `datumContextMenu` ("Bu düzlemde sketch başlat", "Düzlemi düzenle...") + "Taşı".
* Sketch bölgesi veya eğrisi: "Sketch'i düzenle (K)", "Çek", "3B görünümde gizle".
* Sketch modunda: Seç, Çizgi, Dikdörtgen, Çember, Yay · Kısıtlar ▸ · Ölçüler ▸ · Sil · 3B'ye Dön · Vazgeç.
Menü objectName'leri korunur: `edgeContextMenu`, `datumContextMenu`, `menuFeatureParams`.

---

## 4. (d) Klavye kısayolları

Bağlam (context): **G** = global (pencere), **V** = görünüm veya kaplama odakta, **S** = sketch modu, **T** = bir araç etkin,
**B** = boyut kutusu odakta. Tek harfli kısayollar `QLineEdit` veya `QAbstractSpinBox` odaktayken **çalışmaz**
(Qt `ShortcutOverride` bunu zaten sağlar. Tablo hücre düzenleyicileri için de doğrulanmalı).

| Tuş | İşlev (Türkçe (İngilizce)) | SpaceClaim | Bağlam | Bugün | Sahip |
|---|---|---|---|---|---|
| S | Seç (Select) | [D] | G | — | B |
| P | Çek (Pull) | [D] | G | — | B |
| M | Taşı (Move) | [D] | G | — | B |
| F | Doldur (Fill) | [D] | G | **Sığdır** (taşındı → Z) | B |
| I | Birleştir (Combine) | [D] | G | — | B |
| K | Sketch modu (Sketch Mode) | [D] | G | **Kesit** (taşındı → X) | A |
| X | Kesit modu (Section Mode) | [D] | G | — | A |
| D | 3B modu (3D Mode) | [D] | G | — | A |
| L | Çizgi (Line) | [D] | G (3B'de sketch'e geçirir) | — | B |
| R | Dikdörtgen (Rectangle) | [D] | G | — | B |
| C | Çember (Circle) | [D] | G | — | B |
| T | Kırp (Trim Away) | [D] | S | — | B |
| U | Kadar kılavuzu (Up To) | [D] | T (Çek, Taşı) | — | B |
| E | Ölç (Measure) | [D] | G | — | B (2. faz) |
| O | Radyal menü (Radial Menu) | [D] | V | — | B (2. faz) |
| H | Ana görünüm (Home) | [D] | G | — | A |
| V | Plan görünümü (Plan View) | [D] | G | — | A (B viewer API) |
| Z | Sığdır (Zoom Extents) | [U: üçüncü taraf] | G | — | A |
| Alt+Z | Sığdır (Zoom Extents) | [D] | G | — | A |
| Ctrl++ / Ctrl+- | Yakınlaştır / Uzaklaştır | [D] | G | — | A |
| ← / → | Önceki / sonraki görünüm | [D] | V | — | A (B kamera geçmişi) |
| Ctrl+→ / Ctrl+← | Izgarayı içeri / dışarı taşı | [D] | S | — | B |
| Tab | Boyut kutusunda alan değiştir; kutu yoksa araç kılavuzları arasında dolaş | [D kılavuz], [U alan] | T | — | B |
| Space | Boyut kutusunu aç (sürükleme sırasında ya da önce) | [U] | T | — | B |
| 0-9 . - ( | Boyut kutusunu bu karakterle aç | [U] | T, V | — | B |
| Enter | Tamamla (Complete) | [D] | T/B | — | B |
| Esc | İptal: kutu → sürükleme → çizim aşaması → araç → Seç | [D] | G | sketch'te var | B |
| Del | Sil (sketch öğesi. 3B'de seçili özellik(ler) için onaylı silme) | [D] | G | sketch'te var | B (sketch), A (3B) |
| Ctrl+Z / Ctrl+Y | Geri al / Yinele | [D] | G | Undo/Redo | A |
| Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S | Yeni / Aç / Kaydet / Farklı kaydet | [D] | G | aynı | A |
| Ctrl+G | Grup oluştur (= parametre oluştur ve bağla) | [D] | G/B | — | A (panel), B (kutu) |
| Ctrl+A | Aynı türden tümünü seç | [D] | V | — | B |
| Ctrl+Shift+I | Seçimi tersine çevir | [D] | V | — | B (2. faz) |
| Ctrl+H / Alt+H / Ctrl+Shift+H | Gizle / Diğerlerini gizle / Tümünü göster (gövde) | [D] | G | — | A |
| Shift+↑↓, Alt+Shift+↑↓, Ctrl+Shift+↑↓ | Birincil ölçüyü normal / küçük / büyük adımla değiştir | [D] | B | — | B |
| Shift+→←, (Alt/Ctrl)+Shift+→← | İkincil ölçü | [D] | B | — | B |
| Numpad * / + / - | Ağaç düğümünü tamamen aç / aç / kapat | [D] | Yapı ağacı | Qt varsayılanı | A |
| F5 | Şimdi denetle [PC] | — | G | aynı | A |
| Ctrl+Shift+A | Uygulama Olarak Kaydet [PC] (SpaceClaim'de "bileşeni etkinleştir" [D], bizde bileşen yok) | — | G | aynı | A |
| Alt+F | Dosya menüsü | [D] | G | Qt mnemonic | A |
| B, Ctrl+C/X/V, Ctrl+D, Ctrl+Shift+C/X, Ctrl+T/L/Q, Ctrl+Shift+F, Ctrl+P | Desteklenmez: durum çubuğu açıklaması | [D] | G | — | A |

Uygulama: WP-A, `Shortcuts.cpp` içinde tek bir tablo (`struct ShortcutDef { const char* actionObjectName; const char* keys; Context ctx; }`)
tutar ve eylemlere `setShortcut` uygular. WP-B'nin araç eylemlerinin kısayollarını da WP-A bu tablodan atar. WP-B eylem
oluştururken kısayol vermez. Tek harfli kısayollar `Qt::WindowShortcut` bağlamındadır. Tab, Space, rakamlar, Enter, Esc ve
Shift+oklar **kısayol değildir**: görünüm ve sketch kaplamasına kurulan olay filtresi bunları önce `ToolManager::routeKey`
fonksiyonuna verir (§7.3).

---

## 5. (f) Sketch'te anında ölçü girişi (on-the-fly dimensions)

[D]: SpaceClaim'in boyutlu sketch özelliği, o anda çizilen çizgi ve başka çizgilere veya noktalara göre kesin ölçü verir [D].
Tab, Space ve Enter ayrıntıları resmi alt sayfalarda okunamadı. Aşağıdaki davranış uygulayıcı deneyimine dayanır [U].

### 5.1 Ortak kurallar
1. Bir çizim aracı ilk tıklamasını aldıktan sonra (ör. dikdörtgenin ilk köşesi) imleç hareket ettikçe **ölçü alanları** canlı
   güncellenir: alanlar ölçülen kenarın yanında, ekran koordinatında küçük çerçeveli kutulardır.
2. Kullanıcı bir **rakam**, `.`, `-` veya `(` yazarsa birincil alan o karakterle başlayarak odak alır. **Space** boş alana odak verir.
   Harfle başlayan ifade (ör. `W/2`) için önce Space'e basılır (L, R, C, T tuşları araç kısayolu olduğu için).
3. **Tab** sıradaki alana geçer. Yazılmış alan **kilitlenir** (kilit simgesiyle gösterilir). İmleç hareketi yalnızca kilitsiz
   alanları değiştirir. Shift+Tab bir önceki alana döner.
4. **Enter** öğeyi oluşturur: kilitli alanlar yazılan değeri, kilitsizler imleç değerini alır. Çizim aynı araçla sürer
   (Çizgi aracında sonraki segment son noktadan başlar).
5. **Esc** önce alanı, sonra çizim aşamasını, en son aracı iptal eder (bugünkü Esc zinciri korunur).
6. Alanlar **ifade** kabul eder (`40`, `W`, `W/2+3`). Tamamlayıcı (completer) parametre adlarını önerir. Geçersiz ifade kırmızı
   görünür ve Enter kabul etmez.
7. **Ctrl+G** alanı bir parametreye dönüştürür (`NewParameterDialog`: ad önerisi `Sketch2_W`, "Uygulamada göster"). Parametre
   `installParameterCommitHook` yoluyla anında Gruplar tablosuna düşer (v6 davranışı).
8. Seçenekler panelinde **"Yazılan ölçüleri kısıt olarak sakla" (Keep typed dimensions)**, varsayılan **açık** [PC]: yazılan
   (kilitli) her alan sketch'e bir **sürücü ölçü kısıtı** ekler (`addConstraintChecked`) ve ifade `valueExpr` olur. Böylece
   değer sonradan çift tıklayıp ya da parametreyle değiştirilebilir. Kilitlenmemiş (fareyle belirlenen) alanlar kısıt eklemez.
   Kapalıysa yazılan değer yalnızca geometriyi belirler (SpaceClaim'in doğrudan davranışı).
9. Mevcut otomatik yatay ve dikey yakalama (snap) aynen kalır. **Shift basılıyken** yakalama kapanır [U].

### 5.2 Araç başına alanlar

| Araç | 1. tıklama | Alanlar (Tab sırası) | Eklenen kısıtlar (alan kilitliyse) |
|---|---|---|---|
| Çizgi (Line) L | başlangıç noktası | **Uzunluk**, **Açı** (+X'ten, °) | `Length` (çizgi), `Angle` yalnızca açı 0/90 dışındaysa (0/90 ise `Horizontal`/`Vertical`) |
| Dikdörtgen (Rectangle) R | ilk köşe | **Genişlik** (u), **Yükseklik** (v). İşaret yönü imlecin bulunduğu çeyrekten gelir | alt kenara `Length` = G, sol kenara `Length` = Y |
| Çember (Circle) C | merkez | **Çap** | `Diameter` |
| Yay (Sweep Arc) | merkez → başlangıç | 2. aşamada **Yarıçap**, 3. aşamada **Açı** (süpürme) | `Radius`, `Angle` (merkez-başlangıç ile merkez-bitiş arası; çözücü `Angle` kısıtı iki çizgi ister. Bu durumda iki yapı çizgisi eklenir. Desteklenmiyorsa açı yalnızca geometriyi belirler) |
| Nokta | — | **U**, **V** (orijine göre, alan açılınca) | `DistanceX`, `DistanceY` (orijin noktası id 0'a göre) |

Örnek (kabul senaryosu 3): R → orijine tıkla → `60` → Tab → `40` → Enter → 60×40 dikdörtgen. Alt kenarda `Length=60`,
sol kenarda `Length=40` kısıtları vardır ve köşe orijinle çakışıktır. DOF = 0 olur, çünkü addRectangle H/V ve köşe çakışıklığını
kendisi ekler, ilk köşe de orijin noktasına yakalanır. Bunu sağlamak için Rect aracı ilk köşede `ensurePoint(snap)` kullanmalı
ve `addRectangle` mevcut noktayı kullanacak bir aşırı yükleme (overload) almalıdır. Bu bir `SketchData` değişikliği değildir:
SketchEditor içinde 4 nokta ve 4 çizgi elle oluşturulur.

### 5.3 Ölçü düzenleme (çizimden sonra)
* Bugünkü gibi ölçü etiketine **çift tık** → `DimensionDialog`. Ek olarak **tek tık + yazmaya başlama** da desteklenir: etiket
  seçilir, rakam yazılınca etiketin yerinde satır içi düzenleyici açılır. Enter bununla commit eder.
* Kısıt ve ölçü düğmeleri (Kısıtlar ve Ölçüler bağlamsal grupları) bugünkü "önce seç, sonra düğme" mantığıyla çalışır.

---

## 6. Görünüm ve sketch kaplaması (WP-B teknik notları)

### 6.1 Sketch'i görünümün üzerine taşıma
* `SketchEditor` `ShapeViewer`'ın **çocuğu** olur. `setGeometry(viewer->rect())`, `WA_TranslucentBackground`, `autoFillBackground=false`.
  `SectionControls` kaplaması bu tekniğin Xvfb ve llvmpipe altında çalıştığını zaten gösteriyor.
* Görünüm **ortografiktir** (`proj.ortho`). Bu yüzden sketch düzleminden ekrana dönüşüm tam bir **afin** dönüşümdür.
  SketchEditor'un `toScreen`/`toSketch` (bugün `org_` + `scale_`) fonksiyonları bir `QTransform planeToScreen_` ve tersine
  dönüşür. Çizimde `QPainter::setTransform` kullanılır. Kalemler kozmetiktir (`QPen::setCosmetic(true)`), metinler ve ölçü
  etiketleri ekran koordinatında çizilir. Böylece kullanıcı sketch sırasında modeli **çevirebilir** ve eğik bakışta da çizebilir:
  çemberler elips görünür, isabet testi ters dönüşümle yapılır. Düzlem bakış yönüne neredeyse paralelse
  (|det| < 1e-3 · ölçek²) çizim girdisi kapatılır ve şu mesaj çıkar: "Düzlem kenardan görünüyor: Plan görünümü için V."
* Kamera değişince viewer `cameraChanged()` sinyalini yayar. SketchEditor bu sinyalle `planeToScreen_` dönüşümünü günceller.
  Kaplamada **tekerlek** ve **orta düğme** olayları viewer'a iletilir (`QCoreApplication::sendEvent`). Sol tık sketch'e kalır.
* Sketch modunda 3B model görünür kalır (bugün eksik olan "3B altlık"). Gövdeler %35 opaklığa iner
  (`setTransparentFaces`, mevcut API).
* `SketchEditor::fitView()`, viewer'dan sketch sınır kutusuna sığdırmasını ister (`fitToBox` API'si).
  `pointScreenPos(id)` aynı imzayla kalır, çünkü test düzenekleri bu fonksiyonla tıklama konumu hesaplar.
* `SketchEditor::Tool` enum'una yalnızca **sona** ekleme yapılır (`Trim`). `main.cpp --selftest` mevcut değerleri kullanır.

### 6.2 ShapeViewer eklemeli API (yalnızca ekleme; mevcut imzalar değişmez, runner etkilenmez)
```cpp
enum class NavigationScheme { Legacy, SpaceClaim };
void setNavigationScheme(NavigationScheme s);               // designer: SpaceClaim; runner: varsayılan Legacy
enum class DragMode { None, Spin, Pan, Zoom };              // sol sürükleme kipi (Yönlendir grubu düğmeleri)
void setLeftDragMode(DragMode m);
QPointF worldToScreen(const double p[3]) const;             // widget pikseli
bool screenRay(const QPointF& px, double origin[3], double dir[3]) const;
QTransform planeToScreen(const double o[3], const double u[3], const double v[3]) const;  // ortografik → afin
void setPlanView(const double o[3], const double u[3], const double v[3], bool fit);      // normal bakış, v yukarı
void setViewToFace(const QString& faceName);                // Shift+orta çift tık
void setHomeView();                                         // izometrik + sığdır
void fitToBox(const double mn[3], const double mx[3]);
bool previousView(); bool nextView();                       // 20 adımlık kamera geçmişi
QString pickAt(const QPoint& px, int kinds, int* kindOut);  // kinds: yüz|kenar|datum; kenar 6 px önceliği
void setPreselection(const QString& name);                  // hover vurgusu (yüz veya kenar)
signals: void cameraChanged(); void doubleClicked(const QString& name, int kind); void tripleClicked(...);
```
* Kamera "turntable" (az/el, el ∈ [-90°, 90°]) olarak kalırsa eğik datum düzlemine Plan görünümü verilemez. Kamera iç yapısı
  **kuaterniyona** çevrilir (yalnızca `ShapeViewer.cpp` içinde). `setView(View)` aynı sonuçları verir.
* `SpaceClaim` şemasında Ctrl+sol tık artık "kenar seç" değil "seçime ekle"dir. Viewer bunu `facePicked`/`edgePicked` sinyallerine
  `QGuiApplication::keyboardModifiers()` ile birlikte iletir. Seçimi viewer değil SelectTool yönetir. `Legacy` şemada davranış
  aynen kalır (runner).

### 6.3 Sketch bölgeleri
`SketchRegions.cpp` (WP-B), çözülmüş sketch'lerden (`ModelResult::solvedSketches` ve çerçeveler: `FeatureInfo::sketchFrame`
ya da `pc::model::sketchFrame`) `buildProfile` ile yüzleri üretir ve `sketch:<sid>:r<k>` adlarıyla WP-A'nın `updateViewer`
compound'una ekler (§7.5 sözleşme fonksiyonu). Bölge yüzleri imalat denetimine ve dışa aktarıma **girmez** (yalnızca görüntü).

### 6.4 Bırakınca commit
SpaceClaim sürüklemeyi bıraktığında değişikliği uygular [U]. Bizde her commit imalat denetiminden geçer (ortalama 60–110 ms
yeniden üretim, hata olursa modal inceleme penceresi açılır). Bu yüzden kural şöyledir: fare bırakılınca değer kutuda kalır ve
**600 ms içinde başka girdi yoksa** otomatik commit olur. Bu sürede rakam, Tab veya Space gelirse kutu düzenlemeye geçer ve commit
Enter'ı bekler. Seçenekler panelinde "Bırakınca hemen uygula" ayarı vardır (varsayılan kapalı).

---

## 7. Arayüz sözleşmesi (WP-A ↔ WP-B)

### 7.1 Dosya: `src/designer/ToolApi.h` (+ `ToolApi.cpp`)
* **Sahibi WP-A**, P0'da oluşturulur, P0 sonunda **dondurulur**. Değişiklik isteği `docs/CONTRACT_REQUESTS.md` dosyasına
  "SC-UX:" önekiyle yazılır. Değişikliği iki taraf onaylar, WP-A uygular.
* `ToolApi.cpp` yalnızca `SelectionModel` uygulamasını içerir (WP-A). `ToolManager` sınıfının **uygulaması** WP-B'nin
  `ToolManager.cpp` dosyasındadır. P0'da derlenebilir bir boş (stub) sürüm WP-A tarafından yazılır, sahipliği hemen WP-B'ye geçer.

```cpp
#pragma once
// PersistCAD designer — SpaceClaim-UX contract between the shell (WP-A) and the tools (WP-B). FROZEN after P0.
#include "pc/Document.h"
#include "pc/Model.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <TopoDS_Compound.hxx>
#include <map>
#include <set>
class QAction; class QKeyEvent; class QWidget;
namespace pc::viewer { class ShapeViewer; }

namespace pcd {
class SketchEditor;

enum class DesignMode { ThreeD, Sketch, Section };

// ---- selection --------------------------------------------------------------------------------
struct SelItem {
  enum class Kind { Face, Edge, Body, Datum, SketchRegion, SketchCurve, SketchPoint, Sketch, Feature, Parameter };
  Kind kind = Kind::Face;
  QString name;   // Face/Edge: persistent name; Body: body name; Datum/Feature/Sketch: feature id;
                  // SketchRegion "sketch:<sid>:r<k>"; SketchCurve "sketch:<sid>:e<id>"; SketchPoint "sketch:<sid>:p<id>";
                  // Parameter: parameter name
  bool operator==(const SelItem& o) const { return kind == o.kind && name == o.name; }
};
enum class SelFilter { Smart, Face, Edge, Body, Datum, Sketch };

class SelectionModel : public QObject {          // implemented in ToolApi.cpp (WP-A)
  Q_OBJECT
public:
  using QObject::QObject;
  const QList<SelItem>& items() const { return items_; }
  bool isEmpty() const { return items_.isEmpty(); }
  QList<SelItem> ofKind(SelItem::Kind k) const;
  void set(const QList<SelItem>& items);         // emits changed() if different; pushes the old one for revert()
  void add(const SelItem& it);
  void toggle(const SelItem& it);
  void clear();
  void revert();                                 // "Seçimi geri al" (status bar)
  SelFilter filter() const { return filter_; }
  void setFilter(SelFilter f);
  bool accepts(SelItem::Kind k) const;           // filter test
signals:
  void changed();
  void filterChanged(pcd::SelFilter f);
private:
  QList<SelItem> items_, prev_;
  SelFilter filter_ = SelFilter::Smart;
};

// ---- tool descriptors (rendered by WP-A: Options panel; by WP-B: mini-toolbar, tool guides) -----
struct ToolOption {
  enum class Type { Bool, Choice, Expr, Text, Action };
  QString key;        // stable, e.g. "merge", "edge", "bothSides", "ruler", "createGroup", "keepDims"
  QString label;      // Turkish UI text, e.g. "Kes (Cut)"
  Type type = Type::Bool;
  QVariant value;     // Bool: bool; Choice: QString key of the chosen item; Expr/Text: QString
  QStringList choices;       // Choice: "key|Etiket (English)" entries
  QString group;      // section title in the Options panel, e.g. "Birleştirme"
  QString tip;
  QString icon;       // pcd::makeIcon kind
  bool enabled = true;
  bool onMiniToolbar = false;
};
struct ToolGuide { QString key; QString label; QString icon; QString tip; };

// ---- host (implemented by MainWindow, WP-A) ----------------------------------------------------
class IDesignHost {
public:
  virtual ~IDesignHost() = default;
  virtual QWidget* hostWindow() = 0;
  virtual const pc::Document& document() const = 0;
  virtual const pc::model::ModelResult& modelResult() const = 0;
  virtual int rollbackIndex() const = 0;                         // -1 = none
  virtual pc::viewer::ShapeViewer* viewer() const = 0;
  virtual SketchEditor* sketchEditor() const = 0;
  virtual SelectionModel* selection() const = 0;
  virtual DesignMode designMode() const = 0;
  virtual bool setDesignMode(DesignMode m) = 0;                  // K/X/D semantics; false if refused/cancelled
  virtual bool beginSketch(const pc::SketchPlane& plane) = 0;    // enters Sketch mode on a new sketch
  virtual bool editSketch(const std::string& sketchFeatureId) = 0;
  virtual bool finishSketch(bool accept) = 0;                    // commit ("Sketch'i Bitir") / discard
  virtual std::string openSketchId() const = 0;                  // "" if not sketching
  // change pipeline: mfg validation + review dialog + one undo entry. false = cancelled/refused.
  virtual bool commitDocument(const pc::Document& cand, const QString& undoText) = 0;
  virtual bool insertFeature(const pc::Feature& f, const QString& undoText) = 0;   // at the rollback position
  virtual bool changeParameter(const std::string& name, const std::string& expr) = 0;
  // NewParameterDialog; on OK the parameter is committed (undoable) and its name returned, "" = cancelled
  virtual QString createParameterInteractive(const QString& suggestedName, pc::ParamKind kind,
                                             const QString& valueExpr, QWidget* parent) = 0;
  virtual void setGhost(const TopoDS_Shape& s) = 0;              // preview (clearGhost with a null shape)
  virtual void showPrompt(const QString& text, int ms = 0) = 0;  // status bar message area
  virtual void showQuickMeasure(const QString& text) = 0;
  virtual void openClassicEditor(const std::string& featureId) = 0;   // FeatureDialog / sketch edit
  virtual bool featureDialogOpen() const = 0;
  virtual void refreshViewer(bool keepCamera = true) = 0;        // rebuild the viewer compound (regions, overlays)
};

// ---- tools (implemented by WP-B) --------------------------------------------------------------
class IActiveTool : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual QString id() const = 0;            // "select","pull","move","fill","combine","plane","shell",
                                             // "sketch.select","sketch.line","sketch.rect","sketch.circle",
                                             // "sketch.arc","sketch.point","sketch.trim"
  virtual QString title() const = 0;         // "Çek (Pull)"
  virtual QString prompt() const = 0;        // status-bar instruction for the current step
  virtual QList<ToolGuide> guides() const { return {}; }
  virtual QString activeGuide() const { return {}; }
  virtual void setActiveGuide(const QString&) {}
  virtual QList<ToolOption> options() const { return {}; }
  virtual void setOption(const QString& /*key*/, const QVariant& /*value*/) {}
  virtual void activate(IDesignHost* host) = 0;
  virtual void deactivate() = 0;
  virtual bool busy() const { return false; } // drag or dimension entry in progress
  virtual bool cancel() { return false; }     // one Esc step; false = nothing left to cancel
signals:
  void optionsChanged();
  void guidesChanged();
  void promptChanged(const QString& text);
};

class ToolManager : public QObject {            // implemented in ToolManager.cpp (WP-B)
  Q_OBJECT
public:
  ToolManager(IDesignHost* host, QObject* parent);   // parent = MainWindow (actions stay findChildren-visible)
  ~ToolManager() override;
  // Checkable actions, objectName "tool.<id>", texts as in SPEC §1.3 ("Seç", "Çizgi", "Dikdörtgen", ...).
  // NO shortcuts are set here (WP-A's Shortcuts.cpp assigns them).
  QAction* action(const QString& toolId) const;
  QList<QAction*> editActions() const;        // Seç, Çek, Taşı, Doldur
  QList<QAction*> sketchActions() const;      // Çizgi, Dikdörtgen, Çember, Yay, Nokta, Yapı çizgisi, Kırp
  QList<QAction*> constraintActions() const;  // 11 constraints, texts unchanged
  QList<QAction*> dimensionActions() const;   // 7 dimensions, texts unchanged
  QList<QAction*> otherActions() const;       // Birleştir, Düzlem (tool), Kabuk (tool), "Sil" (sketch)
  IActiveTool* active() const;
  QString activeId() const;
  bool activate(const QString& toolId);
  bool cancel();                              // Esc chain; false when already idle in Select
  void installKeyRouting(QWidget* w);         // event filter: Tab/Space/digits/Enter/Esc/Shift+arrows first
  void onDesignModeChanged(pcd::DesignMode m);
  QWidget* createMiniToolbar(QWidget* viewer);     // owned overlays (WP-B)
  QWidget* createToolGuideBar(QWidget* viewer);
signals:
  void activeToolChanged(const QString& id);
  void optionsChanged();                      // active tool's options changed (Options panel re-renders)
  void promptChanged(const QString& text);
private:
  struct Impl; Impl* d;
};

// ---- WP-B free functions called by WP-A --------------------------------------------------------
// Adds pickable sketch-region faces ("sketch:<sid>:r<k>") of the given (not consumed / explicitly shown) sketches.
int appendSketchRegionFaces(const pc::Document& doc, const pc::model::ModelResult& model,
                            const std::set<std::string>& sketchIds, TopoDS_Compound& comp,
                            std::map<std::string, TopoDS_Face>& faceNames);
} // namespace pcd
```

### 7.2 Sinyal akışı
1. Görünüm tıklaması → viewer `facePicked`/`edgePicked`/`datumPicked` sinyalleri → **WP-B'nin aktif aracı** bunları işler (host bu
   sinyalleri SelectTool kurulana kadar `onFacePicked`'e de bağlı tutar). `pick_ == SketchFace`, açık FeatureDialog (wantsFaces /
   wantsEdges) ve iki kenardan düzlem bekleyişi gibi **host'a ait özel durumlar önceliklidir**. Host `IDesignHost::featureDialogOpen()`
   açıkken araç tıklamaları ToolManager'a iletmez (FeatureDialog yönlendirmesi bugünkü gibi çalışır, `gui_harness_qa` bunu bekler).
2. `SelectionModel::changed` → WP-A: Özellikler paneli, durum çubuğundaki seçim listesi, vurgulama (`setHighlightedFaces/Edges`).
   **Vurgulamanın tek sahibi WP-A'dır.** WP-B yalnızca önvurgulama (`setPreselection`) ve hayalet (`IDesignHost::setGhost`) kullanır.
3. `ToolManager::activeToolChanged` / `optionsChanged` → WP-A: ribbon'daki checked durumu, Seçenekler paneli
   (`active()->options()` ile yeniden çizim; panel kullanıcı değişikliğini `active()->setOption` ile geri yazar).
4. `promptChanged` → WP-A: `statusBar()->showMessage(text)` (süresiz).
5. `IDesignHost::commitDocument` → `tryCommit` → `applyDocument` → `refreshViewer` → WP-A, `appendSketchRegionFaces` fonksiyonunu çağırır.

### 7.3 Klavye yönlendirmesi
`ToolManager::installKeyRouting(viewer)` ve `installKeyRouting(sketchEditor)` çağrılarını WP-A yapar (P0'da). Olay filtresi
`QEvent::ShortcutOverride` ve `KeyPress` olaylarını yakalar. Tab, Space, rakamlar, Enter, Esc ve Shift/Alt/Ctrl+oklar aktif araç
istediğinde tüketilir; harf kısayolları QAction'lara bırakılır.

### 7.4 Davranış uyumluluğu
* Bütün eski eylem **metinleri** ve **objectName**'leri bulunabilir kalmalıdır: "Yeni", "Aç...", "Kaydet", "Farklı Kaydet...",
  "Uygulama Olarak Kaydet... (Save as App)", "STEP (.step)...", "STL (.stl)...", "Geri Al", "Yinele", "Sketch...", "Düzlem...",
  "Extrude...", "Revolve...", "Delik...", "Fillet...", "Chamfer...", "Shell...", "Pattern...", "Gövdeleri Birleştir (Combine)...",
  "Sığdır", "Seç", "Nokta", "Çizgi", "Dikdörtgen", "Çember", "Yay", "Yapı çizgisi", "Sil", "Sketch'i Bitir", "Vazgeç",
  11 kısıt ve 7 ölçü metni; `actShowPlanes`, `actSketchDims3D`, `menuSectionPlane`, `actSectionXZ/YZ/XY/Quarter`,
  `actSectionFlip`, `sectionToggle`, `sketchOffset`, `bodiesTree`, `edgeContextMenu`, `datumContextMenu`, `menuFeatureParams`,
  `actPlaneNormal`, `actPlaneAngle`, `actPlaneTwoEdges`, `actPlaneDialog`, `actSketchOnPlane`, `actEditPlane`.
* `gui_harness_qa::clickToolButton`, **görünür** bir `QToolButton` arar. Bu yüzden sketch modunda "Dikdörtgen", "Seç", "Uzunluk",
  "Çakışık", "Çember", "Çap", "Sığdır", "Sketch'i Bitir", "Vazgeç" düğmeleri **Tasarım** sekmesinde ve görünür olmalıdır. Tasarım
  sekmesi sketch moduna girerken otomatik seçilir. "Seç" metnine sahip **tek** bir eylem vardır.
* Ana pencerede `QStandardItemModel` kullanan **tek** `QTableView` parametre tablosudur (iki düzenek bunu varsayar).
  Özellikler paneli `QTreeWidget` + `setItemWidget` kullanır.
* Durum mesajları `QStatusBar::showMessage` ile yazılmaya devam eder (`gui_harness_v2`, gövde seçince
  `currentMessage()` içinde gövde adını arar).
* `MainWindow`'un herkese açık otomasyon API'si (`enterSketchEdit`, `beginSketchOn`, `finishSketchPublic`, `createFeature`,
  `featureTree()`, `bodiesTree()`, `selectedEdge()`, `selectedDatum()`, `sectionControls()`, …) aynı imzayla kalır.
  `selectedEdge()` SelectionModel'deki son kenarı döndürür.
* Kesit kısayolu K'dan X'e taşınır. `gui_harness_v4` kısayol kullanmaz, değişiklik gerekmez.
* `gui_harness_v2` §1 ve §8 bugün **Ctrl+tık → kenar menüsü** bekler. Yeni davranışta menü **sağ tık** ile açılır. Düzeneği
  **WP-B** günceller (§10.3).

### 7.5 P0 (sözleşme önyüklemesi, en çok 0,5 gün, WP-A yapar, WP-B bu sırada kendi dosyalarına başlayabilir)
1. `src/designer/sources.cmake`: `file(GLOB CONFIGURE_DEPENDS ...)` ile `PC_DESIGNER_SOURCES` değişkeni (main.cpp hariç tüm
   `src/designer/*.cpp/*.h` ve `src/designer/tools/*.cpp/*.h`). `src/designer/CMakeLists.txt` ve `tests/gui/CMakeLists.txt`
   bu değişkeni kullanır. **Bundan sonra hiçbir WP CMake dosyasını düzenlemez** (yeni dosyalar glob ile otomatik girer;
   yeni test düzeneği ekleyen WP, düzeneğini `tests/gui/CMakeLists.txt` sonuna **tek blok** olarak ekler. Çakışma riski
   satır sonu eklemeyle sınırlıdır).
2. `ToolApi.h` (yukarıdaki metin), `ToolApi.cpp` (SelectionModel), `tools/ToolManager.cpp` boş sürümü (eylemleri bugünkü
   metinlerle oluşturur, `activate` hiçbir şey yapmaz, `appendSketchRegionFaces` 0 döndürür).
3. `MainWindow` `IDesignHost` arayüzünü uygular (eski özel fonksiyonlara ince sarmalayıcılar) ve ToolManager'ı oluşturur.
4. Tüm mevcut ctest'ler yeşil kalır. P0 commit'i iki WP'nin ortak tabanıdır.

---

## 8. (g) Seçenekler paneli içeriği (araç başına)

Panel başlığı "Seçenekler - <araç başlığı>" biçimindedir. Gruplar kalın başlık, Bool = `QCheckBox`, Choice = radyo grubu (≤ 4 seçenek)
ya da `QComboBox`, Expr = `ExprEdit`, Action = `QPushButton`. ★ = mini araç çubuğunda da görünür.

| Araç | Seçenekler (key: Etiket (English)) |
|---|---|
| Seç (Select) | `filter`: Seçim filtresi (Akıllı/Yüz/Kenar/Gövde/Düzlem/Sketch) · `loops`: Çift tık kenar döngüsü seçer ✓ · 2. faz: `box`: Kutu seçimi |
| Çek (Pull) | **Birleştirme**: `merge` ★ Otomatik / Ekle (Add) / Kes (Cut) / Birleştirme yok (No Merge) / Kesişim [PC] · `targets`: Hedef gövdeler (Otomatik ▾ çoklu seçim) · `keepTool`: Kesiciyi koru · `bodyName`: Yeni gövde adı (Text, yalnızca Birleştirme yok'ta) · **Kenar**: `edge` ★ Yuvarlat (Round) / Pah (Chamfer) · **Yön**: `bothSides` ★ İki yöne çek (Pull Both Sides, = `symmetric`) · `full` Tam dönüş 360° (Full Pull, yalnızca Revolve) · `draft`: Eğim açısı (Expr, Extrude `draft`) [PC] · **Ölçü**: `ruler` ★ Cetvel (Ruler) (2. faz) · `createGroup` ★ Grup oluştur (Ctrl+G) (Action) · `applyOnRelease`: Bırakınca hemen uygula |
| Taşı (Move) | `pattern` ★ Desen oluştur (Create Patterns) · `ruler` ★ Cetvel · `moveGrid`: Izgarayı taşı (Move grid) · `createGroup` ★ Grup oluştur |
| Doldur (Fill) | `mode`: Kaldır (sil) / Bastır (yalnızca cut özellikleri) · bilgi etiketi: "Seçili yüzü oluşturan özellik: Fillet1 (kenar 2/4)" |
| Birleştir (Combine) | `op` ★ Çıkar / Kesişim / Birleştir · `keepTools`: Kesiciyi koru · `largestOnly`: Yalnız en büyük parça |
| Düzlem (Plane) | `kind`: Otomatik / Yüzden / Kenara dik / Kenardan açılı / İki kenardan / Eksen açılı · `show`: 3B'de göster · `dialog`: Diyalogla oluştur... (Action) |
| Kabuk (Shell) | `thickness` (Expr) · `direction`: İçe / Dışa |
| Sketch araçları (hepsi) | `keepDims` Yazılan ölçüleri kısıt olarak sakla ✓ · `construction` ★ Yapı çizgisi · `snapGrid` Izgaraya yakala ✓ · `gridStep`: Izgara adımı (Expr, mm) · `autoHV` Otomatik yatay/dikey ✓ |
| Çizgi | + `chain` Zincirleme çiz ✓ |
| Dikdörtgen | + `fromCenter` Merkezden ★ |
| Çember / Yay | (ortak seçenekler) |
| Kırp (Trim) | bilgi: "Silinecek parçaya tıklayın" |

---

## 9. (h) Özellikler paneli içeriği (seçim türüne göre)

`QTreeWidget` (2 kolon: Özellik | Değer). Düzenlenebilir değerler `setItemWidget` ile `ExprEdit` (parametre seçicisiyle) ya da
`QComboBox` olur. Değişiklik `IDesignHost::commitDocument` ile gider (tek geri alma adımı). Salt okunur satırlar gridir.
Kategoriler kalın üst düğümlerdir. Birden çok öğe seçiliyse ortak alanlar gösterilir ve farklı değerler "Çeşitli (Varies)" yazılır [D: "Varies"].

| Seçim | Kategori: alanlar (düzenlenebilir = ✎) |
|---|---|
| **Gövde (Body)** | Genel: Ad ✎ (`renameBody`), Renk ✎ (yalnızca görünüm), Görünür ✎ · Fiziksel: Hacim mm³, Yüzey alanı mm², Ağırlık merkezi, Sınır kutusu · Geçmiş: Oluşturan özellik (bağlantı → Geçmiş sekmesinde seçer), Gövdeyi değiştiren özellikler · İmalat [PC]: İmalat denetimine dahil ✎ (oluşturan özelliğin `mfg` özelliği) |
| **Yüz (Face)** | Genel: Kalıcı ad (kopyalanabilir), Tür (Düzlem / Silindir / Koni / Küre / Torus / Serbest), Alan, Normal (düzlemsel) veya Yarıçap ve eksen (silindirik) · Gövde · Oluşturan özellik · Özelliğe göre ✎: Fillet yüzü → Yarıçap ✎; Chamfer yüzü → Mesafe ✎; Hole duvarı → Çap ✎ ve Derinlik ✎; Extrude end/start → Mesafe ✎ ve Yön ✎; Revolve → Açı ✎ |
| **Kenar (Edge)** | Kalıcı ad, Tür (Doğru / Çember / …), Uzunluk, Komşu yüzler (2 ad), Fillet/Chamfer'a ait mi |
| **Datum düzlemi** | Ad (özellik kimliği), Tür ✎, Ofset ✎, Döndürme ✎, Açı ✎ (edgeAngle / axisAngle), t ✎, Ters çevir ✎, 3B'de göster ✎, Referanslar (salt okunur) |
| **Sketch (3B'de ya da ağaçtan)** | Düzlem (XY / XZ / YZ / yüz adı / düzlem kimliği), Düzlem ofseti ✎, Eleman / kısıt / ölçü sayısı, DOF ve çözücü mesajı, Kullanan özellikler, 3B'de göster ✎ |
| **Sketch bölgesi** | Sketch, bölge sırası, alan, çevre, delik sayısı |
| **Sketch eğrisi (sketch modunda)** | Tür, Uzunluk / Yarıçap (sürücü ölçü varsa ✎ ifadesi), Yapı geometrisi ✎, İlgili kısıtlar listesi (Del ile silinebilir) |
| **Sketch noktası** | U, V, Sabit ✎, Çakışık olduğu öğeler |
| **Özellik (Geçmiş sekmesinden)** | Tüm `props` değerleri, alan türüne göre (FeatureDialog ile aynı anahtarlar ve etiketler; ifade alanları ✎, op/direction/kind ✎ combo), Bastırılmış ✎, Durum (ok / hata mesajı), Parametreler (`featureParameterSummary`). Ekstra: "Klasik diyalogda aç..." düğmesi |
| **Parametre (Gruplar sekmesinden)** | Ad, İfade ✎, Değer, Birim, Min ✎, Maks ✎, Uygulamada göster ✎, Etiket ✎, Grup ✎, Açıklama ✎, Kullanıldığı yerler |
| **Seçim yok** | Belge: Dosya, Parametre / özellik / gövde sayısı, Toplam hacim, İmalat süreç ön ayarı (salt okunur, "Ayarlar..." bağlantısı) |

---

## 10. (i) Fazlı uygulama planı ve iş paketleri

### 10.1 Dosya sahipliği (ayrık)

**WP-A "kabuk" (shell) — sahip olduğu ve oluşturduğu dosyalar**
* Mevcut: `src/designer/MainWindow.h`, `MainWindow.cpp`, `MainWindowOps.cpp`, `MainWindowV2.cpp`, `FeatureDialog.h/.cpp`,
  `ExprEdit.h/.cpp`, `MfgDialogs.h/.cpp`, `Icons.h/.cpp` (yeni ikonlar: pull, move, fill, combine, home, planview, spin, pan, zoom,
  mode3d, modesketch, modesection, trim, group, props, options; WP-B ikon isterse `CONTRACT_REQUESTS.md` üzerinden ister ya da
  kendi dosyasında yerel olarak çizer), `main.cpp`, `src/designer/CMakeLists.txt` (yalnızca P0), `src/designer/STATUS.md` (A
  bölümü), `docs/KULLANIM_KILAVUZU.md`.
* Yeni: `ToolApi.h`, `ToolApi.cpp`, `sources.cmake`, `Ribbon.h/.cpp` (Ribbon, RibbonGroup, uygulama menü düğmesi, hızlı erişim),
  `StructurePanel.h/.cpp` (Yapı ağacı + sekme kabı; Geçmiş sekmesine `tree_`, Gruplar sekmesine `paramView_` yerleştirir),
  `SelectionPanel.h/.cpp`, `OptionsPanel.h/.cpp`, `PropertiesPanel.h/.cpp`, `StatusBarWidgets.h/.cpp`, `Shortcuts.h/.cpp`,
  `DesignMode.cpp` (MainWindow'un mod fonksiyonları; ayrı çeviri birimi).
* Testler: `tests/gui/gui_harness_sc_shell.cpp` (yeni), mevcut `gui_harness.cpp`, `gui_harness_v3.cpp`, `gui_harness_v4.cpp`,
  `gui_harness_v5.cpp` güncellemeleri.

**WP-B "araçlar" (tools) — sahip olduğu ve oluşturduğu dosyalar**
* Mevcut: `src/designer/SketchEditor.h/.cpp`, `src/viewer/ShapeViewer.cpp`, `src/viewer/pc/ShapeViewer.h` (**yalnızca ekleme**),
  `src/viewer/Tessellation.*`, `src/viewer/SectionControls.*` (gerekirse; `toggleAction` API'si aynı kalır), `src/viewer/STATUS.md`.
* Yeni (`src/designer/tools/`): `ToolManager.cpp`, `SelectTool.h/.cpp`, `PullTool.h/.cpp`, `PullMapping.h/.cpp` (saf mantık:
  seçim + belge → aday Document/Feature; GUI bağımlılığı yok), `MoveTool.h/.cpp`, `FillTool.h/.cpp`, `CombineTool.h/.cpp`,
  `PlaneTool.h/.cpp`, `ShellTool.h/.cpp`, `SketchToolAdapter.h/.cpp` (sketch araçlarını IActiveTool'a sarar),
  `DimensionBox.h/.cpp`, `ViewportOverlay.h/.cpp` (oklar, tutamaçlar, önvurgulama çizimi), `ToolGuideBar.h/.cpp`,
  `MiniToolbar.h/.cpp`, `SketchRegions.cpp`, `TrimLogic.h/.cpp` (sketch kırpma, saf mantık).
* Testler: `tests/gui/gui_harness_sc_tools.cpp` (yeni), mevcut `gui_harness_v2.cpp`, `gui_harness_v6.cpp`, `gui_harness_qa.cpp`
  güncellemeleri. `tests/gui/harness_robust.h` **dondurulmuştur** (ikisi de düzenlemez; ek yardımcılar kendi düzenek dosyalarına).

**Hiçbir WP'nin dokunmadığı yerler**: `src/core`, `src/sketch`, `src/model`, `src/mfg`, `src/apppkg`, `src/appui`, `src/runner`
(model değişiklikleri CR-1…CR-5 olarak model sahibine gider, §11).

### 10.2 Fazlar

**P0 — Sözleşme önyüklemesi (WP-A, ≤ 0,5 gün)**: §7.5.

**Faz 1 — "İlk 10 dakika" (paralel, ~6–8 iş günü)**

| WP-A | WP-B |
|---|---|
| A1 Ribbon + Dosya menüsü + hızlı erişim. Menü çubuğu gizlenir, `mainBar_`, `sketchBar_`, `sketchBar2_` kaldırılır. Eylemler ribbon gruplarına (Tasarım/Görüntü/Ölçüm/İmalat). Bağlamsal sketch grupları | B1 Viewer eklemeli API (§6.2): SpaceClaim gezinme şeması, sol sürükleme kipi, kamera kuaterniyonu, `worldToScreen`, `planeToScreen`, `setPlanView`, `setHomeView`, `fitToBox`, kamera geçmişi, düz tıkla kenar önceliği, önvurgulama, `cameraChanged` |
| A2 Dock düzeni: Yapı paneli (Yapı/Geçmiş/Seçim/Gruplar sekmeleri, altta), Seçenekler/Özellikler sekmeleri, İmalat Denetimi altta, QSettings, "Düzeni sıfırla" | B2 SketchEditor kaplaması (§6.1): afin dönüşüm, şeffaf çocuk widget, olay iletme, 3B altlık; `pointScreenPos` uyumu |
| A3 `setDesignMode` (K/X/D) + "Sketch düzlemi seç" durumu (hover önizlemesi mevcut `ShapeViewer::setSketchPlane` ızgarasıyla, imleç altındaki düzlemsel yüze/datuma/orijin düzlemine; tıklama düzlemi seçer) + X → seçili düzlemde kesit | B3 Sketch anında ölçü girişi (§5): DimensionBox, Tab/Enter/Esc/Space/rakam, kilitli alanlar, sürücü kısıtlar, Ctrl+G; Dikdörtgen ilk köşe yakalama |
| A4 Shortcuts.cpp tablosu (§4), desteklenmeyen tuş mesajları, Ctrl+Y | B4 ToolManager + SelectTool (tık / Ctrl+tık / çift tık döngü / üçlü tık gövde, filtre) + ToolGuideBar + MiniToolbar (sketch ve Çek) |
| A5 OptionsPanel (ToolOption çizimi), PropertiesPanel (§9; 1. fazda gövde, yüz, kenar, datum, sketch, özellik, parametre) | B5 PullTool + PullMapping: bölge→Extrude, end/start→mesafe, fillet/chamfer yüzü→yarıçap, kenar→Fillet/Chamfer, Alt+tık→Revolve, Hole duvarı→çap; önizleme hayaleti; parametre bağlama kuralları; Ekle/Kes/Birleştirme yok |
| A6 Durum çubuğu (istem, seçim listesi, hızlı ölçüm, filtre, ← →), Ctrl+H/Alt+H/Ctrl+Shift+H, Yapı ağacı ↔ seçim eşitlemesi | B6 `appendSketchRegionFaces` + 3B'ye Dön akışı (commit → Çek aracı + bölge vurgusu) |
| A7 Düzenek güncellemeleri (gui_harness, v3, v4, v5) + yeni `gui_harness_sc_shell` | B7 Düzenek güncellemeleri (v2 sağ tık kenar menüsü, v6, qa) + yeni `gui_harness_sc_tools` |

**Faz 2 — "Doğrudan modelleme genişlemesi" (~6 iş günü)**
* WP-A: Seçim sekmesinde "Benzerini seç", Gruplar sekmesinde çift tıkla aracı yeniden çağırma (araç etkinleştirme isteğini
  ToolManager'a iletir), Ölçüm sekmesinde Kütle özellikleri, ribbon daraltma, Görüntü sekmesi tamamlanması.
* WP-B: Taşı (§2.7: düzlem, sketch ofseti, desen), Doldur (§2.8), Birleştir aracı (§2.9), Düzlem aracı (§2.10), Kabuk aracı,
  Kadar (U), Cetvel, Kırp (T), Ölç (E), radyal menü (O), kutu seçimi, Ctrl+A, Ctrl+Shift+I, Ctrl+T dönme merkezi.

**Faz 3 — Model sözleşme istekleri gerçekleşince (§11)**: aynı sketch'in bölgelerini ayrı ayrı çekmek (CR-1), herhangi bir
düzlemsel yüzü çekmek (CR-2), gövde taşıma ve döndürme (CR-3), asimetrik iki yöne çekme (CR-4), datum eksen ve nokta (CR-5).
UI tarafı yalnızca PullMapping/MoveTool satırlarını açar.

Entegrasyon: her faz sonunda iki WP aynı dalda birleşir, tüm ctest'ler ve `ACCEPTANCE.md` maddeleri çalıştırılır. Birleşme
sırası: önce WP-B'nin viewer değişiklikleri (eklemeli), sonra WP-A, sonra WP-B'nin designer/tools değişiklikleri.

### 10.3 Etkilenen mevcut testler (güncelleme sahibi)

| Test | Olası kırılma | Sahip | Yapılacak |
|---|---|---|---|
| `gui_harness_v2` §1, §8 | Ctrl+tık artık kenar menüsü açmıyor | B | Kenar konumunda `customContextMenuRequested` (sağ tık) ile `edgeContextMenu` beklenir. `edgePicked` ve `selectedEdge()` kontrolleri düz tıkla aynen kalır |
| `gui_harness_v2` "Uzunluk" tetikleme, `Tool::Rect` | yok (metinler ve enum aynı) | B | — |
| `gui_harness_v4` | Kesit düğmesi bir `QToolBar` içinde ve "Görünüm" menüsünde bulunmalı | A | Ribbon grupları gömülü QToolBar kullanır ve "Görünüm" QMenu'sü korunur, bu yüzden değişiklik beklenmez. Kısayol K→X olur |
| `gui_harness_qa` | `clickToolButton` görünür düğme arar; "Sığdır" metni; Ctrl+tıkla Fillet listesine kenar | B | Değişiklik beklenmez. Kırılırsa önce davranış uyumluluğu düzeltilir (§7.4), düzenek en son değiştirilir |
| `gui_harness_v6` | sketch editörü kaplama olunca koordinatlar | B | `pointScreenPos` uyumu korunursa değişiklik yok |
| `gui_harness`, `v3`, `v5` | paramView araması, menü metinleri | A | Değişiklik beklenmez |
| `persistcad --selftest/--selftest2`, `--sketch-screenshot` | sketch artık ayrı sayfa değil | A (main.cpp) + B | `--sketch-screenshot` görünümün tamamını (model + kaplama) yakalar |
| `tests/e2e.sh --update-docs` ekran görüntüleri | düzen değişir | A | Faz 1 sonunda `docs/screenshots/final_*.png` yenilenir |

### 10.4 Riskler
1. **Sketch kaplaması + kamera**: turntable kameradan kuaterniyona geçiş ve `planeToScreen` doğruluğu. Önlem: B1'de sayısal birim
   testi (rastgele 50 kamera; `toScreen(toSketch(p)) == p` ±0,5 px).
2. **Kısayol çakışması**: tek harfli kısayolların tablo hücre düzenleyicilerinde, ExprEdit tamamlayıcı açılır listesinde ve
   DimensionBox'ta yanlışlıkla tetiklenmesi. Önlem: düzenekte her odak türü için "harf yaz → araç değişmedi" kontrolü.
3. **Parametrik anlam farkı**: SpaceClaim'de çekilen yüz "geometridir". Bizde bir özelliğe ya da parametreye bağlıdır. Türetilmiş
   parametreye bağlı çekme reddedilir. Kullanıcıya mesaj açık olmalı.
4. **Commit gecikmesi + modal inceleme penceresi**: Çek sonrası imalat hatası modal pencere açar ve "akış" bozulur. Önlem: §6.4
   kuralı. İnceleme penceresi "Vazgeç" derse araç Armed durumunda kalır.
5. **Bölge eşleme**: CR-1 gelene kadar çok bölgeli sketch'te tek bölge çekilemez. Önlem: bu durumda tüm bölgeleri vurgulayan uyarı:
   "Bu sketch'in 3 bölgesi var; hepsi birlikte çekilecek (bölge seçimi CR-1 ile gelecek)."
6. **İki WP ve tek MainWindow**: WP-B'nin host'a ihtiyaç duyduğu her şey `IDesignHost` üzerinden gider. Yeni ihtiyaç doğarsa
   `CONTRACT_REQUESTS.md` yoluyla iletilir, WP-B `MainWindow*` dosyalarına **dokunmaz**.

---

## 11. Model sözleşme istekleri (her iki WP'nin dışında; model sahibine)

| No | İstek | Gerekçe |
|---|---|---|
| CR-1 | Extrude ve Revolve: `regions = "u,v;u,v"` (sketch düzleminde iç noktalar). Boşsa tüm profil yüzleri kullanılır (bugünkü davranış). Bir nokta hiçbir yüzün içinde değilse özellik hatası verir | SpaceClaim'de bir sketch'in her bölgesi ayrı çekilir |
| CR-2 | Extrude: `face=<düzlemsel yüz adı>` (sketch yerine). Taban = o yüz, yön = dışa normal. Yan yüz adları `<fid>.side.k` | Herhangi bir düzlemsel yüzü çekmek ("kutunun yan yüzünü çek") |
| CR-3 | `FeatureType::Transform` (veya `Move`): `bodies`, `tx,ty,tz`, `axis`, `angle`, `copy=0|1` | Taşı aracıyla gövde öteleme, döndürme ve kopyalama |
| CR-4 | Extrude `direction=twoSided`, `distance2` | İki yöne farklı çekme |
| CR-5 | Datum eksen ve nokta özellikleri | Oluştur > Eksen / Nokta, Döndür kılavuzunda eksen seçimi |

Bunlar `docs/CONTRACT_REQUESTS.md` dosyasına "SC-UX CR-n" başlığıyla yazılmalıdır.

---

## 12. Doğrulanamayan SpaceClaim bilgileri (uygulamadan önce gerçek kurulumda kontrol edin)
* Tasarım sekmesindeki grupların tam sırası ve Oluştur grubunun tam içeriği (Düzlem, Eksen, Nokta, Orijin, Desenler, Kabuk, Ofset,
  Ayna). Resmi sayfalarda Yönlendir, Kesişim ve Düzenle grup sayfaları var, içerik sayfaları erişilemedi.
* Çizerken Tab/Space/Enter ile ölçü girişinin ayrıntıları. Resmi "Dimensioning the Current Line" alt sayfası okunamadı. Tab'ın
  iki alan arasında geçtiği PADT blogunda Çek için anlatılıyor.
* Z'nin tek başına Sığdır olması (üçüncü taraf listesi). Resmi listede **Alt+Z** var. İkisini de destekliyoruz.
* Tekerlek ile yakınlaştırma (resmi sayfa yalnızca Ctrl+orta sürüklemeyi yazıyor).
* Seçenekler ve Özellikler panellerinin varsayılan olarak sol altta sekmeli durması (resmi sayfa: Özellikler solda, Yapı ve Seçenekler'in altında).
* 3B modunda bir sketch aracı (L/R/C) seçilince sketch moduna geçilip düzlem sorulması.
* Yeni belgenin hangi modda açıldığı (biz 3B + Seç ile açıyoruz; K basınca boş belgede doğrudan XY).
* Radyal menünün nasıl açıldığı (resmi kısayol listesinde O. Radyal menü sayfası açma yolunu belirtmiyor).
* Sketch mini araç çubuğunda ayrı bir "Seç" düğmesi olup olmadığı. Resmi sayfa 4 düğme listeliyor (3B'ye dön, Yeni sketch
  düzlemi seç, Izgarayı taşı, Plan görünümü). Biz bu dördünü uyguluyoruz.
* Çekmeyi bırakınca anında uygulama (biz 600 ms gecikmeli otomatik commit kullanıyoruz, §6.4).
* Space tuşunun ölçü kutusunu açması.

## 13. Kaynaklar
* Ansys SpaceClaim 2025 R2 çevrimiçi yardımı (ansyshelp.ansys.com, Discovery SpaceClaim user manual): Keyboard Shortcuts,
  The SpaceClaim Interface, Pull (overview, Tool Guides, Options), Move Tool Guides, Move Options, Edit Ribbon Group, Fill,
  Revolving Faces, Rounding Edges, Design Modes, Sketch Mini-Toolbar, The Sketch Grid, Groups, Options panel, Properties,
  Status Bar, Radial Menu, Mouse Gestures, Structure Tree.
* RS DesignSpark: "What keyboard shortcuts are available in DSM" (aynı çekirdek, kısayollar örtüşüyor).
* tutorialtactic.com SpaceClaim kısayol rehberi (üçüncü taraf; Z = Zoom Extents yalnızca burada).
* PADT, "New Tricks for an Old Dog: Eric Learns ANSYS SpaceClaim" yazı 2 ve 3 (Çek'te Tab ile iki alan, Alt+tık ile döndürme,
  Taşı tutamacı ve cetvel).
* Eğitim sunumu (Google Slides, SCDM Introduction): seçim tıklamaları (çift/üçlü tık, kutu yönleri), Yapı paneli = geçmiş değil.

---

## 14. Bootstrap tamamlandı (P0, 15 Kasım 2026)

**Dondurulan sözleşme:** `SC-UX ToolApi 1.0` (`pcd::kToolApiVersionMajor = 1`, `kToolApiVersionMinor = 0`).
Değişiklik yalnızca `docs/CONTRACT_REQUESTS.md` içinde "SC-UX:" önekli istekle yapılır (§7.1).

Oluşturulan / değiştirilen dosyalar:
* `src/designer/ToolApi.h` — §7.1 metninin tamamı aynı imzalarla + aşağıdaki eklemeler ([P0+] işaretli).
* `src/designer/ToolApi.cpp` — `SelectionModel` uygulaması + `selKindText`, `selectionSummary`, `selFilterText`.
* `src/designer/tools/ToolManager.cpp` — **boş (stub) ToolManager** + `appendSketchRegionFaces` (0 döndürür). Sahibi artık WP-B;
  dosya adı korunarak içeriği değiştirilir (glob ikinci bir tanımı da derler, bağlama hatası olur).
* `src/designer/sources.cmake` — `PC_DESIGNER_SOURCES` (glob, `CONFIGURE_DEPENDS`; `src/designer/*.{cpp,h}` ve
  `src/designer/tools/*.{cpp,h}`, `main.cpp` hariç) ve `PC_DESIGNER_INCLUDE_DIRS`.
* `src/designer/CMakeLists.txt` — tüm tasarım kodu bir kez `pc_designer` statik kütüphanesine derlenir; `persistcad` = `main.cpp` + `pc_designer`.
* `tests/gui/CMakeLists.txt` — `pc_add_gui_harness(<ad> SOURCES ... [RUNNER] [TIMEOUT s] [ARGS ...] [DEFINES ...])` fonksiyonu;
  mevcut 7 düzenek aynı test adları, argümanlar, zaman aşımları ve tanımlarla bu fonksiyona taşındı ve `pc_designer`'a bağlanır.
  Yeni düzenek ekleyen WP dosya sonuna tek bir `pc_add_gui_harness(...)` satırı ekler.
* `tests/gui/gui_harness_sc_shell.cpp` (WP-A), `tests/gui/gui_harness_sc_tools.cpp` (WP-B) — ctest'e kayıtlı yer tutucular
  (`[ ok ] placeholder`; sözleşmenin derlenip bağlandığını da denetler). `PC_SC_SHOTS_DIR` = `docs/spaceclaim_ux/shots`.
* `tests/gui/sc_stub_host.h` — `pcd::test::StubDesignHost` (başlık-yalnız, sözleşmenin parçası değil; WP-B serbestçe genişletir).

Sonuç: tam derleme + `ctest` 191/191 geçti (Xvfb; 7 eski GUI düzeneği + 2 yer tutucu + birim testleri). `persistcad --selftest` geçti.

§7.1 metninden sapmalar ve eklemeler (hepsi eklemeli; §7.1'deki hiçbir imza değişmedi):
1. **MainWindow henüz `IDesignHost` uygulamıyor ve ToolManager oluşturmuyor** (§7.5 adım 3 ertelendi): "mevcut davranış
   değişmez" kuralı gereği. Bu iş WP-A'nın A1/A3'ünde yapılır. Stub ToolManager eylemleri "Seç", "Çizgi", kısıt/ölçü metinlerini
   tekrar ürettiği için, MainWindow ToolManager'ı oluşturduğu anda eski `sketchBar_`/`sketchBar2_` eylemleri kaldırılmalıdır (§7.4: tek "Seç").
2. Stub dosyası adı `tools/ToolManager.cpp` (§7.5 adı); ayrı bir `ToolManagerStub.cpp` yok.
3. Düzenekler tasarım kaynaklarını tek tek derlemek yerine `pc_designer` kütüphanesine bağlanır (davranış aynı, derleme ~7 kat az).
   Önceden runner kaynaklarını derlemeyen v6 artık `RUNNER` seçeneğiyle derler (zaten `persistcad_runner` bağımlılığı vardı).
4. `IDesignHost` eklemeleri: `beginSketchPlanePick`, `openSketchFrame`, `expressionContext` (ExprEdit/DimensionBox için çalışma
   belgesi), `deleteFeatureInteractive`, `setFeatureSuppressed` (Doldur), `evaluateCandidate` (hayalet/Kadar, commit'siz),
   `evaluateExpression`, `setToolEmphasis` (araç vurgusu; seçim vurgusu WP-A'da kalır).
5. `IActiveTool` isteğe bağlı kancaları: `pick(PickEvent)`, `hover`, `wantsKey`/`keyPress`, `createGroup` (Ctrl+G), `deleteSelection` (Del),
   `createOptionsWidget`, `populateContextMenu`, `documentChanged`; sinyal `finished()`.
6. `ToolManager` eklemeleri: `toolIds`, `routeKey` (§4'te adı geçiyordu), `handlePick`, `handleHover`, `createGroup`, `deleteSelection`,
   `populateContextMenu`, `onDocumentChanged` (host her `applyDocument` sonrası çağırır), `createOptionsWidget`, `setOption`,
   `cycleGuide`; sinyal `guidesChanged`. Araç dışı eylem objectName'leri: `tool.sketch.construction`, `tool.sketch.delete`,
   `tool.constraint.<SkConstraintType int>`, `tool.dimension.<int>`. "sketch.select" ayrı eylem değildir, tek "Seç" (`tool.select`) paylaşılır.
7. Yeni türler: `PickEvent` (öğe, değiştirici tuşlar, düğme, tık sayısı 1/2/3, görünüm pikseli); `ToolOption::Type::Info`,
   `ToolOption::paramHint/paramKind`; `ToolGuide::enabled`; `SelectionModel::count/contains/namesOfKind/remove/canRevert`;
   `Q_DECLARE_METATYPE` (SelItem, SelFilter, DesignMode).
8. `SelectionModel::accepts`: Özellik ve Parametre her filtrede kabul edilir; Yüz filtresi sketch bölgesini, Kenar filtresi sketch
   eğrisini de kabul eder; `add` filtreyi uygular, `set` uygulamaz (programatik seçim).
