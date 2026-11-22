# WP-B "tools" — teslim notları (SC-UX Faz 1 + Faz 2'nin bir kısmı)

Durum (15 Kasım 2026, /home/claude/wpB): temiz `cmake -G Ninja` + tam derleme, Xvfb altında **ctest 191/191 geçti**
(`gui_harness_sc_tools`: 101 kontrol, 0 hata). `persistcad --selftest` geçti. `ToolApi.h` değiştirilmedi (SC-UX ToolApi 1.0).

## 1. Sahip olunan dosyalar (değişen / yeni) — birleştirmede yalnızca bunları kopyalayın

Değişen (mevcut, WP-B sahibi):
* `src/designer/SketchEditor.h`, `src/designer/SketchEditor.cpp` — kaplama (overlay) kipi, afin düzlem→ekran dönüşümü,
  anında ölçü alanları, Kırp aracı (`Tool::Trim` enum'un SONUNA eklendi), Ctrl+→/← ızgara taşıma, test kancaları.
* `src/viewer/pc/ShapeViewer.h` (**yalnızca ekleme**), `src/viewer/ShapeViewer.cpp`, `src/viewer/STATUS.md` (SC-UX bölümü eklendi).
* `src/designer/tools/ToolManager.cpp` — P0 stub'ının yerine gerçek ToolManager (dosya adı aynı).
* `tests/gui/gui_harness_sc_tools.cpp` — yer tutucunun yerine gerçek TL düzeneği.
* `tests/gui/gui_harness_v2.cpp` — kenar menüsü yardımcıları navigasyon şemasına duyarlı (aşağıda §3).

Yeni (`src/designer/tools/`, glob ile derlenir, CMake değişikliği yok):
`ToolBase.h/.cpp` (WP-B içi taban sınıf), `SelectTool.h/.cpp`, `PullTool.h/.cpp`, `PullMapping.h/.cpp`, `MoveTool.h/.cpp`,
`FillTool.h/.cpp`, `CombineTool.h/.cpp`, `PlaneTool.h/.cpp`, `ShellTool.h/.cpp`, `SketchToolAdapter.h/.cpp`,
`DimensionBox.h/.cpp`, `ViewportOverlay.h/.cpp`, `ToolGuideBar.h/.cpp`, `MiniToolbar.h/.cpp`,
`SketchRegions.h/.cpp` (`appendSketchRegionFaces` burada + yardımcılar), `TrimLogic.h/.cpp`.

Yeni çıktı: `docs/spaceclaim_ux/shots/B_*.png` ve ACCEPTANCE adlarıyla `ac04_rect_dims.png`, `ac13_pull_armed.png`, `ac14_box.png`, `ac24_revolve.png` (`pc_gui_harness_sc_tools --shots`).

Dokunulmayan: `ToolApi.h/.cpp`, `MainWindow*`, `main.cpp`, CMake dosyaları, `harness_robust.h`, `sc_stub_host.h`, model/sketch/core.

## 2. Ne yapıldı (kısa)
* **Viewer** (eklemeli API, varsayılan `Legacy` → runner değişmez): kuaterniyon kamera; `setNavigationScheme(SpaceClaim)`:
  orta sürükle = çevir, Shift+orta = kaydır, Ctrl+orta = yakınlaştır, tekerlek = imlece yakınlaştır, sol sürükle =
  `setLeftDragMode` (varsayılan hiçbir şey), düz tık 6 px içindeki kenarı (örtülme denetimli) yüze tercih eder,
  `picked(name, kind, mods, clickCount, pos)` + `doubleClicked`/`tripleClicked`, Shift+orta çift tık = yüze bak,
  hover'da yüz VE kenar önvurgusu (`hovered`); `worldToScreen`, `screenRay`, `planeToScreen`, `pixelsPerMm`, `setPlanView`,
  `setViewToFace`, `setHomeView`, `fitToBox`, `previousView/nextView` (20), `cameraChanged`, `pickAt(px,kinds,&kind)`,
  `setPreselection`, `faceCentroid`; test özellikleri `cameraAz/cameraEl/halfH/target/ghostVisible`. `sketch:*:r*` adlı yüzler
  varsayılan olarak açık mavi + yarı saydam. Sketch ızgarası yakınlaştırmaya uyar (eğik bakışta da görünümü kaplar).
* **SketchEditor**: `attachToViewer(viewer)` + `setPlaneFrame(o,u,v)` → görünümün şeffaf çocuğu; orta düğme/tekerlek viewer'a
  iletilir, eğik bakışta çizim doğru düzleme düşer (düzenekte hata < 1e-4 mm); kenardan bakışta giriş kapanır ("… V.").
  `planView()` (V). Bağlanmadığında eski bağımsız 2B mod (bugünkü MainWindow, qa/v6 düzenekleri değişmeden geçer).
  Çizgi/Dikdörtgen/Çember/Yay/Nokta için **DimensionBox** alanları: rakam/./-/( veya Space açar, Tab kilitler ve sonraki alana
  geçer (Shift+Tab geri), Enter oluşturur, Esc adım adım geri alır, ifadeler + tamamlayıcı, geçersiz ifade kırmızı ve Enter
  reddedilir, Ctrl+G / "P" → parametre (sürücü ölçüye bağlanır), Shift+↑↓/→← adım. Yazılan (kilitli) alanlar sürücü kısıt olur
  (`Length`, `Diameter`, `Radius`, `DistanceX/Y`); dikdörtgen ilk köşesi yakalanan noktayı (orijin) yeniden kullanır → 60 Tab 40
  Enter = DOF 0. "Sketch kapanınca parametre kaybı" düzeltmesi korunur (TL düzeneği + v6 geçer).
* **ToolManager**: tüm eylemler (SPEC §1.3 metinleri, `tool.<id>`, kısayolsuz), araç nesneleri, Esc zinciri
  (kutu → sürükleme/çizim aşaması → araç → Seç → seçimi temizle; sketch modundan çıkmaz), tuş yönlendirme (ShortcutOverride +
  KeyPress), viewer fare filtresi (tutamaç sürükleme viewer'dan önce), 3B'de sketch aracı seçilince `setDesignMode(Sketch)`,
  kısıt/ölçü/Sil/Yapı çizgisi eylemleri SketchEditor'a bağlı, Tab ile kılavuz döngüsü, bağlam menüsüne "Yuvarlat/Pahla (Çek)".
* **Çek** (PullTool + PullMapping): bölge → Extrude (Otomatik/Ekle/Kes/Birleştirme yok/Kesişim; negatif = cut+reverse; İki
  yöne = symmetric), Alt+tık sketch çizgisi → Revolve (Döndür kılavuzu, dönme yayı tutamacı), `.end` → mesafe (Δ, ikinci alan
  "Toplam"), `.start` → ofset+mesafe, fillet/chamfer yüzü → yarıçap/mesafe, delik duvarı/tabanı → çap/derinlik, Revolve
  yüzleri → açı, silindirik yan yüz + Çap/Yarıçap ölçüsü → sketch ölçüsü, kenar(lar) → yeni Fillet/Chamfer. Parametre kuralı:
  düz sayılı parametre → `changeParameter` ("T = 10 (parametre güncellendi)"), türetilmiş → red ("… türetilmiş bir ifadeye
  bağlı (Lbore = Hb + T - 4); Gruplar panelinden düzenleyin."). Sarı ok (60 px), ok başına tık yön çevirir, sürüklemede hayalet
  (≤ 12 Hz, prizma/dönme) ve değer etiketi, bırakınca 600 ms sonra otomatik commit (rakam/Tab/Space ile kutuya geçilir),
  "Bırakınca hemen uygula" seçeneği, Ctrl+G (kutu açıkken ya da son Çek'ten sonra) → `<fid>_<prop>` önerili parametre.
* **Taşı**: datum düzlemi (`offset`, bağlama kuralı), sketch düzlemi (`offsetExpr`), "Desen oluştur" + Extrude/Delik yüzü →
  X/Y/Z oklarıyla doğrusal Pattern (count 2, spacing); gövde → CR-3 mesajı. **Doldur**: fillet/chamfer kenarı çıkarma (boşalırsa
  sil), tek konumlu delik silme, kesik Extrude/Revolve bastırma (ya da "Kaldır"), Shell yüzünü kapatma. **Birleştir**: Hedef/
  Kesici kılavuzları, mini araç çubuğunda Çıkar/Kesişim/Birleştir, Enter = seçili işlem. **Düzlem**: yüz → faceOffset, 1 kenar →
  edgeNormal (t 0.5), 2 kenar → twoEdges; sonra Taşı. **Kabuk**: yüz(ler) + kalınlık + Enter. **Kırp**: TrimLogic (çizgi bölme,
  çember → yay, yay kısaltma/bölme; yeni uçlara PointOnEntity).
* **ToolGuideBar** (görünümün sağında dikey şerit, `toolGuideBar`, düğmeler `guide.<key>`), **MiniToolbar** (`miniToolbar`;
  sketch: Seç / 3B'ye Dön / Yeni sketch düzlemi / Izgarayı taşı / Plan görünümü; 3B araçlar: `onMiniToolbar` seçenekleri ikonlu,
  son tıklamanın yanında, imleç uzaklaşınca saydamlaşır), **ViewportOverlay** (`viewportOverlay`, `handleCount` test kancası).

## 3. WP-A için entegrasyon notları (sıra önemli)
1. `MainWindow : IDesignHost`; `tm_ = new ToolManager(this, this)`; `tm_->installKeyRouting(viewer_)`,
   `tm_->installKeyRouting(sketchEd_)`; `tm_->createMiniToolbar(viewer_)`, `tm_->createToolGuideBar(viewer_)`.
   Başvuru uygulaması: `tests/gui/gui_harness_sc_tools.cpp` içindeki `ScHost` (≈250 satır, tüm akışları çalıştırıyor).
2. `viewer_->setNavigationScheme(NavigationScheme::SpaceClaim)`. `picked(...)` → `PickEvent{SelItem{kind,name}, mods,
   LeftButton, clickCount, pos}` → `tm_->handlePick` (host'un özel durumları önce). `hovered` → `tm_->handleHover`.
   Ad→tür: `sketch:<sid>:r<k>` SketchRegion, `:e<id>` SketchCurve, kind PickEdge → Edge, PickDatum → Datum, diğer → Face
   (`ToolBase::normalize` aynısını yapar). SpaceClaim şemasında düz tık da `edgePicked` yayar: `onEdgePicked` **kenar menüsünü
   artık açmamalı**; menü yalnızca `customContextMenuRequested` (sağ tık). `gui_harness_v2` buna hazır (şemaya göre Ctrl+tık
   ya da tık + QContextMenuEvent kullanır).
3. Sketch kaplaması: `QStackedWidget` kalkar; `sketchEd_->attachToViewer(viewer_)` bir kez. Oturum açarken (`beginSketch`,
   `enterSketchEdit`) `setContext(&work_)`, `setSketch(...)`, **`setPlaneFrame(o,u,v)`** (çerçeve: `pc::model::sketchFrame`
   — yeni sketch için sketch'i içeren aday belgeyle), `show(); raise(); setFocus()`; isteğe bağlı `planView()`. Çıkışta `hide()`
   (ızgarayı viewer'dan editör kaldırır). Sketch modunda gövdeler `setTransparentFaces(..., 0.35)`. V = `sketchEditor()->planView()`.
   Ofset değişince (ExprEdit `sketchOffset` ya da Ctrl+→/← → `planeOffsetChanged`) çerçeveyi yeniden `setPlaneFrame` ile verin.
4. `designMode()` **yeni değeri döndürdükten sonra** `tm_->onDesignModeChanged(m)` çağrılmalı (sketch → `sketch.select`,
   3B → `select`, mini araç çubuğu yeniden kurulur). 3B'de L/R/C → ToolManager `setDesignMode(Sketch)` çağırır; host o anda sketch
   açmalı (seçili düzlemsel yüz / datum, boş belgede XY, değilse "Sketch düzlemi seç").
5. `updateViewer`: `appendSketchRegionFaces(doc_, model_, unconsumedSketches(doc_, rollback_), comp, faces)` (bölgeler yalnızca
   görüntü; imalat/dışa aktarmaya girmez). **İstek**: sketch kaplama kenarlarına yapı çizgileri de eklensin (bugün
   `addSketchOverlay` atlıyor) — ya da `appendSketchCurveEdges(...)` (SketchRegions.h) kullanın; Alt+tık ile dönme ekseni buna dayanır.
6. Her `applyDocument` (commit/undo/redo/aç/parametre) sonrası `tm_->onDocumentChanged()`.
7. `createParameterInteractive` sketch açıkken parametreyi `expressionContext()`'e (work_) de eklemeli (ScHost gibi);
   SketchEditor adaptörü sonra `reloadParams()` çağırır. Kutu/alan tamamlayıcıları `document()`/`work_` parametrelerinden beslenir.
8. `setGhost(TopoDS_Shape())` = `clearGhost()`. `promptChanged` → `statusBar()->showMessage` (araçlar ayrıca `showPrompt` da çağırır).
9. Seçim vurgusu WP-A'da; `setToolEmphasis` (çok bölgeli sketch, Birleştir hedef/kesici) farklı renkle çizilmeli.
10. Eski `sketchBar_/sketchBar2_` eylemleri kaldırılmalı (tek "Seç": `tool.select`); ribbon `tm_->editActions()/sketchActions()/
    constraintActions()/dimensionActions()/otherActions()`; kısayollar Shortcuts.cpp'den (F = Doldur; SketchEditor'ün bağımsız
    moddaki `F = fitView` tuşu yalnızca QAction yoksa çalışır). `gui_harness_qa` "Seç/Dikdörtgen/Uzunluk/Çap…" düğmelerini
    bu eylemlerle bulmalı (metinler aynı).
11. `tm_->populateContextMenu(menu, underCursor)` görünüm menüsünün sonunda çağrılsın. Ctrl+G ve Del için önce
    `tm_->createGroup()` / `tm_->deleteSelection()`; false dönerse WP-A davranışı.
12. MiniToolbar / ToolGuideBar / ViewportOverlay viewer'ın çocuklarıdır; SketchEditor `show()` sonrası bunlar kendini `raise()`
    eder (ChildAdded); WP-A başka kaplama eklerse aynı sırayı koruyun.

## 4. WP-A'ya istekler / sözleşme
* ToolApi değişikliği gerekmedi. `docs/CONTRACT_REQUESTS.md`'ye yeni istek yok.
* İstek A-1: yapı çizgileri 3B sketch kaplamasında seçilebilir olsun (§3 madde 5).
* İstek A-2: `createParameterInteractive` sketch sırasında `work_`'a da eklesin (§3 madde 7).
* İstek A-3: SpaceClaim şemasında `onEdgePicked` menü açmasın; sağ tık menüsü (§3 madde 2).
* İstek A-4: `Klasik ▾ > Düzlem...` için PlaneTool'un "Diyalogla oluştur" seçeneği şu an yalnızca bilgi satırı (yeni özellik
  diyaloğunu açacak bir host API'si yok; istenirse ToolApi 1.1'de `openNewFeatureDialog(FeatureType)`).

## 5. Yalnızca WP-A entegrasyonu eksik olduğu için başarısız olan testler
* Yok: bu kopyada 191/191 geçiyor. TL düzeneği kendi `ScHost`'unu kullanır; `gui_harness_sc_shell` WP-A'nın yer tutucusu.
  Entegrasyondan sonra ACCEPTANCE "SH + TL" maddeleri (12, 20, 22, 23, 39) için WP-A panel/menü kısımlarını eklemeli.

## 6. Bilinen eksikler
* Çizgi açısı 0/90 dışında yazılırsa yalnızca geometriyi belirler (`Angle` kısıtı iki çizgi ister); Yay süpürme açısı da kısıt olmaz.
* §5.3 "ölçü etiketine tek tık + yazma" satır içi düzenleme yok (çift tık → DimensionDialog duruyor).
* Çek: Kadar (U) ve Cetvel kılavuzları devre dışı (2. faz); "Yön" kılavuzu yalnızca bilgi (ok başına tık yönü çevirir);
  çok bölgeli sketch tüm bölgeleriyle çekilir (CR-1); düz yan yüz çekme CR-2 mesajı; `.start` yalnızca sketch'i paylaşılmayan,
  simetrik olmayan Extrude'da.
* Taşı: dönme yayları yalnızca görsel; gövde taşıma CR-3; sketch modunda eğri taşıma (2. faz) yok; tutamaç düzlemin
  çerçeve orijininde çizilir. Gizli kenar seçmek için görünüm döndürülmeli (düzenekte 4. dikey kenar programatik eklendi).
* Seçim: çift tık kenar döngüsü = kenarın ilk komşu yüzünün tüm kenarları; kutu seçimi, Ctrl+Shift+I, radyal menü (O), Ölç (E) yok.
* Doldur: desen (Pattern) yüzleri ve çok konumlu delik desteklenmez (mesaj verir); fillet kenar seçicisi `face:` ise kenar çıkarılamaz.
* Mini araç çubuğu 3B'de son tıklamanın sağ altında (30, 58) çıkar; seçenek paneli (WP-A) aynı ToolOption listesini çizer.
