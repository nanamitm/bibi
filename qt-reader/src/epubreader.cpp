#include "epubreader.h"
#include <miniz.h>
#include <QDomDocument>
#include <QDomText>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>

struct EpubReader::ZipImpl {
    mz_zip_archive    archive{};
    QByteArray        epubData;   // owns the buffer for mz_zip_reader_init_mem
    std::atomic<bool> isOpen{false};
    mutable QMutex    mutex;
};

EpubReader::EpubReader(QObject* parent)
    : QObject(parent), m_zip(new ZipImpl) {}

EpubReader::~EpubReader() {
    close();
    delete m_zip;
}

bool EpubReader::open(const QString& filePath) {
    close();
    m_filePath = filePath;

    // QFile handles Unicode/Japanese paths on all platforms correctly.
    // We load the entire EPUB into memory so miniz can access it without
    // needing a file handle (which would require a locale-encoded path).
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_lastError = QString("ファイルを開けません: %1").arg(f.errorString());
        return false;
    }
    m_zip->epubData = f.readAll();
    f.close();

    if (!mz_zip_reader_init_mem(&m_zip->archive,
                                 m_zip->epubData.constData(),
                                 static_cast<size_t>(m_zip->epubData.size()), 0)) {
        m_zip->epubData.clear();
        m_lastError = "ZIPアーカイブとして読み込めません（DRM保護またはファイル破損の可能性があります）";
        return false;
    }
    m_zip->isOpen = true;

    if (!parseContainer()) {
        close();
        return false;
    }
    return true;
}

void EpubReader::close() {
    {
        QMutexLocker lk(&m_zip->mutex);
        if (m_zip->isOpen) {
            mz_zip_reader_end(&m_zip->archive);
            m_zip->isOpen = false;
        }
    }
    m_zip->epubData.clear();
    m_filePath.clear();
    m_lastError.clear();
    m_metadata = {};
    m_toc.clear();
    m_spine.clear();
    m_idToHref.clear();
    {
        QMutexLocker lk(&m_mimeLock);
        m_mimeTypes.clear();
    }
    m_opfDir.clear();
    {
        QMutexLocker lk(&m_cacheLock);
        m_lruList.clear();
        m_lruIndex.clear();
        m_cacheBytes = 0;
    }
}

bool EpubReader::isOpen() const { return m_zip && m_zip->isOpen; }
QString       EpubReader::filePath()     const { return m_filePath; }
QString       EpubReader::lastError()    const { return m_lastError; }
EpubMetadata  EpubReader::metadata()    const { return m_metadata; }
QList<NavPoint> EpubReader::toc()       const { return m_toc; }
int           EpubReader::chapterCount() const { return m_spine.size(); }
EpubChapter   EpubReader::chapter(int i) const { return m_spine.value(i); }

QByteArray EpubReader::fileData(const QString& epubPath) const {
    // Reject path traversal: ".." segments could reference ZIP entries outside the EPUB root
    for (const auto& seg : epubPath.split(u'/')) {
        if (seg == QLatin1String("..")) return {};
    }

    // ① キャッシュ確認（ヒット時は先頭に移動して LRU 順を更新）
    {
        QMutexLocker lk(&m_cacheLock);
        auto it = m_lruIndex.find(epubPath);
        if (it != m_lruIndex.end()) {
            m_lruList.splice(m_lruList.begin(), m_lruList, it.value());
            return it.value()->second;
        }
    }

    // ② ZIP から解凍
    if (!m_zip->isOpen) return {};
    QByteArray result;
    {
        QMutexLocker zipLk(&m_zip->mutex);
        if (!m_zip->isOpen) return {};
        size_t size = 0;
        void* data = mz_zip_reader_extract_file_to_heap(
            &m_zip->archive, epubPath.toUtf8().constData(), &size, 0);
        if (!data) return {};
        result = QByteArray(static_cast<const char*>(data), static_cast<int>(size));
        mz_free(data);
    }

    // ③ キャッシュへ書き込み（上限超過時は末尾の最古エントリから追い出す）
    if (result.size() <= kCacheLimit) {
        QMutexLocker lk(&m_cacheLock);
        if (!m_lruIndex.contains(epubPath)) {
            while (!m_lruList.empty() && m_cacheBytes + result.size() > kCacheLimit) {
                m_cacheBytes -= m_lruList.back().second.size();
                m_lruIndex.remove(m_lruList.back().first);
                m_lruList.pop_back();
            }
            m_lruList.emplace_front(epubPath, result);
            m_lruIndex[epubPath] = m_lruList.begin();
            m_cacheBytes += result.size();
        }
    }
    return result;
}

