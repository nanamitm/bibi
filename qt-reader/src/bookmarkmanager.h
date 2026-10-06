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

// しおり・読書位置の永続化。
// 複数プロセスが同じファイルを共有しても互いの変更を消さないよう、
// 各操作はロックを取ってディスクから読み直し、変更を加えてから原子的に書き戻す。
class BookmarkManager : public QObject {
    Q_OBJECT
public:
    explicit BookmarkManager(QObject* parent = nullptr);

    void addBookmark(const Bookmark& bm);
    bool removeBookmark(const QString& id);
    bool renameBookmark(const QString& id, const QString& label);
    QList<Bookmark> bookmarksForEpub(const QString& epubPath);

    void saveReadingPosition(const ReadingPosition& pos);
    bool readingPositionForEpub(const QString& epubPath, ReadingPosition* pos);

    bool exportBackup(const QString& filePath, QString* errorMessage = nullptr);
    bool importBackup(const QString& filePath, QString* errorMessage = nullptr,
                      int* importedBookmarks = nullptr,
                      int* importedReadingPositions = nullptr);

private:
    QString storageDir() const;
    QString storagePath() const;
    QString readingPositionsPath() const;
    QString lockPath() const;

    // 呼び出し側でストレージロックを保持していること
    void loadBookmarks();
    void loadReadingPositions();
    void saveBookmarks() const;
    void saveReadingPositions() const;

    QList<Bookmark> m_bookmarks;
    QList<ReadingPosition> m_readingPositions;
};
