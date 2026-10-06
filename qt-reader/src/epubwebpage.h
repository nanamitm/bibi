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
    void readingPositionChanged(double position);
    // ユーザーが EPUB 内リンクをクリックして別ドキュメントへ移動しようとしたときに発火する。
    // href は "path/to/chapter.xhtml" または "path/to/chapter.xhtml#fragment" 形式。
    void navigationToHref(const QString& href);
    // EPUB 内の外部リンク（http / https / mailto）がクリックされたときに発火する。
    // 画面内では開かず、確認のうえ既定のブラウザ等に渡すかは受け手が決める。
    void externalLinkRequested(const QUrl& url);

protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override {
        const QString scheme = url.scheme();
        if (scheme == QLatin1String("epub")) {
            if (isMainFrame && type == NavigationTypeLinkClicked) {
                // 同一ドキュメント内のフラグメントジャンプはブラウザに任せる。
                if (url.path() == this->url().path())
                    return true;
                QString href = url.path().mid(1);
                if (url.hasFragment())
                    href += u'#' + url.fragment();
                emit navigationToHref(href);
                return false;
            }
            return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
        }

        // 本の外への遷移はリーダー画面内では行わない。
        if (type == NavigationTypeLinkClicked &&
            (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
             scheme == QLatin1String("mailto"))) {
            emit externalLinkRequested(url);
            return false;
        }
        // iframe の about:blank / data: / blob: などは本の中身として許可する。
        if (!isMainFrame &&
            (scheme == QLatin1String("about") || scheme == QLatin1String("data") ||
             scheme == QLatin1String("blob")))
            return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
        return false;
    }

    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& msg, int line,
                                  const QString& src) override
    {
        if (msg == "epub-nav:left")   { emit navLeft();   return; }
        if (msg == "epub-nav:right")  { emit navRight();  return; }
        if (msg == "epub-page-ready") { emit pageReady(); return; }
        if (msg.startsWith("epub-position:")) {
            bool ok = false;
            double pos = msg.mid(QStringLiteral("epub-position:").size()).toDouble(&ok);
            if (ok) emit readingPositionChanged(pos);
            return;
        }
        QWebEnginePage::javaScriptConsoleMessage(level, msg, line, src);
    }
};
