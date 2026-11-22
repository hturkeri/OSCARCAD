# 2B çizimden 3B'ye — "2B çizim (eğri olarak)", Katmanlar, Görünüşü yerleştir (SC-2D-IMPORT)

SpaceClaim iş akışı ("Convert 2D Drawings to 3D Models in Minutes with SpaceClaim"): bir DXF / vektör PDF çizimi
3B sahneye **eğri** olarak açılır, **katmanlar** ile parçaları gösterilir/gizlenir, yan görünüşler dik düzlemlere
**katlanıp** asıl görünüşe oturtulur, kapalı konturlar **Doldur** (bölge) ve **Çek** ile katıya dönüşür.
Bu paket eğrileri, katmanları ve görünüş yerleştirmeyi sağlar; Fill / Pull / Up To v2'nin işidir (aşağıda mevcut
Fill+Pull ile doğrulandı).

## Kullanım
* **Dosya > İçe aktar > 2B çizim (eğri olarak)…** (`actImport2D`; ayrıca şerit **Görüntü > Katmanlar / 2B çizim**: 2B çizim, Katmanlar, Görünüş seç, Görünüşü yerleştir — Tasarım sekmesinde yer yok, SpaceClaim da katmanları Display sekmesinde tutar) veya `.dxf` /
  `.pdf` dosyasını pencereye **sürükle-bırak**. Seçili bir datum düzlemi varsa çizim o düzleme, yoksa XY'ye konur.
  Tek geri alma adımı ("2B çizim içe aktar: <dosya>").
  * Gerçek ölçek: DXF `$INSUNITS` (pc_drawing ingest mm'ye çevirir; negatif kapsam kaydırması geri alınır → DXF
    orijini korunur). PDF: ölçü oranları (`SheetLayout::scaleFromDims`, güven ≥ 0.5) → antet ölçeği → aksi halde
    kullanıcıya sorulur (QInputDialog, "parça mm / kağıt mm").
  * Katmanlar: DXF katman adları (GORUNUR, GIZLI, EKSEN, CERCEVE, ANTET …); PDF'de çizgi rolüne göre (Görünür,
    Gizli çizgiler, Eksen, Çerçeve/Antet, Tarama, Diğer #RRGGBB …). Ölçü çizgileri/okları (DIMENSION blokları,
    S4 rolü Dimension/Extension/Leader) ve tüm yazılar **"Ölçüler/Notlar"** katmanına gider: yapı (construction)
    eğrileri + 3B etiketler; geometri olarak kullanılmaz. Çerçeve/antet/tarama/sembol eğrileri de yapı eğrisidir
    (bölge oluşturmaz). Çizgi tipi → çizgi stili (HIDDEN/DASH → kesikli, CENTER → eksen, DOT/PHANTOM → noktalı);
    renk = çizim rengi (beyaz → koyu mürekkep, açık renkler koyulaştırılır).
  * Splinelar / elipsler / Bezier'ler çoklu çizgi, yay ve çemberler gerçek yay/çember; tarama ve görüntüler atlanır.
  * Raster / taranmış PDF reddedilir ("Teknik resimden içe aktar" sihirbazı önerilir).
* **Katmanlar** sekmesi (Yapı paneli: Yapı / Geçmiş / Seçim / Gruplar / **Katmanlar**, `layersTree`): her satırda
  görünürlük kutusu, renk örneği (çift tık = renk), çizgi stili, öğe sayısı. Sağ tık (`layersContextMenu`):
  Göster / Gizle / **Yalnız bunu göster** / Tüm katmanları göster / Renk… / Çizgi stili ▸ / Yeniden adlandır… /
  Yeni katman / **Seçimi bu katmana taşı** / Katmandaki öğeleri seç. Mevcut gövde ve sketch'ler **Katman0**'dadır.
  Gizli katman: eğrileri ve bölgeleri çizilmez, seçilemez; gövdelerinin yüzleri gizlenir (tıklama içinden geçer).
  Her katman değişikliği yeniden üretim yapmayan TEK geri alma adımıdır (`LayerCommand`). `.pcad`'de saklanır.
* **Görünüş seç** (`actViewSelect`, açılır/kapanır; Katmanlar sekmesindeki "Görünüş seç" / "Yerleştir..." düğmeleri; ayrıca içe aktarılmış bir eğriye sağ tık > Görünüş seç): bir
  eğriye tek tık, o görünüşün birbirine değen (sınır kutuları 0.5 mm içinde) tüm geometri eğrilerini seçer
  (görünür, gizli, eksen katmanları; Ölçüler/Notlar hariç). Ctrl+tık ekler.
