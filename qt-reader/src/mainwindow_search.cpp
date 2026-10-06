#include "mainwindow.h"
#include "mainwindow_private.h"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QWebEngineView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QToolBar>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QTreeWidget>
#include <QAbstractItemView>
#include <QTabWidget>
#include <QStatusBar>

void MainWindow::showSearch() {
    if (!m_searchBar) return;
    m_searchBar->setVisible(true);
    const QString selected = m_activeView ? m_activeView->selectedText().trimmed() : QString();
    if (!selected.isEmpty() && !selected.contains('\n'))
        m_searchEdit->setText(selected);
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void MainWindow::runSearch() {
    if (!m_reader->isOpen()) {
        QMessageBox::information(this, tr("検索"), tr("EPUBが開かれていません。"));
        return;
    }

    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    if (m_searchWatcher && m_searchWatcher->isRunning()) return;

    m_searchQuery = query;
    m_searchResults.clear();
    m_searchIndex = -1;
    refreshSearchResultList();
    updateSearchCountLabel();
    m_searchPrevAct->setEnabled(false);
    m_searchNextAct->setEnabled(false);
    m_searchListAct->setEnabled(false);
    m_searchEdit->setEnabled(false);
    statusBar()->showMessage(tr("検索中..."));

    EpubReader* reader = m_reader;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_searchCancel = cancel;
    m_searchWatcher->setFuture(
        QtConcurrent::run([reader, query, cancel]() -> QList<EpubReader::SearchResult> {
            return reader->search(query, cancel.get());
        }));
}

void MainWindow::cancelSearchAndWait() {
    if (!m_searchWatcher || !m_searchWatcher->isRunning()) return;

    // Clearing the query turns the pending onSearchFinished() into a no-op.
    m_postSearchAction = nullptr;
    m_searchQuery.clear();
    if (m_searchCancel)
        m_searchCancel->store(true);
    m_searchWatcher->waitForFinished();
}

void MainWindow::onSearchFinished() {
    const QString query = m_searchQuery;
    if (m_searchWatcher->future().isCanceled() || query.isEmpty()) {
        m_searchEdit->setEnabled(true);
        statusBar()->clearMessage();
        return;
    }

    m_searchResults = m_searchWatcher->result();
    m_searchIndex = m_searchResults.isEmpty() ? -1 : firstSearchResultAtOrAfterCurrentChapter();

    const bool hasResults = !m_searchResults.isEmpty();
    m_searchPrevAct->setEnabled(hasResults);
    m_searchNextAct->setEnabled(hasResults);
    m_searchListAct->setEnabled(hasResults);
    m_searchEdit->setEnabled(true);
    refreshSearchResultList();
    updateSearchCountLabel();
    statusBar()->clearMessage();

    if (!hasResults)
        statusBar()->showMessage(tr("「%1」は見つかりませんでした").arg(query), 3000);

    if (m_postSearchAction) {
        auto action = std::move(m_postSearchAction);
        m_postSearchAction = nullptr;
        action();
    }
}

void MainWindow::searchNext() {
    if (!m_searchBar->isVisible())
        showSearch();
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    const bool freshSearch = query != m_searchQuery || m_searchResults.isEmpty();
    if (freshSearch) {
        m_postSearchAction = [this]() {
            if (m_searchResults.isEmpty()) return;
            m_searchIndex = firstSearchResultAtOrAfterCurrentChapter();
            updateSearchCountLabel();
            refreshSearchResultList();
            jumpToSearchResult(m_searchIndex);
        };
        runSearch();
        return;
    }
    if (m_searchResults.isEmpty()) return;

    m_searchIndex = (m_searchIndex < 0)
        ? firstSearchResultAtOrAfterCurrentChapter()
        : (m_searchIndex + 1) % m_searchResults.size();

    updateSearchCountLabel();
    refreshSearchResultList();
    jumpToSearchResult(m_searchIndex);
}

void MainWindow::searchPrevious() {
    if (!m_searchBar->isVisible())
        showSearch();
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    const bool freshSearch = query != m_searchQuery || m_searchResults.isEmpty();
    if (freshSearch) {
        m_postSearchAction = [this]() {
            if (m_searchResults.isEmpty()) return;
            m_searchIndex = lastSearchResultAtOrBeforeCurrentChapter();
            updateSearchCountLabel();
            refreshSearchResultList();
            jumpToSearchResult(m_searchIndex);
        };
        runSearch();
        return;
    }
    if (m_searchResults.isEmpty()) return;

    m_searchIndex = (m_searchIndex < 0)
        ? lastSearchResultAtOrBeforeCurrentChapter()
        : (m_searchIndex - 1 + m_searchResults.size()) % m_searchResults.size();

    updateSearchCountLabel();
    refreshSearchResultList();
    jumpToSearchResult(m_searchIndex);
}

void MainWindow::openSearchResults() {
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    if (m_searchResults.isEmpty() && !query.isEmpty() &&
        !(m_searchWatcher && m_searchWatcher->isRunning())) {
        m_postSearchAction = [this]() {
            if (m_searchResults.isEmpty()) return;
            refreshSearchResultList();
            if (m_sideTabs && m_searchResultTree) {
                m_sideTabs->setVisible(true);
                m_sideTabs->setCurrentWidget(m_searchResultTree);
                m_searchResultTree->setFocus(Qt::OtherFocusReason);
            }
        };
        runSearch();
        return;
    }
    refreshSearchResultList();
    if (m_sideTabs && m_searchResultTree) {
        m_sideTabs->setVisible(true);
        m_sideTabs->setCurrentWidget(m_searchResultTree);
        m_searchResultTree->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::closeSearchBar() {
    cancelSearchAndWait();
    clearSearchHighlights();
    m_searchResults.clear();
    m_searchIndex = -1;
    m_searchQuery.clear();
    refreshSearchResultList();
    updateSearchCountLabel();
    if (m_searchEdit)
        m_searchEdit->setEnabled(true);
    if (m_searchPrevAct)
        m_searchPrevAct->setEnabled(false);
    if (m_searchNextAct)
        m_searchNextAct->setEnabled(false);
    if (m_searchListAct)
        m_searchListAct->setEnabled(false);
    if (m_searchBar)
        m_searchBar->setVisible(false);
}

void MainWindow::clearSearchHighlights() {
    m_highlightedSearchQuery.clear();
    m_highlightedSearchChapter = -1;

    if (!m_activePage) return;
    m_activePage->runJavaScript(loadScript(":/scripts/bibi_clear_highlights.js"));
}

void MainWindow::refreshSearchResultList() {
    if (!m_searchResultTree) return;

    m_searchResultTree->clear();
    for (int i = 0; i < m_searchResults.size(); ++i) {
        const auto& result = m_searchResults[i];
        auto* item = new QTreeWidgetItem(m_searchResultTree);
        item->setText(0, result.chapterTitle);
        item->setText(1, QString::number(result.occurrenceIndex + 1));
        item->setText(2, result.context);
        item->setToolTip(0, result.chapterTitle);
        item->setToolTip(2, result.context);
        item->setData(0, Qt::UserRole, i);
    }

    selectSearchResultItem(m_searchIndex);
}

void MainWindow::selectSearchResultItem(int resultIndex) {
    if (!m_searchResultTree) return;
    if (resultIndex < 0 || resultIndex >= m_searchResultTree->topLevelItemCount()) return;

    auto* current = m_searchResultTree->topLevelItem(resultIndex);
    m_searchResultTree->setCurrentItem(current);
    m_searchResultTree->scrollToItem(current, QAbstractItemView::PositionAtCenter);
}

void MainWindow::updateSearchCountLabel() {
    if (!m_searchCountLabel) return;
    if (m_searchWatcher && m_searchWatcher->isRunning()) {
        m_searchCountLabel->setText(tr("検索中..."));
        return;
    }
    if (m_searchResults.isEmpty()) {
        m_searchCountLabel->setText(m_searchQuery.isEmpty() ? tr("0 / 0") : tr("見つかりません"));
        return;
    }
    m_searchCountLabel->setText(tr("%1 / %2").arg(m_searchIndex + 1).arg(m_searchResults.size()));
}

void MainWindow::onSearchResultActivated(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    const int resultIndex = item->data(0, Qt::UserRole).toInt();
    if (resultIndex < 0 || resultIndex >= m_searchResults.size()) return;

    m_searchIndex = resultIndex;
    updateSearchCountLabel();
    selectSearchResultItem(m_searchIndex);
    jumpToSearchResult(m_searchIndex);
}

int MainWindow::firstSearchResultAtOrAfterCurrentChapter() const {
    if (m_searchResults.isEmpty()) return -1;
    for (int i = 0; i < m_searchResults.size(); ++i) {
        if (m_searchResults[i].chapterIndex >= m_currentChapter)
            return i;
    }
    return 0;
}

int MainWindow::lastSearchResultAtOrBeforeCurrentChapter() const {
    if (m_searchResults.isEmpty()) return -1;
    for (int i = m_searchResults.size() - 1; i >= 0; --i) {
        if (m_searchResults[i].chapterIndex <= m_currentChapter)
            return i;
    }
    return m_searchResults.size() - 1;
}

void MainWindow::jumpToSearchResult(int resultIndex) {
    if (resultIndex < 0 || resultIndex >= m_searchResults.size()) return;

    const EpubReader::SearchResult target = m_searchResults[resultIndex];
    const int chapterIndex = target.chapterIndex;
    const QString query = m_searchQuery;
    if (query.trimmed().isEmpty()) {
        goToChapter(chapterIndex);
        return;
    }

    // Every match of this chapter is highlighted; offsets refer to
    // EpubReader::searchableText(), which the script rebuilds from the DOM.
    QJsonArray matches;
    for (const auto& result : m_searchResults) {
        if (result.chapterIndex == chapterIndex)
            matches.append(QJsonArray{result.matchStart, result.matchLength});
    }

    int activeChapter = -1;
    if (m_activePage) {
        const QString activePath = m_activePage->url().path().mid(1);
        activeChapter = hrefToChapterIndex(activePath);
    }
    const bool targetIsDisplayed = chapterIndex == activeChapter;
    const bool sameHighlightedChapter =
        targetIsDisplayed &&
        chapterIndex == m_highlightedSearchChapter &&
        query == m_highlightedSearchQuery;
    const bool restoreSearchFocus = m_searchBar && m_searchBar->isVisible() && m_searchEdit;

    const QJsonObject params{
        {"needle",  query},
        {"current", target.occurrenceIndex},
        {"scroll",  !sameHighlightedChapter},
        {"matches", matches},
    };
    // A single arg() call: the JSON is never re-scanned for placeholders.
    const QString script = loadScript(":/scripts/bibi_search_highlight.js")
        .arg(QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact)));

    auto applyHighlight = [this, script, restoreSearchFocus]() {
        m_activePage->runJavaScript(script);

        if (restoreSearchFocus) {
            QTimer::singleShot(0, this, [this] {
                if (m_searchBar && m_searchBar->isVisible() && m_searchEdit)
                    m_searchEdit->setFocus(Qt::OtherFocusReason);
            });
        }
    };

    m_highlightedSearchQuery = query;
    m_highlightedSearchChapter = chapterIndex;

    if (targetIsDisplayed) {
        applyHighlight();
        return;
    }

    if (m_swapBuffer && m_swapToChapter == chapterIndex) {
        m_postSwapAction = applyHighlight;
        return;
    }

    goToChapter(chapterIndex);
    m_postSwapAction = applyHighlight;
}
