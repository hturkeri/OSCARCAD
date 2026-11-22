// SC-UX shell (WP-A): keyboard shortcut table. See Shortcuts.h.
#include "Shortcuts.h"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

namespace pcd {

const QList<ShortcutDef>& shortcutTable() {
  using C = ShortcutContext;
  static const QList<ShortcutDef> t = {
      // tools (WP-B actions, shortcuts assigned by the shell)
      {"tool.select", "S", C::Global},
      {"tool.pull", "P", C::Global},
      {"tool.move", "M", C::Global},
      {"tool.fill", "F", C::Global},
      {"tool.combine", "I", C::Global},
      {"tool.sketch.line", "L", C::Global},
      {"tool.sketch.rect", "R", C::Global},
      {"tool.sketch.circle", "C", C::Global},
      {"tool.sketch.trim", "T", C::Sketch},
      // modes
      {"actModeSketch", "K", C::Global},
      {"actModeSection", "X", C::Global},
      {"actMode3D", "D", C::Global},
      // orient
      {"actHomeView", "H", C::Global},
      {"actPlanView", "V", C::Global},
      {"actZoomExtents", "Z;Alt+Z", C::Global},
      {"actZoomIn", "Ctrl++;Ctrl+=", C::Global},
      {"actZoomOut", "Ctrl+-", C::Global},
      {"actPrevView", "Left", C::View},
      {"actNextView", "Right", C::View},
      // edit / file
      {"actDelete", "Del", C::Global},
      {"actUndo", "Ctrl+Z", C::Global},
      {"actRedo", "Ctrl+Y;Ctrl+Shift+Z", C::Global},
      {"actNew", "Ctrl+N", C::Global},
      {"actOpen", "Ctrl+O", C::Global},
      {"actSave", "Ctrl+S", C::Global},
      {"actSaveAs", "Ctrl+Shift+S", C::Global},
      {"actCreateGroup", "Ctrl+G", C::Global},
      {"actHideBody", "Ctrl+H", C::Global},
      {"actHideOthers", "Alt+H", C::Global},
      {"actShowAll", "Ctrl+Shift+H", C::Global},
      {"actCheckNow", "F5", C::Global},
      {"actSaveAsApp", "Ctrl+Shift+A", C::Global},
      {"actFileMenu", "Alt+F", C::Global},
      // SC23 WP-S: standard views (SpaceClaim 2023 R1 Views panel [G]), help, panel focus cycle
      {"actViewIso", "Ctrl+0", C::Global},
      {"actViewTrimetric", "Ctrl+1", C::Global},
      {"actViewTop", "Ctrl+2", C::Global},
      {"actViewBottom", "Ctrl+3", C::Global},
      {"actViewLeft", "Ctrl+4", C::Global},
      {"actViewRight", "Ctrl+5", C::Global},
      {"actViewFront", "Ctrl+6", C::Global},
      {"actHelp", "F1", C::Global},
      {"actFocusNext", "F6", C::Global},
      {"actFocusPrev", "Shift+F6", C::Global},
  };
  return t;
}

int applyShortcuts(QWidget* window, QWidget* viewWidget) {
  int n = 0;
  const auto actions = window->findChildren<QAction*>();
  for (const ShortcutDef& d : shortcutTable()) {
    for (QAction* a : actions) {
      if (a->objectName() != QLatin1String(d.actionObjectName)) continue;
      QList<QKeySequence> ks;
      for (const QString& k : QString::fromLatin1(d.keys).split(';', Qt::SkipEmptyParts)) ks << QKeySequence(k);
      a->setShortcuts(ks);
      if (d.ctx == ShortcutContext::View && viewWidget) {
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        if (!viewWidget->actions().contains(a)) viewWidget->addAction(a);
      } else {
        a->setShortcutContext(Qt::WindowShortcut);
        if (!window->actions().contains(a)) window->addAction(a);
      }
      // keep the key visible in the tooltip, SpaceClaim style: "Çek (Pull) — P"
      QString tip = a->toolTip();
      const QString keyText = ks.isEmpty() ? QString() : ks.first().toString(QKeySequence::NativeText);
      if (!keyText.isEmpty() && !tip.contains(QString::fromUtf8(" — ") + keyText)) a->setToolTip(tip + QString::fromUtf8(" — ") + keyText);
      ++n;
      break;
    }
  }
  return n;
}

void installUnsupportedKeys(QWidget* window, std::function<void(const QString&)> showMessage) {
  struct U { const char* keys; const char* text; };
  static const U table[] = {
      {"B", "Harmanla (Blend, B) PersistCAD'de yok."},
      {"Ctrl+C", "Kopyala (Copy, Ctrl+C) PersistCAD'de yok: Pano desteklenmez."},
      {"Ctrl+X", "Kes (Cut, Ctrl+X) PersistCAD'de yok: Pano desteklenmez."},
      {"Ctrl+V", "Yapıştır (Paste, Ctrl+V) PersistCAD'de yok: Pano desteklenmez."},
      {"Ctrl+D", "Çoğalt (Duplicate, Ctrl+D) PersistCAD'de yok."},
      {"Ctrl+Shift+C", "Yüzleri kopyala (Ctrl+Shift+C) PersistCAD'de yok."},
      {"Ctrl+Shift+X", "Kes (Ctrl+Shift+X) PersistCAD'de yok."},
      {"Ctrl+T", "Dönme merkezi koy (Ctrl+T) PersistCAD'de henüz yok (2. faz)."},
      {"Ctrl+L", "Dönme merkezini bul (Ctrl+L) PersistCAD'de henüz yok (2. faz)."},
      {"Ctrl+Q", "Dönme merkezini temizle (Ctrl+Q) PersistCAD'de henüz yok (2. faz)."},
      {"Ctrl+Shift+F", "Ctrl+Shift+F PersistCAD'de yok."},
      {"Ctrl+P", "Yazdır (Print, Ctrl+P) PersistCAD'de yok."},
      {"E", "Ölç (Measure, E) aracı PersistCAD'de henüz yok (2. faz): Ölçüm sekmesinde Kütle özellikleri var."},
      {"O", "Radyal menü (O) henüz yok (2. faz)."},
      {"U", "Kadar (Up To, U): Çek (P) aracında, çekilecek bölge / yüz seçiliyken kullanılır."},   // SC-PULLUPTO
      {"Ctrl+A", "Tümünü seç (Ctrl+A): Seç aracı etkinken 3B görünümde çalışır (önce S, görünüme tıklayın)."},
      {"Ctrl+Shift+I", "Seçimi tersine çevir (Ctrl+Shift+I) henüz yok (2. faz)."},
  };
  for (const U& u : table) {
    auto* a = new QAction(window);
    a->setObjectName(QString("unsupported.%1").arg(QLatin1String(u.keys)));
    a->setShortcut(QKeySequence(QLatin1String(u.keys)));
    a->setShortcutContext(Qt::WindowShortcut);
    const QString msg = QString::fromUtf8(u.text);
    QObject::connect(a, &QAction::triggered, window, [showMessage, msg] { showMessage(msg); });
    window->addAction(a);
  }
}

QString richToolTip(const QAction* a) {
  if (!a || a->text().isEmpty()) return QString();
  // short descriptions for commands whose tooltip is only "Ad (English)" (texts in PersistCAD terms, no invented
  // SpaceClaim behaviour)
  static const QHash<QString, const char*> desc = {
      {"tool.select", "Yüz, kenar, gövde ya da sketch öğesi seçer; Ctrl ile ekler / çıkarır."},
      {"tool.pull", "Yüz, kenar ya da sketch bölgesini çeker: bölgeye tıklayın, oku sürükleyin ya da mesafeyi yazın (Space)."},
      {"tool.fill", "Seçili yuvarlatma, pah ya da kesiği doldurup kaldırır; açık sketch uçlarını gösterir."},
      {"tool.combine", "Hedef ve kesici gövdeyi seçip birleştirir ya da keser."},
      {"tool.plane", "Seçili yüzden ya da kenardan datum düzlemi oluşturur."},
      {"tool.move", "Seçili gövdeyi ya da yüzü taşır / döndürür; tutamacı sürükleyin ya da değer yazın."},
      {"tool.shell", "Seçili yüzü kaldırıp gövdeyi et kalınlığıyla boşaltır."},
      {"tool.sketch.line", "Sketch'te çizgi çizer; iki nokta tıklayın, uzunluğu yazabilirsiniz."},
      {"tool.sketch.rect", "Sketch'te dikdörtgen çizer; iki köşe tıklayın, Tab ile ölçüler arasında geçin."},
      {"tool.sketch.circle", "Sketch'te çember çizer; merkez ve yarıçap tıklayın ya da çapı yazın."},
      {"tool.sketch.arc", "Sketch'te yay çizer."},
      {"tool.sketch.point", "Sketch'te nokta koyar (delik merkezi vb.)."},
      {"tool.sketch.construction", "Yapı çizgisi: profile katılmayan yardımcı çizgi."},
      {"tool.sketch.trim", "Kesişimler arasındaki eğri parçasını siler."},
      {"actFinishSketch", "Sketch'i belgeye işler; kapalı bölgeler yüzey olur, 3B'ye dönülür."},
      {"actCancelSketch", "Bu sketch oturumundaki değişiklikleri atar."},
      {"actUndo", "Son işlemi geri alır."},
      {"actRedo", "Geri alınan işlemi yineler."},
      {"actNew", "Yeni boş belge."},
      {"actOpen", "Bir .pcad belgesi açar."},
      {"actSave", "Belgeyi kaydeder."},
  };
  QString tip = a->toolTip().isEmpty() ? a->text() : a->toolTip();
  QString key;
  if (!a->shortcut().isEmpty()) key = a->shortcut().toString(QKeySequence::NativeText);
  const QString dash = QString::fromUtf8(" — ");
  if (int i = tip.lastIndexOf(dash); i > 0) {   // "... — P": the key part (also when the action has no shortcut)
    if (key.isEmpty()) key = tip.mid(i + dash.size()).trimmed();
    tip = tip.left(i);
  }
  QString name = tip, text;
  if (int i = tip.indexOf(QLatin1String(": ")); i > 0) { name = tip.left(i); text = tip.mid(i + 2); }
  if (text.isEmpty() && desc.contains(a->objectName())) text = QString::fromUtf8(desc.value(a->objectName()));
  QString html = QString("<b>%1</b>").arg(name.toHtmlEscaped());
  if (!key.isEmpty()) html += QString("&nbsp;&nbsp;<span style='color:#4a5b70'>(%1)</span>").arg(key.toHtmlEscaped());
  if (!text.isEmpty()) html += QString("<br>%1").arg(text.toHtmlEscaped());
  if (!a->isEnabled()) html += QString::fromUtf8("<br><span style='color:#8a5a00'>Şu an pasif.</span>");
  html += QString::fromUtf8("<br><span style='color:#6b7a8c'><i>F1: yardım</i></span>");
  return "<qt>" + html + "</qt>";
}

bool isTextInputWidget(const QWidget* w) {
  if (!w) return false;
  if (auto* le = qobject_cast<const QLineEdit*>(w)) return !le->isReadOnly();
  if (qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QTextEdit*>(w) || qobject_cast<const QPlainTextEdit*>(w)) return true;
  if (auto* cb = qobject_cast<const QComboBox*>(w)) return cb->isEditable();
  if (auto* v = qobject_cast<const QAbstractItemView*>(w)) {
    const QModelIndex i = v->currentIndex();
    return i.isValid() && (i.flags() & Qt::ItemIsEditable) && (v->editTriggers() & QAbstractItemView::AnyKeyPressed);
  }
  return false;
}

namespace {
class TextInputGuard : public QObject {
public:
  using QObject::QObject;
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() != QEvent::ShortcutOverride) return false;
    auto* ke = static_cast<QKeyEvent*>(e);
    const Qt::KeyboardModifiers m = ke->modifiers() & ~(Qt::KeypadModifier);
    if (m != Qt::NoModifier && m != Qt::ShiftModifier) return false;   // Ctrl/Alt shortcuts stay active
    const QString t = ke->text();
    const bool printable = !t.isEmpty() && t.at(0).isPrint();
    const bool editKey = ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace || ke->key() == Qt::Key_Left ||
                         ke->key() == Qt::Key_Right || ke->key() == Qt::Key_Home || ke->key() == Qt::Key_End;
    if (!printable && !editKey) return false;
    auto* w = qobject_cast<QWidget*>(o);
    if (!w || w != QApplication::focusWidget()) return false;
    if (!isTextInputWidget(w)) return false;
    e->accept();   // the key is delivered to the text widget, no shortcut fires
    return false;
  }
};
} // namespace

void installTextInputShortcutGuard(QObject* appOrWindow) {
  static QObject* installed = nullptr;
  QObject* target = QCoreApplication::instance() ? static_cast<QObject*>(QCoreApplication::instance()) : appOrWindow;
  if (installed == target) return;
  installed = target;
  target->installEventFilter(new TextInputGuard(target));
}

} // namespace pcd
