#pragma once
#include "pc/MfgChecks.h"
#include "pc/MfgSettings.h"
#include <QDialog>
#include <QString>

namespace pcd {

QString severityText(pc::mfg::Severity s);
QString domainText(pc::mfg::Domain d);

// Editor for pc::MfgSettings with process presets.
class MfgSettingsDialog : public QDialog {
  Q_OBJECT
public:
  explicit MfgSettingsDialog(const pc::MfgSettings& s, QWidget* parent = nullptr);
  pc::MfgSettings settings() const;
  ~MfgSettingsDialog() override;
private:
  struct Impl;
  Impl* d;
};

// Modal review shown when a change produces Error-level manufacturing issues.
class ChangeReviewDialog : public QDialog {
  Q_OBJECT
public:
  enum Result { Cancel = 0, ApplySuggestions = 1, ForceApply = 2 };
  ChangeReviewDialog(const pc::mfg::Report& r, const QString& what, QWidget* parent = nullptr);
  Result choice() const { return choice_; }
private:
  Result choice_ = Cancel;
};

} // namespace pcd
