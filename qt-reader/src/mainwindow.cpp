#include "mainwindow.h"
#include "mainwindow_private.h"
#include "epuburlscheme.h"
#include <QWebEngineView>
#include <QEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QApplication>
#include <QMessageBox>
#include <QSplitter>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QFileInfo>
#include <QStatusBar>

// ── Constructor / Destructor ──────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_reader(new EpubReader(this))
    , m_bookmarkManager(new BookmarkManager(this))
    , m_urlScheme(new EpubUrlScheme(this))
    , m_recentFiles(new RecentFiles(this))
{
    setupUi();
    setupMenuBar();
    setupToolBar();
    setupSearchBar();

    setWindowTitle(tr("Bibi Qt Reader"));
    resize(1200, 800);

    QSettings s;
    setZoomFactor(s.value(kZoomFactorSettingKey, 1.0).toDouble(), false);
    setPreloadMode(preloadModeFromIndex(s.value(kPreloadModeSettingKey, 2).toInt()), false);
    m_pixelSwapDetection = s.value(kPixelSwapDetectionSettingKey, true).toBool();
    restoreGeometry(s.value("geometry").toByteArray());
    restoreState(s.value("windowState").toByteArray());
    if (s.contains("splitter"))
        m_splitter->restoreState(s.value("splitter").toByteArray());
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    saveCurrentReadingPosition();
    m_prefetchFuture.waitForFinished();
    QSettings s;
    s.setValue("geometry",    saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("splitter",    m_splitter->saveState());
    event->accept();
}

// ── Event filter ─────────────────────────────────────────────────────────

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == qApp &&
        (event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::PaletteChange)) {
        updatePageBackgroundColor();
    }

    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            QObject* p = obj;
            while (p) {
                if (p == m_viewA || p == m_viewB || p == m_viewC) {
                    applyCtrlWheelZoom(we->angleDelta().y());
                    return true;
                }
                p = p->parent();
            }
        }
    }
    if (obj == m_viewContainer && event->type() == QEvent::Resize) {
        const QSize s = m_viewContainer->size();
        m_viewA->setGeometry(0, 0, s.width(), s.height());
        m_viewB->setGeometry(0, 0, s.width(), s.height());
        m_viewC->setGeometry(0, 0, s.width(), s.height());
    }
    return QMainWindow::eventFilter(obj, event);
}

// ── EPUB Loading ──────────────────────────────────────────────────────────

void MainWindow::openEpub(const QString& filePath) {
    saveCurrentReadingPosition();
    m_prefetchFuture.waitForFinished();
    if (!m_reader->open(filePath)) {
        QMessageBox::critical(this, tr("エラー"),
            tr("EPUBファイルを開けませんでした:\n%1\n\n原因: %2")
            .arg(filePath, m_reader->lastError()));
        return;
    }
    m_recentFiles->addFile(filePath);
    if (!m_folderRootConfigured)
        setFolderRoot(QFileInfo(filePath).absolutePath());
    QTimer::singleShot(0, this, [this, filePath] {
        selectFolderFile(filePath);
    });

    m_urlScheme->setReader(m_reader);
    m_currentChapter = -1;
    m_currentScrollPosition = 0.0;
    invalidatePreloads();

    m_hrefIndex.clear();
    for (int i = 0; i < m_reader->chapterCount(); ++i)
        m_hrefIndex.insert(m_reader->chapter(i).href, i);

    // Clear search state so stale results from the previous book are not shown.
    // closeSearchBar() resets all search fields and hides the bar if visible.
    closeSearchBar();

    m_isRtl = (m_reader->metadata().pageProgressionDirection == "rtl");

    if (m_isRtl) {
        m_leftAct->setText(tr("◀ 次へ"));
        m_leftAct->setToolTip(tr("次のページ（←）"));
        m_rightAct->setText(tr("前へ ▶"));
        m_rightAct->setToolTip(tr("前のページ（→）"));
    } else {
        m_leftAct->setText(tr("◀ 前へ"));
        m_leftAct->setToolTip(tr("前のページ（←）"));
        m_rightAct->setText(tr("次へ ▶"));
        m_rightAct->setToolTip(tr("次のページ（→）"));
    }

    m_tocTree->clear();
    populateToc(m_reader->toc());
    m_tocTree->expandAll();
    refreshBookmarkList();

    updateWindowTitle();
    updateNavigationActions();

    ReadingPosition pos;
    if (m_reader->chapterCount() > 0 &&
        m_bookmarkManager->readingPositionForEpub(m_reader->filePath(), &pos) &&
        pos.chapterIndex >= 0 && pos.chapterIndex < m_reader->chapterCount()) {
        jumpToReadingPosition(pos);
        statusBar()->showMessage(tr("前回の位置から再開しました"), 1500);
    } else if (m_reader->chapterCount() > 0) {
        goToChapter(0);
    }
}
