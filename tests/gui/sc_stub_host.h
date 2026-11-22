// SC-UX P0: minimal IDesignHost for GUI-less / shell-less tests of WP-B tools (header-only, test support only).
// Holds a Document, regenerates it on commit (no mfg validation, no dialogs), records every call so a test can
// assert what a tool asked the host to do. viewer() / sketchEditor() are null unless a test sets them.
// Owner: shared. WP-B may extend it freely (it is NOT part of the frozen ToolApi.h contract).
#pragma once
#include "ToolApi.h"
#include "pc/Expression.h"
#include <QWidget>
#include <stdexcept>

namespace pcd::test {

class StubDesignHost : public IDesignHost {
public:
  explicit StubDesignHost(pc::Document doc = pc::Document()) : doc_(std::move(doc)) { regen(); }

  // --- test-side state ---
  QStringList log;                       // "commit:<undoText>", "param:<name>=<expr>", "prompt:<text>", ...
  QString lastPrompt, lastQuickMeasure;
  TopoDS_Shape ghost;
  QStringList emphasisFaces, emphasisEdges;
  int commits = 0;
  bool refuseCommits = false;            // simulate "Vazgeç" in the review dialog
  QString nextParameterName;             // createParameterInteractive answer ("" = cancelled)
  QWidget* window = nullptr;
  pc::viewer::ShapeViewer* viewerPtr = nullptr;
  SketchEditor* sketchPtr = nullptr;
  DesignMode mode = DesignMode::ThreeD;
  std::string sketchId;
  SelectionModel sel;
  pc::Document& mutableDocument() { return doc_; }
  void setDocument(const pc::Document& d) { doc_ = d; regen(); }

  // --- IDesignHost ---
  QWidget* hostWindow() override { return window; }
  const pc::Document& document() const override { return doc_; }
  const pc::model::ModelResult& modelResult() const override { return model_; }
  int rollbackIndex() const override { return -1; }
  pc::viewer::ShapeViewer* viewer() const override { return viewerPtr; }
  SketchEditor* sketchEditor() const override { return sketchPtr; }
  SelectionModel* selection() const override { return const_cast<SelectionModel*>(&sel); }
  DesignMode designMode() const override { return mode; }
  bool setDesignMode(DesignMode m) override { mode = m; log << QString("mode:%1").arg(int(m)); return true; }
  bool beginSketch(const pc::SketchPlane&) override { mode = DesignMode::Sketch; sketchId = "SketchNew"; log << "beginSketch"; return true; }
  bool editSketch(const std::string& id) override { mode = DesignMode::Sketch; sketchId = id; log << "editSketch"; return true; }
  bool finishSketch(bool accept) override { mode = DesignMode::ThreeD; sketchId.clear(); log << QString("finishSketch:%1").arg(accept); return true; }
  std::string openSketchId() const override { return sketchId; }
  bool beginSketchPlanePick() override { log << "planePick"; return true; }
  bool openSketchFrame(double o[3], double u[3], double v[3], double n[3]) const override {
    if (sketchId.empty()) return false;
    const double O[3] = {0, 0, 0}, U[3] = {1, 0, 0}, V[3] = {0, 1, 0}, N[3] = {0, 0, 1};
    for (int i = 0; i < 3; ++i) { o[i] = O[i]; u[i] = U[i]; v[i] = V[i]; n[i] = N[i]; }
    return true;
  }
  pc::Document* expressionContext() override { return &doc_; }
  bool commitDocument(const pc::Document& cand, const QString& undoText) override {
    log << "commit:" + undoText;
    if (refuseCommits) return false;
    doc_ = cand;
    regen();
    ++commits;
    return true;
  }
  bool insertFeature(const pc::Feature& f, const QString& undoText) override {
    pc::Document c = doc_;
    c.features.push_back(f);
    return commitDocument(c, undoText);
  }
  bool changeParameter(const std::string& name, const std::string& expr) override {
    log << QString("param:%1=%2").arg(QString::fromStdString(name), QString::fromStdString(expr));
    pc::Document c = doc_;
    if (!c.params.setExpression(name, expr)) return false;
    return commitDocument(c, QString("Parametre: %1").arg(QString::fromStdString(name)));
  }
  bool deleteFeatureInteractive(const std::string& id) override {
    pc::Document c = doc_;
    const int i = c.indexOf(id);
    if (i < 0) return false;
    c.features.erase(c.features.begin() + i);
    return commitDocument(c, QString("Sil: %1").arg(QString::fromStdString(id)));
  }
  bool setFeatureSuppressed(const std::string& id, bool s) override {
    pc::Document c = doc_;
    auto* f = c.feature(id);
    if (!f) return false;
    f->suppressed = s;
    return commitDocument(c, QString("Bastır: %1").arg(QString::fromStdString(id)));
  }
  pc::model::ModelResult evaluateCandidate(const pc::Document& cand) const override { return pc::model::regenerate(cand); }
  bool evaluateExpression(const QString& expr, double* value, QString* error) const override {
    try {
      const auto vals = doc_.evaluateParams();
      const double v = pc::evalWith(expr.toStdString(), vals);
      if (value) *value = v;
      return true;
    } catch (const std::exception& e) {
      if (error) *error = QString::fromUtf8(e.what());
      return false;
    }
  }
  QString createParameterInteractive(const QString& suggestedName, pc::ParamKind kind, const QString& valueExpr,
                                     QWidget*) override {
    log << "newParam:" + suggestedName;
    if (nextParameterName.isEmpty()) return QString();
    pc::Parameter p;
    p.name = nextParameterName.toStdString();
    p.expression = valueExpr.toStdString();
    p.kind = kind;
    pc::Document c = doc_;
    if (!c.params.add(p) || !commitDocument(c, "Parametre ekle")) return QString();
    return nextParameterName;
  }
  void setGhost(const TopoDS_Shape& s) override { ghost = s; }
  void showPrompt(const QString& text, int) override { lastPrompt = text; log << "prompt:" + text; }
  void showQuickMeasure(const QString& text) override { lastQuickMeasure = text; }
  void setToolEmphasis(const QStringList& f, const QStringList& e) override { emphasisFaces = f; emphasisEdges = e; }
  void showOperationFeedback(const pcd::OpStatus& st) override { lastOperation = st; }   // [SC23]
  pcd::OpStatus lastOperation;
  void openClassicEditor(const std::string& id) override { log << "classic:" + QString::fromStdString(id); }
  bool featureDialogOpen() const override { return false; }
  void refreshViewer(bool) override { log << "refreshViewer"; }

private:
  void regen() { model_ = pc::model::regenerate(doc_); }
  pc::Document doc_;
  pc::model::ModelResult model_;
};

} // namespace pcd::test
