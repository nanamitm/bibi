#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QMap>
#include <QHash>
#include <QMutex>
#include <list>

struct NavPoint {
    QString label;
    QString src;   // epub-root-relative path, may include #fragment
    QList<NavPoint> children;
};

struct EpubMetadata {
    QString title;
    QString creator;
    QString language;
    QString identifier;
    QString publisher;
    QString pageProgressionDirection; // ltr, rtl, default
};

struct EpubChapter {
    QString href;       // epub-root-relative path
    QString mediaType;
    bool    linear = true;
};

class EpubReader : public QObject {
    Q_OBJECT
public:
    explicit EpubReader(QObject* parent = nullptr);
    ~EpubReader();

    bool open(const QString& filePath);
    void close();
    bool isOpen() const;

    QString       filePath() const;
    QString       lastError() const;
    EpubMetadata  metadata() const;
    QList<NavPoint> toc() const;
    int           chapterCount() const;
    EpubChapter   chapter(int index) const;

    QByteArray fileData(const QString& epubPath) const;
    QString    mimeTypeForPath(const QString& epubPath) const;

    // 前後チャプターのリソースをバックグラウンドで先読みしキャッシュに格納する
    void prefetchChapter(int index) const;

    struct SearchResult {
        int     chapterIndex;
        int     occurrenceIndex = 0;
        QString chapterTitle;
        QString context;
        QString href;
    };
    QList<SearchResult> search(const QString& query) const;

private:
    bool parseContainer();
    bool parseOpf(const QString& opfPath);
    bool parseTocNcx(const QString& ncxPath);
    bool parseNavXhtml(const QString& navPath);

    QList<NavPoint> parseNavList(const class QDomElement& olEl,
                                 const QString& baseDir) const;

    static QString resolvePath(const QString& baseDir, const QString& rel);

    QString         m_filePath;
    QString         m_lastError;
    EpubMetadata    m_metadata;
    QList<NavPoint> m_toc;
    QList<EpubChapter> m_spine;
    QMap<QString, QString> m_idToHref;   // manifest id -> epub-root-relative href
    QMap<QString, QString> m_mimeTypes;  // epub-root-relative href -> mediaType
    QString         m_opfDir;

    // LRU キャッシュ（100MB上限、超過時は最古エントリから追い出す）
    using LruList = std::list<std::pair<QString, QByteArray>>;
    mutable LruList                            m_lruList;   // front = 最近使用
    mutable QHash<QString, LruList::iterator>  m_lruIndex;  // key → リスト位置
    mutable QMutex                             m_cacheLock;
    mutable qint64                             m_cacheBytes = 0;
    mutable QMutex                             m_mimeLock;  // protects m_mimeTypes (IO thread)
    static constexpr qint64                    kCacheLimit  = 100LL * 1024 * 1024;

    struct ZipImpl;
    ZipImpl* m_zip = nullptr;
};
