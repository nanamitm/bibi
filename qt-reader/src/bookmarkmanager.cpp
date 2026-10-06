#include "bookmarkmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QLockFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>
#include <QDebug>

namespace {
constexpr int kLockTimeoutMs = 5000;
constexpr int kStaleLockMs   = 30000;

QJsonObject bookmarkToJson(const Bookmark& bm) {
    QJsonObject obj;
    obj["id"]             = bm.id;
    obj["epubPath"]       = bm.epubPath;
    obj["chapterIndex"]   = bm.chapterIndex;
    obj["scrollPosition"] = bm.scrollPosition;
    obj["label"]          = bm.label;
    obj["createdAt"]      = bm.createdAt.toString(Qt::ISODate);
    return obj;
}

Bookmark bookmarkFromJson(const QJsonObject& obj) {
    Bookmark bm;
    bm.id             = obj["id"].toString();
    bm.epubPath       = normalizedEpubPath(obj["epubPath"].toString());
    bm.chapterIndex   = obj["chapterIndex"].toInt();
    bm.scrollPosition = qBound(0.0, obj["scrollPosition"].toDouble(), 1.0);
    bm.label          = obj["label"].toString();
    bm.createdAt      = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
    if (!bm.createdAt.isValid())
        bm.createdAt = QDateTime::currentDateTime();
    return bm;
}

QJsonObject readingPositionToJson(const ReadingPosition& pos) {
    QJsonObject obj;
    obj["epubPath"]       = pos.epubPath;
    obj["chapterIndex"]   = pos.chapterIndex;
    obj["scrollPosition"] = pos.scrollPosition;
    obj["updatedAt"]      = pos.updatedAt.toString(Qt::ISODate);
    return obj;
}

ReadingPosition readingPositionFromJson(const QJsonObject& obj) {
    ReadingPosition pos;
    pos.epubPath       = normalizedEpubPath(obj["epubPath"].toString());
    pos.chapterIndex   = obj["chapterIndex"].toInt();
    pos.scrollPosition = qBound(0.0, obj["scrollPosition"].toDouble(), 1.0);
    pos.updatedAt      = QDateTime::fromString(obj["updatedAt"].toString(), Qt::ISODate);
    if (!pos.updatedAt.isValid())
        pos.updatedAt = QDateTime::currentDateTime();
    return pos;
}

// Serialises access to the storage files across processes. Unlocks on destruction.
class StorageLock {
public:
    explicit StorageLock(const QString& path) : m_lock(path) {
        m_lock.setStaleLockTime(kStaleLockMs);
        if (!m_lock.tryLock(kLockTimeoutMs))
            qWarning() << "BookmarkManager: cannot acquire storage lock, continuing without it:"
                       << m_lock.error();
    }

private:
    QLockFile m_lock;
};

enum class ReadStatus { Missing, Unreadable, Corrupt, Ok };

ReadStatus readJsonArray(const QString& path, QJsonArray* out) {
    QFile f(path);
    if (!f.exists()) return ReadStatus::Missing;
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "BookmarkManager: cannot read" << path << ":" << f.errorString();
        return ReadStatus::Unreadable;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &error);
    f.close();
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        // Keep the broken file for manual recovery instead of silently overwriting it.
        const QString backup = path + ".corrupt-" +
            QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        if (QFile::rename(path, backup))
            qWarning() << "BookmarkManager:" << path << "is corrupt; moved to" << backup;
        else
            qWarning() << "BookmarkManager:" << path << "is corrupt and could not be moved aside";
        return ReadStatus::Corrupt;
    }

    *out = doc.array();
    return ReadStatus::Ok;
}

// Writes via a temporary file and atomic rename so a crash never leaves a truncated file.
bool writeFileAtomically(const QString& path, const QByteArray& data, QString* errorMessage) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = f.errorString();
        return false;
    }
    if (f.write(data) != data.size()) {
        if (errorMessage) *errorMessage = f.errorString();
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        if (errorMessage) *errorMessage = f.errorString();
        return false;
    }
    return true;
}
}

BookmarkManager::BookmarkManager(QObject* parent) : QObject(parent) {
    StorageLock lock(lockPath());
    loadBookmarks();
    loadReadingPositions();
}

QString BookmarkManager::storageDir() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