QString EpubReader::mimeTypeForPath(const QString& path) const {
    QMutexLocker lk(&m_mimeLock);
    return m_mimeTypes.value(path, {});
}

void EpubReader::prefetchChapter(int index) const {
    if (!isOpen() || index < 0 || index >= m_spine.size()) return;

    const EpubChapter& ch = m_spine[index];
    QByteArray html = fileData(ch.href); // HTMLをキャッシュ
    if (html.isEmpty() || !isOpen()) return;

    QString chapterDir = QFileInfo(ch.href).path();
    if (chapterDir == ".") chapterDir = "";

    // src/href 属性に含まれる画像・CSSなどを先読み（HTMLリンクは除外）
    static const QRegularExpression resRe(
        R"((?:src|href)\s*=\s*["']([^"'#?]+)["'])",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression skipRe(
        R"(\.(x?html?|opf|ncx)(\?.*)?$)",
        QRegularExpression::CaseInsensitiveOption);

    auto it = resRe.globalMatch(QString::fromUtf8(html));
    while (it.hasNext() && isOpen()) {
        QString src = QUrl::fromPercentEncoding(
            it.next().captured(1).toUtf8());
        if (skipRe.match(src).hasMatch()) continue;
        QString resolved = resolvePath(chapterDir, src);
        if (!resolved.isEmpty()) fileData(resolved); // キャッシュに格納
    }
}

// Resolve relative href against a base directory (both epub-root-relative)
QString EpubReader::resolvePath(const QString& baseDir, const QString& rel) {
    if (rel.startsWith('/')) return rel.mid(1);
    if (baseDir.isEmpty())   return rel;
    // Use QUrl for correct relative-path resolution (handles ../)
    QUrl base("epub:///" + baseDir + "/dummy");
    return base.resolved(QUrl(rel)).path().mid(1);  // strip leading /
}

bool EpubReader::parseContainer() {
    QByteArray data = fileData("META-INF/container.xml");
    if (data.isEmpty()) {
        m_lastError = "META-INF/container.xml が見つかりません（EPUBとして無効なファイルです）";
        return false;
    }

    QDomDocument doc;
    QString parseError;
    if (!doc.setContent(data, &parseError)) {
        m_lastError = QString("container.xml の解析に失敗: %1").arg(parseError);
        return false;
    }

    QDomNodeList rootfiles = doc.elementsByTagName("rootfile");
    if (rootfiles.isEmpty()) {
        m_lastError = "container.xml に rootfile 要素がありません";
        return false;
    }

    QString opfPath = rootfiles.at(0).toElement().attribute("full-path");
    if (opfPath.isEmpty()) {
        m_lastError = "rootfile の full-path 属性がありません";
        return false;
    }

    m_opfDir = QFileInfo(opfPath).path();
    if (m_opfDir == ".") m_opfDir = "";

    return parseOpf(opfPath);
}

