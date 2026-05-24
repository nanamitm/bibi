#pragma once

#include <QObject>
#include <QList>
#include <QStringList>

class QAction;
class QMenu;

class RecentFiles : public QObject {
    Q_OBJECT
public:
    explicit RecentFiles(QObject* parent = nullptr);

    void attachToMenu(QMenu* fileMenu);
    void addFile(const QString& filePath);

signals:
    void openRequested(const QString& filePath);
    void missingFile(const QString& filePath);

private:
    void load();
    void save() const;
    void removeFile(const QString& filePath);
    void updateMenu();

    QMenu* m_menu = nullptr;
    QAction* m_clearAct = nullptr;
    QList<QAction*> m_fileActs;
    QStringList m_files;
};
