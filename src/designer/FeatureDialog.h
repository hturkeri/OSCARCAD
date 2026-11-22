#pragma once
// Property form for Extrude / Revolve / Hole / Fillet / Chamfer / Shell / Pattern features.
// Shown modeless so that faces / edges can be picked in the 3D viewer meanwhile.
#include "ExprEdit.h"
#include "pc/Document.h"
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <map>

namespace pcd {

class FeatureDialog : public QDialog {
  Q_OBJECT
public:
  // `work` is a working copy of the document (parameters may be added through the form).
  // `bodies`: names of the bodies currently in the model (targets / Combine lists).
  FeatureDialog(pc::Document* work, const pc::Feature& f, bool isNew, QWidget* parent = nullptr,
                const QStringList& bodies = QStringList());

  pc::Feature feature() const;        // result after accept()
  bool isNewFeature() const { return new_; }
  bool wantsFaces() const;            // Shell (face list) and Fillet/Chamfer (face -> all edges)
  bool wantsEdges() const;            // Fillet / Chamfer
  void addFaceRef(const QString& name);
  void addEdgeRef(const QString& name);
  QStringList refFaces() const;       // for highlighting in the viewer
  QStringList refEdges() const;

  // v2 (Plane): picked-edge / picked-face routing and live preview status.
  void setPreviewStatus(const QString& text, bool ok);
  QString planeKind() const;          // current kind of a Plane dialog ("" otherwise)
  QStringList targetBodies() const;   // checked target bodies (Extrude/Revolve/Hole/Pattern)
  void refreshParams();               // working document's parameters changed (picker / completer / live values)
  ExprEdit* exprField(const QString& key) const { auto it = exprs_.find(key.toStdString()); return it == exprs_.end() ? nullptr : it->second; }
  static pc::ParamKind kindForKey(const QString& key);

signals:
  void refsChanged();
  void changed();                     // any field edited (drives the live plane preview)

protected:
  void accept() override;

private:
  void build();
  ExprEdit* addExpr(const QString& key, const QString& label, const QString& def);
  QComboBox* addCombo(const QString& key, const QString& label, const QList<QPair<QString, QString>>& items, const QString& def);
  QCheckBox* addCheck(const QString& key, const QString& label, bool def);
  void addRefList(const QString& hint);
  void fillSketchCombo(QComboBox* cb);
  void fillAxisCombo();
  void buildPlane();
  void buildCombine();
  void buildBodyOps(bool withOp);
  QListWidget* addBodyChecklist(const QString& label, const QStringList& checked, int minH = 70);
  static QStringList checkedItems(const QListWidget* l);
  void setPlaneFields();
  void planeEdgeFilled(int slot);
  void updateOpState();

  pc::Document* work_;
  pc::Feature f_;
  bool new_;
  QFormLayout* form_ = nullptr;
  std::map<std::string, ExprEdit*> exprs_;
  std::map<std::string, QComboBox*> combos_;
  std::map<std::string, QCheckBox*> checks_;
  QListWidget* refs_ = nullptr;
  QComboBox* axisCombo_ = nullptr;
  QStringList bodies_;
  // body ops
  QListWidget* targets_ = nullptr;
  QCheckBox* keepTool_ = nullptr;
  QLineEdit* bodyName_ = nullptr;
  QComboBox* opCombo_ = nullptr;
  // Combine
  QComboBox* targetCombo_ = nullptr;
  QListWidget* tools_ = nullptr;
  QCheckBox* keepTools_ = nullptr;
  // Plane
  QComboBox* kindCombo_ = nullptr;
  QComboBox* planeAxisCombo_ = nullptr;   // Plane kind=axisAngle: world axis the plane contains
  QLineEdit* refEdit_[3] = {nullptr, nullptr, nullptr};     // edge1, edge2, face
  QPushButton* pickBtn_[3] = {nullptr, nullptr, nullptr};
  int pickTarget_ = 0;
  QLabel* previewLbl_ = nullptr;
};

} // namespace pcd
