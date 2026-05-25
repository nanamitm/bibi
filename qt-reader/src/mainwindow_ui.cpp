#include "mainwindow.h"
#include "mainwindow_private.h"
#include <QWebEngineView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFileSystemModel>
#include <QTreeView>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QPalette>
#include <QTabWidget>
#include <QTimer>

void MainWindow::updateStatus() {
    if (!m_reader->isOpen() || m_currentChapter < 0) {
        m_statusLabel->clear();
        m_progressBar->setValue(0);
        return;
    }

    const int chapterCount = qMax(1, m_reader->chapterCount());
    const int chapterProgress = qRound(m_currentScrollPosition * 100.0);
    const double overall = (m_currentChapter + m_currentScrollPosition) / chapterCount;
    const QString chapterTitle = chapterLabel(m_currentChapter);
    m_statusLabel->setText(
        tr("章 %1/%2 ・ %3 ・ 章内 %4% ・ ズーム %5%")
            .arg(m_currentChapter + 1)
            .arg(m_reader->chapterCount())
            .arg(chapterTitle)
            .arg(chapterProgress)
            .arg(qRound(m_zoomFactor * 100)));
    m_statusLabel->setToolTip(chapterTitle);
    m_progressBar->setValue(qBound(0, qRound(overall * 1000.0), 1000));
}

void MainWindow::updatePageBackgroundColor() {
    const QColor color = palette().color(QPalette::Window);
    for (QWebEngineView* view : {m_viewA, m_viewB, m_viewC}) {
        if (!view || !view->page()) continue;
        view->page()->setBackgroundColor(m_loadedViews.contains(view) ? Qt::white : color);
    }
    if (m_viewContainer)
        m_viewContainer->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name()));
}

void MainWindow::updateNavigationActions() {
    bool has     = m_reader->isOpen();
    bool hasPrev = has && m_currentChapter > 0;
    bool hasNext = has && m_currentChapter >= 0
                       && m_currentChapter < m_reader->chapterCount() - 1;

    m_coverAct->setEnabled(has && m_currentChapter > 0);
    m_leftAct->setEnabled(m_isRtl ? hasNext : hasPrev);
    m_rightAct->setEnabled(m_isRtl ? hasPrev : hasNext);
    m_endAct->setEnabled(has && m_currentChapter >= 0
                             && m_currentChapter < m_reader->chapterCount() - 1);
    m_bookmarkAct->setEnabled(has && m_currentChapter >= 0);
}

void MainWindow::updateWindowTitle() {
    if (m_reader->isOpen() && !m_reader->metadata().title.isEmpty())
        setWindowTitle(tr("%1 — Bibi Qt Reader").arg(m_reader->metadata().title));
    else
        setWindowTitle(tr("Bibi Qt Reader"));
}

void MainWindow::setZoomFactor(double zoomFactor, bool saveSetting) {
    m_zoomFactor = qBound(0.25, zoomFactor, 4.0);
    m_zoomFactor = qRound(m_zoomFactor * 100.0) / 100.0;
    if (saveSetting) {
        QSettings s;
        s.setValue(kZoomFactorSettingKey, m_zoomFactor);
    }
    applyZoomToViews();
    updateStatus();
}

void MainWindow::setPreloadMode(PreloadMode mode, bool saveSetting) {
    if (m_preloadMode == mode) {
        if (saveSetting) {
            QSettings s;
            s.setValue(kPreloadModeSettingKey, preloadModeToIndex(mode));
        }
        return;
    }

    m_preloadMode = mode;
    if (saveSetting) {
        QSettings s;
        s.setValue(kPreloadModeSettingKey, preloadModeToIndex(mode));
    }

    invalidatePreloads();
    QTimer::singleShot(0, this, &MainWindow::refreshPreloads);
}

int MainWindow::preloadModeToIndex(PreloadMode mode) const {
    switch (mode) {
    case PreloadMode::None: return 0;
    case PreloadMode::NextOnly: return 1;
    case PreloadMode::NextAndPrevious: return 2;
    }
    return 2;
}

MainWindow::PreloadMode MainWindow::preloadModeFromIndex(int index) const {
    switch (index) {
    case 0: return PreloadMode::None;
    case 1: return PreloadMode::NextOnly;
    case 2: return PreloadMode::NextAndPrevious;
    default: return PreloadMode::NextAndPrevious;
    }
}

void MainWindow::applyCtrlWheelZoom(int delta) {
    if (delta > 0)
        setZoomFactor(m_zoomFactor + 0.1);
    else if (delta < 0)
        setZoomFactor(m_zoomFactor - 0.1);
}

void MainWindow::showSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("設定"));

    auto* layout = new QFormLayout(&dlg);

    auto* zoomSpin = new QSpinBox(&dlg);
    zoomSpin->setRange(25, 400);
    zoomSpin->setSingleStep(10);
    zoomSpin->setSuffix("%");
    zoomSpin->setValue(qRound(m_zoomFactor * 100.0));
    layout->addRow(tr("ズーム率"), zoomSpin);

    auto* preloadCombo = new QComboBox(&dlg);
    preloadCombo->addItem(tr("なし（省メモリ）"));
    preloadCombo->addItem(tr("次ページのみ（標準）"));
    preloadCombo->addItem(tr("前後ページ（高速）"));
    preloadCombo->setCurrentIndex(preloadModeToIndex(m_preloadMode));
    layout->addRow(tr("先読み"), preloadCombo);

    auto* pixelSwapCheck = new QCheckBox(tr("ページ描画を待ってから切り替え（推奨）"), &dlg);
    pixelSwapCheck->setChecked(m_pixelSwapDetection);
    pixelSwapCheck->setToolTip(tr(
        "有効: ページが描画されたことを確認してから切り替えます（チラつきを抑えます）\n"
        "無効: 描画を待たずに即切り替えます（切り替え速度優先）"));
    layout->addRow(tr("ページ切り替え"), pixelSwapCheck);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    setZoomFactor(zoomSpin->value() / 100.0);
    setPreloadMode(preloadModeFromIndex(preloadCombo->currentIndex()));

    const bool newPixelSwap = pixelSwapCheck->isChecked();
    if (newPixelSwap != m_pixelSwapDetection) {
        m_pixelSwapDetection = newPixelSwap;
        QSettings s;
        s.setValue(kPixelSwapDetectionSettingKey, m_pixelSwapDetection);
    }
}

