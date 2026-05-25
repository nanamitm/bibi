#include "mainwindow.h"
#include <QWebEngineView>
#include <QUrl>
#include <QTimer>

void MainWindow::goToChapter(int index) {
    if (!m_reader->isOpen()) return;
    if (index < 0 || index >= m_reader->chapterCount()) return;

    const int fromChapter = m_currentChapter;
    m_currentChapter = index;
    const bool scrollToEnd = m_scrollToEnd;
    m_currentScrollPosition = scrollToEnd ? 1.0 : 0.0;
    m_scrollToEnd = false;

    BufferedView* matched = nullptr;
    if (bufferedChapterMatches(m_nextBuffer, index, scrollToEnd))
        matched = &m_nextBuffer;
    else if (bufferedChapterMatches(m_previousBuffer, index, scrollToEnd))
        matched = &m_previousBuffer;

    m_swapFromChapter = fromChapter;
    m_swapToChapter = index;

    if (matched) {
        matched->chapter.forNavigation = true;
        m_swapBuffer = matched;
        if (matched->chapter.ready)
            scheduleSwap();
    } else {
        invalidatePreloads();
        const bool movingPrevious = fromChapter >= 0 && index == fromChapter - 1;
        loadBufferedChapter(m_nextBuffer, index, scrollToEnd, true, !movingPrevious, movingPrevious);
        m_swapBuffer = &m_nextBuffer;
    }
    updateNavigationActions();
    updateStatus();
}

void MainWindow::goToHref(const QString& href) {
    QString path = href.section('#', 0, 0);
    QString frag = href.section('#', 1);

    int idx = hrefToChapterIndex(path);
    if (idx < 0) return;

    m_currentChapter = idx;
    m_currentScrollPosition = 0.0;
    EpubChapter ch   = m_reader->chapter(idx);
    QUrl url("epub:///" + ch.href);
    if (!frag.isEmpty()) url.setFragment(frag);
    invalidatePreloads();
    m_swapBuffer = &m_nextBuffer;
    m_swapFromChapter = -1;
    m_swapToChapter = idx;
    m_scrollToEnd = false;
    m_nextBuffer.chapter.chapterIndex = idx;
    m_nextBuffer.chapter.ready = false;
    m_nextBuffer.chapter.forNavigation = true;
    m_nextBuffer.chapter.scrollToEnd = false;
    m_nextBuffer.chapter.alignImageOnlyInitialRight = true;
    m_nextBuffer.chapter.alignImageOnlyInitialLeft = false;
    setViewLoaded(m_nextBuffer.view, false);
    m_nextBuffer.view->setUrl(url);

    updateNavigationActions();
    updateStatus();
}

int MainWindow::hrefToChapterIndex(const QString& path) const {
    return m_hrefIndex.value(path, -1);
}

void MainWindow::goToCover() {
    if (!m_reader->isOpen() || m_reader->chapterCount() <= 0) return;
    m_scrollToEnd = false;
    goToChapter(0);
}

void MainWindow::goToBookEnd() {
    if (!m_reader->isOpen() || m_reader->chapterCount() <= 0) return;
    m_scrollToEnd = true;
    goToChapter(m_reader->chapterCount() - 1);
}

void MainWindow::nextChapter(bool scrollToEnd) {
    if (m_currentChapter + 1 < m_reader->chapterCount()) {
        m_scrollToEnd = scrollToEnd;
        goToChapter(m_currentChapter + 1);
    }
}

void MainWindow::prevChapter(bool scrollToEnd) {
    if (m_currentChapter > 0) {
        m_scrollToEnd = scrollToEnd;
        goToChapter(m_currentChapter - 1);
    }
}

void MainWindow::onLeftKey()  { if (m_isRtl) nextChapter(); else prevChapter(); }
void MainWindow::onRightKey() { if (m_isRtl) prevChapter(); else nextChapter(); }

void MainWindow::onNavLeft() {
    if (m_isRtl) nextChapter(false);
    else         prevChapter(true);
}
void MainWindow::onNavRight() {
    if (m_isRtl) prevChapter(true);
    else         nextChapter(false);
}
