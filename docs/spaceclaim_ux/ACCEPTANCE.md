# SpaceClaim kullanıcısının ilk 10 dakikası — kabul listesi

Her madde, SpaceClaim'e alışmış bir kullanıcının ilk denemelerinde yapacağı **gözlenebilir** bir davranıştır. Her biri bir
GUI düzeneği (harness) kontrolüne eşlenir. Düzenekler:
* **SH** = `tests/gui/gui_harness_sc_shell.cpp` (WP-A)
* **TL** = `tests/gui/gui_harness_sc_tools.cpp` (WP-B)

Ortak yardımcılar (her düzenek kendi dosyasında tanımlar, `harness_robust.h` dondurulmuştur):
* `key(w, "K")` → `QTest::keyClick(viewer veya sketch kaplaması, Qt::Key_K)`
* `type(s)` → odaktaki widget'a `QTest::keyClicks`
* `vclick(p, mods)` → görünümde gerçek fare tıklaması
* `sketchPx(u,v)` → `w.sketchEditor()` üzerindeki ekran noktası (dönüşüm `planeToScreen`'den; `pointScreenPos(0)` = orijin)
* `regionPx(name)` → bölge yüzünün ağırlık merkezinin `viewer->worldToScreen` görüntüsü
* `facePx(name)` → yüz ağırlık merkezinin ekran konumu (görünür olduğu, `pickAt` ile doğrulanır)
* `waitFor(pred, ms)`

Test kancaları: tabloda geçen `previewSize`, `ghostVisible()`, `cameraAz`/`halfH`/`target`, `handleCount()` gibi salt okunur
değerleri, sahibi olan WP `Q_PROPERTY` ya da herkese açık getter olarak sağlar (SketchEditor ve viewer: WP-B; paneller: WP-A).
Ürün davranışını değiştirmezler.

Faz sütunu: 1 = Faz 1 kabulü için zorunlu, 2 = Faz 2, 3 = model CR'larına bağlı.

| # | Kullanıcı davranışı (gözlenebilir) | Otomatik kontrol | Düzenek | Faz |
|---|---|---|---|---|
| 1 | Uygulama açılır. Üstte **Dosya** düğmesi, hızlı erişim (Yeni/Aç/Kaydet/Geri Al/Yinele) ve **Tasarım · Görüntü · Ölçüm · İmalat** sekmeleri görünür. Menü çubuğu yoktur | `findChild<QTabBar*>("ribbon")` ya da `Ribbon` sekme metinleri == {"Tasarım","Görüntü","Ölçüm","İmalat"}; `!w.menuBar()->isVisible()`; `quickAccessBar` 5 görünür düğme | SH | 1 |
| 2 | Tasarım sekmesinde **Yönlendir, Sketch, Mod, Düzenle, Kesişim, Oluştur** grupları bu sırayla görünür. Seç/Çek/Taşı/Doldur büyük düğmedir | `ribbonGroup.*` başlıklarının x sırası; `tool.pull` düğmesinin ikon boyutu ≥ 32 | SH | 1 |
| 3 | **K**'ya basar (boş belge). Sketch modu açılır, ızgara XY'de görünür. Mod grubunda "Sketch modu" seçilidir, ribbon'da Kısıtlar/Ölçüler/Sketch Oturumu grupları belirir | `key K` → `w.inSketchMode()`; `w.document()` henüz değişmedi; aktif mod eylemi checked; bağlamsal grupların `isVisible()` değeri | SH | 1 |
| 4 | **R**'ye basar, orijine tıklar, **60** yazar, **Tab**, **40** yazar, **Enter**'a basar → 60×40 dikdörtgen oluşur, ölçüler görünür, DOF 0 olur | `key R`; `vclick(sketchPx(0,0))`; `type("60")`; Tab; `type("40")`; Return → sketch'te 4 çizgi; `Length` kısıtları {"60","40"}; `sketchEditor()->dof()==0`; durum çubuğunda "Sketch DOF: 0" | TL | 1 |
| 5 | Çizerken fareyi oynatır: yazılmış (kilitli) genişlik sabit kalır, yükseklik imleçle değişir | 4. adımda `60` + Tab sonrası fare hareketi → önizleme dikdörtgeninin genişliği 60 ± 1e-6, yüksekliği imlece göre değişiyor (`sketchEditor()->property("previewSize")` test kancası) | TL | 1 |
| 6 | **C**'ye basar, (100,0)'a tıklar, **20** yazar, Enter → Ø20 çember ve `Diameter=20` kısıtı | çember sayısı +1; `Diameter` kısıtı "20"; yarıçap 10 | TL | 1 |
| 7 | **L** ile çizgiye başlar, **Space**'e basar, **W/2** yazar → ifade kabul edilir (W parametresi varsa) ve tamamlayıcı W'yi önerir | bracket.pcad açık; `Length` kısıtının `valueExpr == "W/2"`; geçersiz ifade (`W/`) sonrası Enter'da öğe sayısı değişmez | TL | 1 |
| 8 | Ölçü kutusundayken **Ctrl+G** → yeni parametre penceresi açılır (ad önerisi var, "Uygulamada göster"). Tamam → parametre **Gruplar** tablosunda görünür ve kısıt ona bağlanır | `NewParameterDialog` modal yakalanır, `npName` doldurulur, `npExposed` işaretlenir, OK → `paramRow(w, ad) >= 0`; kısıtın `valueExpr == ad`; parametre `exposed` | TL | 1 |
| 9 | Esc zinciri: kutu açıkken Esc kutuyu kapatır, tekrar Esc çizimi iptal eder, tekrar Esc aracı Seç'e döndürür, tekrar Esc seçimi temizler. Sketch modundan **çıkmaz** | her Esc sonrası durum (`DimensionBox` görünürlüğü, `drawStage`, `ToolManager::activeId()`, seçim) ve `w.inSketchMode()` | TL | 1 |
| 10 | Sketch sırasında **orta düğmeyle sürükler**: model döner, sketch eğik görünür ve çizime devam edilebilir (eğik bakışta çizgi doğru düzleme düşer) | `QTest::mousePress/Move/Release` MiddleButton → `cameraChanged` yayıldı; sonra Çizgi ile 2 tık → yeni noktaların sketch koordinatları, tıklanan ekran noktalarının `screenRay`∩düzlem sonucuna ±1e-3 eşit | TL | 1 |
| 11 | **V**'ye basar → sketch düzlemine dik bakış (Plan görünümü). Sketch'in `v` ekseni yukarı bakar | V sonrası `planeToScreen` matrisi: eksenler ortogonal, ölçek eşit, v ekranda yukarı (m22 < 0) | TL | 1 |
| 12 | **D**'ye basar → sketch commit edilir ve 3B moduna dönülür. Dikdörtgen ve çember **yarı saydam mavi bölge** olarak görünür. Geçmiş sekmesinde "Sketch1" vardır | `!w.inSketchMode()`; `doc.features` içinde Sketch; `appendSketchRegionFaces` → viewer'da `sketch:Sketch1:r0` adı `pickAt(regionPx)` ile bulunuyor; undo yığınında +1 | SH (mod) + TL (bölge) | 1 |
| 13 | **P**'ye basar, dikdörtgen bölgesine tıklar → bölgede sarı ok çıkar, durum çubuğunda Çek istemi görünür | `ToolManager::activeId()=="pull"`; `statusBar()->currentMessage()` "Çek" ile başlar; kaplama `ViewportOverlay::handleCount()==1` | TL | 1 |
| 14 | **8** yazar, **Enter** → 60×40×8 katı oluşur. Geçmişe **Extrude** eklenir (`distance=8`). Hacim **19200 mm³** olur. Tek geri alma adımı oluşur | `modelResult().bodies.size()==1`; `massProperties` hacmi 19200 ± 0,5; Extrude `distance=="8"`; `undoStack()->count()` +1 | TL | 1 |
| 15 | Yazmak yerine oku sürükler ve bırakır. Hayalet önizleme görünür, 600 ms sonra commit olur | sürükleme sırasında ghost var (`viewer` test kancası `ghostVisible()`); bırakıştan 1 s sonra Extrude var ve mesafe sürüklemeyle orantılı (>0) | TL | 1 |
| 16 | Az önce yazılan **8** değerini parametre yapar: Çek sonrası **Ctrl+G** → "Extrude1_distance" önerilir. Ad **T** verilip "Uygulamada göster" işaretlenir → Gruplar'da T, Extrude `distance=T` | `paramRow(w,"T")>=0`; `exposed`; Extrude `distance=="T"` | TL | 1 |
| 17 | Katının **üst yüzüne** tıklayıp (Çek açıkken) **2** yazar, Enter → kalınlık 10 olur. Mesafe T'ye bağlı olduğu için **T = 10** güncellenir, durum çubuğu bunu söyler | `facePx("Extrude1.end")` tık; `type("2")`; Return → `pexpr("T")=="10"`; mesaj "parametre güncellendi" içerir | TL | 1 |
| 18 | Türetilmiş bir parametreye bağlı yüzü çeker (ör. bracket'te `Lbore = Hb + T - 4` ile sürülen derinlik) → işlem reddedilir, açıklayıcı mesaj çıkar, belge değişmez | `toJson(doc)` öncesi == sonrası; `currentMessage()` "türetilmiş" içerir | TL | 1 |
| 19 | Bir **kenara** tıklar (düz tık, Ctrl yok), **3** yazar, Enter → yeni **Fillet** (r=3) oluşur | `pickAt` ile kenar; Fillet `refs.size()==1`, `radius=="3"` | TL | 1 |
| 20 | Ctrl ile 4 dikey kenarı seçer, Seçenekler'de **Pah (Chamfer)** seçer, **1** yazar → tek **Chamfer** özelliği, 4 kenar | Chamfer `refs.size()==4`, `distance=="1"`; Seçenekler panelinde `edge` radyo değeri "chamfer" | TL + SH (panel) | 1 |
| 21 | Fillet yüzüne tıklar, **5** yazar → aynı Fillet'in yarıçapı 5 olur (yeni özellik eklenmez) | özellik sayısı değişmez; `Fillet1.radius=="5"` | TL | 1 |
| 22 | Yeni sketch: katının üst yüzünü seçer, **K** → sketch o yüzde açılır (`OnFace`). Ø10 çember çizer, **D**, P, bölgeye tıklar, **-5** yazar → kesik (cut) cep oluşur (Otomatik birleştirme: negatif = Kes) | sketch `plane.kind==OnFace`, `faceRef=="Extrude1.end"`; yeni Extrude `op=="cut"`, `direction=="reverse"`; hacim azaldı | TL + SH | 1 |
| 23 | Seçenekler'de **Birleştirme yok (No Merge)** seçip bölge çeker → ikinci gövde oluşur. Yapı sekmesinde iki gövde görünür | Extrude `op=="new"`; `bodiesTree()->topLevelItemCount()==2` (ya da Yapı ağacının Gövdeler düğümünde 2 çocuk) | TL + SH | 1 |
| 24 | Revolve: XZ'de bir dikdörtgen ve bir **yapı çizgisi** çizer, D, P, bölgeye tıklar, **Alt+tık** ile yapı çizgisini seçer → dönme tutamacı çıkar. **360** yazar → **Revolve** (axis = çizgi id) | Revolve özelliği, `axis` = yapı çizgisi entity id, `angle=="360"`; ToolGuideBar'da "Döndür" kılavuzu etkin | TL | 1 |
| 25 | Çek sırasında **Tab** (kutu kapalıyken) → araç kılavuzları sırayla etkinleşir (Seç → Yön → Döndür → …) | `ToolGuideBar` checked düğmesinin sırası | TL | 1 |
| 26 | Orta düğme sürükle = çevir, Shift+orta = kaydır, Ctrl+orta yukarı/aşağı = yakınlaştır, tekerlek = yakınlaştır. **Sol sürükle boşlukta modeli döndürmez** | her hareket sonrası kamera parametrelerindeki değişim (`viewer->property("cameraAz"/"halfH"/"target")` test kancası); sol sürükle sonrası kamera aynı | TL | 1 |
| 27 | **H** → ana (izometrik) görünüm ve sığdır. **Z** ve **Alt+Z** → sığdır. **←** → önceki görünüm, **→** → sonraki | kamera durumları; ← sonrası H öncesi kameraya eşit | SH (+TL kamera API) | 1 |
| 28 | **X** → kesit görünümü açılır (eski K davranışı). Kesit modu düğmesi seçilidir. **D** → kesit kapanır | `sectionControls()->sectionOn()`; D sonrası false; `K` artık kesit açmaz | SH | 1 |
| 29 | **F**'ye basınca **Doldur** etkinleşir (artık Sığdır değil) | `ToolManager::activeId()=="fill"`; kamera değişmedi | SH | 1 |
| 30 | Sağ tık ile bir kenarda bağlam menüsü açılır. "Bu kenara dik düzlem" seçilince datum düzlemi oluşur | `customContextMenuRequested` → `popupMenu("edgeContextMenu")` → `actPlaneNormal` tetikle → Plane özelliği +1 | TL (v2 güncellemesi) | 1 |
| 31 | Datum düzlemine tıklayıp **K** → o düzlemde sketch (`OnPlane`) açılır | `plane.kind==OnPlane`, `planeRef==Plane1` | SH | 1 |
| 32 | Bir gövdeyi seçer: **Özellikler** sekmesinde Ad, Hacim, Oluşturan özellik görünür. Ad'ı "Govde" yapınca gövde yeniden adlandırılır | PropertiesPanel `QTreeWidget` satırları; ad düzenleyicisine yaz → `bodyNames()` "Govde" içerir | SH | 1 |
| 33 | Fillet yüzünü seçer: Özellikler'de **Yarıçap** düzenlenebilir. **R** parametresi seçicisinden seçilince Fillet `radius=R` olur | ExprEdit `paramPick` → "R = …" → `Fillet.radius=="R"` | SH | 1 |
| 34 | **Gruplar** sekmesinde T'nin değerini 12 yapar → model güncellenir (imalat denetimi çalışır) ve yüz adları korunur | `editParamCell(T, İfade, "12")` → `Extrude1.end` hâlâ `modelResult().faces` içinde; hacim değişti | SH | 1 |
| 35 | **Geçmiş** sekmesinde Extrude1'e sağ tık → "Buraya kadar geri sar" çalışır. Çift tık → klasik diyalog açılır | `rollbackPublic` eşdeğeri sonuç; `featureDialog()!=nullptr` | SH | 1 |
| 36 | **Ctrl+Z** son Çek'i geri alır, **Ctrl+Y** yineler | özellik sayısı −1/+1; `actRedo` Ctrl+Y kısayoluna sahip | SH | 1 |
| 37 | Tek harfli kısayollar metin girerken tetiklenmez: Gruplar tablosunda hücreye "P" yazmak Çek aracını açmaz. DimensionBox'ta "D" yazmak 3B moduna geçmez | hücre düzenleyicisine `keyClicks("PD")` → `activeId()` değişmedi, `inSketchMode()` değişmedi | SH | 1 |
| 38 | Desteklenmeyen SpaceClaim kısayolu (B, Ctrl+C) → durum çubuğunda "PersistCAD'de yok" açıklaması çıkar, hata olmaz | `currentMessage()` "yok" içerir | SH | 1 |
| 39 | Seçim filtresi "Kenar" iken yüze tıklamak yüz seçmez | filtre combo = Kenar; `vclick(facePx)` → SelectionModel boş | TL + SH | 1 |
| 40 | Durum çubuğu: seçim listesi "1 yüz: Extrude1.end", Hata/Uyarı sayıları, Yenileme ms ve Hacim görünür | `selectionList` etiketi metni; mevcut etiketler korunuyor | SH | 1 |
| 41 | Uygulama Olarak Kaydet (Dosya menüsü ve Ctrl+Shift+A) eskisi gibi çalışır. Çek sırasında oluşturulan T parametresi uygulamada giriş olarak görünür | mevcut `gui_harness_v6` Save-as-App akışı + T `exposed` | SH (v6 B) | 1 |
| 42 | Tüm eski düzenekler (`gui_harness`, v2–v6, qa) ve birim testleri geçer | `ctest --output-on-failure` | A+B | 1 |
| 43 | **M** ile bir datum düzlemini seçip normal okunu sürükler veya **15** yazar → Plane `offset=15` | Plane `offset=="15"` | TL | 2 |
| 44 | Taşı + **Desen oluştur** ile bir deliğin (Hole) yüzünü X oku boyunca **20** taşır → Pattern (linear, count 2, spacing 20) | Pattern `source==Hole1`, `spacing=="20"` | TL | 2 |
| 45 | **F** (Doldur) ile fillet yüzüne tıklar, Enter → o kenar fillet'ten çıkar (tek kenarsa Fillet silinir) | `Fillet.refs` −1 ya da özellik silindi | TL | 2 |
| 46 | **I** (Birleştir): hedef gövdeye, sonra kesici gövdeye tıklar, mini araç çubuğundan "Çıkar" → Combine (cut) | Combine `op=="cut"`, `target`, `tools` | TL | 2 |
| 47 | Çek + **U** (Kadar) ile kesiği bir yüze kadar çeker → mesafe hedefe olan uzaklığa eşit olur | Extrude `distance` == hesaplanan uzaklık (±1e-6) | TL | 2 |
| 48 | Sketch'te **T** (Kırp) ile çizgi parçasına tıklar → kesişimler arasındaki parça silinir | eleman sayısı ve uç noktaları | TL | 2 |
| 49 | Bir sketch'in **iki bölgesini** farklı mesafelerle çeker (8 ve 15) → iki Extrude, her biri kendi bölgesiyle | Extrude `regions` farklı; hacim = A1·8 + A2·15 | TL | 3 (CR-1) |
| 50 | Katının **yan yüzünü** çeker **5** → yüzden extrude (join) | Extrude `face=="Extrude1.side.e3"` | TL | 3 (CR-2) |
| 51 | Bir gövdeyi **M** ile X yönünde 30 taşır | Transform özelliği, `tx=="30"` | TL | 3 (CR-3) |

## Çalıştırma
```bash
cmake -S . -B build -G Ninja && ninja -C build -j1 pc_gui_harness_sc_shell pc_gui_harness_sc_tools
xvfb-run -a -s "-screen 0 1600x1000x24" build/bin/pc_gui_harness_sc_shell
xvfb-run -a -s "-screen 0 1600x1000x24" build/bin/pc_gui_harness_sc_tools
ctest --test-dir build --output-on-failure
```
Her düzenek, faz 1 maddeleri geçtiğinde `docs/spaceclaim_ux/shots/` altına ekran görüntüleri yazar (`--shots` bayrağıyla):
`ac04_rect_dims.png`, `ac13_pull_armed.png`, `ac14_box.png`, `ac24_revolve.png`, `ac32_properties.png`.

## Entegrasyon kapsamı (birleşik sürüm, 15 Kasım 2026)
* **E2E** = `tests/gui/gui_harness_sc_e2e.cpp`: gerçek `MainWindow` (WP-A kabuğu + WP-B araçları) ile ilk 10 dakika akışı
  (yeni belge → K → R 60 Tab 40 → D → P 8 → Ctrl+G T → Gruplar → Ctrl+Z/Y → üst yüz +2 → kenar 3 (Fillet) → fillet yüzü 5 →
  yüzde sketch Ø10 + Ctrl+G → Çek "Kes" → Delik → Taşı + Desen → H/←/→ → X/D → kaydet/aç → Uygulama olarak kaydet →
  `--validate-only` / `--set T=20` → XZ düzlem seçimi + yapı çizgisi + Alt+tık → Revolve). `--shots` →
  `shots/final_*.png`. Kapsadığı maddeler: 3, 4, 5, 8, 12, 13, 14, 16, 17, 19, 21, 22, 24, 26, 27, 28, 34, 36, 41, 44.
* SH (`sc_shell`, gerçek pencere): 1–3, 12, 20 (panel), 22, 23, 27–29, 31–41. TL (`sc_tools`, test host'u): 4–26, 39, 43, 45,
  46, 48. Madde 30: `gui_harness_v2` (sağ tık kenar menüsü). Madde 42: tüm `ctest`.
* Hiçbir düzenekte olmayan: **47** (Çek + Kadar/U, 2. faz, araçta devre dışı), **49–51** (model CR-1/2/3, 3. faz).
* Yalnızca test host'unda (gerçek pencerede değil) sınananlar: 6, 7, 9, 10, 11, 15, 18, 20 (pah), 23 (Çek ile), 25, 43, 45,
  46, 48.

