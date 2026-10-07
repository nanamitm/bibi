#include "epubreader.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <atomic>
#include <miniz.h>

namespace {

QString readUtf8(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

QString chapter(const QString& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>t</title></head>"
           "<body>" + body + "</body></html>";
}

QByteArray makeArchive(const QList<QPair<QString, QByteArray>>& files) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return {};
    for (const auto& file : files) {
        const QByteArray name = file.first.toUtf8();
        mz_zip_writer_add_mem(&zip, name.constData(), file.second.constData(),
                              static_cast<size_t>(file.second.size()), MZ_DEFAULT_COMPRESSION);
    }
    void* buffer = nullptr;
    size_t size = 0;
    QByteArray epub;
    if (mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
        epub = QByteArray(static_cast<const char*>(buffer), static_cast<qsizetype>(size));
        mz_free(buffer);
    }
    mz_zip_writer_end(&zip);
    return epub;
}

// Builds a minimal EPUB 3: OEBPS/content.opf, a nav document and the given chapters.
QByteArray makeEpub(const QList<QPair<QString, QString>>& chapters) {
    QString manifest = "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>";
    QString spine;
    QString toc;
    for (int i = 0; i < chapters.size(); ++i) {
        const QString id = QString("c%1").arg(i + 1);
        manifest += QString("<item id=\"%1\" href=\"%1.xhtml\" media-type=\"application/xhtml+xml\"/>").arg(id);
        spine += QString("<itemref idref=\"%1\"/>").arg(id);
        toc += QString("<li><a href=\"%1.xhtml\">%2</a></li>").arg(id, chapters[i].first);
    }

    QList<QPair<QString, QByteArray>> files = {
        {"mimetype", "application/epub+zip"},
        {"META-INF/container.xml",
         "<?xml version=\"1.0\"?><container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
         "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>"},
        {"OEBPS/content.opf",
         ("<?xml version=\"1.0\" encoding=\"UTF-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
          "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>テストの本</dc:title><dc:language>ja</dc:language></metadata>"
          "<manifest>" + manifest + "</manifest><spine page-progression-direction=\"rtl\">" + spine + "</spine></package>").toUtf8()},
        {"OEBPS/nav.xhtml",
         chapter("<nav xmlns:epub=\"http://www.idpf.org/2007/ops\" epub:type=\"toc\"><ol>" + toc + "</ol></nav>").toUtf8()},
    };
    for (int i = 0; i < chapters.size(); ++i)
        files.append({QString("OEBPS/c%1.xhtml").arg(i + 1), chapter(chapters[i].second).toUtf8()});

    return makeArchive(files);
}

} // namespace

class TestEpubReader : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString writeEpub(const QString& name, const QList<QPair<QString, QString>>& chapters) {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write(makeEpub(chapters));
        return path;
    }

