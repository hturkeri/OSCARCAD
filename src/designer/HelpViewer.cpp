// SC23 WP-S (S-P1-6): F1 help viewer. See HelpViewer.h.
#include "HelpViewer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

namespace pcd {

namespace {
const char* kGuide = "KULLANIM_KILAVUZU.md";
}

QString HelpViewer::guidePath() {
  QStringList dirs;
  const QString env = qEnvironmentVariable("PC_DOCS_DIR");
  if (!env.isEmpty()) dirs << env;
  dirs << QFileInfo(QStringLiteral(__FILE__)).absoluteDir().absoluteFilePath("../../docs");   // source tree of this build
  for (const QString& base : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
    QDir d(base);
    for (int up = 0; up < 6; ++up) {
      dirs << d.absoluteFilePath("docs");
      if (!d.cdUp()) break;
    }
  }
  for (const QString& dir : dirs) {
    const QString p = QDir(dir).absoluteFilePath(kGuide);
    if (QFileInfo::exists(p)) return QDir::cleanPath(p);
  }
  return QString();
}

HelpViewer::HelpViewer(QWidget* parent) : QDialog(parent) {
  setObjectName("helpViewer");
  setWindowTitle("PersistCAD Yardım — Kullanım Kılavuzu (F1)");
  setModal(false);
  resize(820, 640);
  auto* v = new QVBoxLayout(this);
  auto* row = new QHBoxLayout;
  row->addWidget(new QLabel("Bul:", this));
  find_ = new QLineEdit(this);
  find_->setObjectName("helpFind");
  find_->setPlaceholderText("Araç ya da konu adı (ör. Çek, Görünümler)");
  find_->setClearButtonEnabled(true);
  row->addWidget(find_, 1);
  auto* next = new QPushButton("Sonraki", this);
  row->addWidget(next);
  v->addLayout(row);
  browser_ = new QTextBrowser(this);
  browser_->setObjectName("helpBrowser");
  browser_->setOpenExternalLinks(true);
  v->addWidget(browser_, 1);
  status_ = new QLabel(this);
  status_->setObjectName("helpStatus");
  status_->setStyleSheet("color:#5a6b80;");
  v->addWidget(status_);
  path_ = guidePath();
  QFile f(path_);
  if (!path_.isEmpty() && f.open(QIODevice::ReadOnly)) {
    browser_->setSearchPaths({QFileInfo(path_).absolutePath()});   // relative image links of the guide
    browser_->setMarkdown(QString::fromUtf8(f.readAll()));
    loaded_ = true;
    status_->setText(QDir::toNativeSeparators(path_));
  } else {
    browser_->setMarkdown(QString("# Kullanım kılavuzu bulunamadı\n\n`docs/%1` bu kurulumda yok. "
                                  "`PC_DOCS_DIR` ortam değişkeni ile klasörü gösterebilirsiniz.").arg(kGuide));
    status_->setText("Kılavuz bulunamadı");
  }
  connect(find_, &QLineEdit::returnPressed, this, [this] { if (!browser_->find(find_->text())) findText(find_->text()); });
  connect(next, &QPushButton::clicked, this, [this] { if (!browser_->find(find_->text())) findText(find_->text()); });
}

bool HelpViewer::findText(const QString& text) {
  if (text.isEmpty()) { browser_->moveCursor(QTextCursor::Start); return true; }
  browser_->moveCursor(QTextCursor::Start);
  const bool ok = browser_->find(text);
  status_->setText(ok ? QString("“%1” bulundu").arg(text) : QString("“%1” kılavuzda geçmiyor").arg(text));
  return ok;
}

HelpViewer* HelpViewer::showHelp(QWidget* parent, const QString& topic) {
  auto* hv = parent ? parent->findChild<HelpViewer*>("helpViewer", Qt::FindDirectChildrenOnly) : nullptr;
  if (!hv) hv = new HelpViewer(parent);
  hv->find_->setText(topic);
  hv->findText(topic);
  hv->show();
  hv->raise();
  return hv;
}

} // namespace pcd
