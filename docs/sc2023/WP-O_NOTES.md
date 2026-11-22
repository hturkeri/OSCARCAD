# SC23 WP-O "işlem/durum" — uygulama notları

Tarih: 18 Kasım 2026. Taban: P0-SC23 (`P0_CONTRACT.md`). Kapsam: `ANALYSIS.md` §5.1 O-P0-1…6, O-P1-1…4
(O-P1-5 WP-S ile ortak, burada yapılmadı). Kanıt etiketleri `PROMPT.txt`'teki gibi; aşağıdaki kararların hepsi
**[Ö]**'dür, aksi yazmıyorsa. SpaceClaim davranışı diye sunulan tek şey 2023 R1'de görülen **[G]** End Sketch
Editing → Design + Pull geçişidir.

## 1. Yapılanlar

| Kimlik | Ne yapıldı | Dosyalar |
|---|---|---|
| O-P0-1 | `ToolBase::operation()/setOperation()/emitMomentary()`; Çek tam durum makinesi (`PullTool::reportState`: AwaitingSelection / AwaitingInput / Preview / Invalid / Committing / Completed / Cancelled); sketch araçları (`SketchToolAdapter::reportState`: ilk tık = AwaitingSelection, çizim = AwaitingInput, değer yazılırken Preview, Enter'da geçersiz metin = Invalid, eleman oluşunca Completed, Esc = Cancelled); Taşı (seçim / geçersiz / değer / Completed) ve Doldur (seçim / Enter sonucu) | `tools/ToolBase.*`, `tools/PullTool.*`, `tools/SketchToolAdapter.*`, `tools/MoveTool.*`, `tools/FillTool.cpp` |
| O-P0-2 | Odak görünümden (ve alt öğelerinden) çıkınca ya da pencere `WindowDeactivate` alınca 600 ms zamanlayıcısı durur, önizleme kalır (`previewHeld()`), örtük commit yok; Enter uygular. Ctrl+Z/Ctrl+Y önizlemede: `ToolManager` tuş yönlendiricisi önce aracı iptal eder, olay kabul edilmez → Geri Al çalışır (−1). Değer alanı odaktayken aynı şeyi `DimensionBox` yapar. Araç çubuğundan Geri Al gibi belgeyi önizleme altında değiştiren her şey `PullTool::documentChanged` → önizleme düşer (Cancelled) | `tools/PullTool.cpp`, `tools/ToolManager.cpp`, `tools/DimensionBox.cpp` |
| O-P0-3 | `tools/ValueInput.*` (saf mantık): birimler mm/cm/m/in/"/°/deg/rad, ondalık virgül, ifade/parametre değerlendirmesi çağırana ait (host'un `evaluateExpression`'ı), sıfır ret + neden, negatif = ters (kabul), \|v\| > max(1e5 mm, 100 × model köşegeni) → `NeedsConfirm`; `LargeValueGate` aynı metinle ikinci Enter'da geçirir. `DimensionBox::setReasonValidator/reason/setFieldTip/invalidCommit`: geçersiz metnin nedeni alanın tooltip'inde; Enter → Invalid (HUD kırmızı + status). 1e7 modal açmaz (ilk Enter'da hiç commit yok) | `tools/ValueInput.*`, `tools/DimensionBox.*`, `tools/PullTool.cpp`, `tests/test_value_input.cpp` |
| O-P0-4 | `tryCommit`: `MODEL.FEATURE_FAILED` (yeni) ve `MODEL.REGENERATE` hiçbir zaman modal değil, hiçbir zaman uygulanmaz (`autoYes_` dahil). Önceki model + rapor kalır; `showOperationFeedback(Invalid)` → HUD kırmızı + status, özellik adı + öneri + "Önceki model korundu (<işlem>)". Neden, `MainWindow` dinamik özelliği `sc23.commitError` ile araca geçer (Çek kendi Invalid durumunu aynı metinle yayar). Gerçek imalat kuralı hataları eski inceleme penceresine gider | `MainWindowOps.cpp`, `tools/PullTool.cpp` |
| O-P0-5 | Sketch bitince (düğme ya da D) özet: bölge / delikli bölge / delik / açık uç sayısı → status + HUD (Info; açık uç varsa Warning, 10 s). Açık uçlar `ViewportOverlay::setGapMarkers` ile kırmızı (Doldur'un işaretçisi `drawGapMarker`'a taşındı); belge değişince / sketch moduna girince silinir | `MainWindowOps.cpp`, `tools/ViewportOverlay.*`, `tools/FillTool.cpp`, `tools/ToolManager.cpp` |
| O-P0-6 | Yapı: `Gövdeler (n)` (2. sütun "Katı"), `Yüzeyler (n)` (çekilmemiş bölgeler, tık = SketchRegion seçimi, tooltip alan/çevre/delik), `Eğriler (n)` (bölgesiz ya da açık uçlu sketch'ler), `Düzlemler`, `Sketch'ler` (korundu). Kök `Part1*` (pencerenin `ModifiedChange` olayını izler). Bölge Özellikleri: Tür = Yüzey, Alan (B-rep, mm²), Çevre (dış tel, mm), Delik sayısı. `sketchesNode()/bodiesNode()/planesNode()` aynı; yeni `surfacesNode()/curvesNode()/rootNode()` | `StructurePanel.*`, `MainWindowStructure.cpp`, `PropertiesPanel.cpp` |
| O-P1-1 | "Sketch'i Bitir" düğmesi (`actFinishSketch_`) → commit sonrası `tools_->activate("pull")` + `ribbon_->setCurrentTab("design")` (mevcut public Ribbon API'si, `MainWindowOps.cpp`'den), bölgeler vurgulanır. D → Seç değişmedi (`gui_harness_sc_shell` 332 geçiyor) | `MainWindowOps.cpp` |
| O-P1-2 | Çek seçenekleri "Genel" grubunda (Birleştirme, yeni gövde adı, kesiciyi koru); "Yüzeyleri kalınlaştır (Thicken surfaces)" işaretli + pasif, gerekçe tooltip'te (SC23-CR-1/2) | `tools/PullTool.cpp` |
| O-P1-3 | "Birim" Info satırı (`Belge birimi: mm …`), değer alanının tooltip'i birim yardımı; `OpStatus.unit` | `tools/PullTool.cpp`, `tools/ValueInput.h` (`kDocumentLengthUnit`) |
| O-P1-4 | `PullTool` Q_PROPERTY `secondaryRefs` ("Döndürme ekseni: sketch:S:e3", "Kadar/Cetvel hedefi: …"); değişince `operationChanged` yeniden yayılır (bkz. §2) | `tools/PullTool.*` |

## 2. Sapmalar ve kararlar

1. **İkincil referanslar OpStatus'ta alan değil.** `OpStatus` donmuş (state, toolId, reason, valueText, unit).
   İkincil referanslar aracın `secondaryRefs` özelliğidir; değiştiğinde aynı `OpStatus` yeniden yayılır, dinleyici
   (WP-S Seçim paneli) özelliği yeniden okur. Alan istenirse `CONTRACT_REQUESTS.md` "SC23:" ile.
2. **Başarısız özellik kuralı yalnız YENİ hatalara.** Belgenin mevcut modelinde zaten hatalı olan özellikler yeni
   değişiklikleri engellemez (yoksa hatalı bir belgede hiçbir şey düzeltilemezdi). `deleteFeature`'da kullanıcının
   onayladığı bağımlılar ("bağlı özellikler hata verebilir") hata verebilir (`sc23.acceptFailed`); başka her hata
   silmeyi reddeder. Bastırma (`toggleSuppress`) için istisna yok.
3. **Hata nedeninin araca iletimi** `IDesignHost`'ta API olmadığından `MainWindow`'un dinamik özelliği
   `sc23.commitError` ile (her `tryCommit` başında temizlenir). Bir API istenirse "SC23:" isteği.
4. **Düğme mi D mi** ayrımı `finishSketch` içinde `sender() == actFinishSketch_` ile (MainWindow.h'ye üye
   eklemeden). WP-S Sketch sekmesine aynı `actFinishSketch_` eylemini koyarsa davranış aynı kalır; yeni bir eylem
   eklerse o eylem de `finishSketch`'i çağırmalı ya da bu karşılaştırmaya eklenmeli.
5. **Ctrl+Z önizlemede** tablodaki gibi "önce iptal, sonra undo (−1)". Yalnız 3B araçlarda; sketch araçlarının
   kendi geri alma zinciri değişmedi.
6. **Odak kaybı** = odak görünümün (ve alt öğelerinin, örn. değer alanı) dışına çıkması ya da pencerenin
   `WindowDeactivate` alması. Zamanlayıcı yeniden başlamaz; Enter uygular, Esc iptal eder.
7. **Birimler yalnız Çek değer alanında.** Sketch ölçü alanları metni kısıt ifadesi olarak sakladığı için birim
   soneki orada çevrilmez (`pc::Expression` birim bilmez, SC23-CR-3). Sketch alanında geçersiz metin yine neden
   tooltip'i + Invalid verir.
8. **Büyük değer onayından sonra** (ikinci Enter) değer normal boru hattından geçer; gerçek imalat kuralı hatası
   çıkarsa inceleme penceresi (etkileşimli kipte) açılabilir — bu "gerçek imalat kuralı" kuralının gereği.
   İlk Enter hiçbir zaman commit etmez ve modal açmaz.
9. **Yüzey/Eğri türleri türetilmiştir** (görüntü durumu, dosyada yok). Yapı yeniden kurulurken çekilmemiş
   sketch'lerin bölgeleri yeniden hesaplanır (tamamen tüketilmiş sketch'ler atlanır).
10. Doldur'un açık uç etiketi "boşluk" → "açık uç" (ortak işaretçi).

## 3. MainWindow.h

**Ekleme yok.** Gerekenler `MainWindowOps.cpp`'de serbest fonksiyon (`sc23ShowSketchSummary`, yalnız public API) ve
dinamik özelliklerle (`sc23.commitError`, `sc23.acceptFailed`) çözüldü.

## 4. Testler

* `tests/test_value_input.cpp`: 22 gtest (birimler, ifadeler/parametreler, ret nedenleri, sıfır/negatif, büyük
  değer sınırı ve ikinci Enter kapısı, OperationState).
* `tests/gui/sc2023_op_checks.inc`: P0 duman kontrolleri + T01–T09, T12 (gerçek `MainWindow`, gerçek tuş/fare
  girdisi; T05/T12'de `setAutoYes(false)` + modal izleyici). 171/171 (shell kontrolleriyle birlikte).
* `tests/gui/gui_harness_qa.cpp`: ilk "Sketch'i Bitir" sonrası araç Çek kontrolü + Seç'e dönüş (`tool.select`).
* Görüntüler: `docs/sc2023/shots/wpo_01_end_sketch_pull.png`, `wpo_02_solid.png`, `wpo_03_open_profile.png`,
  `wpo_04_nested_props.png`, `wpo_05_invalid_value.png`, `wpo_07_preview.png`, `wpo_07_preview_held.png`,
  `wpo_12_failed_feature.png` (`pc_gui_harness_sc2023 --shots`).

## 5. Birleşmede dikkat

* `DesignMode.cpp::showOperationFeedback` P0 hâliyle bırakıldı (Invalid → kırmızı kalıcı, Completed/Cancelled
  nedenli → 2 s Info). `ToolManager::cancel()` (boşta Seç'te Esc) ve `onDocumentChanged()` artık etkin aracın
  durumunu yeniden yayar: "sonraki eylem" kırmızı şeridi temizler.
* `StructurePanel` ağacı 2 sütunlu (0 = ad, 1 = tür); başlık gizli. Satır metinleri (gövde adı) değişmedi.
* `ViewportOverlay` boya sırası: araç, sonra açık uç işaretleri.
