#include "recentfiles.h"

#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>

RecentFiles::RecentFiles(QObject* parent)
    : QObject(parent)
{
    load();
}

void RecentFiles::attachToMenu(QMenu* fileMenu) {
    if (!fileMenu) return;

    m_menu = fileMenu->addMenu(tr("最近開いたファイル"));
    for (int i = 0; i < 10; ++i) {
        QAction* act = m_menu->addAction(QString());
        act->setVisible(false);
        connect(act, &QAction::triggered, this, [this, act] {
            const QString path = act->data().toString();
            if (path.isEmpty()) return;
            if (!QFileInfo::exists(path)) {
                removeFile(path);
                emit missingFile(path);
                return;
            }
            emit openRequested(path);
        });
        m_fileActs.append(act);
    }

    m_menu->addSeparator();
    m_clearAct = m_menu->addAction(tr("履歴を消去"));
    connect(m_clearAct, &QAction::triggered, this, [this] {
        m_files.clear();
        save();
        updateMenu();
    });

    updateMenu();
}

void RecentFiles::addFile(const QString& filePath) {
    const QString path = QFileInfo(filePath).absoluteFilePath();
    if (path.isEmpty()) return;

    m_files.removeAll(path);
    m_files.prepend(path);
    while (m_files.size() > 10)
        m_files.removeLast();
    save();
    updateMenu();
}

void RecentFiles::load() {
    QSettings s;
    m_files = s.value("recentFiles").toStringList();
    m_files.removeDuplicates();
    while (m_files.size() > 10)
        m_files.removeLast();
}

void RecentFiles::save() const {
    QSettings s;
    s.setValue("recentFiles", m_files);
}

void RecentFiles::removeFile(const QString& filePath) {
    if (m_files.removeAll(filePath) > 0) {
        save();
        updateMenu();
    }
}

void RecentFiles::updateMenu() {
    if (!m_menu) return;

    const int count = qMin(m_files.size(), m_fileActs.size());
    for (int i = 0; i < m_fileActs.size(); ++i) {
        QAction* act = m_fileActs[i];
        if (i < count) {
            const QString path = m_files[i];
            act->setText(tr("%1  %2").arg(i + 1).arg(QFileInfo(path).fileName()));
            act->setToolTip(path);
            act->setData(path);
            act->setVisible(true);
        } else {
            act->setVisible(false);
            act->setData({});
        }
    }

    const bool hasRecent = !m_files.isEmpty();
    m_menu->setEnabled(hasRecent);
    if (m_clearAct)
        m_clearAct->setEnabled(hasRecent);
}
