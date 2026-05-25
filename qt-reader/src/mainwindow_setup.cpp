#include "mainwindow.h"
#include "mainwindow_private.h"
#include "epuburlscheme.h"
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QSplitter>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QLineEdit>
#include <QApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QDir>
#include <QTimer>
#include <QMessageBox>

void MainWindow::setupUi() {
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_sideTabs = new QTabWidget(m_splitter);
    m_sideTabs->setMinimumWidth(200);
    m_sideTabs->setMaximumWidth(380);

    m_tocTree = new QTreeWidget(m_sideTabs);
    m_tocTree->setHeaderLabel(tr("目次"));
    m_tocTree->setMinimumWidth(180);
    m_tocTree->setAnimated(true);

    m_bookmarkTree = new QTreeWidget(m_sideTabs);
    m_bookmarkTree->setHeaderLabels({tr("しおり"), tr("章"), tr("章内"), tr("作成日時")});
    m_bookmarkTree->setRootIsDecorated(false);
    m_bookmarkTree->setAlternatingRowColors(true);
    m_bookmarkTree->setSortingEnabled(true);
    m_bookmarkTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bookmarkTree->setTextElideMode(Qt::ElideRight);
    m_bookmarkTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_bookmarkTree->header()->setStretchLastSection(false);
    m_bookmarkTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_bookmarkTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_bookmarkTree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_bookmarkTree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_bookmarkTree->setColumnWidth(1, 86);
    m_bookmarkTree->setColumnWidth(2, 52);
    m_bookmarkTree->setColumnWidth(3, 118);

    m_searchResultTree = new QTreeWidget(m_sideTabs);
    m_searchResultTree->setHeaderLabels({tr("章"), tr("候補"), tr("本文")});
    m_searchResultTree->setRootIsDecorated(false);
    m_searchResultTree->setAlternatingRowColors(true);
    m_searchResultTree->setTextElideMode(Qt::ElideRight);
    m_searchResultTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_searchResultTree->header()->setStretchLastSection(false);
    m_searchResultTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_searchResultTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_searchResultTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_searchResultTree->setColumnWidth(0, 110);
    m_searchResultTree->setColumnWidth(1, 52);

    m_folderTab = new QWidget(m_sideTabs);
    auto* folderLayout = new QVBoxLayout(m_folderTab);
    folderLayout->setContentsMargins(4, 4, 4, 4);
    folderLayout->setSpacing(4);

    auto* chooseFolderButton = new QPushButton(tr("フォルダーを選択…"), m_folderTab);
    folderLayout->addWidget(chooseFolderButton);

    m_folderModel = new QFileSystemModel(this);
    m_folderModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_folderModel->setNameFilters({QStringLiteral("*.epub"), QStringLiteral("*.zip")});
    m_folderModel->setNameFilterDisables(false);

    m_folderTree = new QTreeView(m_folderTab);
    m_folderTree->setModel(m_folderModel);
    m_folderTree->setHeaderHidden(true);
    m_folderTree->setAnimated(true);
    m_folderTree->setSortingEnabled(true);
    m_folderTree->sortByColumn(0, Qt::AscendingOrder);
    m_folderTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int column = 1; column < m_folderModel->columnCount(); ++column)
        m_folderTree->hideColumn(column);
    folderLayout->addWidget(m_folderTree, 1);

    m_sideTabs->addTab(m_tocTree, tr("目次"));
    m_sideTabs->addTab(m_bookmarkTree, tr("しおり"));
    m_sideTabs->addTab(m_searchResultTree, tr("検索結果"));
    m_sideTabs->addTab(m_folderTab, tr("フォルダー"));

    auto* profile = new QWebEngineProfile("BibiQtReader", this);
    profile->installUrlSchemeHandler("epub", m_urlScheme);

    {
        const QStringList candidates = { "Noto Serif CJK JP", "Noto Serif JP" };
        const QStringList installed  = QFontDatabase::families();
        for (const QString& f : candidates) {
            if (installed.contains(f)) {
                auto* ws = profile->settings();
                ws->setFontFamily(QWebEngineSettings::StandardFont, f);
                ws->setFontFamily(QWebEngineSettings::SerifFont,    f);
                break;
            }
        }
    }

    m_viewContainer = new QWidget(m_splitter);
    m_viewContainer->installEventFilter(this);

    m_pageA = new EpubWebPage(profile, this);
    m_viewA = new QWebEngineView(m_viewContainer);
    m_viewA->setPage(m_pageA);

    m_pageB = new EpubWebPage(profile, this);
    m_viewB = new QWebEngineView(m_viewContainer);
    m_viewB->setPage(m_pageB);

    m_pageC = new EpubWebPage(profile, this);
    m_viewC = new QWebEngineView(m_viewContainer);
    m_viewC->setPage(m_pageC);

    m_viewA->show();
    m_viewB->show();
    m_viewC->show();
    m_viewA->raise();
    m_activeView  = m_viewA;  m_activePage  = m_pageA;
    m_nextBuffer.view = m_viewB;
    m_nextBuffer.page = m_pageB;
    m_previousBuffer.view = m_viewC;
    m_previousBuffer.page = m_pageC;
    updatePageBackgroundColor();

    for (EpubWebPage* pg : {m_pageA, m_pageB, m_pageC}) {
        connect(pg, &EpubWebPage::navigationToHref, this, [this, pg](const QString& href) {
            if (m_activePage == pg) goToHref(href);
        });
        connect(pg, &EpubWebPage::navLeft, this, [this, pg]() {
            if (m_activePage == pg) onNavLeft();
        }, Qt::QueuedConnection);
        connect(pg, &EpubWebPage::navRight, this, [this, pg]() {
            if (m_activePage == pg) onNavRight();
        }, Qt::QueuedConnection);
        connect(pg, &EpubWebPage::readingPositionChanged, this, [this, pg](double pos) {
            if (m_activePage == pg) {
                m_currentScrollPosition = qBound(0.0, pos, 1.0);
                updateStatus();
            }
        }, Qt::QueuedConnection);
        connect(pg, &EpubWebPage::pageReady, this, [this, pg]() {
            if (BufferedView* buffer = bufferForPage(pg)) {
                if (buffer->chapter.chapterIndex < 0) return;
                const QString loadedPath = pg->url().path().mid(1);
                const QString expectedPath = m_reader->chapter(buffer->chapter.chapterIndex).href;
                if (loadedPath != expectedPath) return;

                applyZoomToViews();
                if (buffer->chapter.alignImageOnlyInitialRight) {
                    pg->runJavaScript(
                        "if (window._bibiAlignImageOnlyInitialRight) "
                        "window._bibiAlignImageOnlyInitialRight();");
                } else if (buffer->chapter.alignImageOnlyInitialLeft) {
                    pg->runJavaScript(
                        "if (window._bibiAlignImageOnlyInitialLeft) "
                        "window._bibiAlignImageOnlyInitialLeft();");
                }
                buffer->chapter.ready = true;
                if (buffer->chapter.forNavigation) {
                    m_swapBuffer = buffer;
                    scheduleSwap();
                }
            }
        }, Qt::QueuedConnection);
    }

    for (QWebEngineView* view : {m_viewA, m_viewB, m_viewC}) {
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested,
                this, [this, view](const QPoint& pos) {
                    showReaderContextMenu(view, pos);
                });
        connect(view, &QWebEngineView::loadFinished, this, [this, view](bool ok) {
            handleLoadFinished(view, ok);
        });
        connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl& url) {
            if (view != m_activeView || url.scheme() != "epub") return;
            QString path = url.path().mid(1);
            int idx = hrefToChapterIndex(path);
            if (idx >= 0 && idx != m_currentChapter) {
                m_currentChapter = idx;
                updateNavigationActions();
                updateStatus();
            }
        });
    }

    m_splitter->addWidget(m_sideTabs);
    m_splitter->addWidget(m_viewContainer);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({220, 980});

    setCentralWidget(m_splitter);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("%p%");
    m_progressBar->setFixedWidth(150);
    statusBar()->addPermanentWidget(m_progressBar);

    connect(m_tocTree, &QTreeWidget::itemClicked,
            this,      &MainWindow::onTocItemClicked);
    connect(m_bookmarkTree, &QTreeWidget::itemActivated,
            this,           &MainWindow::onBookmarkActivated);
    connect(m_bookmarkTree, &QTreeWidget::customContextMenuRequested,
            this,           &MainWindow::showBookmarkContextMenu);
    connect(m_searchResultTree, &QTreeWidget::itemActivated,
            this, &MainWindow::onSearchResultActivated);
    connect(m_searchResultTree, &QTreeWidget::itemClicked,
            this, &MainWindow::onSearchResultActivated);
    connect(chooseFolderButton, &QPushButton::clicked,
            this, &MainWindow::chooseFolderRoot);
    connect(m_folderTree, &QTreeView::activated,
            this, &MainWindow::onFolderFileActivated);

    QSettings settings;
    m_folderRootConfigured = settings.contains(kFolderRootSettingKey);
    const QString folderRoot = settings.value(kFolderRootSettingKey, QDir::homePath()).toString();
    setFolderRoot(QDir(folderRoot).exists() ? folderRoot : QDir::homePath(), false);

    m_swapCheckTimer = new QTimer(this);
    m_swapCheckTimer->setInterval(16);
    connect(m_swapCheckTimer, &QTimer::timeout, this, &MainWindow::trySwap);

    qApp->installEventFilter(this);
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("ファイル(&F)"));

    auto* openAct = fileMenu->addAction(tr("EPUBを開く(&O)…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(
            this, tr("EPUBを開く"), {},
            tr("EPUBファイル (*.epub);;すべてのファイル (*)"));
        if (!path.isEmpty()) openEpub(path);
    });

    fileMenu->addSeparator();

    m_recentFiles->attachToMenu(fileMenu);
    connect(m_recentFiles, &RecentFiles::openRequested,
            this, &MainWindow::openEpub);
    connect(m_recentFiles, &RecentFiles::missingFile, this, [this](const QString& path) {
        QMessageBox::warning(this, tr("最近開いたファイル"),
            tr("ファイルが見つかりません:\n%1\n\n履歴から削除します。").arg(path));
    });

    fileMenu->addSeparator();

    auto* quitAct = fileMenu->addAction(tr("終了(&Q)"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* viewMenu = menuBar()->addMenu(tr("表示(&V)"));

    auto* zoomInAct = viewMenu->addAction(tr("拡大"));
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, this, [this] {
        setZoomFactor(m_zoomFactor + 0.1);
    });

    auto* zoomOutAct = viewMenu->addAction(tr("縮小"));
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, this, [this] {
        setZoomFactor(m_zoomFactor - 0.1);
    });

    auto* resetZoomAct = viewMenu->addAction(tr("標準サイズ"));
    resetZoomAct->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(resetZoomAct, &QAction::triggered, this, [this] {
        setZoomFactor(1.0);
    });

    viewMenu->addSeparator();

    auto* toggleTocAct = viewMenu->addAction(tr("目次パネル(&T)"));
    toggleTocAct->setCheckable(true);
    toggleTocAct->setChecked(true);
    connect(toggleTocAct, &QAction::toggled, m_sideTabs, &QWidget::setVisible);

    viewMenu->addSeparator();

    auto* settingsAct = viewMenu->addAction(tr("設定(&P)…"));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::showSettings);

    auto* bmMenu = menuBar()->addMenu(tr("しおり(&B)"));

    auto* addBmAct = bmMenu->addAction(tr("しおりを追加(&A)"));
    addBmAct->setShortcut(Qt::CTRL | Qt::Key_D);
    connect(addBmAct, &QAction::triggered, this, &MainWindow::addBookmark);

    auto* manageBmAct = bmMenu->addAction(tr("しおり一覧(&M)…"));
    connect(manageBmAct, &QAction::triggered, this, &MainWindow::manageBookmarks);

    bmMenu->addSeparator();

    auto* exportBackupAct = bmMenu->addAction(tr("バックアップを書き出し(&E)…"));
    connect(exportBackupAct, &QAction::triggered, this, &MainWindow::exportBookmarkBackup);

    auto* importBackupAct = bmMenu->addAction(tr("バックアップを読み込み(&I)…"));
    connect(importBackupAct, &QAction::triggered, this, &MainWindow::importBookmarkBackup);

    auto* searchMenu = menuBar()->addMenu(tr("検索(&S)"));

    m_searchMenuAct = searchMenu->addAction(tr("検索(&F)…"));
    m_searchMenuAct->setShortcut(Qt::CTRL | Qt::Key_F);
    connect(m_searchMenuAct, &QAction::triggered, this, &MainWindow::showSearch);
}

