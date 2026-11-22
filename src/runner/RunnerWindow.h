#pragma once
// persistcad_runner main window: generated parameter inputs + 3D view + manufacturability panel.
#include "RunnerCore.h"
#include "pc/ShapeViewer.h"
#include "pc/SectionControls.h"
#include <set>
#include <string>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>
#include <memory>

class QLabel;
class QListWidget;
class QPushButton;
class QTabBar;
class QFrame;
class QDoubleSpinBox;
class QSpinBox;
class QSlider;
class QToolButton;
class QAction;
class QCheckBox;
class QComboBox;
class QGroupBox;

class QScrollArea;

namespace pc::runner {

class RunnerWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit RunnerWindow(pc::app::AppDefinition def, QWidget* parent = nullptr);
  ~RunnerWindow() override;

  // Values from --set: exposed ones go to the inputs, others become hidden overrides.
  // Returns false (and fills err) for unknown names.
  bool setCliValues(const std::map<std::string, double>& v, QString* err);
  // Start first validation (defaults first, then CLI values if any). `settled()` fires when done.
  void startup();
  QImage screenshot();
  void showResults();            // scroll the parameter panel to the "Hesaplanan değerler" box (also --show-results)
  void setShowPlanes(bool on);   // v2: datum plane overlay (also --show-planes)
  // v4: section view controls (toolbar "Kesit" + preset + offset; also --section) and per-body transparency
  pc::viewer::SectionControls* sectionControls() const { return section_; }
  pc::viewer::ShapeViewer* viewer() const { return viewer_; }
  void setBodyTransparent(const QString& body, bool on);

signals:
  void settled();

private:
  struct Row;
  void buildUi();
  void buildParamPanel(QWidget* host);
  void rowChanged(int idx);
  void scheduleUpdate();
  void requestValidation(bool defaultsOnly);
  void startJob();
  void onJobFinished();
  void applyResult(const pc::mfg::ValidationResult& r);
  void updateIndicators(const pc::mfg::ValidationResult& r);
  void refreshIssueList();
  void updateInfo();
  void updateResults(const pc::ParamValues& values);   // "Hesaplanan değerler": labelled derived parameters
  void updateBodies();        // v2: body list, colours, datum planes
  void applyVisibility();     // v2: hidden bodies -> viewer, datum plane toggle
  void applySuggestionsAll();
  void applySuggestion(int rowIdx);
  void resetAll();
  void exportShape(bool step);
  void about();
  std::map<std::string, double> currentValues() const;
  void setRowValue(int idx, double v, bool block);

  pc::app::AppDefinition def_;
  std::shared_ptr<const pc::app::AppDefinition> defShared_;
  std::vector<std::unique_ptr<Row>> rows_;
  std::map<std::string, double> extra_;       // hidden overrides (non-exposed --set)
  std::map<std::string, double> pendingCli_;

  pc::viewer::ShapeViewer* viewer_ = nullptr;
  QListWidget* issueList_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QLabel* summary_ = nullptr;
  QLabel* infoLbl_ = nullptr;
  QGroupBox* resultsBox_ = nullptr;
  QScrollArea* paramScroll_ = nullptr;
  QLabel* resultsLbl_ = nullptr;              // read-only results (labelled, non-exposed parameters)
  std::vector<std::string> results_;
  QLabel* status_ = nullptr;
  QFrame* banner_ = nullptr;
  QLabel* bannerText_ = nullptr;
  QPushButton* bannerApply_ = nullptr;
  QPushButton* updateBtn_ = nullptr;
  QLabel* dirtyLbl_ = nullptr;
  QAction *actSTL_ = nullptr, *actSTEP_ = nullptr;
  QTimer debounce_;
  // v2 multi-body
  QGroupBox* bodyBox_ = nullptr;
  QListWidget* bodyList_ = nullptr;
  QCheckBox* planesChk_ = nullptr;
  QComboBox* bodyFilter_ = nullptr;
  bool updatingBodies_ = false;
  pc::viewer::SectionControls* section_ = nullptr;   // v4
  std::set<std::string> transBodies_;                // v4: translucent bodies (by name)

  QFutureWatcher<pc::mfg::ValidationResult> watcher_;
  bool running_ = false, pending_ = false;
  int gen_ = 0, jobGen_ = 0;
  bool defaultsOnly_ = false, latestDefaultsOnly_ = false;
  std::map<std::string, std::string> latest_;

  pc::mfg::ValidationResult last_;            // last validation result (maybe not shown)
  pc::mfg::ValidationResult shown_;           // last accepted/shown model
  bool hasShown_ = false;
  double fitBox_[6] = {0, 0, 0, 0, 0, 0};   // bbox the camera was last fitted to
  bool hasFitBox_ = false;
  bool waitingSettled_ = false;
  QStringList nothing_;
};

} // namespace pc::runner
