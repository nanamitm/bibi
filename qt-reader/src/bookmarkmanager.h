#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>

struct Bookmark {
    QString   id;
    QString   epubPath;
    int       chapterIndex   = 0;
    double    scrollPosition = 0.0; // 0.0–1.0
    QString   label;
    QDateTime createdAt;
};

struct ReadingPosition {
    QString   epubPath;
    int       chapterIndex   = 0;
    double    scrollPosition = 0.0; // 0.0–1.0
    QDateTime updatedAt;
};

class BookmarkManager : public QObject {
    Q_OBJECT
public:
    explicit BookmarkManager(QObject* parent = nullptr);

    void addBookmark(const Bookmark& bm);
    bool removeBookmark(const QString& id);
    bool renameBookmark(const QString& id, const QString& label);
    QList<Bookmark> bookmarksForEpub(const QString& epubPath) const;

    void saveReadingPosition(const ReadingPosition& pos);
    bool readingPositionForEpub(const QString& epubPath, ReadingPosition* pos) const;

    void load();
    void save();

private:
    QString storagePath() const;
    QString readingPositionsPath() const;
    void loadReadingPositions();
    void saveReadingPositions() const;

    QList<Bookmark> m_bookmarks;
    QList<ReadingPosition> m_readingPositions;
};
