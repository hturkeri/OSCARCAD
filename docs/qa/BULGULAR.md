# PersistCAD — QA Bulguları (müşteri senaryosu)

Test: `tests/gui/gui_harness_qa.cpp` (hedef `pc_gui_harness_qa`, ctest `gui_harness_qa`).
Çalıştırma: `xvfb-run -a -s "-screen 0 1600x1000x24" build-qa/bin/pc_gui_harness_qa --keep /tmp/qa_out` (~100 sn, ekran görüntüleri `docs/qa/`).
Tüm adımlar yalnızca arayüz üzerinden sürülür (menü, araç çubuğu düğmeleri, fare ile sketch/viewer, parametre tablosu,
modal diyaloglar); `Document` yalnızca doğrulama için okunur.

## Özet

| Adım | İlk koşu (F'nin düzeltmelerinden önceki/ilk kod) | Son koşu (F'nin güncel kodu) |
|---|---|---|
| a) dikdörtgen + W/D/Dh ölçüleri, yeni parametre | OK (diyalogda "Yeni adlandırılmış parametre" vardı; "uygulamada göster" yoktu) | OK ("Uygulamada göster" işareti de var) |
| b) Sketch'i bitir: kısıt/ölçü/parametre/DOF 0 | OK (bu yolda veri kaybı yok) | OK |
| b3) sketch açıkken tablo düzenlemesi / parametre ekleme, Bitir'den sonra kalıyor mu | (kod okumasına göre **kaybolurdu**: `finishSketch` eski `work_` kopyasını commit ediyordu; harness'e F'nin açıklamasından sonra eklendi) | OK |
| b2) kapanan sketch 3B'de görünür | **FAIL** | OK (v6: teller çiziliyor) |
| c) sketch'i yeniden aç, çıkış yolları | OK | OK |
| d) Extrude mesafesi -> yeni parametre T | Kullanılabilir ama gizli ("+P" küçük düğme) | OK ("Yeni…" + liste kutusu) |
| d2) Fillet yarıçapı -> mevcut parametre R | Yalnızca elle yazma / tamamlayıcı | OK (liste kutusundan seçim) |
| e) W/T değişimi, undo/redo, kaydet/aç | OK | OK |
| f) Uygulama Olarak Kaydet | OK | OK |
| g) üretilen uygulama | OK | OK |

Not: İlk koşu, F kodu değiştirmeye başlamadan hemen önce alındı; harness'in kendi hataları (ağaçta çift tık, yanlış "+P"
düğmesi) giderildikten sonra senaryonun tamamı güncel kodla geçti. F'nin eski kodda **gerçekten** düzelttiği şeyleri ayrıca
ayrıştıramadım (kaynak sürüm kontrolünde değil); bu yüzden "ilk koşu" sütunu kod okumasıyla da desteklenmiştir.

## Açık bulgular (son koşu: 0 başarısız / 178 kontrol)

### B5 (DÜŞÜK, görsel) — Üretilen uygulamada W büyüyünce model görünüm alanından taşıyor
`docs/qa/08_app_after_W100.png`: W=60->100 yapılınca kamera yeniden sığdırılmıyor, parça sağ kenarda kırpılıyor (kullanıcı "model bozuldu" sanabilir).
Beklenen: ilk açılışta sığdır; parametre değişince bbox görünümden taşıyorsa otomatik `fitAll` (veya en azından taşma uyarısı). `RunnerWindow::applyResult` -> `viewer_->setShape(..., keepCamera)`.

### B6 (DÜŞÜK) — Ölçü etiketleri 3B'de yok
Kapanan sketch artık 3B'de tel olarak çiziliyor (B1 düzeltildi) ama W/D/Dh ölçü değerleri yalnız sketch'e girince görünüyor.
İsteğe bağlı iyileştirme: sketch seçiliyken 3B'de ölçü etiketleri.

### B1 (YÜKSEK) — DÜZELTİLDİ (v6, son koşuda doğrulandı) — Sketch bitince 3B görünümden tamamen kayboluyor (müşteri şikâyeti #1'in muhtemel kök nedeni)

* Tekrar üretme:
  1. Dosya > Yeni; Ekle > Sketch > XY düzlemi.
  2. Dikdörtgen çiz, "Uzunluk" ile W=60 ve D=40 (yeni parametre), daire çiz, "Çap" ile Dh=10.
  3. "Sketch'i Bitir".
* Beklenen: Profil (dikdörtgen + delik) 3B görünümde (XY düzlemi üzerinde) görünür kalır; Özellik ağacında Sketch1 seçilince
  vurgulanır; ölçüler en azından sketch seçilince ya da sketch'e girince görülebilir. Extrude edilmeden önce de kullanıcı
  çizdiğini görebilmeli.
