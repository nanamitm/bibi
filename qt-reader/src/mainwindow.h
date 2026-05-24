#pragma once
#include <QMainWindow>
#include <QFuture>
#include <QTimer>
#include <functional>
#include "epubreader.h"
#include "bookmarkmanager.h"
#include "epubwebpage.h"
#include "recentfiles.h"

class QWebEngineView;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QSplitter;
class QLabel;
class QProgressBar;
class QAction;
class QMenu;
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
    void loadStandbyChapter(int index, bool scrollToEnd, bool forNavigation);
    void preloadNextChapter();
    int nextReadingChapterIndex() const;
    void clearStandbyChapter();
    bool standbyChapterMatches(int index, bool scrollToEnd) const;
    void goToCover();
    void goToBookEnd();
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
    void refreshBookmarkList();
    void onBookmarkActivated(QTreeWidgetItem* item, int column);
    void showBookmarkContextMenu(const QPoint& pos);
    void showReaderContextMenu(QWebEngineView* view, const QPoint& pos);
    void jumpToBookmark(const Bookmark& bm);
    void jumpToReadingPosition(const ReadingPosition& pos);
    void saveCurrentReadingPosition();
    QString chapterLabel(int chapterIndex) const;

    void updateNavigationActions();
    void updateWindowTitle();
    void updateStatus();

    void setZoomFactor(double zoomFactor, bool saveSetting = true);
    void applyCtrlWheelZoom(int delta);
    void applyZoomToViews();

    int hrefToChapterIndex(const QString& hrefNoFrag) const;

    EpubReader*      m_reader;
    BookmarkManager* m_bookmarkManager;
    EpubUrlScheme*   m_urlScheme;

    struct BufferedChapter {
        int chapterIndex = -1;
        bool ready = false;
        bool forNavigation = false;
        bool scrollToEnd = false;
    };

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
    QTabWidget*      m_sideTabs;
    QTreeWidget*     m_bookmarkTree;
    QSplitter*       m_splitter;
    QLabel*          m_statusLabel;
    QProgressBar*    m_progressBar;

    QAction* m_leftAct;
    QAction* m_rightAct;
    QAction* m_coverAct;
    QAction* m_endAct;
    QAction* m_bookmarkAct;
    RecentFiles* m_recentFiles = nullptr;

    int    m_currentChapter  = -1;
    bool   m_isRtl           = false;
    double m_zoomFactor      = 1.0;
    double m_currentScrollPosition = 0.0;
    bool m_scrollToEnd     = false;
    BufferedChapter m_standbyChapter;
    QFuture<void> m_prefetchFuture;

    QTimer* m_swapCheckTimer = nullptr;
    int     m_swapCheckCount = 0;
    static constexpr int kMaxSwapChecks = 32; // 32 × 16ms ≒ 500ms タイムアウト

    std::function<void()> m_postSwapAction;
};
