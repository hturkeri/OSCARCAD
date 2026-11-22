# Yüz / gövde renkleri (SC-COLOR)

SpaceClaim'deki gibi yüzlere ve gövdelere renk verme. Yalnızca görünüm durumudur: geometriyi, hacmi, imalat
denetimini değiştirmez, yeniden üretim (regeneration) tetiklemez, tek adımda geri alınır.

## Kullanım
* Görüntü sekmesi > **Stil** grubu: **Renk** düğmesi (palet: 5x8 SpaceClaim benzeri örnek, **Diğer renkler…**
  = `QColorDialog`, native değil, **Rengi sıfırla**) ve küçük **Rengi sıfırla** düğmesi. Seçili yüzlere yüz rengi,
  seçili gövdelere gövde rengi uygular.
* Viewport sağ tık > **Renk ▸** (aynı palet, hedef = seçim). Özellikler paneli: yüz / gövde için **Renk** satırı
  (palet menülü düğme; yüz gövdeden miras alıyorsa "(gövdeden)" yazar).
* Gövde rengi gövdenin tüm yüzlerine uygulanır, yüzün kendi rengi varsa o kazanır. Gövde rengi vermek yüz
  renklerini silmez; **Rengi sıfırla** gövdede gövde + yüz renklerini siler, yüzde yalnız yüz rengini (gövde rengine döner).
* Seçim / hover vurgusu renkli yüzde de görünür kalır: vurgu rengi yüzün rengine yakınsa (RGB uzaklığı < 120)
  sarı / mavi / turuncu / siyah / beyaz alternatiflerinden en uzak olanı kullanılır (`ShapeViewer::contrastHighlight`).

## Veri modeli
`pc::ViewState::colors` (`src/core/pc/Document.h`): renk başına bir `NamedSelection` benzeri grup
(`color` = "#rrggbb", üyeler = Face ve Body kalıcı referansları). .pcad `"view"."colors"`:
`[{"color":"#ff0000","members":[{kind,ref,sig,...}]}]`; boşsa anahtar yazılmaz (eski dosyalar aynı kalır).
`pc::model::resolveViewColors` (`src/model/ViewColors.cpp`) gizli yüzlerle aynı sağlamlıkta çözer
(`pc::model::resolve`): bölünen yüzün tüm parçaları rengi korur, tüketilen yüz sessizce düşer (belgede kalır),
gövde üyesi (bölünen gövde parçaları dahil) tüm yüzlerini boyar; önce gövde, sonra yüz geçişi.
Gövde yeniden adlandırma renk üyelerini de günceller.

## STEP / Save-as-App
* `exportSTEPNamed`: kullanıcı rengi (yüz / gövde) adlandırılmış seçim rengini ezer; gövde rengi solid etiketine
  ve her yüze yazılır (adlandırılmış seçimi olmayan yüzler için de alt-şekil etiketi oluşturulur). OCCT saf
  kırmızı/yeşil/mavi/sarı... renkleri `DRAUGHTING_PRE_DEFINED_COLOUR('red')` olarak yazar.
* Save-as-App: `MainWindow::appPayloadDocument()` gizle/saydam durumunu temizler ama `view.colors` taşır; runner
  (`RunnerWindow::applyVisibility`) `resolveViewColors` ile aynı renkleri gösterir (yüz + gövde).

## Test
`tests/gui/gui_harness_colors.cpp` (`ctest -R gui_harness_colors`, `--shots` -> `docs/colors/shots/*.png`): kırmızı yüz /
mavi gövde piksel kontrolü, yüz override, vurgu görünürlüğü, bağlam menüsü, Özellikler, undo/redo, bölünen yüz (L=100) tüm
parçalar kırmızı, Diğer renkler (non-native), sıfırlama, kaydet/aç, STEP renkleri, Save-as-App JSON + runner pikselleri.