void MainWindow::populateToc(const QList<NavPoint>& pts, QTreeWidgetItem* parent) {
    for (const auto& np : pts) {
        QTreeWidgetItem* item = parent
            ? new QTreeWidgetItem(parent)
            : new QTreeWidgetItem(m_tocTree);
        item->setText(0, np.label);
        item->setData(0, Qt::UserRole, np.src);
        populateToc(np.children, item);
    }
}

void MainWindow::onTocItemClicked(QTreeWidgetItem* item, int /*column*/) {
    QString src = item->data(0, Qt::UserRole).toString();
    if (!src.isEmpty()) goToHref(src);
}

void MainWindow::chooseFolderRoot() {
    const QString currentRoot = m_folderModel ? m_folderModel->rootPath() : QDir::homePath();
    const QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("フォルダーを選択"),
        currentRoot.isEmpty() ? QDir::homePath() : currentRoot);
    if (folder.isEmpty()) return;
    setFolderRoot(folder);
    m_sideTabs->setCurrentWidget(m_folderTab);
}

void MainWindow::onFolderFileActivated(const QModelIndex& index) {
    if (!m_folderModel || !index.isValid()) return;

    const QFileInfo info = m_folderModel->fileInfo(index);
    if (info.isDir()) {
        m_folderTree->setExpanded(index, !m_folderTree->isExpanded(index));
        return;
    }

    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("epub") || suffix == QStringLiteral("zip"))
        openEpub(info.absoluteFilePath());
}

void MainWindow::setFolderRoot(const QString& path, bool saveSetting) {
    if (!m_folderModel || !m_folderTree) return;

    const QString rootPath = QDir(path).exists() ? QDir(path).absolutePath() : QDir::homePath();
    const QModelIndex rootIndex = m_folderModel->setRootPath(rootPath);
    m_folderTree->setRootIndex(rootIndex);
    if (saveSetting) {
        QSettings s;
        s.setValue(kFolderRootSettingKey, rootPath);
        m_folderRootConfigured = true;
    }
}

void MainWindow::selectFolderFile(const QString& filePath) {
    if (!m_folderModel || !m_folderTree) return;

    const QFileInfo info(filePath);
    if (!info.exists()) return;

    const QModelIndex index = m_folderModel->index(info.absoluteFilePath());
    if (!index.isValid()) return;

    m_folderTree->scrollTo(index, QAbstractItemView::PositionAtCenter);
    m_folderTree->selectionModel()->select(
        index,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_folderTree->setCurrentIndex(index);
}

void MainWindow::showReaderContextMenu(QWebEngineView* view, const QPoint& pos) {
    if (view != m_activeView || !m_reader->isOpen()) return;

    QMenu menu(this);

    QAction* copyAct = nullptr;
    if (!view->page()->selectedText().isEmpty()) {
        copyAct = menu.addAction(tr("コピー"));
        menu.addSeparator();
    }

    QAction* bookmarkAct = menu.addAction(tr("しおりを追加"));
    menu.addSeparator();

    QAction* coverAct = menu.addAction(tr("表紙へ"));
    QAction* prevAct = menu.addAction(m_isRtl ? tr("前へ") : tr("前へ"));
    QAction* nextAct = menu.addAction(m_isRtl ? tr("次へ") : tr("次へ"));
    QAction* endAct = menu.addAction(tr("末尾へ"));
    coverAct->setEnabled(m_coverAct->isEnabled());
    prevAct->setEnabled(m_isRtl ? m_rightAct->isEnabled() : m_leftAct->isEnabled());
    nextAct->setEnabled(m_isRtl ? m_leftAct->isEnabled() : m_rightAct->isEnabled());
    endAct->setEnabled(m_endAct->isEnabled());

    menu.addSeparator();
    QAction* zoomInAct = menu.addAction(tr("拡大"));
    QAction* zoomOutAct = menu.addAction(tr("縮小"));
    QAction* resetZoomAct = menu.addAction(tr("標準サイズ"));

    menu.addSeparator();
    QAction* searchAct = menu.addAction(tr("検索"));

    QAction* selected = menu.exec(view->mapToGlobal(pos));
    if (!selected) return;

    if (selected == copyAct) {
        view->page()->triggerAction(QWebEnginePage::Copy);
    } else if (selected == bookmarkAct) {
        addBookmark();
    } else if (selected == coverAct) {
        goToCover();
    } else if (selected == prevAct) {
        prevChapter();
    } else if (selected == nextAct) {
        nextChapter();
    } else if (selected == endAct) {
        goToBookEnd();
    } else if (selected == zoomInAct) {
        setZoomFactor(m_zoomFactor + 0.1);
    } else if (selected == zoomOutAct) {
        setZoomFactor(m_zoomFactor - 0.1);
    } else if (selected == resetZoomAct) {
        setZoomFactor(1.0);
    } else if (selected == searchAct) {
        showSearch();
    }
}
