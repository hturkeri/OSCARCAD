# SC23 WP-S "kabuk/yerleşim" — uygulama notları

Tarih: 18 Kasım 2026. Kapsam: ANALYSIS.md §5.1 S-P0-1, S-P0-2, S-P1-1 … S-P1-9; testler T10, T11, T13, T14
(`tests/gui/sc2023_shell_checks.inc`, `gui_harness_sc2023`). Kanıt etiketleri ANALYSIS §0'daki gibidir.

## 1. Yapılanlar

| Kimlik | Uygulama | Dosyalar |
|---|---|---|
| S-P0-1 | Gruplar hiç sıkıştırılmaz (genişlik = kendi ipucu, etiket kısalmaz). Sığmazsa daraltma sırası (`setCollapsePriority`, düşük önce, −1 asla): seviye 1 **küçük** (büyük düğmeler simge+metin, 3 satır), seviye 2 **grup açılır düğmesi** ("Gövde / Görünürlük ▾", aynı QAction'lar QMenu'de). Hâlâ sığmazsa sağda **»** (`ribbonOverflow`) taşan grupların komutlarını listeler. `setMaxLevel(Compact)`: Sketch > Düzenle yalnız küçülür. Yeniden boyut, sekme ve grup göster/gizle olaylarında yeniden hesaplanır; görünmeyen şeritte ölçülmez. `Ribbon::minimumSizeHint` pencereyi genişletmez. Ölçüm: 1240×800 Tasarım'da Oluştur ve Gövde küçük, gerisi büyük; 1024×768'de Mod ve Oluştur küçük, Gövde ve İçe aktar açılır. Sketch sekmesi 1240'ta tam, 1024'te Yönlendir/Kısıtlar/Ölçüler açılır, Düzenle küçük. | `Ribbon.h/.cpp`, `MainWindowShell.cpp::buildRibbon`, `MainWindow.cpp` (Görüntü öncelikleri) |
| S-P0-2 | HUD: kalın araç adı · seçim, yarı saydam etiket; ileti şeridi **simge + metin** (bilgi/uyarı/hata, yalnız renk değil), 520 px'te kaydırma, tıklayınca kapanır (tıklama görünüme geçmez). `guideArea()` sabit yuva (iletiyle oynamaz). Sketch açıkken SketchEditor'ün kendi "Sketch: … DOF" rozeti sol üste çizildiği için HUD bu rozetin altına iner. HUD hatası durum çubuğunda da hata simgesi gösterir. Donmuş API ve objectName'ler değişmedi. | `ViewportHud.cpp`, `MainWindowShell.cpp::buildHud` |
| S-P1-1 | **Sketch** sekmesi (Dosya'dan sonra, Tasarım'dan önce [G]): Yönlendir · Oluştur · Değiştir · Kısıtlar · Ölçüler · Düzenle · Sketch'i Bitir. Sketch modunda kendiliğinden seçilir, çıkınca Tasarım'a dönülür. 3B'de çizim grupları **görünür ama pasif** (ipucu nedeni yazar). Tasarım'da bağlamsal grup kalmadı; Kesişim/Oluştur/Gövde/İçe aktar sketch açıkken pasif (gizli değil). Grup anahtarları `constraints`, `dimensions`, `sketchSession` korundu. "Sketch'i Bitir" düğmesi mevcut `actFinishSketch_` QAction'ını kullanır (küçük ve açılır kopyalar da aynı QAction); WP-O'nun `sender() == actFinishSketch_` ayrımı bozulmaz. | `MainWindowShell.cpp` (`buildRibbon`, `syncSketchRibbon`, `designModeChanged` bağlantısı) |
| S-P1-2 | `DocumentTabBar` (`documentTabs`): görünümün altında "Part1" / "Part1*", ipucunda yol; `updateTitle()` besler. Merkez = `centralArea` (görünüm + belge şeridi). | `DocumentTabBar.*`, `MainWindow.cpp` |
| S-P1-3 | Koyu mavi durum çubuğu (`mainStatusBar`): solda istem (+ hata simgesi `statusSeverity`; simge sol kenar boşluğunda, metinle çakışmaz), ortada seçim/ölçü, sağda geri al, filtre, sayaçlar, ←/→. DOF renkleri koyu zemine uyarlandı. | `MainWindowShell.cpp::buildStatusBar`, `MainWindow.cpp` |
| S-P1-4 | `ShapeViewer::View::Trimetric` (eklemeli, az −62°, el 22°). `actViewIso … actViewBack` (Ctrl+0 … Ctrl+6, Arka kısayolsuz) Görünüm menülerinde ve **Görünümler** sekmesinde (Yapı panelinin son sekmesi). Oluştur = bakış yönünü oturumda saklar (belgeye yazılmaz, [Ö]); Sil yalnız kayıtlılar. Uygulama: çift tık / Enter. | `ViewsPanel.*`, `src/viewer/pc/ShapeViewer.h`, `src/viewer/ShapeViewer.cpp`, `MainWindowShell.cpp`, `MainWindow.cpp`, `Shortcuts.cpp` |
| S-P1-5 | Araç kılavuzları sol üstte, HUD'un `guideArea()` yuvasında; Seç tek başına üstte, diğerleri 2 sütun. Sketch mini çubuğu HUD'un altında. | `tools/ToolGuideBar.*`, `tools/MiniToolbar.*` |
| S-P1-6 | Zengin ipucu: ad, kısayol, açıklama (eylem ipucundan ya da kısa tablodan), pasifse neden, "F1: yardım". Eylem ipuçları değişmedi (`richToolTip()` uygulama düzeyinde süzgeçle). **F1** `HelpViewer` (QTextBrowser + Markdown, arama) içinde `docs/KULLANIM_KILAVUZU.md` açar; fare şerit düğmesindeyse komut adını arar. F3 yok. | `Shortcuts.*`, `HelpViewer.*`, `MainWindowShell.cpp` |
| S-P1-7 | Sağ tık: Son sketch düzlemini kullan, Görünüm ▸, Görünürlüğü tersine çevir (tek undo), Yapı ağacında bul, Kes/Kopyala/Yapıştır (pasif, gerekçeli), Sil, Özellikler. Menüyle birlikte mini çubuk menünün üstünde açılır (araçta mini seçenek varsa). Clip/Spin/Anchor/Suppress eklenmedi. | `MainWindowV2.cpp`, `tools/MiniToolbar.*` |
| S-P1-8 | Seçim paneli: "Seçili: Yüz (1)" başlığı, "Tüm türler (All Types)" tür listesi → `SelectionModel::setFilter` (durum çubuğu filtresiyle eşli), arama ve "Tüm gövdelerde ara" gerekçeli pasif ([D]). | `SelectionPanel.*` |
| S-P1-9 | Panel başlığında daralt/ayır/kapat (`dockCollapse` …), min. genişlik 240, sol sütun dar pencerede ≤ %26 (hedef %22, 240–330). Yapı sekme çubuğu kaydırmalı; 230 px'ten geniş sayfalar (Seçim, Gruplar, Katmanlar) panel içinde kayar. Seçenekler sığmazsa Seçenekler paneli uzar (Yapı ≥ 170 px). İfade alanlarında parametre seçici + "Yeni…" ikinci satırda. **F6 / Shift+F6** odak döngüsü: görünüm → Yapı → Seçenekler → şerit sekmeleri. | `MainWindowShell.cpp`, `MainWindow.cpp`, `OptionsPanel.cpp`, `ShellUtil.h` |