bool EpubReader::parseOpf(const QString& opfPath) {
    QByteArray data = fileData(opfPath);
    if (data.isEmpty()) {
        m_lastError = QString("OPFファイルが見つかりません: %1").arg(opfPath);
        return false;
    }

    QDomDocument doc;
    QString parseError;
    if (!doc.setContent(data, &parseError)) {
        m_lastError = QString("OPFの解析に失敗: %1").arg(parseError);
        return false;
    }

    QDomElement pkg = doc.documentElement();

    // ── Metadata ──────────────────────────────────────────────────────────
    QDomElement meta = pkg.firstChildElement("metadata");
    if (!meta.isNull()) {
        auto getText = [&](const QString& tag) -> QString {
            for (const QString& t : {tag, "dc:" + tag}) {
                QDomNodeList nodes = meta.elementsByTagName(t);
                if (!nodes.isEmpty()) return nodes.at(0).toElement().text().trimmed();
            }
            return {};
        };
        m_metadata.title      = getText("title");
        m_metadata.creator    = getText("creator");
        m_metadata.language   = getText("language");
        m_metadata.identifier = getText("identifier");
        m_metadata.publisher  = getText("publisher");
    }

    // ── Manifest ──────────────────────────────────────────────────────────
    QDomElement manifest = pkg.firstChildElement("manifest");
    QString ncxId, navId;
    QMap<QString, QString> newMimeTypes;

    QDomNodeList items = manifest.elementsByTagName("item");
    for (int i = 0; i < items.size(); ++i) {
        QDomElement item  = items.at(i).toElement();
        QString id        = item.attribute("id");
        QString href      = item.attribute("href");
        QString mediaType = item.attribute("media-type");
        QString properties= item.attribute("properties");

        // Decode percent-encoding in href
        QString fullHref  = resolvePath(m_opfDir, QUrl::fromPercentEncoding(href.toUtf8()));
        m_idToHref[id]    = fullHref;
        newMimeTypes[fullHref] = mediaType;

        if (mediaType == "application/x-dtbncx+xml") ncxId = id;
        if (properties.contains("nav"))              navId = id;
    }
    // Publish the completed map atomically so the IO thread never sees a partial build
    {
        QMutexLocker lk(&m_mimeLock);
        m_mimeTypes = std::move(newMimeTypes);
    }

    // ── Spine ─────────────────────────────────────────────────────────────
    QDomElement spine = pkg.firstChildElement("spine");
    m_metadata.pageProgressionDirection =
        spine.attribute("page-progression-direction", "ltr");

    QDomNodeList itemrefs = spine.elementsByTagName("itemref");
    for (int i = 0; i < itemrefs.size(); ++i) {
        QDomElement ref = itemrefs.at(i).toElement();
        QString idref   = ref.attribute("idref");
        bool linear     = ref.attribute("linear", "yes") != "no";

        QString href = m_idToHref.value(idref);
        if (!href.isEmpty()) {
            EpubChapter ch;
            ch.href      = href;
            ch.mediaType = m_mimeTypes.value(href, "application/xhtml+xml");
            ch.linear    = linear;
            m_spine.append(ch);
        }
    }

    // ── TOC ───────────────────────────────────────────────────────────────
    if (!navId.isEmpty() && m_idToHref.contains(navId)) {
        parseNavXhtml(m_idToHref[navId]);
    } else if (!ncxId.isEmpty() && m_idToHref.contains(ncxId)) {
        parseTocNcx(m_idToHref[ncxId]);
    }

    if (m_spine.isEmpty())
        m_lastError = "スパイン（読書順）が空です。EPUBの構造が不正です";
    return !m_spine.isEmpty();
}

// ── EPUB 2: NCX ───────────────────────────────────────────────────────────