private slots:
    void searchableTextMatchesSharedFixtures_data() {
        QTest::addColumn<QString>("xhtml");
        QTest::addColumn<QString>("expected");

        // The same files are checked against bibi_search_highlight.js (tests/js),
        // which keeps the C++ and DOM-side text extraction in lock-step.
        const QDir dir(QStringLiteral(BIBI_SEARCH_FIXTURES));
        const QStringList fixtures = dir.entryList({"*.xhtml"}, QDir::Files, QDir::Name);
        QVERIFY2(!fixtures.isEmpty(), "no search fixtures found");
        for (const QString& name : fixtures) {
            QString expected = readUtf8(dir.filePath(QFileInfo(name).completeBaseName() + ".txt"));
            while (expected.endsWith(u'\n') || expected.endsWith(u'\r'))
                expected.chop(1);
            QTest::newRow(qPrintable(name)) << readUtf8(dir.filePath(name)) << expected;
        }
    }

    void searchableTextMatchesSharedFixtures() {
        QFETCH(QString, xhtml);
        QFETCH(QString, expected);
        QCOMPARE(EpubReader::searchableText(xhtml), expected);
    }

    // Cases that only exist on the C++ side (Chromium parses these documents itself).
    void searchableTextParserQuirks_data() {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("expected");

        const QString xhtml11 =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">"
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>t</title></head><body>";

        QTest::newRow("XHTML 1.1 named entities")
            << xhtml11 + "<p>東<em>京</em>&nbsp;<ruby>漢<rt>かん</rt></ruby>&hellip;&mdash;&amp;</p></body></html>"
            << QString::fromUtf8("東京 漢…—&");
        QTest::newRow("unknown entity under a DOCTYPE is dropped")
            << xhtml11 + "<p>a&unknownentity;b</p></body></html>"
            << "ab";
        QTest::newRow("named entities without DOCTYPE")
            << chapter("<p>東<em>京</em>&nbsp;<ruby>漢<rt>かん</rt></ruby>&hellip;</p>")
            << QString::fromUtf8("東京 漢…");
        QTest::newRow("not well-formed: regex fallback")
            << chapter("<p>東<em>京</em><br>x</p><script>var s='SCRIPT';</script>")
            << "東京 x";
    }

    void searchableTextParserQuirks() {
        QFETCH(QString, html);
        QFETCH(QString, expected);
        QCOMPARE(EpubReader::searchableText(html), expected);
    }

    void opensEpubAndReadsToc() {
        const QString path = writeEpub("book.epub", {{"第一章", "<p>a</p>"}, {"第二章", "<p>b</p>"}});
        EpubReader reader;
        QVERIFY2(reader.open(path), qPrintable(reader.lastError()));
        QCOMPARE(reader.metadata().title, QString::fromUtf8("テストの本"));
        QCOMPARE(reader.metadata().pageProgressionDirection, QStringLiteral("rtl"));
        QCOMPARE(reader.chapterCount(), 2);
        QCOMPARE(reader.chapter(1).href, QStringLiteral("OEBPS/c2.xhtml"));
        QCOMPARE(reader.toc().size(), 2);
        QCOMPARE(reader.toc().at(0).label, QString::fromUtf8("第一章"));
        QCOMPARE(reader.toc().at(0).src, QStringLiteral("OEBPS/c1.xhtml"));
    }

    void rejectsInvalidFiles() {
        const QString path = m_dir.filePath("not-a-zip.epub");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("plain text");
        f.close();

        EpubReader reader;
        QVERIFY(!reader.open(path));
        QVERIFY(!reader.lastError().isEmpty());
        QVERIFY(!reader.isOpen());
    }

    void preservesStructureErrorsAfterCleanup_data() {
        QTest::addColumn<QByteArray>("container");
        QTest::addColumn<QByteArray>("opf");
        QTest::addColumn<QString>("expected");
        const QByteArray validContainer =
            "<container><rootfiles><rootfile full-path=\"book.opf\"/></rootfiles></container>";
        QTest::newRow("missing container") << QByteArray() << QByteArray() << QString("container.xml");
        QTest::newRow("invalid container XML") << QByteArray("<container") << QByteArray() << QString("container.xml");
        QTest::newRow("missing OPF") << validContainer << QByteArray() << QString("OPF");
        QTest::newRow("invalid OPF XML") << validContainer << QByteArray("<package") << QString("OPF");
        QTest::newRow("empty spine") << validContainer << QByteArray("<package><manifest/><spine/></package>")
                                    << QString::fromUtf8("スパイン");
    }

    void preservesStructureErrorsAfterCleanup() {
        QFETCH(QByteArray, container);
        QFETCH(QByteArray, opf);
        QFETCH(QString, expected);
        QList<QPair<QString, QByteArray>> files = {{"mimetype", "application/epub+zip"}};
        if (!container.isEmpty()) files.append({"META-INF/container.xml", container});
        if (!opf.isEmpty()) files.append({"book.opf", opf});
        const QString path = m_dir.filePath("invalid-structure.epub");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeArchive(files));
        f.close();
        EpubReader reader;
        QVERIFY(!reader.open(path));
        QVERIFY2(reader.lastError().contains(expected), qPrintable(reader.lastError()));
        QVERIFY(!reader.isOpen());
        QCOMPARE(reader.chapterCount(), 0);
        QVERIFY(reader.fileData("mimetype").isEmpty());
        const QString good = writeEpub("after-error.epub", {{"一", "<p>ok</p>"}});
        QVERIFY2(reader.open(good), qPrintable(reader.lastError()));
        QVERIFY(reader.lastError().isEmpty());
    }

    void rejectsPathTraversal() {
        const QString path = writeEpub("traversal.epub", {{"一", "<p>a</p>"}});
        EpubReader reader;
        QVERIFY(reader.open(path));
        QVERIFY(!reader.fileData("OEBPS/c1.xhtml").isEmpty());
        QVERIFY(reader.fileData("OEBPS/../META-INF/container.xml").isEmpty());
        QVERIFY(reader.fileData("../OEBPS/c1.xhtml").isEmpty());
    }

    void searchReportsOffsetsIntoSearchableText() {
        const QList<QPair<QString, QString>> chapters = {
            {"第一章", "<p>東<em class=\"sesame\">京</em>と東京</p>"},
            {"第二章", "<p><ruby>東<rt>とう</rt>京<rt>きょう</rt></ruby>駅</p>"},
        };
        const QString path = writeEpub("search.epub", chapters);
        EpubReader reader;
        QVERIFY(reader.open(path));

        const auto results = reader.search(QString::fromUtf8("東京"));
        QCOMPARE(results.size(), 3);

        const int expectedChapter[]    = {0, 0, 1};
        const int expectedOccurrence[] = {0, 1, 0};
        for (int i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            QCOMPARE(r.chapterIndex, expectedChapter[i]);
            QCOMPARE(r.occurrenceIndex, expectedOccurrence[i]);
            QCOMPARE(r.chapterTitle, chapters[r.chapterIndex].first);
            const QString text = EpubReader::searchableText(
                QString::fromUtf8(reader.fileData(reader.chapter(r.chapterIndex).href)));
            QCOMPARE(text.mid(r.matchStart, r.matchLength), QString::fromUtf8("東京"));
        }
    }

    void searchIsCaseInsensitiveAndNormalizesWhitespace() {
        const QString path = writeEpub("case.epub", {{"一", "<p>Hello <b>World</b></p>"}});
        EpubReader reader;
        QVERIFY(reader.open(path));
        QCOMPARE(reader.search("hello   world").size(), 1);
        QCOMPARE(reader.search("HELLOWORLD").size(), 0);
        QCOMPARE(reader.search("").size(), 0);
    }

    void searchStopsWhenCancelled() {
        const QString path = writeEpub("cancel.epub", {{"一", "<p>x</p>"}, {"二", "<p>x</p>"}});
        EpubReader reader;
        QVERIFY(reader.open(path));
        std::atomic<bool> cancel{true};
        QCOMPARE(reader.search("x", &cancel).size(), 0);
        cancel = false;
        QCOMPARE(reader.search("x", &cancel).size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestEpubReader)
#include "tst_epubreader.moc"
