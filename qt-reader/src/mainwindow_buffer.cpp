#include "mainwindow.h"
#include "mainwindow_private.h"
#include <QtConcurrent/QtConcurrent>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <QPalette>
#include <QSettings>
#include <QStatusBar>

void MainWindow::handleLoadFinished(QWebEngineView* view, bool ok) {
    if (!ok) {
        BufferedView* buf = nullptr;
        if (view == m_nextBuffer.view)     buf = &m_nextBuffer;
        else if (view == m_previousBuffer.view) buf = &m_previousBuffer;

        if (buf && buf->chapter.forNavigation) {
            m_swapCheckTimer->stop();
            if (m_swapBuffer == buf)
                m_swapBuffer = nullptr;
            // Restore the chapter index that goToChapter() had already advanced.
            if (m_swapFromChapter >= 0)
                m_currentChapter = m_swapFromChapter;
            m_swapFromChapter = -1;
            m_swapToChapter   = -1;
            buf->chapter = {};
            updateNavigationActions();
            updateStatus();
            statusBar()->showMessage(tr("ページの読み込みに失敗しました"), 3000);
        }
        return;
    }
    setViewLoaded(view, true);

    applyZoomToViews();

    view->page()->runJavaScript(loadScript(":/scripts/bibi_ellipsis.js"));
    view->page()->runJavaScript(loadScript(":/scripts/bibi_reading_insets.js"));
    view->page()->runJavaScript(loadScript(":/scripts/bibi_position_reporter.js"));

    const int isRtl                    = m_isRtl ? 1 : 0;
    const int scrollToEnd              = scrollToEndForView(view) ? 1 : 0;
    const int alignImageOnlyInitialRight = alignImageOnlyInitialRightForView(view) ? 1 : 0;
    const int alignImageOnlyInitialLeft  = alignImageOnlyInitialLeftForView(view) ? 1 : 0;
    m_scrollToEnd = false;
    view->page()->runJavaScript(
        loadScript(":/scripts/bibi_page_ready.js")
            .arg(isRtl).arg(scrollToEnd)
            .arg(alignImageOnlyInitialRight).arg(alignImageOnlyInitialLeft));

    const int sign = m_isRtl ? -1 : 1;
    view->page()->runJavaScript(loadScript(":/scripts/bibi_wheel.js").arg(sign));

    const int rtl = m_isRtl ? 1 : 0;
    view->page()->runJavaScript(loadScript(":/scripts/bibi_keyboard.js").arg(rtl));

    view->page()->runJavaScript(loadScript(":/scripts/bibi_drag.js"));

    // Skip if a prefetch is already running: overwriting the future would orphan it,
    // making waitForFinished() in openEpub() unable to wait for it → data race on m_spine.
    if (!m_prefetchFuture.isRunning()) {
        const int cur      = m_currentChapter;
        EpubReader* reader = m_reader;
        m_prefetchFuture = QtConcurrent::run([reader, cur]() {
            reader->prefetchChapter(cur + 1);
            reader->prefetchChapter(cur - 1);
        });
    }
}

void MainWindow::scheduleSwap() {
    m_swapCheckCount = 0;
    m_swapCheckTimer->start();
}

void MainWindow::trySwap() {
    if (!m_swapBuffer || !m_swapBuffer->view) {
        m_swapCheckTimer->stop();
        return;
    }

    if (!m_pixelSwapDetection) {
        m_swapCheckTimer->stop();
        performSwap();
        return;
    }

    QImage img = m_swapBuffer->view->grab().toImage();

    bool hasContent = false;
    if (!img.isNull() && img.width() > 0 && img.height() > 0) {
        const QRgb ref = img.pixel(0, 0);
        const int  xStep = qMax(1, img.width()  / 12);
        const int  yStep = qMax(1, img.height() / 12);
        for (int y = 0; y < img.height() && !hasContent; y += yStep)
            for (int x = 0; x < img.width() && !hasContent; x += xStep)
                if (img.pixel(x, y) != ref) hasContent = true;
    }

    if (hasContent || ++m_swapCheckCount >= kMaxSwapChecks) {
        m_swapCheckTimer->stop();
        performSwap();
    }
}