bool EpubReader::parseTocNcx(const QString& ncxPath) {
    QByteArray data = fileData(ncxPath);
    if (data.isEmpty()) return false;

    QDomDocument doc;
    if (!doc.setContent(data)) return false;

    QString ncxDir = QFileInfo(ncxPath).path();
    if (ncxDir == ".") ncxDir = "";

    // Find navMap (may be inside <ncx> root or directly)
    QDomNodeList maps = doc.elementsByTagName("navMap");
    if (maps.isEmpty()) return false;

    // Store ncxDir in a context-accessible way for parseNavPoints
    // Temporarily reuse m_opfDir trick: we pass baseDir implicitly via closure
    // parseNavPoints uses m_opfDir; here we use a temporary wrapper
    struct NcxParser {
        const EpubReader* self;
        QString baseDir;

        QList<NavPoint> parse(const QDomElement& parent) const {
            QList<NavPoint> result;
            QDomNodeList children = parent.childNodes();
            for (int i = 0; i < children.size(); ++i) {
                QDomElement el = children.at(i).toElement();
                if (el.tagName() != "navPoint") continue;

                NavPoint np;
                QDomElement label = el.firstChildElement("navLabel")
                                      .firstChildElement("text");
                np.label = label.text().trimmed();

                QDomElement content = el.firstChildElement("content");
                QString src = QUrl::fromPercentEncoding(
                    content.attribute("src").toUtf8());
                // Resolve fragment-stripped path, then re-append fragment
                QString path = src.section('#', 0, 0);
                QString frag = src.section('#', 1);
                QString resolved = EpubReader::resolvePath(baseDir, path);
                np.src = frag.isEmpty() ? resolved : resolved + "#" + frag;

                np.children = parse(el);
                if (!np.label.isEmpty()) result.append(np);
            }
            return result;
        }
    };

    NcxParser parser{this, ncxDir};
    m_toc = parser.parse(maps.at(0).toElement());
    return true;
}

// ── EPUB 3: Navigation Document ───────────────────────────────────────────

bool EpubReader::parseNavXhtml(const QString& navPath) {
    QByteArray data = fileData(navPath);
    if (data.isEmpty()) return false;

    QDomDocument doc;
    doc.setContent(data); // no namespace processing — simpler

    QString navDir = QFileInfo(navPath).path();
    if (navDir == ".") navDir = "";

    QDomNodeList navs = doc.elementsByTagName("nav");
    for (int i = 0; i < navs.size(); ++i) {
        QDomElement nav = navs.at(i).toElement();
        // Check epub:type attribute (with or without namespace prefix)
        QString epubType = nav.attribute("epub:type");
        if (epubType.isEmpty()) epubType = nav.attribute("type");
        if (epubType != "toc") continue;

        QDomElement ol = nav.firstChildElement("ol");
        if (!ol.isNull()) {
            m_toc = parseNavList(ol, navDir);
        }
        break;
    }
    return true;
}

QList<NavPoint> EpubReader::parseNavList(const QDomElement& olEl,
                                          const QString& baseDir) const {
    QList<NavPoint> result;
    QDomNodeList lis = olEl.childNodes();

    for (int i = 0; i < lis.size(); ++i) {
        QDomElement li = lis.at(i).toElement();
        if (li.tagName() != "li") continue;

        NavPoint np;
        QDomElement a = li.firstChildElement("a");
        if (a.isNull()) a = li.firstChildElement("span");

        np.label = a.text().trimmed();
        QString href = QUrl::fromPercentEncoding(a.attribute("href").toUtf8());

        if (!href.isEmpty()) {
            QString path = href.section('#', 0, 0);
            QString frag = href.section('#', 1);
            QString resolved = resolvePath(baseDir, path);
            np.src = frag.isEmpty() ? resolved : resolved + "#" + frag;
        }

        QDomElement subOl = li.firstChildElement("ol");
        if (!subOl.isNull()) {
            np.children = parseNavList(subOl, baseDir);
        }

        if (!np.label.isEmpty()) result.append(np);
    }
    return result;
}

// ── Full-text search ──────────────────────────────────────────────────────

static bool isIgnorableSearchElement(const QString& tagName) {
    const QString tag = tagName.toLower();
    return tag == "script" || tag == "style" || tag == "title" ||
           tag == "meta" || tag == "link" || tag == "head";
}

