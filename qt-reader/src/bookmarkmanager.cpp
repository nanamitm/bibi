#include "bookmarkmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>

namespace {
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
    bm.epubPath       = obj["epubPath"].toString();
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
    pos.epubPath       = obj["epubPath"].toString();
    pos.chapterIndex   = obj["chapterIndex"].toInt();
    pos.scrollPosition = qBound(0.0, obj["scrollPosition"].toDouble(), 1.0);
    pos.updatedAt      = QDateTime::fromString(obj["updatedAt"].toString(), Qt::ISODate);
    if (!pos.updatedAt.isValid())
        pos.updatedAt = QDateTime::currentDateTime();
    return pos;
}
}

BookmarkManager::BookmarkManager(QObject* parent) : QObject(parent) {
    load();
    loadReadingPositions();
}

QString BookmarkManager::storagePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/bookmarks.json";
}

QString BookmarkManager::readingPositionsPath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/reading-positions.json";
}

void BookmarkManager::addBookmark(const Bookmark& bm) {
    Bookmark normalized = bm;
    if (normalized.id.isEmpty())
        normalized.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_bookmarks.append(normalized);
    save();
}

bool BookmarkManager::removeBookmark(const QString& id) {
    if (id.isEmpty()) return false;

    for (int i = 0; i < m_bookmarks.size(); ++i) {
        if (m_bookmarks[i].id == id) {
            m_bookmarks.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

bool BookmarkManager::renameBookmark(const QString& id, const QString& label) {
    if (id.isEmpty()) return false;

    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty()) return false;

    for (Bookmark& bm : m_bookmarks) {
        if (bm.id == id) {
            bm.label = trimmed;
            save();
            return true;
        }
    }
    return false;
}

QList<Bookmark> BookmarkManager::bookmarksForEpub(const QString& epubPath) const {
    QList<Bookmark> result;
    for (const auto& bm : m_bookmarks) {
        if (bm.epubPath == epubPath) result.append(bm);
    }
    return result;
}

void BookmarkManager::saveReadingPosition(const ReadingPosition& pos) {
    if (pos.epubPath.isEmpty()) return;

    ReadingPosition normalized = pos;
    normalized.scrollPosition = qBound(0.0, normalized.scrollPosition, 1.0);
    if (!normalized.updatedAt.isValid())
        normalized.updatedAt = QDateTime::currentDateTime();

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

bool BookmarkManager::readingPositionForEpub(const QString& epubPath,
                                             ReadingPosition* pos) const {
    for (const ReadingPosition& existing : m_readingPositions) {
        if (existing.epubPath == epubPath) {
            if (pos) *pos = existing;
            return true;
        }
    }
    return false;
}

void BookmarkManager::save() {
    QJsonArray arr;
    for (const auto& bm : m_bookmarks)
        arr.append(bookmarkToJson(bm));
    QFile f(storagePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void BookmarkManager::saveReadingPositions() const {
    QJsonArray arr;
    for (const ReadingPosition& pos : m_readingPositions)
        arr.append(readingPositionToJson(pos));
    QFile f(readingPositionsPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void BookmarkManager::load() {
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;

    m_bookmarks.clear();
    bool needsSave = false;
    for (const QJsonValue& v : doc.array()) {
        Bookmark bm = bookmarkFromJson(v.toObject());
        if (bm.id.isEmpty()) {
            bm.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            needsSave = true;
        }
        m_bookmarks.append(bm);
    }
    if (needsSave)
        save();
}

void BookmarkManager::loadReadingPositions() {
    QFile f(readingPositionsPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;

    m_readingPositions.clear();
    for (const QJsonValue& v : doc.array()) {
        ReadingPosition pos = readingPositionFromJson(v.toObject());
        if (!pos.epubPath.isEmpty())
            m_readingPositions.append(pos);
    }
}

bool BookmarkManager::exportBackup(const QString& filePath, QString* errorMessage) const {
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

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
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

    save();
    saveReadingPositions();
    return true;
}