void MainWindow::performSwap() {
    if (!m_swapBuffer || !m_swapBuffer->view || !m_swapBuffer->page) return;

    promoteBuffer(*m_swapBuffer);
    m_swapBuffer = nullptr;
    m_swapFromChapter = -1;
    m_swapToChapter = -1;

    if (m_postSwapAction) {
        auto action = std::move(m_postSwapAction);
        m_postSwapAction = nullptr;
        QTimer::singleShot(0, this, [action]{ action(); });
    }

    QTimer::singleShot(0, this, &MainWindow::refreshPreloads);
}

void MainWindow::loadBufferedChapter(BufferedView& buffer, int index, bool scrollToEnd, bool forNavigation,
                                     bool alignImageOnlyInitialRight, bool alignImageOnlyInitialLeft) {
    if (!m_reader->isOpen()) return;
    if (index < 0 || index >= m_reader->chapterCount()) return;

    m_swapCheckTimer->stop();
    buffer.chapter.chapterIndex = index;
    buffer.chapter.ready = false;
    buffer.chapter.forNavigation = forNavigation;
    buffer.chapter.scrollToEnd = scrollToEnd;
    buffer.chapter.alignImageOnlyInitialRight = alignImageOnlyInitialRight;
    buffer.chapter.alignImageOnlyInitialLeft = alignImageOnlyInitialLeft;
    m_scrollToEnd = scrollToEnd;

    EpubChapter ch = m_reader->chapter(index);
    setViewLoaded(buffer.view, false);
    buffer.view->setUrl(QUrl("epub:///" + ch.href));
}

int MainWindow::nextReadingChapterIndex() const {
    if (!m_reader->isOpen() || m_currentChapter < 0) return -1;
    const int next = m_currentChapter + 1;
    return (next >= 0 && next < m_reader->chapterCount()) ? next : -1;
}

int MainWindow::previousReadingChapterIndex() const {
    if (!m_reader->isOpen() || m_currentChapter < 0) return -1;
    const int previous = m_currentChapter - 1;
    return (previous >= 0 && previous < m_reader->chapterCount()) ? previous : -1;
}

void MainWindow::refreshPreloads() {
    if (m_preloadMode == PreloadMode::None) return;
    if (!m_reader->isOpen() || m_postSwapAction) return;
    if (m_swapCheckTimer && m_swapCheckTimer->isActive()) return;

    const int next = nextReadingChapterIndex();
    if (next >= 0 &&
        !(m_nextBuffer.chapter.chapterIndex == next && !m_nextBuffer.chapter.forNavigation)) {
        loadBufferedChapter(m_nextBuffer, next, false, false, true, false);
    }

    if (m_preloadMode != PreloadMode::NextAndPrevious) return;

    const int previous = previousReadingChapterIndex();
    if (previous >= 0 &&
        !(m_previousBuffer.chapter.chapterIndex == previous && !m_previousBuffer.chapter.forNavigation)) {
        loadBufferedChapter(m_previousBuffer, previous, true, false, false, true);
    }
}

void MainWindow::invalidatePreloads() {
    if (m_swapCheckTimer)
        m_swapCheckTimer->stop();
    m_nextBuffer.chapter = {};
    m_previousBuffer.chapter = {};
    m_swapBuffer = nullptr;
}

bool MainWindow::bufferedChapterMatches(const BufferedView& buffer, int index, bool scrollToEnd) const {
    return buffer.chapter.chapterIndex == index &&
           buffer.chapter.scrollToEnd == scrollToEnd;
}

MainWindow::BufferedView* MainWindow::bufferForPage(EpubWebPage* page) {
    if (m_nextBuffer.page == page) return &m_nextBuffer;
    if (m_previousBuffer.page == page) return &m_previousBuffer;
    return nullptr;
}

const MainWindow::BufferedView* MainWindow::bufferForPage(EpubWebPage* page) const {
    if (m_nextBuffer.page == page) return &m_nextBuffer;
    if (m_previousBuffer.page == page) return &m_previousBuffer;
    return nullptr;
}

bool MainWindow::scrollToEndForView(QWebEngineView* view) const {
    if (view && view->page() == m_nextBuffer.page)
        return m_nextBuffer.chapter.scrollToEnd;
    if (view && view->page() == m_previousBuffer.page)
        return m_previousBuffer.chapter.scrollToEnd;
    return m_scrollToEnd;
}

