// Spike: render a PDF page to a 300-dpi grayscale PNG with QtPdf (Qt6 module, LGPLv3; PDFium inside, BSD).
#include <QGuiApplication>
#include <QPdfDocument>
#include <QImage>
#include <QPainter>
#include <cstdio>
int main(int argc,char**argv){ QGuiApplication app(argc,argv); QPdfDocument doc;
  if(doc.load(argv[1])!=QPdfDocument::Error::None){ printf("load failed\n"); return 1; }
  int page=atoi(argv[2])-1; QSizeF pt=doc.pagePointSize(page); QSize px((int)(pt.width()*300/72),(int)(pt.height()*300/72));
  QImage img=doc.render(page,px).convertToFormat(QImage::Format_Grayscale8);
  // QtPdf renders transparent background -> composite on white
  QImage white(img.size(),QImage::Format_Grayscale8); white.fill(255);
  QImage argb=doc.render(page,px); QImage out(px,QImage::Format_RGB32); out.fill(Qt::white);
  { QPainter p(&out); p.drawImage(0,0,argb); }
  out.convertToFormat(QImage::Format_Grayscale8).save(argv[3]); printf("pages=%d rendered %dx%d -> %s\n",doc.pageCount(),px.width(),px.height(),argv[3]); }