## 2. Testler
* `sc2023_shell_checks.inc` (150 kontrolün kabuk kısmı): HUD, kılavuzlar, Sketch sekmesi, **T10** (H/V/Ctrl+1/orta sürükleme
  sketch çerçevesini ve öğeleri değiştirmez), **T11** (sekme değişimi araç/seçim/Seçenekler/HUD/undo'yu korur), **T14**
  (hover seçmez; sağ tık seçili olmayan yüzü seçer, çoklu seçimi korur; menü öğeleri; tersine çevirme tek undo),
  belge sekmesi, koyu durum çubuğu, Görünümler, ipucu + F1, Seçim paneli, panel daraltma, F6, **T13** 1240×800 ve
  1024×768'de (Seç/Çek/Taşı/Doldur, Dikdörtgen, Sketch'i Bitir görünür + kırpılmamış + fareyle tıklanır; sıkışmış
  etiket ve araç çubuğu » yok; sol sütun ve Seçenekler-Çek kaydırmasız).
* Güncellenen harness'ler: `gui_harness_sc_shell` (sekme listesi + Sketch, Tasarım grup sırası, Yapı sekmelerine
  Görünümler, sketch modunda Sketch sekmesi, 3B'de bağlamsal gruplar pasif), `gui_harness_v4` (görünür Kesit düğmesi;
  grupların gizli küçük kopyası olabilir). `gui_harness_qa`, `colors`, `visibility`, `video2d` değişiklik gerektirmedi.

## 3. Sapmalar / açık noktalar
* `MainWindow.h` değişmedi (yeni üye yok). Paylaşılan yardımcı `setDockCollapsed` yeni `ShellUtil.h`'de.
* WP-O'nun `setSketchMode` (MainWindowOps.cpp) grup görünürlük değişiklikleri `designModeChanged` sonrası
  `syncSketchRibbon` ile geri alınır (dosyaya dokunulmadı). WP-O o satırları kaldırırsa davranış aynı kalır.
* SketchEditor'ün sol üst rozeti (WP-O dosyası) HUD ile aynı köşededir; HUD sketch'te rozetin altına iner. Birleşme
  sonrası rozetin HUD'a taşınması önerilir.
* İfade alanlarının seçici/"Yeni…" düğmesi `ExprEdit` dosyası değiştirilmeden yalnız yerleşimde ikinci satıra alındı
  (Sketch'i Bitir > Düzlem ofseti ve Seçenekler paneli).
* Görünümler: kayıtlı görünüm yalnız yönü saklar (zoom/hedef korunur), belgeye yazılmaz; uygulama çift tık/Enter
  (SpaceClaim'deki tıklama davranışı [D]). Arka'nın listede yeri [D].
* Panel "sabitleme" ikonlarının SpaceClaim'deki anlamı [D]; PersistCAD daraltma uygular.
* `ctest` (build dizini, -j2): 356/356 geçti.

## 4. Ekran görüntüleri (`docs/sc2023/shots/`, `gui_harness_sc2023 --shots`)
`wps_1240_design_pull.png`, `wps_1240_sketch.png`, `wps_1240_after_sketch.png`, `wps_1240_overview.png`,
`wps_1024_design_pull.png`, `wps_1024_sketch.png`, `wps_1024_after_sketch.png`, `wps_context_menu.png`,
`wps_views_panel.png`, `wps_panel_collapsed.png`; P0 görüntüleri `p0_hud_pull.png`, `p0_hud_invalid.png` yenilendi.
