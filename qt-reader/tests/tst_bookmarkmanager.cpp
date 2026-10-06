#include "bookmarkmanager.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

// An absolute, already normalized path on the current platform ("C:/Books/x" or "/Books/x").
QString bookPath(const QString& name) {
    return QDir::rootPath() + "Books/" + name;
}

// The same file spelled the way older versions could have stored it: with
// backslashes on Windows (command line / file association), with ".." elsewhere.
QString legacySpelling(const QString& name) {
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(bookPath(name));
#else
    return QDir::rootPath() + "Books/sub/../" + name;
#endif
}

QString jsonEscaped(QString path) {
    return path.replace("\\", "\\\\");
}

void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace

class TestBookmarkManager : public QObject {
    Q_OBJECT

private:
    QString m_dir;

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        m_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    void init() {
        QDir(m_dir).removeRecursively();
        QDir().mkpath(m_dir);
    }

    void cleanupTestCase() {
        QDir(m_dir).removeRecursively();
    }

    // Two instances (e.g. two books opened from the file manager) must not
    // discard each other's changes.
    void instancesMergeInsteadOfOverwriting() {
        BookmarkManager a, b;

        Bookmark first;
        first.epubPath = bookPath("book.epub");
        first.label = "A";
        a.addBookmark(first);
        Bookmark second;
        second.epubPath = bookPath("book.epub");
        second.label = "B";
        b.addBookmark(second);
        QCOMPARE(a.bookmarksForEpub(bookPath("book.epub")).size(), 2);

        ReadingPosition one;
        one.epubPath = bookPath("one.epub");
        one.chapterIndex = 3;
        ReadingPosition two;
        two.epubPath = bookPath("two.epub");
        two.chapterIndex = 5;
        a.saveReadingPosition(one);
        b.saveReadingPosition(two);

        ReadingPosition read;
        QVERIFY(a.readingPositionForEpub(bookPath("one.epub"), &read));
        QCOMPARE(read.chapterIndex, 3);
        QVERIFY(a.readingPositionForEpub(bookPath("two.epub"), &read));
        QCOMPARE(read.chapterIndex, 5);
        QVERIFY(!QFile::exists(m_dir + "/storage.lock"));
    }

    void corruptFileIsMovedAsideNotWiped() {
        BookmarkManager manager;
        Bookmark bm;
        bm.epubPath = bookPath("book.epub");
        manager.addBookmark(bm);
        manager.addBookmark(bm);

        writeFile(m_dir + "/bookmarks.json", "{ broken");
        manager.addBookmark(bm);

        QCOMPARE(manager.bookmarksForEpub(bookPath("book.epub")).size(), 3);
        QVERIFY(!QDir(m_dir).entryList({"bookmarks.json.corrupt-*"}).isEmpty());
    }

    void backupRoundTrip() {
        BookmarkManager manager;
        Bookmark bm;
        bm.epubPath = bookPath("book.epub");
        bm.label = "x";
        manager.addBookmark(bm);
        ReadingPosition pos;
        pos.epubPath = bookPath("book.epub");
        pos.chapterIndex = 2;
        manager.saveReadingPosition(pos);

        const QString backup = m_dir + "/backup.json";
        QString error;
        QVERIFY2(manager.exportBackup(backup, &error), qPrintable(error));

        QDir(m_dir).remove("bookmarks.json");
        QDir(m_dir).remove("reading-positions.json");
        int bookmarks = 0;
        int positions = 0;
        QVERIFY2(manager.importBackup(backup, &error, &bookmarks, &positions), qPrintable(error));
        QCOMPARE(bookmarks, 1);
        QCOMPARE(positions, 1);
        QCOMPARE(manager.bookmarksForEpub(bookPath("book.epub")).size(), 1);
    }

    void rejectsForeignBackup() {
        const QString path = m_dir + "/foreign.json";
        writeFile(path, R"({"format":"SomethingElse","version":1})");
        BookmarkManager manager;
        QString error;
        QVERIFY(!manager.importBackup(path, &error));
        QVERIFY(!error.isEmpty());
    }

    void normalizesEpubPaths() {
        QCOMPARE(normalizedEpubPath(legacySpelling("a.epub")), bookPath("a.epub"));
        QCOMPARE(normalizedEpubPath(QDir::rootPath() + "Books/x/../a.epub"), bookPath("a.epub"));
        QCOMPARE(normalizedEpubPath(QString()), QString());
    }

    // Data written by older versions used whatever spelling the path arrived in.
    void migratesLegacyPathKeys() {
        const QString legacy = legacySpelling("a.epub");
        const QString canonical = bookPath("a.epub");
        writeFile(m_dir + "/reading-positions.json",
                  QString("[{\"epubPath\":\"%1\",\"chapterIndex\":7,\"updatedAt\":\"2026-01-02T00:00:00\"},"
                          " {\"epubPath\":\"%2\",\"chapterIndex\":2,\"updatedAt\":\"2026-01-01T00:00:00\"}]")
                      .arg(jsonEscaped(legacy), canonical).toUtf8());
        writeFile(m_dir + "/bookmarks.json",
                  QString("[{\"id\":\"x\",\"epubPath\":\"%1\",\"chapterIndex\":1,\"label\":\"L\"}]")
                      .arg(jsonEscaped(legacy)).toUtf8());

        BookmarkManager manager;
        ReadingPosition read;
        QVERIFY(manager.readingPositionForEpub(canonical, &read));
        QCOMPARE(read.chapterIndex, 7); // newest of the duplicates wins
        QVERIFY(manager.readingPositionForEpub(legacy, &read));
        QCOMPARE(manager.bookmarksForEpub(canonical).size(), 1);
        QCOMPARE(manager.bookmarksForEpub(legacy).size(), 1);

        const QByteArray positions = readFile(m_dir + "/reading-positions.json");
        QCOMPARE(positions.count("a.epub"), 1);
        QVERIFY(positions.contains(canonical.toUtf8()));
        QVERIFY(readFile(m_dir + "/bookmarks.json").contains(canonical.toUtf8()));
    }
};

QTEST_GUILESS_MAIN(TestBookmarkManager)
#include "tst_bookmarkmanager.moc"
