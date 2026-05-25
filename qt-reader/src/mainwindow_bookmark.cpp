#include "mainwindow.h"
#include "mainwindow_private.h"
#include <QWebEngineView>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QDateTime>
#include <QTabWidget>
#include <QAbstractItemView>
#include <QMenu>
#include <QTimer>
#include <QStatusBar>

namespace {

class BookmarkTreeItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem& other) const override {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 2)
            return data(col, Qt::UserRole).toInt() < other.data(col, Qt::UserRole).toInt();
        if (col == 3)
            return data(col, Qt::UserRole).toDateTime() < other.data(col, Qt::UserRole).toDateTime();
        return text(col).localeAwareCompare(other.text(col)) < 0;
    }
};

} // namespace

void MainWindow::addBookmark() {
    if (!m_reader->isOpen() || m_currentChapter < 0) return;

    // Capture at call time — the callback runs asynchronously and these may have changed by then.
    const QString epubPath    = m_reader->filePath();
    const int     chapterIndex = m_currentChapter;
    const QString label0      = chapterLabel(chapterIndex);

    m_activePage->runJavaScript(
        loadScript(":/scripts/bibi_get_reading_position.js"),
        [this, epubPath, chapterIndex, label0](const QVariant& v) {
            if (!m_reader->isOpen() || m_reader->filePath() != epubPath) return;
            const double pos = v.toDouble();
            Bookmark bm;
            bm.id             = QUuid::createUuid().toString(QUuid::WithoutBraces);
            bm.epubPath       = epubPath;
            bm.chapterIndex   = chapterIndex;
            bm.scrollPosition = pos;
            bm.label          = tr("%1  %2%")
                                    .arg(label0)
                                    .arg(qRound(pos * 100.0));
            bm.createdAt      = QDateTime::currentDateTime();
            m_bookmarkManager->addBookmark(bm);
            refreshBookmarkList();
            m_sideTabs->setCurrentWidget(m_bookmarkTree);
            statusBar()->showMessage(tr("しおりを追加しました"), 1500);
        });
}

void MainWindow::manageBookmarks() {
    if (!m_reader->isOpen()) {
        QMessageBox::information(this, tr("しおり"), tr("EPUBが開かれていません。"));
        return;
    }

    refreshBookmarkList();
    m_sideTabs->setVisible(true);
    m_sideTabs->setCurrentWidget(m_bookmarkTree);
    if (m_bookmarkTree->topLevelItemCount() == 0)
        QMessageBox::information(this, tr("しおり"), tr("しおりがありません。"));
}

void MainWindow::exportBookmarkBackup() {
    saveCurrentReadingPosition();

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString defaultName = QStringLiteral("bibi-backup-%1.bibi-backup.json").arg(stamp);
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("バックアップを書き出し"),
        QDir::home().filePath(defaultName),
        tr("Bibi バックアップ (*.bibi-backup.json);;JSON (*.json)"));
    if (filePath.isEmpty()) return;

    QString error;
    if (!m_bookmarkManager->exportBackup(filePath, &error)) {
        QMessageBox::warning(this, tr("バックアップ"),
                             tr("バックアップを書き出せませんでした。\n%1").arg(error));
        return;
    }

    statusBar()->showMessage(tr("バックアップを書き出しました"), 2000);
}

void MainWindow::importBookmarkBackup() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("バックアップを読み込み"),
        QDir::homePath(),
        tr("Bibi バックアップ (*.bibi-backup.json);;JSON (*.json)"));
    if (filePath.isEmpty()) return;

    const auto answer = QMessageBox::question(
        this,
        tr("バックアップを読み込み"),
        tr("バックアップを現在のしおり・読書位置にマージします。\n"
           "同じIDのしおりは上書きされ、読書位置は新しい日時のものが優先されます。\n\n"
           "読み込みますか？"));
    if (answer != QMessageBox::Yes) return;

    int bookmarkCount = 0;
    int readingPositionCount = 0;
    QString error;
    if (!m_bookmarkManager->importBackup(filePath, &error, &bookmarkCount, &readingPositionCount)) {
        QMessageBox::warning(this, tr("バックアップ"),
                             tr("バックアップを読み込めませんでした。\n%1").arg(error));
        return;
    }

    refreshBookmarkList();
    QMessageBox::information(
        this,
        tr("バックアップ"),
        tr("バックアップを読み込みました。\nしおり: %1件\n読書位置: %2件")
            .arg(bookmarkCount)
            .arg(readingPositionCount));
}