QString BookmarkManager::storagePath() const {
    return storageDir() + "/bookmarks.json";
}

QString BookmarkManager::readingPositionsPath() const {
    return storageDir() + "/reading-positions.json";
}

QString BookmarkManager::lockPath() const {
    return storageDir() + "/storage.lock";
}

void BookmarkManager::addBookmark(const Bookmark& bm) {
    StorageLock lock(lockPath());
    loadBookmarks();

    Bookmark normalized = bm;
    normalized.epubPath = normalizedEpubPath(normalized.epubPath);
    if (normalized.id.isEmpty())
        normalized.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_bookmarks.append(normalized);
    saveBookmarks();
}

bool BookmarkManager::removeBookmark(const QString& id) {
    if (id.isEmpty()) return false;

    StorageLock lock(lockPath());
    loadBookmarks();
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        if (m_bookmarks[i].id == id) {
            m_bookmarks.removeAt(i);
            saveBookmarks();
            return true;
        }
    }
    return false;
}

bool BookmarkManager::renameBookmark(const QString& id, const QString& label) {
    if (id.isEmpty()) return false;

    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty()) return false;

    StorageLock lock(lockPath());
    loadBookmarks();
    for (Bookmark& bm : m_bookmarks) {
        if (bm.id == id) {
            bm.label = trimmed;
            saveBookmarks();
            return true;
        }
    }
    return false;
}

QList<Bookmark> BookmarkManager::bookmarksForEpub(const QString& path) {
    const QString epubPath = normalizedEpubPath(path);
    StorageLock lock(lockPath());
    loadBookmarks();

    QList<Bookmark> result;
    for (const auto& bm : m_bookmarks) {
        if (bm.epubPath == epubPath) result.append(bm);
    }
    return result;
}

void BookmarkManager::saveReadingPosition(const ReadingPosition& pos) {
    if (pos.epubPath.isEmpty()) return;

    ReadingPosition normalized = pos;
    normalized.epubPath = normalizedEpubPath(normalized.epubPath);
    normalized.scrollPosition = qBound(0.0, normalized.scrollPosition, 1.0);
    if (!normalized.updatedAt.isValid())
        normalized.updatedAt = QDateTime::currentDateTime();

    StorageLock lock(lockPath());
    loadReadingPositions();
    for (ReadingPosition& existing : m_readingPositions) {
        if (existing.epubPath == normalized.epubPath) {
            existing = normalized;
            saveReadingPositions();
            return;
        }
    }

    m_readingPositions.append(normalized);
    saveReadingPositions();
}

bool BookmarkManager::readingPositionForEpub(const QString& path,
                                             ReadingPosition* pos) {
    const QString epubPath = normalizedEpubPath(path);
    StorageLock lock(lockPath());
    loadReadingPositions();
    for (const ReadingPosition& existing : m_readingPositions) {
        if (existing.epubPath == epubPath) {
            if (pos) *pos = existing;
            return true;
        }
    }
    return false;
}

void BookmarkManager::saveBookmarks() const {
    QJsonArray arr;
    for (const auto& bm : m_bookmarks)
        arr.append(bookmarkToJson(bm));
    QString error;
    if (!writeFileAtomically(storagePath(), QJsonDocument(arr).toJson(), &error))
        qWarning() << "BookmarkManager: cannot write bookmarks file:" << error;
}

void BookmarkManager::saveReadingPositions() const {
    QJsonArray arr;
    for (const ReadingPosition& pos : m_readingPositions)
        arr.append(readingPositionToJson(pos));
    QString error;
    if (!writeFileAtomically(readingPositionsPath(), QJsonDocument(arr).toJson(), &error))
        qWarning() << "BookmarkManager: cannot write reading-positions file:" << error;
}

void BookmarkManager::loadBookmarks() {
    QJsonArray arr;
    switch (readJsonArray(storagePath(), &arr)) {
    case ReadStatus::Missing:
        m_bookmarks.clear();
        return;
    case ReadStatus::Unreadable:
        return; // keep the last known state
    case ReadStatus::Corrupt:
        saveBookmarks(); // the broken file was moved aside; restore the last known state
        return;
    case ReadStatus::Ok:
        break;
    }

    QList<Bookmark> loaded;
    bool needsSave = false;
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        Bookmark bm = bookmarkFromJson(obj);
        if (bm.epubPath != obj["epubPath"].toString())
            needsSave = true; // migrate a non-normalized path written by older versions
        if (bm.id.isEmpty()) {
            bm.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            needsSave = true;
        }
        loaded.append(bm);
    }
    m_bookmarks = loaded;
    if (needsSave)
        saveBookmarks();
}