static bool isImageOnlyElement(const QDomElement& el) {
    if (el.isNull() || isIgnorableSearchElement(el.tagName())) return true;

    const QString tag = el.tagName().toLower();
    if (tag == "img" || tag == "svg" || tag == "object" || tag == "picture")
        return true;

    for (QDomNode n = el.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isText()) {
            if (!n.nodeValue().trimmed().isEmpty())
                return false;
            continue;
        }
        if (n.isElement() && !isImageOnlyElement(n.toElement()))
            return false;
    }
    return true;
}

static void appendVisibleText(const QDomNode& node, QStringList& out) {
    if (node.isText()) {
        const QString text = node.nodeValue().simplified();
        if (!text.isEmpty())
            out.append(text);
        return;
    }

    if (!node.isElement() && !node.isDocument()) return;
    const QDomElement el = node.toElement();
    if (!el.isNull()) {
        if (isIgnorableSearchElement(el.tagName())) return;
        const QString tag = el.tagName().toLower();
        if (tag == "img" || tag == "svg" || tag == "object" || tag == "picture")
            return;
    }

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling())
        appendVisibleText(child, out);
}

static QString extractSearchText(const QString& html) {
    static const QRegularExpression nbsp("&nbsp;");

    QDomDocument doc;
    QString error;
    int line = 0;
    int column = 0;
    if (!doc.setContent(html, &error, &line, &column)) {
        static const QRegularExpression tags(R"(<[^>]*>)");
        QString text = html;
        text.remove(tags);
        text.replace("&lt;",   "<");
        text.replace("&gt;",   ">");
        text.replace("&amp;",  "&");
        text.replace("&quot;", "\"");
        text.replace(nbsp,     " ");
        return text.simplified();
    }

    QDomElement body = doc.elementsByTagName("body").at(0).toElement();
    if (body.isNull())
        body = doc.documentElement();
    if (isImageOnlyElement(body))
        return {};

    QStringList parts;
    appendVisibleText(body, parts);
    QString text = parts.join(' ');
    text.replace("&lt;",   "<");
    text.replace("&gt;",   ">");
    text.replace("&amp;",  "&");
    text.replace("&quot;", "\"");
    text.replace(nbsp,     " ");
    return text.simplified();
}

QList<EpubReader::SearchResult> EpubReader::search(const QString& query) const {
    QList<SearchResult> results;
    if (!m_zip->isOpen || query.isEmpty()) return results;

    // Build href->title map from TOC
    QMap<QString, QString> hrefToTitle;
    std::function<void(const QList<NavPoint>&)> collect = [&](const QList<NavPoint>& pts) {
        for (const auto& np : pts) {
            QString path = np.src.section('#', 0, 0);
            if (!path.isEmpty() && !hrefToTitle.contains(path))
                hrefToTitle[path] = np.label;
            collect(np.children);
        }
    };
    collect(m_toc);

    QRegularExpression re(QRegularExpression::escape(query),
                          QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < m_spine.size(); ++i) {
        const EpubChapter& ch = m_spine[i];
        QByteArray raw = fileData(ch.href);
        if (raw.isEmpty()) continue;

        QString text = extractSearchText(QString::fromUtf8(raw));
        if (text.isEmpty()) continue;
        auto it = re.globalMatch(text);
        int occurrenceIndex = 0;
        while (it.hasNext()) {
            auto match = it.next();
            if (!match.hasMatch()) continue;

            int pos   = match.capturedStart();
            int start = qMax(0, pos - 80);
            int end   = qMin(text.size(), pos + static_cast<int>(query.size()) + 80);

            SearchResult sr;
            sr.chapterIndex    = i;
            sr.occurrenceIndex = occurrenceIndex++;
            sr.href            = ch.href;
            sr.chapterTitle    = hrefToTitle.value(ch.href,
                                     QString("Chapter %1").arg(i + 1));
            sr.context = "..." + text.mid(start, end - start).simplified() + "...";
            results.append(sr);
        }
    }
    return results;
}