void MainWindow::setupToolBar() {
    auto* tb = addToolBar(tr("ナビゲーション"));
    tb->setObjectName("navToolBar");
    tb->setMovable(false);

    m_coverAct = tb->addAction(tr("|< 表紙"));
    m_coverAct->setToolTip(tr("表紙へ移動（Ctrl+Home）"));
    m_coverAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    m_coverAct->setEnabled(false);
    connect(m_coverAct, &QAction::triggered, this, &MainWindow::goToCover);

    m_leftAct = tb->addAction(tr("◀"));
    m_leftAct->setEnabled(false);
    connect(m_leftAct, &QAction::triggered, this, &MainWindow::onLeftKey);

    m_rightAct = tb->addAction(tr("▶"));
    m_rightAct->setEnabled(false);
    connect(m_rightAct, &QAction::triggered, this, &MainWindow::onRightKey);

    m_endAct = tb->addAction(tr("末尾 >|"));
    m_endAct->setToolTip(tr("末尾へ移動（Ctrl+End）"));
    m_endAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_End));
    m_endAct->setEnabled(false);
    connect(m_endAct, &QAction::triggered, this, &MainWindow::goToBookEnd);

    tb->addSeparator();

    m_bookmarkAct = tb->addAction(tr("★ しおり"));
    m_bookmarkAct->setEnabled(false);
    connect(m_bookmarkAct, &QAction::triggered, this, &MainWindow::addBookmark);
}