* **Görünüşü yerleştir…** (`actPlaceView`, `import2dPlaceDialog`, kalıcı olmayan diyalog): seçili görünüşü bir
  kenarı etrafında döndürür (varsayılan 90°) ve kaynak noktayı hedef noktaya yakalar. Varsayılan: komşu (hizalı)
  görünüşe bakan kenar (Otomatik / Üst / Alt / Sol / Sağ), kaynak = katlama kenarının köşesi, hedef = komşu
  görünüşün karşı kenarındaki izdüşümü — yani yan görünüş asıl görünüşün kenarına dik olarak oturur. Kutular
  sürekli çizgi katmanlarından hesaplanır (eksen/gizli çizgi taşmaları kenarı kaydırmaz). Kaynak/Hedef "Seç" ile
  görünümde bir eğriye tıklanarak (tıklamaya en yakın uç / merkez noktası) veya "x; y; z" yazılarak değiştirilir.
  Önizleme = hayalet (bölge yüzleri + eğriler). Uygula = TEK geri alma adımı.
  Sonuç: taşınan eğriler (katman ve içe aktarma özellikleriyle) yeni sketch'lere konur; düzlem XY(+ofset) ya da
  gizli (`show=0`, `import2d=1`) bir **axisAngle datum düzlemidir** (aynı düzleme sonraki yerleştirmeler onu
  yeniden kullanır). Boşalan içe aktarılmış sketch silinir. Bir özelliğin (Extrude…) kullandığı eğriler taşınmaz.
* Eğriler sıradan sketch varlıkları olduğundan kapalı konturlar otomatik olarak bölge (`sketch:<id>:r<k>`) olur ve
  Çek (Pull) ile uzatılır — XY'de de, yerleştirilmiş dik düzlemde de (harness §6).

## Belge gösterimi ("ImportedCurves")
* Her içe aktarılan katman = bir `FeatureType::Sketch` (kısıtsız; id `Cizim<k>_<KATMAN>`, yerleştirilenler
  `<id>_y<k>`) ve props: `importedCurves=1`, `layer`, `source` (dosya adı), `importGroup` (`Cizim<k>`), Notlar
  sketch'inde `notes` = JSON `[{"u","v","h","r","t"}]`. Bu yüzden kalıcı (.pcad), geri alınabilir ve Fill/Pull,
  sketch düzenleyici (çift tık) ve tüm mevcut sketch araçlarıyla uyumludur.
* `Document::layers` (`pc::LayerTable`, `src/core/pc/Layers.h`): katman adı, renk, çizgi stili, görünürlük, üye
  sketch id'leri ve gövde adları; listede olmayan her şey "Katman0". JSON anahtarı `"layers"` (boşsa yazılmaz —
  eski dosyalar bayt-aynı; bozuk girdiler atlanır). Görünüm durumudur: geometri, hacim, imalat denetimi, STEP
  etkilenmez; Save-as-App katmanları siler.

## Sınırlar
* Görünüş yalnızca bir dünya eksenine paralel düzleme yerleştirilebilir (ortografik katlama için yeterli);
  rastgele eğik düzlem reddedilir.
* ~~T-birleşimli konturlar bölge vermez~~ — v2 birleşmesiyle çözüldü: bölgeler artık `pc::sketch::sketchRegions`
  (tolerant arrangement) ile oluşur; lbracket ön görünüşü (iki üst üste dikdörtgen) Doldur ile 3 bölge verir ve tek
  katı olarak çekilir (aşağıda "Entegrasyon").
* Yerleştirilen görünüşün ölçü/not eğrileri yerinde (XY) kalır; görünüş seç'e dahil değildir.
* Kutu (marquee) seçimi yok: görünüş seç, katmandaki öğeleri seç, Ctrl+tık kullanılır.
* Bir gövdenin katmanı gövde adına bağlıdır; gövde yeniden adlandırılırsa Katman0'a döner.
* Çizgi stilleri viewer'da CPU'da kesik çizgilere bölünür (desen ekran boyutuna göre, %25 zoom değişiminde
  yeniden üretilir); bölge sınırı kenarları sürekli çizgilerin altında kalır.

## Test
`tests/gui/gui_harness_import2d.cpp` (ctest `gui_harness_import2d`; `--shots` → `docs/import2d/shots/*.png`):
menü + dosya diyaloğu ile plate DXF içe aktarma (ölçek, katmanlar, notlar), katman kutusu (piksel + seçme
denetimi, geri al), bağlam menüsü (Yalnız bunu göster, renk, stil, yeniden adlandır), Görünüş seç (tek tık),
Görünüşü yerleştir (varsayılan katlama, nokta seçme, yazma, Uygula, geri al/yinele), dik düzlemdeki bölgeyi 60 mm
çekme (hacim 48000) ve üst görünüşü 20 mm çekme (hacim (6000-4π·4.5²)·20 = 114910.6), gövdeyi yeni katmana taşıma
ve gizleme (yüzler seçilemez), kaydet/yeniden aç (katmanlar, renk, gizlilik, eğriler, yerleştirme, notlar), içe
aktarmanın geri alınması, sürükle-bırak, vektör PDF (rol katmanları, gerçek ölçek).