## Değişen / oluşturulan dosyalar (TAM liste)
Yeni:
* `src/designer/MainWindowColors.cpp` (tüm UI + komut; designer kaynakları glob ile alınır, CMake düzenlemesi gerekmez)
* `src/model/ViewColors.cpp`, `src/model/pc/ViewColors.h`
* `tests/gui/gui_harness_colors.cpp`
* `docs/colors/COLORS.md`, `docs/colors/shots/col_0[1-8]_*.png`

Değişen (paylaşılan dosyalardaki küçük kancalar `// SC-COLOR` ile işaretli):
* `src/core/pc/Document.h` — `ViewState::colors` + `empty()` (yeni alan)
* `src/core/Document.cpp` — "view"."colors" yaz / oku
* `src/model/CMakeLists.txt` — `ViewColors.cpp` kaynak listesine eklendi (tek satır)
* `src/model/NamedSelections.cpp` — `exportSTEPNamed`: kullanıcı yüz/gövde renkleri (+ `#include "pc/ViewColors.h"`)
* `src/viewer/ShapeViewer.cpp` — `contrastHighlight` + `drawFaceRange` yüz başına kontrast (yalnız çizim)
* `src/designer/MainWindow.h` — SC-COLOR bildirimleri (friend ColorCommand, üyeler, public API)
* `src/designer/MainWindow.cpp` — `saveAsApp`: `Document copy = appPayloadDocument();` (2 satırın yerine 1)
* `src/designer/MainWindowVis.cpp` — `visUpdate()` sonuna `colorsUpdate();`
* `src/designer/MainWindowV2.cpp` — `applyBodyAppearance`: `applyUserColors(colors)`; gövde yeniden adlandırmada renk grupları; bağlam menüsünde `addColorMenu(m, face);`
* `src/designer/MainWindowShell.cpp` — Stil grubu için `createColorRibbonGroup();`; Özellikler hook'ları `h.colorOf` / `h.colorMenu`
* `src/designer/PropertiesPanel.h/.cpp` — `Hooks::colorOf/colorMenu`, `colorRow()`, showBody/showFace'te çağrı
* `src/runner/RunnerWindow.cpp` — `applyVisibility`: kaydedilmiş renkleri uygula (+ include)
* `tests/gui/CMakeLists.txt` — dosya sonuna tek `pc_add_gui_harness(gui_harness_colors ... RUNNER ...)`

## Paylaşılan kanca farkları (özet)
```
Document.h        + std::vector<NamedSelection> colors;   bool empty() { ... && colors.empty() }
Document.cpp      + jv["colors"] = [{color, members}]  /  read back (invalid colours skipped)
model/CMakeLists  + ViewColors.cpp
NamedSelections.cpp exportSTEPNamed: viewColors = resolveViewColors(doc, r); face hex = viewColors.face[f] else selection colour;
                    body label colour; sub-shape created if name OR colour
ShapeViewer.cpp   + static contrastHighlight(hl, faceColor); drawFaceRange picks it per face
MainWindowVis.cpp visUpdate(): + colorsUpdate();
MainWindowV2.cpp  applyBodyAppearance(): + applyUserColors(colors);  (before the named-selection tint)
                  renameBody: viewSels also contains cand.view.colors groups;  contextMenu: + addColorMenu(m, face);
MainWindowShell.cpp + createColorRibbonGroup();  + h.colorOf / h.colorMenu
MainWindow.cpp    saveAsApp: Document copy = appPayloadDocument();
RunnerWindow.cpp  applyVisibility(): colors[f] = view colour (resolveViewColors(def_.document, m))
tests/gui/CMakeLists.txt  + pc_add_gui_harness(gui_harness_colors ...)
```
## Sınırlamalar
Yapı ağacındaki "Gövdeler" listesi ve runner gövde listesi swatch'ı hâlâ otomatik gövde paletini gösterir (3B görünüm
kullanıcı rengini gösterir). Gizli yüz / saydam durumu için olan Ctrl+H vb. ile birlikte çalışır; renk komutları
yalnız Face / Body öğeleri üzerinde etkilidir.