void BookmarkManager::loadReadingPositions() {
    QJsonArray arr;
    switch (readJsonArray(readingPositionsPath(), &arr)) {
    case ReadStatus::Missing:
        m_readingPositions.clear();
        return;
    case ReadStatus::Unreadable:
        return;
    case ReadStatus::Corrupt:
        saveReadingPositions();
        return;
    case ReadStatus::Ok:
        break;
    }

    QList<ReadingPosition> loaded;
    bool needsSave = false;
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        ReadingPosition pos = readingPositionFromJson(obj);
        if (pos.epubPath.isEmpty()) continue;
        if (pos.epubPath != obj["epubPath"].toString())
            needsSave = true; // migrate a non-normalized path written by older versions

        // Older versions could store one book under several spellings of its path;
        // after normalization keep only the most recent position.
        bool merged = false;
        for (ReadingPosition& existing : loaded) {
            if (existing.epubPath == pos.epubPath) {
                if (pos.updatedAt > existing.updatedAt)
                    existing = pos;
                merged = true;
                needsSave = true;
                break;
            }
        }
        if (!merged)
            loaded.append(pos);
    }
    m_readingPositions = loaded;
    if (needsSave)
        saveReadingPositions();
}

bool BookmarkManager::exportBackup(const QString& filePath, QString* errorMessage) {
    StorageLock lock(lockPath());
    loadBookmarks();
    loadReadingPositions();

    QJsonArray bookmarks;
    for (const Bookmark& bm : m_bookmarks)
        bookmarks.append(bookmarkToJson(bm));

    QJsonArray readingPositions;
    for (const ReadingPosition& pos : m_readingPositions)
        readingPositions.append(readingPositionToJson(pos));

    QJsonObject root;
    root["format"] = "BibiQtReaderBackup";
    root["version"] = 1;
    root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["bookmarks"] = bookmarks;
    root["readingPositions"] = readingPositions;

    return writeFileAtomically(filePath, QJsonDocument(root).toJson(QJsonDocument::Indented),
                               errorMessage);
}

bool BookmarkManager::importBackup(const QString& filePath, QString* errorMessage,
                                   int* importedBookmarks, int* importedReadingPositions) {
    if (importedBookmarks) *importedBookmarks = 0;
    if (importedReadingPositions) *importedReadingPositions = 0;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = f.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) *errorMessage = parseError.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    if (root["format"].toString() != "BibiQtReaderBackup" ||
        root["version"].toInt() != 1) {
        if (errorMessage) *errorMessage = tr("対応していないバックアップ形式です。");
        return false;
    }

    StorageLock lock(lockPath());
    loadBookmarks();
    loadReadingPositions();

    for (const QJsonValue& value : root["bookmarks"].toArray()) {
        Bookmark bm = bookmarkFromJson(value.toObject());
        if (bm.epubPath.isEmpty()) continue;
        if (bm.id.isEmpty())
            bm.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

        bool replaced = false;
        for (Bookmark& existing : m_bookmarks) {
            if (existing.id == bm.id) {
                existing = bm;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            m_bookmarks.append(bm);
        if (importedBookmarks) ++(*importedBookmarks);
    }

    for (const QJsonValue& value : root["readingPositions"].toArray()) {
        ReadingPosition pos = readingPositionFromJson(value.toObject());
        if (pos.epubPath.isEmpty()) continue;

        bool replaced = false;
        for (ReadingPosition& existing : m_readingPositions) {
            if (existing.epubPath == pos.epubPath) {
                if (!existing.updatedAt.isValid() ||
                    !pos.updatedAt.isValid() ||
                    pos.updatedAt >= existing.updatedAt) {
                    existing = pos;
                    if (importedReadingPositions) ++(*importedReadingPositions);
                }
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_readingPositions.append(pos);
            if (importedReadingPositions) ++(*importedReadingPositions);
        }
    }

    saveBookmarks();
    saveReadingPositions();
    return true;
}