Sonuç (17 Kas 2026, Xvfb, Release): `gui_harness_import2d` 93/93; tam `ctest` 338/338 geçti (3 devre dışı test hariç).

## Değişen / yeni dosyalar (TAM liste)
Yeni (doğrudan kopyalanır):
* `src/core/pc/Layers.h`, `src/core/Layers.cpp` — `pc::LayerTable` + JSON
* `src/designer/Import2D.h`, `src/designer/Import2D.cpp` — okuma (pc_drawing), belgeye ekleme, eğri kümeleri,
  katlama önerisi, eğri taşıma (`moveCurves`)
* `src/designer/Import2DUi.h`, `src/designer/Import2DUi.cpp` — `Import2DController`, `LayersPanel`,
  `PlaceViewDialog`, `LayerCommand` (sources.cmake glob: CMake değişikliği gerekmez)
* `tests/gui/gui_harness_import2d.cpp`
* `docs/import2d/IMPORT2D.md`, `docs/import2d/shots/*.png` (01_imported_top … 14_pdf_import)

Paylaşılan dosyalarda küçük, `// SC-2D-IMPORT` işaretli kancalar (dikkatli birleştirme; diff aşağıda):
* `src/designer/MainWindow.h` — ileri bildirimler, `friend`, `import2d_` üyesi, `import2d()` erişimcisi
* `src/designer/MainWindow.cpp` — include, kurucuda `new Import2DController(this)`, `refreshAll` → `refresh()`,
  `sketchShownIn3D` → gizli katman, `updateViewer` → not etiketleri + `decorateViewer()`, Save-as-App katmanları siler
* `src/designer/MainWindowV2.cpp` — `applyBodyAppearance` (gizli katman gövdeleri), `viewerContextMenu` (içe
  aktarılmış eğri menüsü)
* `src/designer/MainWindowOps.cpp` — `onViewerPicked` başında `handlePick` (görünüş seç / nokta seçme)
* `src/designer/MainWindowShell.cpp` — `highlightSelection`: seçili 3B sketch eğrileri de vurgulanır
* `src/core/pc/Document.h`, `src/core/Document.cpp`, `src/core/CMakeLists.txt` — `Document::layers`, JSON `"layers"`
* `src/viewer/pc/ShapeViewer.h`, `src/viewer/ShapeViewer.cpp` — ek API `EdgeStyle`, `setEdgeStyles`,
  `styledEdgeCount` (kenar rengi + kesikli / eksen / noktalı desen)
* `tests/gui/CMakeLists.txt` — dosya sonuna bir `pc_add_gui_harness(gui_harness_import2d …)`
* `tests/gui/gui_harness_sc_shell.cpp` — Yapı sekmeleri beklentisi artık "… Gruplar · Katmanlar"
* `src/designer/STATUS.md` — "SC-2D-IMPORT" bölümü eklendi (sona)

