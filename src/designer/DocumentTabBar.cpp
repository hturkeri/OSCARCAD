// SC23 WP-S (S-P1-2): document tab. See DocumentTabBar.h.
#include "DocumentTabBar.h"
#include "Icons.h"

namespace pcd {

DocumentTabBar::DocumentTabBar(QWidget* parent) : QTabBar(parent) {
  setObjectName("documentTabs");
  setDocumentMode(true);
  setDrawBase(false);
  setExpanding(false);
  setShape(QTabBar::RoundedSouth);
  setUsesScrollButtons(false);
  setElideMode(Qt::ElideNone);
  setFocusPolicy(Qt::NoFocus);
  setIconSize(QSize(14, 14));
  setStyleSheet(
      "QTabBar#documentTabs { background: transparent; }"
      "QTabBar#documentTabs::tab { background: #f8fafc; color: #1b2b40; border: 1px solid #b8c4d3; border-top: none;"
      "  padding: 2px 12px 3px 8px; margin: 0px 0px 0px 4px; border-bottom-left-radius: 3px; border-bottom-right-radius: 3px; }"
      "QTabBar#documentTabs::tab:selected { background: white; color: #0d3f7a; font-weight: 600; }");
  addTab(makeIcon("document", 14), QString());
}

void DocumentTabBar::setDocument(const QString& name, bool modified, const QString& path) {
  modified_ = modified;
  if (count() == 0) addTab(makeIcon("document", 14), QString());
  setTabText(0, name + (modified ? "*" : ""));
  setTabToolTip(0, path.isEmpty() ? QString("%1 (kaydedilmedi)%2").arg(name, modified ? " — değiştirildi" : "")
                                  : QString("%1%2").arg(path, modified ? " — değiştirildi" : ""));
}

QString DocumentTabBar::documentText() const { return count() ? tabText(0) : QString(); }

} // namespace pcd
