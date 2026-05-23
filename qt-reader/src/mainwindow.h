#pragma once
#include <QMainWindow>
#include <QFuture>
#include <QTimer>
#include <functional>
#include "epubreader.h"
#include "bookmarkmanager.h"
#include "epubwebpage.h"

class QWebEngineView;
class QTreeWidget;
class QTreeWidgetItem;
class QSplitter;
class QLabel;
class EpubUrlScheme;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void openEpub(const QString& filePath);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi();
    void setupMenuBar();
    void setupToolBar();

    void handleLoadFinished(QWebEngineView* view, bool ok);
    void scheduleSwap();
    void trySwap();
    void performSwap();

    void goToChapter(int index);
    void goToHref(const QString& href);
    void nextChapter(bool scrollToEnd = false);
    void prevChapter(bool scrollToEnd = false);
    void onLeftKey();
    void onRightKey();
    void onNavLeft();
    void onNavRight();
    void addBookmark();
    void manageBookmarks();
    void showSearch();

    void populateToc(const QList<NavPoint>& pts, QTreeWidgetItem* parent = nullptr);
    void onTocItemClicked(QTreeWidgetItem* item, int column);

    void updateNavigationActions();
    void updateWindowTitle();

    int hrefToChapterIndex(const QString& hrefNoFrag) const;

    EpubReader*      m_reader;
    BookmarkManager* m_bookmarkManager;
    EpubUrlScheme*   m_urlScheme;

    // ダブルバッファ: m_activeView が表示中、m_standbyView がバックグラウンド読み込み
    QWidget*         m_viewContainer  = nullptr;
    QWebEngineView*  m_viewA          = nullptr;
    QWebEngineView*  m_viewB          = nullptr;
    EpubWebPage*     m_pageA          = nullptr;
    EpubWebPage*     m_pageB          = nullptr;
    QWebEngineView*  m_activeView     = nullptr;   // 現在表示中
    QWebEngineView*  m_standbyView    = nullptr;   // バックグラウンド読み込み中
    EpubWebPage*     m_activePage     = nullptr;
    EpubWebPage*     m_standbyPage    = nullptr;

    QTreeWidget*     m_tocTree;
    QSplitter*       m_splitter;
    QLabel*          m_statusLabel;

    QAction* m_leftAct;
    QAction* m_rightAct;
    QAction* m_bookmarkAct;

    int  m_currentChapter  = -1;
    bool m_isRtl           = false;
    bool m_scrollToEnd     = false;
    QFuture<void> m_prefetchFuture;

    QTimer* m_swapCheckTimer = nullptr;
    int     m_swapCheckCount = 0;
    static constexpr int kMaxSwapChecks = 32; // 32 × 16ms ≒ 500ms タイムアウト

    // performSwap() 完了後に一度だけ呼ばれるコールバック
    std::function<void()> m_postSwapAction;
};