## Paylaşılan kanca diff'leri (taban: /home/claude/PersistCAD)
```diff
--- a/src/designer/MainWindow.cpp	2026-11-16 22:55:23.649629786 +0300
+++ b/src/designer/MainWindow.cpp	2026-11-17 08:44:08.621589324 +0300
@@ -1,4 +1,5 @@
 #include "MainWindow.h"
+#include "Import2DUi.h"   // SC-2D-IMPORT
 #include "ParamSelectUi.h"
 #include <BRepBuilderAPI_MakeEdge.hxx>
 #include <BRep_Builder.hxx>
@@ -138,6 +139,7 @@
   buildStatusBar();
   wireSelection();
   createParamSelect();
+  import2d_ = new Import2DController(this);   // SC-2D-IMPORT
 
   // mouse: SpaceClaim navigation scheme (WP-B viewer API; the runner keeps the viewer's Legacy default)
   navFallback_ = !detectNavigationScheme();
@@ -303,10 +305,12 @@
   schedulePanelsRefresh();
   nsRebuildPanel();
   updateVisStatus();
+  if (import2d_) import2d_->refresh();   // SC-2D-IMPORT: Katmanlar
 }
 
 // ------------------------------------------------------------------------------ v6: sketch overlay in 3D
 bool MainWindow::sketchShownIn3D(const std::string& id) const {
+  if (import2d_ && import2d_->sketchHidden(id)) return false;   // SC-2D-IMPORT: hidden layer
   auto it = sketchShown_.find(id);
   if (it != sketchShown_.end()) return it->second;
   // default (SolidWorks-like): sketches not consumed by a solid feature are shown, consumed ones hidden
@@ -439,6 +443,7 @@
   {
     std::vector<viewer::Label3D> labels = sketchLabels_;
     appendNsLabels(labels);
+    if (import2d_) import2d_->appendLabels(labels);   // SC-2D-IMPORT: drawing notes
     viewer_->setLabels(labels);
   }
   sketchOverlayEdges_ = nSketch;
@@ -448,6 +453,7 @@
     if (!model_.shape.IsNull()) bb.Add(comp, model_.shape);
     viewer_->setModel(comp, faces, edges, keepCamera);
   }
+  if (import2d_) import2d_->decorateViewer();   // SC-2D-IMPORT: layer colours / line styles
   applyBodyAppearance();
   applyDatumPlanes();
   applySketchTransparency();   // see-through solids while sketching, opaque otherwise
@@ -805,6 +811,7 @@
   if (featDlg_) featDlg_->reject();
   Document copy = doc_;
   copy.view = ViewState();   // Gizle / Göster is the designer's view state: the app always shows the whole part
+  copy.layers = LayerTable();   // SC-2D-IMPORT: layers are view state too
   appui::runSaveAsApp(copy, QCoreApplication::applicationDirPath() +
 #ifdef Q_OS_WIN
                                        "/persistcad_runner.exe"
--- a/src/designer/MainWindow.h	2026-11-16 23:32:14.983141023 +0300
+++ b/src/designer/MainWindow.h	2026-11-17 08:44:08.602027011 +0300
@@ -42,6 +42,8 @@
 class NamedSelectionPanel;
 class NsCommand;
 class VisCommand;
+class Import2DController;   // SC-2D-IMPORT
+class LayerCommand;         // SC-2D-IMPORT
 
 // SC-UX (WP-A): the designer main window is the SpaceClaim-like shell (ribbon, Structure / Options / Properties
 // panels, status bar widgets, design modes K/X/D) and implements IDesignHost for WP-B's tools (ToolApi.h).
@@ -147,6 +149,7 @@
   int leftDragMode() const { return leftDrag_; }
   // "Seçimden parametre": badge / quick popup / menus / Properties rows for the current selection
   ParamSelectController* paramSelect() const { return paramSel_; }
+  Import2DController* import2d() const { return import2d_; }   // SC-2D-IMPORT: 2B çizim, Katmanlar, görünüş yerleştir
 
   // --- Named selections / Adlandırılmış seçimler (MainWindowNs.cpp, docs/named_selections/MODEL.md) ---
   // SpaceClaim "Groups > Named Selections": Ctrl+G with faces / edges / bodies selected in 3D (sketch / dimension
@@ -240,6 +243,9 @@
   friend class DocCommand;
   friend class NsCommand;
   friend class VisCommand;
+  friend class Import2DController;   // SC-2D-IMPORT
+  friend class LayerCommand;         // SC-2D-IMPORT
+  Import2DController* import2d_ = nullptr;   // SC-2D-IMPORT
   // Gizle / Göster (MainWindowVis.cpp)
   void visUpdate();                       // resolve doc_.view against model_ (after regeneration / view change)
   void updateVisStatus();
--- a/src/designer/MainWindowV2.cpp	2026-11-16 22:55:23.645615847 +0300
+++ b/src/designer/MainWindowV2.cpp	2026-11-17 08:44:08.633579075 +0300
@@ -2,6 +2,7 @@
 #include <pc/DatumSizing.h>
 #include "Icons.h"
 #include "MainWindow.h"
+#include "Import2DUi.h"   // SC-2D-IMPORT
 #include "ParamSelectUi.h"
 #include "StructurePanel.h"
 #include "pc/NamedSelections.h"
@@ -62,6 +63,7 @@
     }
   }
   for (const auto& f : hiddenFaces_) hidden << QString::fromStdString(f);   // Gizle / Göster (MainWindowVis.cpp)
+  if (import2d_) import2d_->appendHiddenFaces(hidden);   // SC-2D-IMPORT: bodies of hidden layers
   // named selections with "Vurguyu göster": their faces keep the selection colour (view state)
   for (const auto& r : nsMgr_.resolutions()) {
     if (!nsTint_.count(r.id)) continue;
@@ -427,6 +429,7 @@
   if (sketching_ || planePick_) return;
   const QPoint g = viewer_->mapToGlobal(pos);
   const QString e = viewer_->edgeAt(pos);
+  if (!e.isEmpty() && import2d_ && import2d_->showCurveMenu(e, g)) return;   // SC-2D-IMPORT: imported curve menu
   if (!e.isEmpty()) {
     selectedEdge_ = e;
     if (!selection_->contains(SelItem{SelItem::Kind::Edge, e}) && selection_->accepts(SelItem::Kind::Edge)) selection_->set({SelItem{SelItem::Kind::Edge, e}});
--- a/src/designer/MainWindowOps.cpp	2026-11-15 13:38:33.020658700 +0300
+++ b/src/designer/MainWindowOps.cpp	2026-11-17 08:44:08.638722944 +0300
@@ -1,6 +1,7 @@
 // MainWindow: change pipeline, features, sketch mode, parameters.
 #include "Icons.h"
 #include "MainWindow.h"
+#include "Import2DUi.h"   // SC-2D-IMPORT
 #include "MfgDialogs.h"
 #include "Ribbon.h"
 #include "pc/Expression.h"
@@ -173,6 +174,7 @@
 }
 
 void MainWindow::onViewerPicked(const QString& name, int kind, Qt::KeyboardModifiers mods, int clicks, const QPoint& pos) {
+  if (import2d_ && import2d_->handlePick(name, kind, mods, clicks, pos)) return;   // SC-2D-IMPORT: view select / anchor pick
   if (clicks >= 2) {
     // double / triple click: host states never use them; the tool gets them (edge loop, body), else nothing
     if (planePick_ || pick_ != Pick::None || featDlg_ || !tools_) return;
--- a/src/designer/MainWindowShell.cpp	2026-11-16 23:32:14.987163133 +0300
+++ b/src/designer/MainWindowShell.cpp	2026-11-17 08:44:08.641657344 +0300
@@ -915,6 +915,7 @@
   viewer_->clearHighlights();
   QStringList faces = facesOfSelection();
   QStringList edges = selection_->namesOfKind(SelItem::Kind::Edge);
+  edges << selection_->namesOfKind(SelItem::Kind::SketchCurve);   // SC-2D-IMPORT: selected 3D sketch curves
   // a named selection clicked in the Gruplar panel highlights in its own colour (until the selection changes)
   QColor faceCol(70, 140, 235), edgeCol(255, 140, 0);
   if (!nsActive_.isEmpty() && selection_->items() == nsActiveItems_)
--- a/src/core/Document.cpp	2026-11-16 22:52:03.326149242 +0300
+++ b/src/core/Document.cpp	2026-11-17 08:34:11.341580032 +0300
@@ -393,6 +393,7 @@
     if (!d.view.transparent.members.empty()) jv["transparent"] = toJson(d.view.transparent)["members"];
     j["view"] = jv;
   }
+  if (!d.layers.empty()) j["layers"] = toJson(d.layers);   // SC-2D-IMPORT
   return j;
 }
 
@@ -450,6 +451,7 @@
         if (jv.contains("transparent")) d.view.transparent = namedSelectionFromJson(json{{"name", "Saydam"}, {"members", jv["transparent"]}});
       } catch (...) { d.view = ViewState(); }
     }
+    if (j.contains("layers")) d.layers = layerTableFromJson(j["layers"]);   // SC-2D-IMPORT (tolerant)
     d.view.hidden.name = "Gizli";
     d.view.hidden.id = "view.hidden";
     d.view.transparent.name = "Saydam";
--- a/src/core/pc/Document.h	2026-11-16 22:52:08.619602884 +0300
+++ b/src/core/pc/Document.h	2026-11-17 08:34:11.337554269 +0300
@@ -2,6 +2,7 @@
 // PersistCAD core — the parametric part document + JSON (de)serialization.
 // File extension: .pcad (JSON).  Schema version: "persistcad/1".
 #include "pc/Feature.h"
+#include "pc/Layers.h"   // SC-2D-IMPORT
 #include "pc/MfgSettings.h"
 #include "pc/NamedSelection.h"
 #include "pc/Parameter.h"
@@ -32,6 +33,7 @@
   MfgSettings mfg;
   std::vector<NamedSelection> namedSelections;   // user-named face/edge/body sets (pc/NamedSelection.h)
   ViewState view;                     // hidden / transparent faces and bodies (view state, see above)
+  LayerTable layers;                  // SC-2D-IMPORT: layers (pc/Layers.h), view state; key "layers" omitted when empty
 
   Feature* feature(const std::string& id);
   const Feature* feature(const std::string& id) const;
--- a/src/core/CMakeLists.txt	2026-11-14 00:36:12.000000000 +0300
+++ b/src/core/CMakeLists.txt	2026-11-17 08:34:11.336847737 +0300
@@ -1,3 +1,4 @@
-add_library(pc_core STATIC Expression.cpp Parameter.cpp SketchData.cpp Document.cpp)
+add_library(pc_core STATIC Expression.cpp Parameter.cpp SketchData.cpp Document.cpp
+            Layers.cpp)   # SC-2D-IMPORT
 target_include_directories(pc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
 target_link_libraries(pc_core PUBLIC nlohmann_json::nlohmann_json)
--- a/src/viewer/pc/ShapeViewer.h	2026-11-15 13:04:02.859426178 +0300
+++ b/src/viewer/pc/ShapeViewer.h	2026-11-17 08:40:02.045030182 +0300
@@ -36,6 +36,12 @@
   QString text;
 };
 
+// SC-2D-IMPORT (additive): display style of a named edge (imported drawing layers: colour + line pattern).
+struct EdgeStyle {
+  QColor color;
+  int pattern = 0;   // 0 solid, 1 dashed, 2 center (long-short), 3 dotted
+};
+
 class ShapeViewer : public QOpenGLWidget {
   Q_OBJECT
   // SC-UX (additive, WP-B): read-only camera state for test harnesses (ACCEPTANCE.md "test kancaları").
@@ -75,6 +81,9 @@
   void setFaceColors(const std::map<std::string, QColor>& faceColors);
   // v2: hide faces (e.g. hidden bodies) without rebuilding the model.
   void setHiddenFaces(const QStringList& names);
+  // SC-2D-IMPORT (additive): named edges drawn in their own colour / pattern (picking unchanged). {} = none.
+  void setEdgeStyles(const std::map<std::string, EdgeStyle>& styles);
+  int styledEdgeCount() const;
 
   enum class View { Iso, Top, Bottom, Front, Back, Left, Right };
   void setView(View v);
--- a/src/viewer/ShapeViewer.cpp	2026-11-15 13:36:53.972073777 +0300
+++ b/src/viewer/ShapeViewer.cpp	2026-11-17 08:56:31.241648441 +0300
@@ -213,6 +213,12 @@
   QSet<QString> hiddenFaces;
   std::vector<QColor> faceCol;      // per mesh face
   std::vector<char> faceHid, edgeHid;
+  // SC-2D-IMPORT: per-edge styles (edgeStyled = mesh edge index -> style index + 1), dash geometry built lazily
+  std::map<std::string, EdgeStyle> edgeStyles;
+  std::vector<int> edgeStyled;
+  std::vector<std::pair<QColor, std::vector<float>>> styledGeom;
+  bool styledDirty = true;
+  double styledScale = 1;   // view half-height the dash lengths were built for (patterns ~ constant on screen)
   int hoverDatum = -1;
   struct LabelTex { GLuint tex = 0; QSize size; };
   std::map<QString, LabelTex> labelTex;
@@ -783,6 +789,7 @@
       }
     }
     secLinesDirty = true;
+    styledDirty = true;   // SC-2D-IMPORT
     edgeHid.assign(mesh.edges.size(), 0);
     if (!hiddenFaces.isEmpty()) {
       QStringList fs;
@@ -827,9 +834,58 @@
   }
   // Draw all visible edge segments (vaoL must be bound).
   void drawEdgeRuns() {
-    if (!anyHidden()) { gl->glDrawArrays(GL_LINES, 0, gm.nL); return; }
+    if (!anyHidden() && edgeStyles.empty()) { gl->glDrawArrays(GL_LINES, 0, gm.nL); return; }
     for (size_t i = 0; i < mesh.edges.size(); ++i)
-      if (!edgeHid[i]) gl->glDrawArrays(GL_LINES, mesh.edges[i].first, mesh.edges[i].count);
+      if (!edgeHid[i] && !(i < edgeStyled.size() && edgeStyled[i])) gl->glDrawArrays(GL_LINES, mesh.edges[i].first, mesh.edges[i].count);
+  }
+  // SC-2D-IMPORT: styled edges as CPU line lists (one per colour); patterns in world units scaled to the scene
+  void rebuildStyled() {
+    styledDirty = false;
+    styledGeom.clear();
+    edgeStyled.assign(mesh.edges.size(), 0);
+    if (edgeStyles.empty()) return;
+    std::map<QRgb, size_t> byColor;
+    styledScale = halfH;
+    const double R = std::max(1e-6, halfH);
+    for (size_t i = 0; i < mesh.edges.size(); ++i) {
+      const auto& e = mesh.edges[i];
+      auto it = edgeStyles.find(e.name.toStdString());
+      if (it == edgeStyles.end()) continue;
+      edgeStyled[i] = 1;
+      const QRgb key = it->second.color.rgb();
+      auto bc = byColor.find(key);
+      if (bc == byColor.end()) { byColor[key] = styledGeom.size(); styledGeom.push_back({it->second.color, {}}); bc = byColor.find(key); }
+      std::vector<float>& out = styledGeom[bc->second].second;
+      // pattern: alternating on/off lengths (fractions of the scene radius)
+      std::vector<double> pat;
+      switch (it->second.pattern) {
+        case 1: pat = {0.022, 0.012}; break;
+        case 2: pat = {0.050, 0.012, 0.010, 0.012}; break;
+        case 3: pat = {0.004, 0.010}; break;
+        default: break;
+      }
+      for (double& x : pat) x *= R;
+      double pos = 0;   // arc length along the edge
+      size_t pi = 0;
+      double left = pat.empty() ? 0 : pat[0];
+      for (int k = 0; k + 1 < e.count; k += 2) {
+        const float* a = &mesh.lines[size_t(e.first + k) * 3];
+        const float* b = &mesh.lines[size_t(e.first + k + 1) * 3];
+        const QVector3D A(a[0], a[1], a[2]), B(b[0], b[1], b[2]);
+        if (pat.empty()) { seg(out, A, B); continue; }
+        const double L = double((B - A).length());
+        double t = 0;
+        while (t < L - 1e-12) {
+          const double step = std::min(left, L - t);
+          if (pi % 2 == 0) seg(out, A + (B - A) * float(t / L), A + (B - A) * float((t + step) / L));
+          t += step;
+          left -= step;
+          if (left <= 1e-12) { pi = (pi + 1) % pat.size(); left = pat[pi]; }
+        }
+        pos += L;
+      }
+      (void)pos;
+    }
   }
 
   QSize labelSize(const QImage& im) const { return im.size(); }
@@ -1145,6 +1201,8 @@
         drawDyn(secLines, GL_LINES);
       }
     }
+    if (!edgeStyles.empty() && (styledDirty || edgeStyled.size() != mesh.edges.size() ||
+                                std::fabs(halfH / std::max(1e-9, styledScale) - 1) > 0.25)) rebuildStyled();   // SC-2D-IMPORT
     if (showEdges && gm.nL > 0)
     for (int cp = 0; cp < clipPasses(); ++cp) {
       beginClip(cp);
@@ -1154,6 +1212,14 @@
         setFlat(vp, ec, 1.0f, k * px * 0.5f, k * py * 0.5f, 0.0004f);
         drawEdgeRuns();
       }
+      if (!edgeStyles.empty()) {   // SC-2D-IMPORT: layer colours / line styles
+        for (const auto& [c, v] : styledGeom)
+          for (int k = 0; k < 2; ++k) {
+            setFlat(vp, c, 1.0f, k * px * 0.5f, k * py * 0.5f, 0.0012f);   // over coincident region outlines
+            drawDyn(v, GL_LINES);
+          }
+        gl->glBindVertexArray(gm.vaoL);
+      }
       if (!hlEdges.isEmpty()) {
         for (const auto& e : mesh.edges) {
           if (e.name.isEmpty() || !hlEdges.contains(e.name)) continue;
@@ -1483,6 +1549,25 @@
   d->rebuildFaceState();
   d->invalidate();
 }
+// SC-2D-IMPORT (additive)
+void ShapeViewer::setEdgeStyles(const std::map<std::string, EdgeStyle>& styles) {
+  if (styles.size() == d->edgeStyles.size()) {
+    bool same = true;
+    for (const auto& [k, v] : styles) {
+      auto it = d->edgeStyles.find(k);
+      if (it == d->edgeStyles.end() || it->second.color != v.color || it->second.pattern != v.pattern) { same = false; break; }
+    }
+    if (same) return;
+  }
+  d->edgeStyles = styles;
+  d->styledDirty = true;
+  d->invalidate();
+}
+int ShapeViewer::styledEdgeCount() const {
+  int n = 0;
+  for (const auto& e : d->mesh.edges) if (d->edgeStyles.count(e.name.toStdString())) ++n;
+  return n;
+}
 void ShapeViewer::setHiddenFaces(const QStringList& names) {
   d->hiddenFaces = QSet<QString>(names.begin(), names.end());
   d->hoverId = 0;
--- a/tests/gui/CMakeLists.txt	2026-11-16 23:02:48.141799577 +0300
+++ b/tests/gui/CMakeLists.txt	2026-11-17 08:46:45.094779767 +0300
@@ -102,3 +102,8 @@
 # menus, named selections, split face stays hidden, undo, save / reopen, STEP has all faces. --shots -> docs/visibility/shots.
 pc_add_gui_harness(gui_harness_visibility SOURCES gui_harness_visibility.cpp TIMEOUT 400
                    DEFINES PC_VIS_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/visibility/shots")
+# SC-2D-IMPORT: 2D drawing as curves (DXF / PDF), Katmanlar tab, Görünüş seç / Görünüşü yerleştir, Fill + Pull on the
+# imported curves (XY + placed vertical plane), save / reopen, undo, drag & drop. --shots -> docs/import2d/shots.
+pc_add_gui_harness(gui_harness_import2d SOURCES gui_harness_import2d.cpp TIMEOUT 400
+                   DEFINES PC_I2D_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/import2d/shots"
+                           PC_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/drawing/corpus")
--- a/tests/gui/gui_harness_sc_shell.cpp	2026-11-16 19:54:40.299423100 +0300
+++ b/tests/gui/gui_harness_sc_shell.cpp	2026-11-17 09:06:49.698796493 +0300
@@ -243,8 +243,9 @@
   if (st) {
     QStringList tabs;
     for (int i = 0; i < st->count(); ++i) tabs << st->tabText(i);
-    check(st->tabPosition() == QTabWidget::South && tabs == QStringList({"Yapı", "Geçmiş", "Seçim", "Gruplar"}),
-          "Structure tabs at the bottom: " + tabs.join(" · ") + " (no Layers / Views)");
+    // SC-2D-IMPORT: the Layers tab ("Katmanlar") now exists (2D drawing import); still no Views tab
+    check(st->tabPosition() == QTabWidget::South && tabs == QStringList({"Yapı", "Geçmiş", "Seçim", "Gruplar", "Katmanlar"}),
+          "Structure tabs at the bottom: " + tabs.join(" · ") + " (Katmanlar, no Views)");
   }
   if (ot) check(ot->tabPosition() == QTabWidget::South && ot->count() == 2 && ot->tabText(0) == "Seçenekler" && ot->tabText(1) == "Özellikler",
                 "Options / Properties tabs at the bottom");
```

