#include "pc/SectionControls.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

namespace pc::viewer {

using Preset = ShapeViewer::SectionPreset;

SectionControls::SectionControls(ShapeViewer* viewer, bool overlay, QWidget* parent)
    : QWidget(overlay ? viewer : parent), viewer_(viewer), overlay_(overlay) {
  setObjectName("sectionPanel");
  toggle_ = new QAction("Kesit", this);
  toggle_->setObjectName("sectionToggle");
  toggle_->setCheckable(true);
  toggle_->setToolTip("Kesit görünümü: modeli bir düzlemle keserek iç geçitleri gösterir (model değişmez)");

  auto* lay = new QHBoxLayout(this);
  lay->setContentsMargins(overlay ? 10 : 4, overlay ? 6 : 0, overlay ? 10 : 4, overlay ? 6 : 0);
  lay->setSpacing(8);
  if (overlay) {
    auto* title = new QLabel("Kesit");
    title->setObjectName("sectionTitle");
    lay->addWidget(title);
  }
  combo_ = new QComboBox;
  combo_->setObjectName("sectionPreset");
  combo_->addItem("XZ düzlemi", int(Preset::XZ));
  combo_->addItem("YZ düzlemi", int(Preset::YZ));
  combo_->addItem("XY düzlemi", int(Preset::XY));
  combo_->addItem("Çeyrek kesit", int(Preset::Quarter));
  combo_->setToolTip("Kesit düzlemi");
  lay->addWidget(combo_);
  slider_ = new QSlider(Qt::Horizontal);
  slider_->setObjectName("sectionOffset");
  slider_->setRange(-100, 100);
  slider_->setValue(0);
  slider_->setPageStep(10);
  slider_->setFixedWidth(150);
  slider_->setToolTip("Kesit düzleminin konumu (modelin sınır kutusu içinde)");
  lay->addWidget(slider_);
  label_ = new QLabel;
  label_->setObjectName("sectionLabel");
  label_->setMinimumWidth(92);
  lay->addWidget(label_);
  auto* center = new QToolButton;
  center->setObjectName("sectionCenter");
  center->setText("Ortala");
  center->setAutoRaise(true);
  center->setToolTip("Düzlemi modelin ortasına al");
  lay->addWidget(center);
  flip_ = new QCheckBox("Ters çevir");
  flip_->setObjectName("sectionFlip");
  flip_->setToolTip("Diğer tarafı göster");
  lay->addWidget(flip_);

  if (overlay) {
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    setStyleSheet(
        "#sectionPanel { background: rgba(255,255,255,235); border: 1px solid #b9c4d3; border-radius: 6px; }"
        "#sectionTitle { font-weight: 600; color: #26466e; }");
    viewer->installEventFilter(this);
    hide();
  } else {
    setEnabled(false);
  }

  connect(toggle_, &QAction::toggled, this, [this] { apply(); });
  connect(combo_, &QComboBox::currentIndexChanged, this, [this] {
    if (preset() == Preset::Quarter) { QSignalBlocker b(slider_); slider_->setValue(0); }
    apply();
  });
  connect(slider_, &QSlider::valueChanged, this, [this] { apply(); });
  connect(flip_, &QCheckBox::toggled, this, [this] { apply(); });
  connect(center, &QToolButton::clicked, this, [this] { slider_->setValue(0); });
  connect(viewer_, &ShapeViewer::sectionChanged, this, [this] { updateLabel(); });
  updateLabel();
}

bool SectionControls::sectionOn() const { return toggle_->isChecked(); }
Preset SectionControls::preset() const { return Preset(combo_->currentData().toInt()); }
int SectionControls::offsetPercent() const { return slider_->value(); }
bool SectionControls::flipped() const { return flip_->isChecked(); }

void SectionControls::setSectionOn(bool on) { toggle_->setChecked(on); }
void SectionControls::setPreset(Preset p) {
  if (p == Preset::None) { setSectionOn(false); return; }
  const int i = combo_->findData(int(p));
  if (i >= 0) combo_->setCurrentIndex(i);
}
void SectionControls::setOffsetPercent(int percent) { slider_->setValue(percent); }
void SectionControls::setFlipped(bool on) { flip_->setChecked(on); }

bool SectionControls::applySpec(const QString& spec) {
  bool ok = false;
  const Preset p = ShapeViewer::sectionPresetFromString(spec, &ok);
  if (!ok) return false;
  if (p == Preset::None) { setSectionOn(false); return true; }
  setPreset(p);
  setSectionOn(true);
  return true;
}

void SectionControls::apply() {
  const bool on = sectionOn();
  const bool quarter = preset() == Preset::Quarter;
  slider_->setEnabled(!quarter);
  if (overlay_) {
    setVisible(on);
    if (on) { adjustSize(); reposition(); raise(); }
  } else {
    setEnabled(on);
  }
  if (on) viewer_->setSectionPreset(preset(), slider_->value() / 100.0, flip_->isChecked());
  else viewer_->setSectionPreset(Preset::None);
  updateLabel();
  emit changed();
}

void SectionControls::updateLabel() {
  QString t;
  if (sectionOn() && viewer_->sectionEnabled()) {
    const double v = viewer_->sectionPosition();
    switch (preset()) {
      case Preset::XZ: t = QString("Y = %1 mm").arg(v, 0, 'f', 2); break;
      case Preset::YZ: t = QString("X = %1 mm").arg(v, 0, 'f', 2); break;
      case Preset::XY: t = QString("Z = %1 mm").arg(v, 0, 'f', 2); break;
      default: t = "merkez"; break;
    }
  } else {
    t = QString::fromUtf8("—");
  }
  label_->setText(t);
}

void SectionControls::reposition() {
  if (!overlay_ || !viewer_) return;
  const QSize s = sizeHint();
  resize(s);
  move(std::max(8, viewer_->width() - s.width() - 12), 12);
}

bool SectionControls::eventFilter(QObject* o, QEvent* e) {
  if (o == viewer_ && e->type() == QEvent::Resize && isVisible()) reposition();
  return QWidget::eventFilter(o, e);
}

} // namespace pc::viewer