* Gerçekte: Viewport tamamen boş (yalnız eksen üçlüsü). Çizim ve ölçü etiketleri ancak sketch'e yeniden girilince görülüyor.
  Müşteri bunu "ölçüleri verdim, sketch'i kapatınca kayboldu" diye algılıyor; oysa kısıtlar, ölçü ifadeleri (W, D, Dh, W/2,
  D/2), parametreler ve DOF=0 dokümanda **duruyor** (harness bunu doğruluyor).
* Kök neden hipotezi: `ShapeViewer` yalnız çözülmüş gövde/yüz/kenarları çiziyor; sketch özelliği gövde üretmediği için
  `MainWindow::updateViewer()` (src/designer/MainWindow.cpp) sketch'ten hiçbir görüntü üretmiyor. `ShapeViewer`'da sketch
  telleri/ölçüleri için overlay API'si yok (`setSketchPlane` yalnız ızgara). Çözüm: `ModelResult::sketchResults`/sketch
  wire'larını (veya doğrudan `Feature::sketch`'i dünya koordinatına çevirip) viewer'a çizgi overlay'i olarak vermek;
  Extrude tüketen sketch'i soluk/gizli göstermek.
* Test: `closed sketch (profile + hole) is drawn in the 3D view` (piksel sayımı). Ekran görüntüsü:
  `docs/qa/02_designer_after_sketch.png` (boş) ve `docs/qa/02_sketch_dims_reopened.png` (sketch'e girince her şey duruyor).

## Düşük öncelikli / kozmetik bulgular

### B2 — DÜZELTİLDİ ("r = R") — Özellik ağacı etiketi "Fillet1  (R R, 1 kenar)"
Yarıçap ifadesi `R` olduğunda "R " öneki ile birleşip "R R" okunuyor. `MainWindow.cpp` satır ~96
(`"  (R %1, %2 kenar)"`) -> `"  (R=%1, %2 kenar)"` daha anlaşılır olur (aynı biçim Extrude için "T, new").

### B3 — DÜZELTİLDİ — Eski durum çubuğu iletileri kalıyor
Sketch'ten çıkınca "2 öğe seçili" (sketch seçim iletisi), Fillet/Chamfer diyaloğu kapanınca "3B görünümde yüz/kenar
seçerek listeyi doldurabilirsiniz." durum çubuğunda kalıyor (`docs/qa/02_designer_after_sketch.png`,
`05_designer_extrude_fillet.png`). `setSketchMode(false)` ve diyalog kapanışında `statusBar()->clearMessage()` önerilir.

### B4 — DÜZELTİLDİ (alan önerisi artık 0.8) — Uygulamada alan başına öneri ile banner önerisi farklı (T = 0.2)
`docs/qa/09_app_violation_T.png`: alan altındaki düğme "Öneri: 0.4 mm — Uygula" (AM kuralı) gösteriyor, ama CNC kuralı için
T ≥ 0.8 gerekiyor; 0.4 uygulanırsa hata kalır, "Önerileri Uygula" ise 0.8'e çıkarıyor (belgelenmiş "en sıkı" davranış).
Alan düğmesinin de en sıkı öneriyi göstermesi tutarlı olur (`RunnerWindow::updateIndicators`).

## Giderilmiş / doğrulanmış olanlar (F'nin işi dahil)

* Ölçü diyaloğundan yeni adlandırılmış parametre (W, D, Dh) oluşturma ve "Uygulamada göster" işareti.
* Parametreler sketch içindeyken parametre tablosunda hemen görünüyor.
* Extrude mesafesi yeni parametreye ("Yeni…" -> NewParameterDialog: ad/değer/birim/min/maks/uygulamada göster) bağlanıyor;
  Fillet yarıçapı mevcut parametreye liste kutusundan bağlanıyor; ağaçta "Extrude1 (T, new)".
* W/T değişimi bbox'ı doğru değiştiriyor (80x40x10), undo/redo, .pcad kaydet/aç sonrası her şey kalıcı.
* Save-as-App diyaloğu W, D, Dh, T, R'yi listeliyor (işaretler, etiket/grup/min/maks korunuyor); üretilen uygulamada her
  parametre için doğru etiket/aralık/grup, W değişince model yeniden üretiliyor, T=0.2 -> banner + öneri -> uygun, STL bbox doğru.

## Test altyapısı notları (hata değil)

* `QTest::mouseDClick` bir öğe görünümünde (QTreeWidget) önce aynı öğeye bir basış olmadan çift tık üretmiyor
  (`QAbstractItemView::pressedIndex`); harness önce tek tık gönderiyor.
* Tamamlayıcı (completer) açıkken Esc diyaloğu kapatır; harness popup'ı gizliyor.