## Entegrasyon (SC-VIDEO2D, 17 Kas 2026): video akışı uçtan uca
`tests/gui/gui_harness_video2d.cpp` (ctest `gui_harness_video2d` ve `gui_harness_video2d_dpi72` = `QT_FONT_DPI=72`):
plate ve lbracket DXF'leri gerçek tıklama / tuşlarla — içe aktar (menü + dosya diyaloğu) → Katmanlar kutularıyla
ölçü / gizli / eksen / çerçeve / antet gizle (seçilemez, Kadar hedefi olamaz) → Görünüş seç + Görünüşü yerleştir
(şerit) → F Enter → P, U, öbür görünüşün köşesi, Enter (hedef = o görünüşün `sketch:<id>:p<k>` noktası, derinlik =
ölçülen uzaklık 8 / 40 / 8 mm) → Renk (Görüntü > Stil) → hacimler `gt.json` ile aynı (45964.25 / 41611.68 mm³) →
tüm akışı Ctrl+Z ile geri al, Ctrl+Y ile yinele (JSON aynı) → kaydet / aç (JSON aynı). 154 denetim.
Görüntüler: `shots/video_plate_*.png`, `shots/video_lbracket_*.png`.

Bulunan ve düzeltilen birleşme hataları (`// SC-VIDEO2D` işaretli, en küçük değişiklik):
* `src/designer/Ribbon.cpp` (`RibbonGroup::newBar`): ribbon cilalandıktan sonra oluşturulan araç çubukları (Görüntü >
  Katmanlar / 2B çizim) 4 px kenar boşluğu tutuyordu; 3 küçük düğmelik sütun 69 px'e sığmıyor, **Görünüşü yerleştir**
  taşma okunun arkasına düşüyordu (tıklanamıyordu). Her çubuğa `padding: 0` sayfası.