bool MainWindow::alignImageOnlyInitialRightForView(QWebEngineView* view) const {
    if (view && view->page() == m_nextBuffer.page)
        return m_nextBuffer.chapter.alignImageOnlyInitialRight;
    if (view && view->page() == m_previousBuffer.page)
        return m_previousBuffer.chapter.alignImageOnlyInitialRight;
    return false;
}

bool MainWindow::alignImageOnlyInitialLeftForView(QWebEngineView* view) const {
    if (view && view->page() == m_nextBuffer.page)
        return m_nextBuffer.chapter.alignImageOnlyInitialLeft;
    if (view && view->page() == m_previousBuffer.page)
        return m_previousBuffer.chapter.alignImageOnlyInitialLeft;
    return false;
}

void MainWindow::promoteBuffer(BufferedView& buffer) {
    QWebEngineView* oldActiveView = m_activeView;
    EpubWebPage* oldActivePage = m_activePage;
    const int oldChapter = m_swapFromChapter;
    const int targetChapter = m_swapToChapter;

    buffer.view->raise();
    buffer.view->setFocus();
    m_activeView = buffer.view;
    m_activePage = buffer.page;

    const bool movedNext = oldChapter >= 0 && targetChapter == oldChapter + 1;
    const bool movedPrevious = oldChapter >= 0 && targetChapter == oldChapter - 1;

    if (&buffer == &m_nextBuffer) {
        QWebEngineView* spareView = m_previousBuffer.view;
        EpubWebPage* sparePage = m_previousBuffer.page;
        if (movedNext) {
            m_previousBuffer.view = oldActiveView;
            m_previousBuffer.page = oldActivePage;
            m_previousBuffer.chapter = {oldChapter, true, false, true, false, true};
            m_nextBuffer.view = spareView;
            m_nextBuffer.page = sparePage;
            m_nextBuffer.chapter = {};
        } else {
            m_nextBuffer.view = oldActiveView;
            m_nextBuffer.page = oldActivePage;
            m_nextBuffer.chapter = {};
            m_previousBuffer.view = spareView;
            m_previousBuffer.page = sparePage;
            m_previousBuffer.chapter = {};
        }
    } else {
        QWebEngineView* spareView = m_nextBuffer.view;
        EpubWebPage* sparePage = m_nextBuffer.page;
        if (movedPrevious) {
            m_nextBuffer.view = oldActiveView;
            m_nextBuffer.page = oldActivePage;
            m_nextBuffer.chapter = {oldChapter, true, false, false, true, false};
            m_previousBuffer.view = spareView;
            m_previousBuffer.page = sparePage;
            m_previousBuffer.chapter = {};
        } else {
            m_previousBuffer.view = oldActiveView;
            m_previousBuffer.page = oldActivePage;
            m_previousBuffer.chapter = {};
            m_nextBuffer.view = spareView;
            m_nextBuffer.page = sparePage;
            m_nextBuffer.chapter = {};
        }
    }
}

void MainWindow::setViewLoaded(QWebEngineView* view, bool loaded) {
    if (!view) return;
    if (loaded)
        m_loadedViews.insert(view);
    else
        m_loadedViews.remove(view);
    updatePageBackgroundColor();
}

void MainWindow::applyZoomToViews() {
    const QString js = loadScript(":/scripts/bibi_zoom.js").arg(m_zoomFactor);

    if (m_viewA && qAbs(m_viewA->zoomFactor() - 1.0) > 1e-6) m_viewA->setZoomFactor(1.0);
    if (m_viewB && qAbs(m_viewB->zoomFactor() - 1.0) > 1e-6) m_viewB->setZoomFactor(1.0);
    if (m_viewC && qAbs(m_viewC->zoomFactor() - 1.0) > 1e-6) m_viewC->setZoomFactor(1.0);
    auto apply = [&js](QWebEngineView* view) {
        if (view && view->page()) view->page()->runJavaScript(js);
    };
    apply(m_activeView);
    apply(m_nextBuffer.view);
    apply(m_previousBuffer.view);
}
