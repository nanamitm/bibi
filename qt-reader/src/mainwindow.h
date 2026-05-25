#pragma once
#include <QMainWindow>
#include <QFuture>
#include <QTimer>
#include <QSet>
#include <functional>
#include "epubreader.h"
#include "bookmarkmanager.h"
#include "epubwebpage.h"
#include "recentfiles.h"

class QWebEngineView;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTreeView;
class QSplitter;
class QLabel;
class QProgressBar;
class QAction;
class QMenu;
class QLineEdit;
class QFileSystemModel;
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
    struct BufferedChapter {
        int chapterIndex = -1;
        bool ready = false;
        bool forNavigation = false;
        bool scrollToEnd = false;
        bool alignImageOnlyInitialRight = false;
        bool alignImageOnlyInitialLeft = false;
    };

    struct BufferedView {
        QWebEngineView* view = nullptr;
        EpubWebPage* page = nullptr;
        BufferedChapter chapter;
    };

    enum class PreloadMode {
        None,
        NextOnly,
        NextAndPrevious
    };

    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void setupSearchBar();

    void handleLoadFinished(QWebEngineView* view, bool ok);
    void scheduleSwap();
    void trySwap();
    void performSwap();

    void goToChapter(int index);
    void goToHref(const QString& href);
    void loadBufferedChapter(BufferedView& buffer, int index, bool scrollToEnd, bool forNavigation,
                             bool alignImageOnlyInitialRight = false,
                             bool alignImageOnlyInitialLeft = false);
    void refreshPreloads();
    void invalidatePreloads();
    int nextReadingChapterIndex() const;
    int previousReadingChapterIndex() const;
    bool bufferedChapterMatches(const BufferedView& buffer, int index, bool scrollToEnd) const;
    BufferedView* bufferForPage(EpubWebPage* page);
    const BufferedView* bufferForPage(EpubWebPage* page) const;
    bool scrollToEndForView(QWebEngineView* view) const;
    bool alignImageOnlyInitialRightForView(QWebEngineView* view) const;
    bool alignImageOnlyInitialLeftForView(QWebEngineView* view) const;
    void promoteBuffer(BufferedView& buffer);
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
    void exportBookmarkBackup();
    void importBookmarkBackup();
    void chooseFolderRoot();
    void onFolderFileActivated(const QModelIndex& index);
    void showSearch();
    void runSearch();
    void searchNext();
    void searchPrevious();
    void openSearchResults();
    void closeSearchBar();
    void clearSearchHighlights();
    void showSettings();
    int firstSearchResultAtOrAfterCurrentChapter() const;
    int lastSearchResultAtOrBeforeCurrentChapter() const;

    void populateToc(const QList<NavPoint>& pts, QTreeWidgetItem* parent = nullptr);
    void onTocItemClicked(QTreeWidgetItem* item, int column);
    void refreshBookmarkList();
    void onBookmarkActivated(QTreeWidgetItem* item, int column);
    void showBookmarkContextMenu(const QPoint& pos);
    void refreshSearchResultList();
    void selectSearchResultItem(int resultIndex);
    void updateSearchCountLabel();
    void onSearchResultActivated(QTreeWidgetItem* item, int column);
    void showReaderContextMenu(QWebEngineView* view, const QPoint& pos);
    void jumpToBookmark(const Bookmark& bm);
    void jumpToReadingPosition(const ReadingPosition& pos);
    void jumpToSearchResult(int chapterIndex, const QString& query, int occurrenceIndex = 0);
    void saveCurrentReadingPosition();
    QString chapterLabel(int chapterIndex) const;
    void setFolderRoot(const QString& path, bool saveSetting = true);
    void selectFolderFile(const QString& filePath);

    void updateNavigationActions();
    void updateWindowTitle();
    void updateStatus();
    void updatePageBackgroundColor();
    void setViewLoaded(QWebEngineView* view, bool loaded);

    void setZoomFactor(double zoomFactor, bool saveSetting = true);
    void setPreloadMode(PreloadMode mode, bool saveSetting = true);
    int preloadModeToIndex(PreloadMode mode) const;
    PreloadMode preloadModeFromIndex(int index) const;
    void applyCtrlWheelZoom(int delta);
    void applyZoomToViews();

    int hrefToChapterIndex(const QString& hrefNoFrag) const;

    EpubReader*      m_reader;
    BookmarkManager* m_bookmarkManager;
    EpubUrlScheme*   m_urlScheme;

    // ダブルバッファ: m_activeView が表示中、m_standby がバックグラウンド読み込み
    QWidget*         m_viewContainer  = nullptr;
    QWebEngineView*  m_viewA          = nullptr;
    QWebEngineView*  m_viewB          = nullptr;
    QWebEngineView*  m_viewC          = nullptr;
    EpubWebPage*     m_pageA          = nullptr;
    EpubWebPage*     m_pageB          = nullptr;
    EpubWebPage*     m_pageC          = nullptr;
    QWebEngineView*  m_activeView     = nullptr;   // 現在表示中
    EpubWebPage*     m_activePage     = nullptr;
    BufferedView     m_nextBuffer;
    BufferedView     m_previousBuffer;
    BufferedView*    m_swapBuffer = nullptr;
    int              m_swapFromChapter = -1;
    int              m_swapToChapter = -1;

    QTreeWidget*     m_tocTree;
    QTabWidget*      m_sideTabs;
    QTreeWidget*     m_bookmarkTree;
    QTreeWidget*     m_searchResultTree = nullptr;
    QWidget*         m_folderTab = nullptr;
    QTreeView*       m_folderTree = nullptr;
    QFileSystemModel* m_folderModel = nullptr;
    bool             m_folderRootConfigured = false;
    QSplitter*       m_splitter;
    QLabel*          m_statusLabel;
    QProgressBar*    m_progressBar;

    QAction* m_leftAct;
    QAction* m_rightAct;
    QAction* m_coverAct;
    QAction* m_endAct;
    QAction* m_bookmarkAct;
    RecentFiles* m_recentFiles = nullptr;

    QToolBar* m_searchBar = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_searchCountLabel = nullptr;
    QAction* m_searchPrevAct = nullptr;
    QAction* m_searchNextAct = nullptr;
    QAction* m_searchListAct = nullptr;
    QAction* m_searchMenuAct = nullptr;
    QList<EpubReader::SearchResult> m_searchResults;
    QString m_searchQuery;
    int m_searchIndex = -1;
    QString m_highlightedSearchQuery;
    int m_highlightedSearchChapter = -1;

    int    m_currentChapter  = -1;
    bool   m_isRtl           = false;
    double m_zoomFactor      = 1.0;
    double m_currentScrollPosition = 0.0;
    bool m_scrollToEnd     = false;
    PreloadMode m_preloadMode = PreloadMode::NextAndPrevious;
    QSet<QWebEngineView*> m_loadedViews;
    QFuture<void> m_prefetchFuture;

    QTimer* m_swapCheckTimer = nullptr;
    int     m_swapCheckCount = 0;
    static constexpr int kMaxSwapChecks = 32; // 32 × 16ms ≒ 500ms タイムアウト

    std::function<void()> m_postSwapAction;
};
