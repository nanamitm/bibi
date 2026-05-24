#pragma once
#include <QMainWindow>
#include <QFuture>
#include <QTimer>
#include <QStringList>
#include <functional>
#include "epubreader.h"
#include "bookmarkmanager.h"
#include "epubwebpage.h"

class QWebEngineView;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QSplitter;
class QLabel;
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

    void loadRecentFiles();
    void saveRecentFiles() const;
    void addRecentFile(const QString& filePath);
    void removeRecentFile(const QString& filePath);
    void updateRecentFilesMenu();
    void openRecentFile(const QString& filePath);

    void updateNavigationActions();
    void updateWindowTitle();
    void updateStatus();

    void applyCtrlWheelZoom(int delta);
    void applyZoomToViews();

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
    QTabWidget*      m_sideTabs;
    QTreeWidget*     m_bookmarkTree;
    QSplitter*       m_splitter;
    QLabel*          m_statusLabel;

    QAction* m_leftAct;
    QAction* m_rightAct;
    QAction* m_bookmarkAct;
    QMenu*   m_recentFilesMenu = nullptr;
    QAction* m_clearRecentFilesAct = nullptr;
    QList<QAction*> m_recentFileActs;
    QStringList m_recentFiles;

    int    m_currentChapter  = -1;
    bool   m_isRtl           = false;
    double m_zoomFactor      = 1.0;
    bool m_scrollToEnd     = false;
    QFuture<void> m_prefetchFuture;

    QTimer* m_swapCheckTimer = nullptr;
    int     m_swapCheckCount = 0;
    static constexpr int kMaxSwapChecks = 32; // 32 × 16ms ≒ 500ms タイムアウト

    std::function<void()> m_postSwapAction;
};
