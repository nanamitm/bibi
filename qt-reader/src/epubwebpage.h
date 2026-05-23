#pragma once
#include <QWebEnginePage>

// javaScriptConsoleMessage をオーバーライドして JS→C++ 通知を受け取る。
// fetch() は EPUB の CSP に遮断される場合があるが、console.log は常に届く。
class EpubWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    using QWebEnginePage::QWebEnginePage;

signals:
    void navLeft();
    void navRight();
    void pageReady();   // img.decode 完了後に JS から通知される

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& msg, int line,
                                  const QString& src) override
    {
        if (msg == "epub-nav:left")   { emit navLeft();   return; }
        if (msg == "epub-nav:right")  { emit navRight();  return; }
        if (msg == "epub-page-ready") { emit pageReady(); return; }
        QWebEnginePage::javaScriptConsoleMessage(level, msg, line, src);
    }
};
