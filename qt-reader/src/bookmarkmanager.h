#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>

struct Bookmark {
    QString   epubPath;
    int       chapterIndex   = 0;
    double    scrollPosition = 0.0; // 0.0–1.0
    QString   label;
    QDateTime createdAt;
};

class BookmarkManager : public QObject {
    Q_OBJECT
public:
    explicit BookmarkManager(QObject* parent = nullptr);

    void addBookmark(const Bookmark& bm);
    void removeBookmark(int globalIndex);
    QList<Bookmark> bookmarksForEpub(const QString& epubPath) const;

    void load();
    void save();

private:
    QString storagePath() const;
    QList<Bookmark> m_bookmarks;
};
