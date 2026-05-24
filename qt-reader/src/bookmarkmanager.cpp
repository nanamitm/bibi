#include "bookmarkmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

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
    m_bookmarks.append(bm);
    save();
}

void BookmarkManager::removeBookmark(int globalIndex) {
    if (globalIndex >= 0 && globalIndex < m_bookmarks.size()) {
        m_bookmarks.removeAt(globalIndex);
        save();
    }
}

bool BookmarkManager::removeBookmark(const QString& epubPath, const QDateTime& createdAt) {
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        const Bookmark& bm = m_bookmarks[i];
        if (bm.epubPath == epubPath && bm.createdAt == createdAt) {
            m_bookmarks.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

bool BookmarkManager::renameBookmark(const QString& epubPath, const QDateTime& createdAt,
                                     const QString& label) {
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty()) return false;

    for (Bookmark& bm : m_bookmarks) {
        if (bm.epubPath == epubPath && bm.createdAt == createdAt) {
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

bool BookmarkManager::clearReadingPosition(const QString& epubPath) {
    for (int i = 0; i < m_readingPositions.size(); ++i) {
        if (m_readingPositions[i].epubPath == epubPath) {
            m_readingPositions.removeAt(i);
            saveReadingPositions();
            return true;
        }
    }
    return false;
}

void BookmarkManager::save() {
    QJsonArray arr;
    for (const auto& bm : m_bookmarks) {
        QJsonObject obj;
        obj["epubPath"]       = bm.epubPath;
        obj["chapterIndex"]   = bm.chapterIndex;
        obj["scrollPosition"] = bm.scrollPosition;
        obj["label"]          = bm.label;
        obj["createdAt"]      = bm.createdAt.toString(Qt::ISODate);
        arr.append(obj);
    }
    QFile f(storagePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void BookmarkManager::saveReadingPositions() const {
    QJsonArray arr;
    for (const ReadingPosition& pos : m_readingPositions) {
        QJsonObject obj;
        obj["epubPath"]       = pos.epubPath;
        obj["chapterIndex"]   = pos.chapterIndex;
        obj["scrollPosition"] = pos.scrollPosition;
        obj["updatedAt"]      = pos.updatedAt.toString(Qt::ISODate);
        arr.append(obj);
    }
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
    for (const QJsonValue& v : doc.array()) {
        QJsonObject obj = v.toObject();
        Bookmark bm;
        bm.epubPath       = obj["epubPath"].toString();
        bm.chapterIndex   = obj["chapterIndex"].toInt();
        bm.scrollPosition = obj["scrollPosition"].toDouble();
        bm.label          = obj["label"].toString();
        bm.createdAt      = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
        m_bookmarks.append(bm);
    }
}

void BookmarkManager::loadReadingPositions() {
    QFile f(readingPositionsPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;

    m_readingPositions.clear();
    for (const QJsonValue& v : doc.array()) {
        QJsonObject obj = v.toObject();
        ReadingPosition pos;
        pos.epubPath       = obj["epubPath"].toString();
        pos.chapterIndex   = obj["chapterIndex"].toInt();
        pos.scrollPosition = obj["scrollPosition"].toDouble();
        pos.updatedAt      = QDateTime::fromString(obj["updatedAt"].toString(), Qt::ISODate);
        if (!pos.epubPath.isEmpty())
            m_readingPositions.append(pos);
    }
}
