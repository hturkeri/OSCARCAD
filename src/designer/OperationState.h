#pragma once
// PersistCAD designer — SC23 operation state (ANALYSIS §4.2). FROZEN after P0-SC23 (docs/sc2023/P0_CONTRACT.md).
// Change requests: docs/CONTRACT_REQUESTS.md, prefix "SC23:".
//
// A tool reports where its operation stands through IActiveTool::operation() / operationChanged() (ToolApi 1.1).
// The tool's own internal stages (PullTool::St, SketchEditor drawStage, ...) stay private details.
// Completed / Cancelled are momentary events: right after them the state becomes AwaitingInput or
// AwaitingSelection again.
#include <QMetaType>
#include <QString>

namespace pcd {

enum class OpState { Idle, AwaitingSelection, AwaitingInput, Preview, Invalid, Committing, Completed, Cancelled };

struct OpStatus {
  OpState state = OpState::Idle;
  QString toolId;      // IActiveTool::id(), e.g. "pull"
  QString reason;      // Invalid: why + how to fix ("Mesafe sıfır olamaz"); Completed: "Extrude1 oluşturuldu"
  QString valueText;   // current value as typed / previewed, e.g. "12.5"
  QString unit;        // display unit of valueText, e.g. "mm", "°"
  bool operator==(const OpStatus& o) const {
    return state == o.state && toolId == o.toolId && reason == o.reason && valueText == o.valueText && unit == o.unit;
  }
  bool operator!=(const OpStatus& o) const { return !(*this == o); }
};

// Short Turkish label of a state (HUD / logs / tests). Never empty.
inline QString opStateText(OpState s) {
  switch (s) {
    case OpState::Idle: return QStringLiteral("Boşta");
    case OpState::AwaitingSelection: return QStringLiteral("Seçim bekleniyor");
    case OpState::AwaitingInput: return QStringLiteral("Değer bekleniyor");
    case OpState::Preview: return QStringLiteral("Önizleme");
    case OpState::Invalid: return QStringLiteral("Geçersiz");
    case OpState::Committing: return QStringLiteral("Uygulanıyor");
    case OpState::Completed: return QStringLiteral("Tamamlandı");
    case OpState::Cancelled: return QStringLiteral("İptal edildi");
  }
  return QStringLiteral("?");
}

} // namespace pcd

Q_DECLARE_METATYPE(pcd::OpState)
Q_DECLARE_METATYPE(pcd::OpStatus)
