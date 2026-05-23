#include "bookmarkmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

BookmarkManager::BookmarkManager(QObject* parent) : QObject(parent) {
    load();
}

QString BookmarkManager::storagePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/bookmarks.json";
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

QList<Bookmark> BookmarkManager::bookmarksForEpub(const QString& epubPath) const {
    QList<Bookmark> result;
    for (const auto& bm : m_bookmarks) {
        if (bm.epubPath == epubPath) result.append(bm);
    }
    return result;
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