void MainWindow::refreshBookmarkList() {
    const bool sorting = m_bookmarkTree->isSortingEnabled();
    m_bookmarkTree->setSortingEnabled(false);
    m_bookmarkTree->clear();
    if (!m_reader->isOpen()) {
        m_bookmarkTree->setSortingEnabled(sorting);
        return;
    }

    const QList<Bookmark> bms = m_bookmarkManager->bookmarksForEpub(m_reader->filePath());
    for (const Bookmark& bm : bms) {
        const QString chapter = chapterLabel(bm.chapterIndex);
        const QString created = bm.createdAt.toString("yyyy/MM/dd HH:mm");
        const int progress = qRound(bm.scrollPosition * 100.0);
        auto* item = new BookmarkTreeItem(m_bookmarkTree);
        item->setText(0, bm.label);
        item->setText(1, chapter);
        item->setText(2, QString("%1%").arg(progress));
        item->setText(3, created);
        item->setData(0, Qt::UserRole, bm.id);
        item->setData(2, Qt::UserRole, progress);
        item->setData(3, Qt::UserRole, bm.createdAt);
        item->setToolTip(0, bm.label);
        item->setToolTip(1, chapter);
        item->setToolTip(2, tr("位置: %1%").arg(progress));
        item->setToolTip(3, created);
    }
    m_bookmarkTree->setSortingEnabled(sorting);
}

void MainWindow::onBookmarkActivated(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    const QString id = item->data(0, Qt::UserRole).toString();
    const QList<Bookmark> bms = m_bookmarkManager->bookmarksForEpub(m_reader->filePath());
    for (const Bookmark& bm : bms) {
        if (bm.id == id) {
            jumpToBookmark(bm);
            return;
        }
    }
}

void MainWindow::showBookmarkContextMenu(const QPoint& pos) {
    auto* item = m_bookmarkTree->itemAt(pos);
    if (!item || !m_reader->isOpen()) return;

    const QString id = item->data(0, Qt::UserRole).toString();
    const QList<Bookmark> bms = m_bookmarkManager->bookmarksForEpub(m_reader->filePath());
    Bookmark bm;
    bool found = false;
    for (const Bookmark& candidate : bms) {
        if (candidate.id == id) {
            bm = candidate;
            found = true;
            break;
        }
    }
    if (!found) return;

    QMenu menu(this);
    QAction* jumpAct = menu.addAction(tr("移動"));
    QAction* renameAct = menu.addAction(tr("名前変更..."));
    QAction* deleteAct = menu.addAction(tr("削除"));
    QAction* selected = menu.exec(m_bookmarkTree->viewport()->mapToGlobal(pos));

    if (selected == jumpAct) {
        jumpToBookmark(bm);
    } else if (selected == renameAct) {
        bool ok = false;
        QString label = QInputDialog::getText(
            this, tr("しおりの名前変更"), tr("しおりの名前:"),
            QLineEdit::Normal, bm.label, &ok);
        if (ok && m_bookmarkManager->renameBookmark(bm.id, label))
            refreshBookmarkList();
    } else if (selected == deleteAct) {
        const auto answer = QMessageBox::question(
            this, tr("しおりを削除"),
            tr("「%1」を削除しますか？").arg(bm.label),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer == QMessageBox::Yes &&
            m_bookmarkManager->removeBookmark(bm.id)) {
            refreshBookmarkList();
            statusBar()->showMessage(tr("しおりを削除しました"), 1500);
        }
    }
}

void MainWindow::jumpToBookmark(const Bookmark& bm) {
    goToChapter(bm.chapterIndex);

    const double pos = bm.scrollPosition;
    m_currentScrollPosition = pos;
    m_postSwapAction = [this, pos]() {
        m_activePage->runJavaScript(
            loadScript(":/scripts/bibi_scroll_to_position.js").arg(pos));
    };
}

void MainWindow::jumpToReadingPosition(const ReadingPosition& pos) {
    goToChapter(pos.chapterIndex);

    const double scrollPosition = pos.scrollPosition;
    m_currentScrollPosition = scrollPosition;
    m_postSwapAction = [this, scrollPosition]() {
        m_activePage->runJavaScript(
            loadScript(":/scripts/bibi_scroll_to_position.js").arg(scrollPosition));
    };
}

void MainWindow::saveCurrentReadingPosition() {
    if (!m_reader->isOpen() || m_currentChapter < 0) return;

    ReadingPosition pos;
    pos.epubPath       = m_reader->filePath();
    pos.chapterIndex   = m_currentChapter;
    pos.scrollPosition = m_currentScrollPosition;
    pos.updatedAt      = QDateTime::currentDateTime();
    m_bookmarkManager->saveReadingPosition(pos);
}

QString MainWindow::chapterLabel(int chapterIndex) const {
    if (m_reader->isOpen() && chapterIndex >= 0 && chapterIndex < m_reader->chapterCount()) {
        const QString href = m_reader->chapter(chapterIndex).href;
        QString title;

        std::function<bool(const QList<NavPoint>&)> findTitle =
            [&](const QList<NavPoint>& pts) -> bool {
                for (const NavPoint& np : pts) {
                    const QString path = np.src.section('#', 0, 0);
                    if (path == href && !np.label.trimmed().isEmpty()) {
                        title = np.label.trimmed();
                        return true;
                    }
                    if (findTitle(np.children)) return true;
                }
                return false;
            };

        if (findTitle(m_reader->toc()))
            return title;
    }
    return tr("Chapter %1").arg(chapterIndex + 1);
}