* `src/designer/Import2D.cpp` (`moveCurves`): `targetPlane()` yeni datum düzlemi eklerken `doc.features` yeniden
  ayrılıyor, `sf` sarkık işaretçi kalıyordu → katlanan sketch `importedCurves` / `layer` özelliklerini kaybediyordu
  (tanımsız davranış). GIZLI katmanı gizlendiğinde (düzlemi ilk oluşturan GORUNUR olunca) ortaya çıktı. `sf` yeniden alınır.
* `src/designer/Import2DUi.cpp` (`setGhostCurves`): yerleştirme önizlemesi klasik `buildProfile` kullanıyordu;
  T-birleşimli görünüşte (lbracket) hiç hayalet çizilmiyordu → `sketchRegions` (v2 bölgeleri).
* `src/model/Regen.cpp` (`doExtrude`): `regions=` listeli (çok bölgeli) Çek'te birbirine değen bölgeler ayrı
  gövdeler oluyordu (L = 3 gövde). Değen prizmalar ad geçmişiyle birleştirilir (tek gövde, SpaceClaim gibi);
  ayrık bölgeler ayrı kalır, `regions=` olmayan belgeler değişmez.
* `src/viewer/ShapeViewer.cpp` (seçim geçişi): bölgeler düzlemlerinin 0.02 mm önünde çizilir; bir gövde yüzüyle
  çakışan bölge (gövdenin altındaki üst görünüş dairesi, yüz üstündeki sketch) yüzün tarafından tıklanamıyordu.
  Seçim geçişinde bölgeler göze doğru 0.1 mm kaydırılır (görüntü değişmez).

Kalan farklar: Doldur, deliklerin iç adalarını da bölge olarak seçer (PULL_UPTO.md tasarımı; delikli plaka için Çek
içinde yalnız dış bölgeye tıklanır). Değen bölgelerden oluşan uç yüzler birleştirilmez (`Extrude1.end:1..3`, eş
düzlemli ayrı yüzler). Katlanmayan görünüşler XY'de kalır. Kadar okunun üstündeki etiket "0 mm" gösterir (değer
kutusu doğru: "Kadar 40 mm").
