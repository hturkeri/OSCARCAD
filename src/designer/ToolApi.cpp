// SC-UX contract: SelectionModel + display helpers (WP-A). See ToolApi.h.
#include "ToolApi.h"

namespace pcd {

QString selKindText(SelItem::Kind k, int count) {
  (void)count;   // Turkish nouns after a number stay singular ("3 kenar")
  switch (k) {
    case SelItem::Kind::Face: return "yüz";
    case SelItem::Kind::Edge: return "kenar";
    case SelItem::Kind::Body: return "gövde";
    case SelItem::Kind::Datum: return "düzlem";
    case SelItem::Kind::SketchRegion: return "bölge";
    case SelItem::Kind::SketchCurve: return "sketch eğrisi";
    case SelItem::Kind::SketchPoint: return "sketch noktası";
    case SelItem::Kind::Sketch: return "sketch";
    case SelItem::Kind::Feature: return "özellik";
    case SelItem::Kind::Parameter: return "parametre";
  }
  return "öğe";
}

QString selectionSummary(const QList<SelItem>& items) {
  if (items.isEmpty()) return QString();
  bool sameKind = true;
  for (const auto& it : items) sameKind = sameKind && it.kind == items.front().kind;
  const QString noun = sameKind ? selKindText(items.front().kind, int(items.size())) : QString("öğe");
  if (items.size() == 1) return QString("1 %1: %2").arg(noun, items.front().name);
  return QString("%1 %2").arg(items.size()).arg(noun);
}

QString selFilterText(SelFilter f) {
  switch (f) {
    case SelFilter::Smart: return "Akıllı";
    case SelFilter::Face: return "Yüz";
    case SelFilter::Edge: return "Kenar";
    case SelFilter::Body: return "Gövde";
    case SelFilter::Datum: return "Düzlem";
    case SelFilter::Sketch: return "Sketch";
  }
  return QString();
}

QList<SelItem> SelectionModel::ofKind(SelItem::Kind k) const {
  QList<SelItem> out;
  for (const auto& it : items_)
    if (it.kind == k) out.push_back(it);
  return out;
}

QStringList SelectionModel::namesOfKind(SelItem::Kind k) const {
  QStringList out;
  for (const auto& it : items_)
    if (it.kind == k) out.push_back(it.name);
  return out;
}

void SelectionModel::set(const QList<SelItem>& items) {
  // de-duplicate, keep order
  QList<SelItem> v;
  for (const auto& it : items)
    if (!v.contains(it)) v.push_back(it);
  if (v == items_) return;
  prev_ = items_;
  items_ = v;
  emit changed();
}

void SelectionModel::add(const SelItem& it) {
  if (items_.contains(it) || !accepts(it.kind)) return;
  prev_ = items_;
  items_.push_back(it);
  emit changed();
}

void SelectionModel::toggle(const SelItem& it) {
  if (items_.contains(it)) {
    remove(it);
    return;
  }
  add(it);
}

void SelectionModel::remove(const SelItem& it) {
  if (!items_.contains(it)) return;
  prev_ = items_;
  items_.removeAll(it);
  emit changed();
}

void SelectionModel::clear() {
  if (items_.isEmpty()) return;
  prev_ = items_;
  items_.clear();
  emit changed();
}

void SelectionModel::revert() {
  if (prev_ == items_) return;
  std::swap(prev_, items_);
  emit changed();
}

void SelectionModel::setFilter(SelFilter f) {
  if (f == filter_) return;
  filter_ = f;
  emit filterChanged(f);
}

bool SelectionModel::accepts(SelItem::Kind k) const {
  using K = SelItem::Kind;
  // Features and parameters are selected from panels (History / Groups), never filtered.
  if (k == K::Feature || k == K::Parameter) return true;
  switch (filter_) {
    case SelFilter::Smart: return true;
    case SelFilter::Face: return k == K::Face || k == K::SketchRegion;
    case SelFilter::Edge: return k == K::Edge || k == K::SketchCurve;
    case SelFilter::Body: return k == K::Body;
    case SelFilter::Datum: return k == K::Datum;
    case SelFilter::Sketch: return k == K::SketchRegion || k == K::SketchCurve || k == K::SketchPoint || k == K::Sketch;
  }
  return true;
}

} // namespace pcd
