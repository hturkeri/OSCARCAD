// SC-UX shell (WP-A): Options panel. See OptionsPanel.h.
#include "OptionsPanel.h"
#include "ExprEdit.h"
#include "Icons.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QToolButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>

namespace pcd {

namespace {
struct Choice { QString key, label; };
QList<Choice> parseChoices(const QStringList& l) {
  QList<Choice> out;
  for (const QString& c : l) {
    const int bar = c.indexOf('|');
    out.push_back(bar < 0 ? Choice{c, c} : Choice{c.left(bar), c.mid(bar + 1)});
  }
  return out;
}
} // namespace

OptionsPanel::OptionsPanel(QWidget* parent) : QWidget(parent) {
  setObjectName("optionsPanel");
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  title_ = new QLabel("Seçenekler", this);
  title_->setObjectName("optionsTitle");
  title_->setStyleSheet("QLabel#optionsTitle { background:#e4ebf3; color:#1b2b40; font-weight:bold; padding:4px 6px; border-bottom:1px solid #c6d1de; }");
  v->addWidget(title_);
  title_->hide();   // the dock title shows "Seçenekler - <araç>" (SPEC §1.2 #10); kept for title()
  scroll_ = new QScrollArea(this);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // SpaceClaim: the panel content follows its width
  v->addWidget(scroll_, 1);
  content_ = new QWidget;
  contentLayout_ = new QVBoxLayout(content_);
  // SC23 merge (S-P1-9 + O-P1-2): dense rows like SpaceClaim's Options panel, so Options - Pull with the "Genel"
  // group fits the 1024×768 column without a scroll bar; group headers keep a little extra space above them.
  contentLayout_->setContentsMargins(8, 4, 8, 4);
  contentLayout_->setSpacing(2);
  scroll_->setWidget(content_);
}

QString OptionsPanel::title() const { return title_->text(); }
QWidget* OptionsPanel::field(const QString& key) const { return fields_.value(key, nullptr); }

QString OptionsPanel::signature(const QList<ToolOption>& opts) const {
  QString s;
  for (const auto& o : opts) s += QString("%1:%2:%3:%4;").arg(o.key).arg(int(o.type)).arg(o.choices.join(',')).arg(o.group);
  return s;
}

void OptionsPanel::clearContent() {
  fields_.clear();
  if (custom_) { custom_->hide(); custom_->deleteLater(); custom_ = nullptr; }
  while (QLayoutItem* it = contentLayout_->takeAt(0)) {
    if (QWidget* w = it->widget()) { w->hide(); w->deleteLater(); }
    delete it;
  }
}

void OptionsPanel::showCustom(const QString& toolTitle, QWidget* custom) {
  const QString t = QString("Seçenekler - %1").arg(toolTitle);
  clearContent();
  opts_.clear();
  sig_ = "custom";
  title_->setText(t);
  if (custom) {
    custom_ = custom;
    custom->setParent(content_);
    contentLayout_->addWidget(custom);
    custom->show();
  }
  contentLayout_->addStretch(1);
  emit titleChanged(t);
}

void OptionsPanel::showOptions(const QString& toolTitle, const QList<ToolOption>& opts) {
  const QString t = toolTitle.isEmpty() ? QString("Seçenekler") : QString("Seçenekler - %1").arg(toolTitle);
  const QString sig = signature(opts);
  const bool sameShape = sig == sig_ && !custom_;
  opts_ = opts;
  if (title_->text() != t) { title_->setText(t); emit titleChanged(t); }
  if (sameShape) { refreshValues(); return; }
  sig_ = sig;
  clearContent();
  build();
}

void OptionsPanel::build() {
  updating_ = true;
  QString group;
  if (opts_.isEmpty()) {
    auto* l = new QLabel("Bu araç için seçenek yok.", content_);
    l->setStyleSheet("color:#6b7a8c;");
    contentLayout_->addWidget(l);
  }
  for (const ToolOption& o : opts_) {
    if (o.group != group && !o.group.isEmpty()) {
      group = o.group;
      auto* g = new QLabel(group, content_);
      g->setStyleSheet("font-weight:bold; color:#24364d; padding-top:3px;");
      contentLayout_->addWidget(g);
    }
    QWidget* w = nullptr;
    const QString key = o.key;
    switch (o.type) {
      case ToolOption::Type::Bool: {
        auto* cb = new QCheckBox(o.label, content_);
        cb->setChecked(o.value.toBool());
        connect(cb, &QCheckBox::toggled, this, [this, key](bool on) { if (!updating_) emit optionChanged(key, on); });
        w = cb;
        break;
      }
      case ToolOption::Type::Choice: {
        const auto cs = parseChoices(o.choices);
        if (cs.size() <= 4) {   // SPEC §8: radio group up to 4 choices, else a combo box
          auto* box = new QWidget(content_);
          auto* bl = new QVBoxLayout(box);
          bl->setContentsMargins(10, 0, 0, 0);
          bl->setSpacing(1);
          if (!o.label.isEmpty() && o.label != o.group) bl->addWidget(new QLabel(o.label, box));
          auto* bg = new QButtonGroup(box);
          for (const auto& c : cs) {
            auto* rb = new QRadioButton(c.label, box);
            rb->setObjectName(QString("opt.%1.%2").arg(key, c.key));
            rb->setChecked(o.value.toString() == c.key);
            rb->setEnabled(o.enabled);
            bg->addButton(rb);
            bl->addWidget(rb);
            const QString ck = c.key;
            connect(rb, &QRadioButton::toggled, this, [this, key, ck](bool on) { if (on && !updating_) emit optionChanged(key, ck); });
          }
          w = box;
        } else {
          auto* row = new QWidget(content_);
          auto* hl = new QHBoxLayout(row);
          hl->setContentsMargins(0, 0, 0, 0);
          hl->addWidget(new QLabel(o.label, row));
          auto* combo = new QComboBox(row);
          combo->setObjectName("opt." + key + ".combo");
          for (const auto& c : cs) combo->addItem(c.label, c.key);
          combo->setCurrentIndex(std::max(0, combo->findData(o.value.toString())));
          hl->addWidget(combo, 1);
          connect(combo, &QComboBox::activated, this, [this, key, combo](int) { if (!updating_) emit optionChanged(key, combo->currentData()); });
          w = row;
        }
        break;
      }
      case ToolOption::Type::Expr: {
        auto* row = new QWidget(content_);
        auto* vl = new QVBoxLayout(row);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(1);
        vl->addWidget(new QLabel(o.label, row));
        static pc::Document fallback;
        auto* ee = new ExprEdit(exprDoc_ ? exprDoc_() : &fallback, row);
        ee->setObjectName("opt." + key + ".expr");
        ee->setText(o.value.toString());
        if (!o.paramHint.isEmpty()) ee->setParamHint(o.paramHint, o.paramKind);
        vl->addWidget(ee);
        // SC23 WP-S (S-P1-9): parameter picker + "Yeni…" on a second row, so the field fits a 240 px panel
        if (QComboBox* pick = ee->picker(); pick && ee->layout()) {
          auto* row2 = new QHBoxLayout;
          row2->setContentsMargins(0, 0, 0, 0);
          row2->setSpacing(2);
          ee->layout()->removeWidget(pick);
          pick->setParent(row);
          pick->setMinimumContentsLength(9);
          row2->addWidget(pick, 1);
          if (auto* nb = ee->findChild<QToolButton*>(QString(), Qt::FindDirectChildrenOnly)) {
            ee->layout()->removeWidget(nb);
            nb->setParent(row);
            row2->addWidget(nb);
          }
          vl->addLayout(row2);
        }
        auto emitIt = [this, key, ee] { if (!updating_ && ee->isValid()) emit optionChanged(key, ee->text()); };
        connect(ee->lineEdit(), &QLineEdit::editingFinished, this, emitIt);
        connect(ee->picker(), &QComboBox::activated, this, [emitIt](int) { QTimer::singleShot(0, emitIt); });
        w = row;
        break;
      }
      case ToolOption::Type::Text: {
        auto* row = new QWidget(content_);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(new QLabel(o.label, row));
        auto* le = new QLineEdit(o.value.toString(), row);
        le->setObjectName("opt." + key + ".text");
        hl->addWidget(le, 1);
        connect(le, &QLineEdit::editingFinished, this, [this, key, le] { if (!updating_) emit optionChanged(key, le->text()); });
        w = row;
        break;
      }
      case ToolOption::Type::Action: {
        auto* b = new QPushButton(o.icon.isEmpty() ? QIcon() : makeIcon(o.icon, 16), o.label, content_);
        connect(b, &QPushButton::clicked, this, [this, key] { emit optionChanged(key, QVariant()); });
        w = b;
        break;
      }
      case ToolOption::Type::Info: {
        auto* l = new QLabel(o.value.toString().isEmpty() ? o.label : o.value.toString(), content_);
        l->setWordWrap(true);
        l->setStyleSheet("color:#4b5b6e;");
        w = l;
        break;
      }
    }
    if (!w) continue;
    w->setObjectName("opt." + key);
    if (!o.tip.isEmpty()) w->setToolTip(o.tip);
    if (o.type != ToolOption::Type::Choice) w->setEnabled(o.enabled);
    contentLayout_->addWidget(w);
    fields_.insert(key, w);
  }
  contentLayout_->addStretch(1);
  updating_ = false;
}

void OptionsPanel::refreshValues() {
  updating_ = true;
  for (const ToolOption& o : opts_) {
    QWidget* w = fields_.value(o.key);
    if (!w) continue;
    switch (o.type) {
      case ToolOption::Type::Bool:
        if (auto* cb = qobject_cast<QCheckBox*>(w)) cb->setChecked(o.value.toBool());
        w->setEnabled(o.enabled);
        break;
      case ToolOption::Type::Choice:
        for (auto* rb : w->findChildren<QRadioButton*>()) {
          rb->setChecked(rb->objectName() == QString("opt.%1.%2").arg(o.key, o.value.toString()));
          rb->setEnabled(o.enabled);
        }
        if (auto* c = w->findChild<QComboBox*>()) c->setCurrentIndex(std::max(0, c->findData(o.value.toString())));
        break;
      case ToolOption::Type::Expr:
        if (auto* ee = w->findChild<ExprEdit*>(); ee && !ee->lineEdit()->hasFocus() && ee->text() != o.value.toString()) ee->setText(o.value.toString());
        w->setEnabled(o.enabled);
        break;
      case ToolOption::Type::Text:
        if (auto* le = w->findChild<QLineEdit*>(); le && !le->hasFocus()) le->setText(o.value.toString());
        w->setEnabled(o.enabled);
        break;
      case ToolOption::Type::Info:
        if (auto* l = qobject_cast<QLabel*>(w)) l->setText(o.value.toString().isEmpty() ? o.label : o.value.toString());
        break;
      case ToolOption::Type::Action: w->setEnabled(o.enabled); break;
    }
  }
  updating_ = false;
}

QVariant OptionsPanel::value(const QString& key) const {
  QWidget* w = fields_.value(key);
  if (!w) return {};
  if (auto* cb = qobject_cast<QCheckBox*>(w)) return cb->isChecked();
  for (auto* rb : w->findChildren<QRadioButton*>())
    if (rb->isChecked()) return rb->objectName().mid(QString("opt.%1.").arg(key).size());
  if (auto* c = w->findChild<QComboBox*>(QString("opt.%1.combo").arg(key))) return c->currentData();
  if (auto* ee = w->findChild<ExprEdit*>()) return ee->text();
  if (auto* le = w->findChild<QLineEdit*>()) return le->text();
  if (auto* l = qobject_cast<QLabel*>(w)) return l->text();
  return {};
}

} // namespace pcd
