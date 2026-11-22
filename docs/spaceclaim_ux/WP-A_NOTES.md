# WP-A "kabuk" (shell) — teslim notları (15 Kasım 2026)

Kapsam: SPEC §10.1 WP-A, Faz 1 (A1–A7). `ToolApi.h` / `ToolApi.cpp` **değişmedi** (SC-UX ToolApi 1.0).
Kabuk yalnızca `ToolApi.h` üzerinden çalışır; P0 stub ToolManager ile de, WP-B'nin gerçek ToolManager'ı ile de.

## 1. Değiştirilen / oluşturulan dosyalar (birleştirmede kopyalanacak tam liste)

Yeni (WP-A):
* `src/designer/Ribbon.h`, `src/designer/Ribbon.cpp` — Ribbon, RibbonGroup, Dosya düğmesi, hızlı erişim
* `src/designer/StructurePanel.h`, `src/designer/StructurePanel.cpp` — Yapı paneli (Yapı/Geçmiş/Seçim/Gruplar)
* `src/designer/SelectionPanel.h`, `src/designer/SelectionPanel.cpp` — Seçim sekmesi
* `src/designer/OptionsPanel.h`, `src/designer/OptionsPanel.cpp` — Seçenekler paneli (ToolOption çizimi)
* `src/designer/PropertiesPanel.h`, `src/designer/PropertiesPanel.cpp` — Özellikler paneli (§9)
* `src/designer/StatusBarWidgets.h`, `src/designer/StatusBarWidgets.cpp`
* `src/designer/Shortcuts.h`, `src/designer/Shortcuts.cpp` — tek kısayol tablosu, desteklenmeyen tuşlar, metin girişi koruması
* `src/designer/DesignMode.cpp` — K/X/D, "Sketch düzlemi seç", Yönlendir komutları, IDesignHost uygulaması, stub köprüsü, gezinme yedeği
* `src/designer/MainWindowShell.cpp` — (SPEC listesinde yok, WP-A'nın yeni çeviri birimi) eylemler, menüler, ribbon, dock'lar, durum çubuğu, seçim bağlantıları, bağlam menüleri, düzen kaydı
* `docs/spaceclaim_ux/WP-A_NOTES.md` (bu dosya)
* `docs/spaceclaim_ux/shots/A_*.png` (12 ekran görüntüsü, `pc_gui_harness_sc_shell --shots`)

Değişen (WP-A sahipliğinde):
* `src/designer/MainWindow.h`, `MainWindow.cpp`, `MainWindowOps.cpp`, `MainWindowV2.cpp`
* `src/designer/Icons.h`, `src/designer/Icons.cpp` (yeni vektör ikonlar: pull, move, fill, home, planview, spin, pan, zoom,
  mode3d, modesketch, trim, group, props, options, measure, mass, hide/showall/hideothers, edges, axes, layout, delete,
  panel, views, prev/nextview, 11 `con.*` kısıt ve 7 `dim.*` ölçü ikonu)
* `src/designer/STATUS.md` (WP-A bölümü eklendi)
* `tests/gui/gui_harness_sc_shell.cpp` (yer tutucu yerine gerçek düzenek)

Değişmeyen: `ToolApi.h/.cpp`, `tools/ToolManager.cpp`, `SketchEditor.*`, `src/viewer/*`, `ExprEdit.*`, `FeatureDialog.*`,
`MfgDialogs.*`, `main.cpp`, tüm CMake dosyaları, `gui_harness.cpp`, `gui_harness_v3.cpp`, `gui_harness_v4.cpp`,
`gui_harness_v5.cpp` (güncelleme gerekmedi), `sc_stub_host.h`, `harness_robust.h`.

## 2. Ne yapıldı (özet)
* **Ribbon** (üçüncü parti yok): Dosya (mavi, `ribbonFileButton`, `menuFile`), hızlı erişim (`quickAccessBar`: Yeni, Aç,
  Kaydet, Geri Al, Yinele), sekmeler **Tasarım · Görüntü · Ölçüm · İmalat** (`ribbonTabBar`, sayfalar `ribbonTab.<anahtar>`).
  Tasarım grupları: Yönlendir | Sketch | Mod | Düzenle | Kesişim | Oluştur | Gövde (+ bağlamsal Kısıtlar | Ölçüler |
  Sketch Oturumu). Pano grubu SPEC §1.3/§3.9 gereği **gösterilmez** (görevde adı geçse de). Büyük düğme 32 px/alt yazı,
  küçük 16 px, 3 satır; kısıt/ölçü düğmeleri ikon-yalnız (ipucunda Türkçe + İngilizce). Etkin araç turuncu.
  Ribbon tam genişlikte, menü çubuğu gizli (gerçek QMenuBar kalır; `setMenuWidget` kullanılmadı, bkz. kod notu).
* **Dock düzeni**: sol üst `dockStructure` (sekmeler altta: Yapı, Geçmiş, Seçim, Gruplar), sol alt `dockOptions`
  (Seçenekler/Özellikler, başlık "Seçenekler - <araç>"), alt `dockIssues` 140 px. Görüntü > Pencere: "Düzeni sıfırla",
  panel aç/kapa. Düzen QSettings'te yalnızca `persistcad` uygulamasında saklanır (düzenekler etkilenmez).
* **Modlar** `MainWindow::setDesignMode` (DesignMode.cpp): K (seçili düzlemsel yüz → OnFace, datum → OnPlane, sketch →
  düzenle, boş belge → XY, aksi halde "Sketch düzlemi seç": düzlemsel yüz hover ızgarası + orijinde XY/XZ/YZ kareleri,
  Esc/D iptal), X (seçili datum/düzlemsel yüzden ya da XZ'den kesit; tekrar X kapatır), D (sketch commit, kesit kapalı,
  Seç). 3B'de L/R/C sketch moduna geçirir. Sketch editörü artık viewer'ın **çocuğu** (QStackedWidget kaldırıldı).
* **Kısayollar** `Shortcuts.cpp` tablosu (SPEC §4), WP-B araç eylemlerinin kısayolları dahil (`tool.select` S,
  `tool.pull` P, ...). Desteklenmeyen tuşlar (B, Ctrl+C/X/V/D, Ctrl+T/L/Q, E, O, U, Ctrl+A, ...) durum çubuğunda açıklama.
  Uygulama genelinde metin girişi koruması (QLineEdit/hücre düzenleyici/ExprEdit/spin/düzenlenebilir hücre odaktayken
  tek harf kısayolları çalışmaz). Ctrl+Y = Yinele. Ctrl+Q artık "Çıkış" değil (SPEC: desteklenmeyen tuş).
* **IDesignHost** MainWindow'da tam uygulandı; ToolManager kurucuda oluşturulur, `installKeyRouting(viewer)` ve
  `installKeyRouting(sketchEditor)` çağrılır; eski `sketchBar_`/`sketchBar2_`/`mainBar_` eylemleri kaldırıldı (tek "Seç").
* **Seçim**: SelectionModel tek kaynak; vurgulama WP-A'da. Görünüm tıklamaları önce host'a özel durumlara, sonra
  `ToolManager::handlePick`'e, tüketilmezse eski davranışa (+SelectionModel) gider. Filtre (durum çubuğu) uygulanır.
* **Paneller**: Seçenekler (genel ToolOption çizimi + `createOptionsWidget`), Özellikler (gövde/yüz/kenar/datum/sketch/
  bölge/özellik/parametre/belge; düzenlemeler `commitDocument`/`changeParameter` ile tek geri alma adımı), Gruplar =
  parametre tablosu ("Uygulamada göster" sütunu, "Kullanıldığı yerler"), Ctrl+G = Grup oluştur (NewParameterDialog).
* **Durum çubuğu**: mesaj alanı (araç istemi), hover, hızlı ölçüm, seçim listesi ("1 yüz: Extrude1.end"), seçimi geri al,
  Hata/Uyarı, seçim filtresi, Yenileme, Hacim, Sketch DOF, ← → görünüm.
* **Gizle/göster** Ctrl+H / Alt+H / Ctrl+Shift+H, **Kütle özellikleri** (Ölçüm), görünüm bağlam menüsü (§3.10).

## 3. Birleştirme (merge) notları
1. **Stub köprüsü**: `toolsReal_ = (tools_->activate("select"), tools_->active() != nullptr)`. Stub'da (active()==nullptr)
   sketch araç/kısıt/ölçü/Sil/Yapı çizgisi eylemleri SketchEditor'ü doğrudan sürer (eski araç çubuğu davranışı). Gerçek
   ToolManager'da köprü **tamamen devre dışıdır**; WP-B'nin `active()` değeri kurucudan sonra `activate("select")` ile
   null olmamalı (wpB'deki güncel ToolManager.cpp böyle).
2. **Viewer API'si derleme anında algılanır** (DesignMode.cpp, SFINAE): `ShapeViewer::NavigationScheme::SpaceClaim`,
   `DragMode`, `setHomeView`, `setPlanView(o,u,v,fit)`, `previousView/nextView`. WP-B'nin wpB'deki `ShapeViewer.h` ile
   denendi: hepsi algılanıp çağrılıyor (nm ile doğrulandı). Algılanınca kabuktaki gezinme yedeği (olay filtresi:
   orta=çevir, Shift+orta=kaydır, Ctrl+orta=yakınlaştır, sol sürükleme yok) **kapanır**. Elle bir şey yapmaya gerek yok.
3. Birleştirme sırası SPEC §10.2'deki gibi: viewer (B) → WP-A dosyaları → B'nin designer/tools dosyaları. Ortak dosya yok;
   `Icons.*` WP-A'nındır (wpB'de farklı görünmesi yalnızca WP-A'nın değişikliğidir).
4. `MainWindow::selectedEdge()` artık SelectionModel'deki son kenarı döndürür (yoksa son tıklanan kenar).
5. Gizli uyumluluk widget'ı: `bodiesTree()` (v2 düzeneği) gizli ama tam bağlı; görünen gövde listesi Yapı ağacındaki
   "Gövdeler" düğümüdür (ACC #23 için `structurePanel()->bodiesNode()` de kullanılabilir).
6. Ribbon yüksekliği 94 px (+ sekme satırı); grup sayfaları pencereyi genişletmez (sağdaki gruplar gerekirse kırpılır).
   1600 px'te sketch modundaki tüm bağlamsal gruplar sığıyor (düzenekte denetleniyor).

## 4. WP-B'ye istekler
* **R1 (Esc ve "Sketch düzlemi seç")**: Kabuk Esc'i viewer olay filtresinde yakalar; WP-B'nin `installKeyRouting`
  filtresi daha sonra kurulduğu için önce çalışır. `planePickActive()` sırasında Esc'i tüketmeyin (ya da `cancel()` false
  döndürün) ki kabuk durumu iptal edebilsin. D zaten çalışıyor.
* **R2 (Del)**: Del pencere kısayoludur (`actDelete`); sketch modunda `ToolManager::deleteSelection()` çağrılır, 3B'de
  önce `deleteSelection()`, false ise kabuk seçili özellikleri onaylı siler. Sketch Del'i bu yoldan bekleyin.
* **R3 (sağ tık kenar menüsü)**: Kabuk sağ tıkta `edgeContextMenu`/`datumContextMenu`/`viewContextMenu` açar ve
  `ToolManager::populateContextMenu` çağırır. Eski Ctrl+tık → kenar menüsü yolu `onEdgePicked`'de **yalnızca araç
  tüketmediğinde** çalışır; SelectTool kenarı tükettiğinde menü açılmaz (SPEC §2.11). v2 düzeneği güncellemesi WP-B'de.
* **R4 (SketchEditor araç senkronu)**: Gerçek ToolManager'da kabuk `SketchEditor::toolChanged`'i dinlemez; aracın
  checked durumunu ToolManager yönetmeli.
* **R5 (ikonlar)**: Kabuk, WP-B eylemlerinde ikon/ipucu yoksa ekler (`decorate`, mevcutları ezmez). Kendi ikonlarınızı
  verirseniz onlar kalır; `makeIcon` artık `pull, move, fill, trim, con.*, dim.*` türlerini de çizer.
* **R6 (test kancaları)**: `viewer->property("cameraAz"/"halfH"/"target")` hazır olduğunda SH düzeneği ACC #27'nin
  "← sonrası H öncesi kamera" karşılaştırmasını ekleyebilir; şimdilik görüntü farkıyla doğrulanıyor.

## 5. Test sonuçları (wpA, stub ToolManager)
* Tam derleme (`cmake -G Ninja`, `ninja`): temiz.
* `ctest` (Xvfb): **191/191** geçti (gui_harness, v2, v3, v4, v5, v6, qa, sc_shell, sc_tools yer tutucusu + birim testleri).
  WP-B eksikliği yüzünden düşen test yok (sc_tools hâlâ P0 yer tutucusu).
* `persistcad --selftest` ve `persistcad samples/bracket.pcad --selftest2` geçti.
* `pc_gui_harness_sc_shell`: ACC 1, 2, 3, 12 (mod), 20 (panel), 22 (K yüzde), 23, 27, 28, 29, 31–41 maddeleri
  (163 kontrol) + OptionsPanel ve stub host sözleşmesi. `--shots` ile A_*.png üretir.

## 6. Bilinen eksikler
* Stub ile: Çek/Taşı/Doldur/Birleştir/Düzlem/Kabuk/Kırp eylemleri yalnızca etkinleşir (durum çubuğu mesajı); işlevleri WP-B.
* Viewer API'si yokken (yalnız stub/legacy viewer): ← → kamera geçmişi yok (mesaj verir); Plan görünümü eğik düzlemde en
  yakın standart görünüme düşer; Ana görünüm = izometrik + sığdır.
* Özellikler paneli: sketch bölgesi alan/çevre/delik sayısı, sketch eğrisi/noktası ayrıntıları ve çoklu seçimde ortak
  alan düzenleme 2. faza kaldı ("Çeşitli (Varies)" yalnızca Ad için).
* Seçim sekmesinde "Benzerini seç", Gruplar'da çift tıkla aracı yeniden çağırma, ribbon daraltma: 2. faz (SPEC §10.2).
* Radyal menü (O), Ölç (E): durum çubuğu "henüz yok" mesajı.
* `docs/KULLANIM_KILAVUZU.md` ve `docs/screenshots/final_*.png` yenilenmedi (Faz 1 sonunda, birleşmiş sürümle).