void MainWindow::setupSearchBar() {
    m_searchWatcher = new QFutureWatcher<QList<EpubReader::SearchResult>>(this);
    connect(m_searchWatcher, &QFutureWatcher<QList<EpubReader::SearchResult>>::finished,
            this, &MainWindow::onSearchFinished);

    m_searchBar = addToolBar(tr("検索"));
    m_searchBar->setObjectName("searchToolBar");
    m_searchBar->setMovable(false);
    m_searchBar->setVisible(false);

    m_searchEdit = new QLineEdit(m_searchBar);
    m_searchEdit->setPlaceholderText(tr("全文検索"));
    m_searchEdit->setMinimumWidth(260);
    m_searchBar->addWidget(m_searchEdit);

    m_searchPrevAct = m_searchBar->addAction(tr("前へ"));
    m_searchNextAct = m_searchBar->addAction(tr("次へ"));
    m_searchCountLabel = new QLabel(tr("0 / 0"), m_searchBar);
    m_searchBar->addWidget(m_searchCountLabel);
    m_searchListAct = m_searchBar->addAction(tr("一覧"));
    auto* closeAct = m_searchBar->addAction(tr("閉じる"));

    m_searchPrevAct->setEnabled(false);
    m_searchNextAct->setEnabled(false);
    m_searchListAct->setEnabled(false);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
            searchPrevious();
        else
            searchNext();
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] {
        m_searchResults.clear();
        m_searchIndex = -1;
        m_searchQuery.clear();
        refreshSearchResultList();
        updateSearchCountLabel();
        m_searchPrevAct->setEnabled(false);
        m_searchNextAct->setEnabled(false);
        m_searchListAct->setEnabled(false);
        if (m_searchEdit->text().trimmed().isEmpty())
            clearSearchHighlights();
    });
    connect(m_searchPrevAct, &QAction::triggered, this, &MainWindow::searchPrevious);
    connect(m_searchNextAct, &QAction::triggered, this, &MainWindow::searchNext);
    connect(m_searchListAct, &QAction::triggered, this, &MainWindow::openSearchResults);
    connect(closeAct, &QAction::triggered, this, &MainWindow::closeSearchBar);

    auto* findNextAct = new QAction(this);
    findNextAct->setShortcut(Qt::Key_F3);
    addAction(findNextAct);
    connect(findNextAct, &QAction::triggered, this, &MainWindow::searchNext);

    auto* findPrevAct = new QAction(this);
    findPrevAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    addAction(findPrevAct);
    connect(findPrevAct, &QAction::triggered, this, &MainWindow::searchPrevious);
}
