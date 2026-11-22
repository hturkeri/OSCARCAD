#pragma once
// WP-3 private: the "Teknik resimden içe aktar" QWizard (7 pages, SPEC §2.12). Public entry points are in
// pc/drawing/ui/ImportAction.h. Object names of the interactive widgets are stable (used by the GUI harness).
#include "pc/drawing/Pipeline.h"
#include "pc/drawing/ui/ImportAction.h"
#include <QWizard>

class QLabel;

namespace pc::drawing::ui {

class WizardPageBase;

// Shared state of all pages.
struct WizardState {
  DrawingImportHost host;
  std::string path;                 // input file
  PipelineOptions opts;
  DrawingIR ir;
  StageResult last;                 // result of the last pipeline run
};

class ImportWizard : public QWizard {
 public:
  enum PageId { PSource, PLayout, PGeometry, PDims, PSketch, P3D, PSummary };
  ImportWizard(QWidget* parent, const DrawingImportHost& host, const QString& initialFile);
  ~ImportWizard() override;

  WizardState& state() { return st_; }
  // Re-run the pipeline from `from` to S7, then refresh every page. Returns false if nothing past S3 exists.
  bool rerunFrom(Stage from);
  // Append a user edit (replacing earlier edits with the same op+key where `replaceKey` is non-empty) and re-run.
  void addEdit(const UserEdit& e, const std::string& replaceKey = {});
  const SynthesisCandidate* chosen() const;
  void refreshPages();

  void accept() override;

 private:
  WizardState st_;
  std::vector<WizardPageBase*> pages_;
};

}  // namespace pc::drawing::ui
