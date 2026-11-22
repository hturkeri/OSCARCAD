// P0 (frozen): Turkish UI labels for enums (wizard). String names for JSON live in Enums.h.
#include "pc/drawing/Enums.h"

namespace pc::drawing {

const char* labelTr(ViewRole r) {
  switch (r) {
    case ViewRole::Unknown: return "Bilinmiyor";
    case ViewRole::Front: return "Ön";
    case ViewRole::Top: return "Üst";
    case ViewRole::Bottom: return "Alt";
    case ViewRole::Left: return "Sol";
    case ViewRole::Right: return "Sağ";
    case ViewRole::Rear: return "Arka";
    case ViewRole::Section: return "Kesit";
    case ViewRole::Detail: return "Detay";
    case ViewRole::Auxiliary: return "Yardımcı";
    case ViewRole::Isometric: return "İzometrik";
    case ViewRole::Ignore: return "Yok say";
  }
  return "?";
}

const char* labelTr(LineRole r) {
  switch (r) {
    case LineRole::Unknown: return "Bilinmiyor";
    case LineRole::Visible: return "Görünür";
    case LineRole::Hidden: return "Gizli";
    case LineRole::Center: return "Eksen";
    case LineRole::Dimension: return "Ölçü";
    case LineRole::Extension: return "Uzatma";
    case LineRole::Leader: return "Kılavuz";
    case LineRole::Hatch: return "Tarama";
    case LineRole::Cutting: return "Kesit düzlemi";
    case LineRole::Thread: return "Diş";
    case LineRole::Border: return "Çerçeve";
    case LineRole::TitleBlock: return "Antet";
    case LineRole::Symbol: return "Sembol";
    case LineRole::Phantom: return "Hayali";
  }
  return "?";
}

const char* labelTr(Strategy s) {
  switch (s) {
    case Strategy::Revolve: return "Döndürme";
    case Strategy::ExtrudeTwoView: return "İki görünüşten çekme";
    case Strategy::ExtrudeSingleView: return "Tek görünüş + kalınlık";
    case Strategy::SectionRevolve: return "Kesitten döndürme";
    case Strategy::MultiFeature: return "Çok özellikli";
    case Strategy::SketchOnly: return "Yalnız sketch";
  }
  return "?";
}

const char* labelTr(Stage s) {
  switch (s) {
    case Stage::Ingest: return "Okuma";
    case Stage::Normalize: return "Normalizasyon";
    case Stage::Layout: return "Sayfa düzeni";
    case Stage::Annotate: return "Çizgi rolleri ve ölçüler";
    case Stage::Views: return "Görünüşler";
    case Stage::Sketch: return "Sketch";
    case Stage::Synthesize: return "3B";
  }
  return "?";
}

}  // namespace pc::drawing
