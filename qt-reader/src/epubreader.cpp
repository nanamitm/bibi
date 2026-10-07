#include "epubreader.h"
#include <miniz.h>
#include <QDomDocument>
#include <QDomText>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QRegularExpression>
#include <QSet>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <functional>

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
        const QString error = m_lastError;
        close();
        m_lastError = error;
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
//
// The searchable text of a chapter is built by the rules below, and
// scripts/bibi_search_highlight.js rebuilds exactly the same string from the
// live DOM so that match offsets computed here can be mapped back onto text
// nodes. Keep both implementations in sync.
//
//  - Walk <body> in document order; text and CDATA nodes are concatenated
//    WITHOUT separators, so inline markup (傍点 <em>, 縦中横 <span>, <a>, ...)
//    does not split words.
//  - Subtrees of kSkippedSearchTags are ignored (ruby readings, images, ...).
//  - Block-level elements (kBlockSearchTags) and <br> emit a line break
//    before and after their content.
//  - Runs of whitespace (space, tab, CR, LF, FF, NBSP) collapse to a single
//    space and the result is trimmed.

namespace {
const QSet<QString> kSkippedSearchTags = {
    "script", "style", "head", "title", "meta", "link", "noscript", "template",
    "rt", "rp", "svg", "img", "object", "picture", "video", "audio", "iframe",
};

const QSet<QString> kBlockSearchTags = {
    "address", "article", "aside", "blockquote", "body", "br", "caption",
    "dd", "div", "dl", "dt", "figcaption", "figure", "footer", "h1", "h2",
    "h3", "h4", "h5", "h6", "header", "hr", "li", "main", "nav", "ol", "p",
    "pre", "section", "table", "td", "th", "tr", "ul",
};

QString localTagName(const QDomElement& el) {
    QString tag = el.tagName();
    const int colon = tag.indexOf(u':');
    if (colon >= 0) tag = tag.mid(colon + 1);
    return tag.toLower();
}

void collectSearchText(const QDomNode& node, QString& raw) {
    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (child.isText() || child.isCDATASection()) {
            raw += child.nodeValue();
        } else if (child.isEntityReference()) {
            collectSearchText(child, raw);
        } else if (child.isElement()) {
            const QString tag = localTagName(child.toElement());
            if (kSkippedSearchTags.contains(tag)) continue;
            const bool block = kBlockSearchTags.contains(tag);
            if (block) raw += u'\n';
            collectSearchText(child, raw);
            if (block) raw += u'\n';
        }
    }
}

bool isSearchWhitespace(QChar c) {
    switch (c.unicode()) {
    case 0x20: case 0x09: case 0x0A: case 0x0C: case 0x0D: case 0xA0:
        return true;
    default:
        return false;
    }
}

// Unlike QString::simplified(), leaves U+3000 (全角スペース) alone.
QString normalizeSearchText(const QString& raw) {
    QString out;
    out.reserve(raw.size());
    bool pendingSpace = false;
    for (const QChar c : raw) {
        if (isSearchWhitespace(c)) {
            pendingSpace = !out.isEmpty();
            continue;
        }
        if (pendingSpace) {
            out += u' ';
            pendingSpace = false;
        }
        out += c;
    }
    return out;
}

// Rough fallback for chapters that are not well-formed XML. Offsets may then
// differ from the DOM; the highlight script re-searches the text in that case.
QString fallbackSearchText(QString html) {
    static const QRegularExpression dropped(
        R"(<(head|script|style|rt|rp|svg)\b[^>]*>.*?</\1\s*>)",
        QRegularExpression::CaseInsensitiveOption |
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression blockTag(
        R"(</?(?:p|div|h[1-6]|li|dd|dt|tr|td|th|br|hr|blockquote|section|article|table|ul|ol|dl|pre)\b[^>]*>)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression anyTag(R"(<[^>]*>)");

    html.remove(dropped);
    html.replace(blockTag, QStringLiteral("\n"));
    html.remove(anyTag);
    html.replace("&nbsp;", QString(QChar(0xA0)));
    html.replace("&lt;",   "<");
    html.replace("&gt;",   ">");
    html.replace("&quot;", "\"");
    html.replace("&apos;", "'");
    html.replace("&amp;",  "&");   // last, so "&amp;lt;" stays "&lt;"
    return normalizeSearchText(html);
}
}

namespace {
// EPUB 2 chapters often declare the XHTML 1.1 DOCTYPE and use HTML named
// entities. Chromium resolves those for known XHTML DTDs, but QDomDocument
// does not load the DTD: with a DOCTYPE it silently drops them, without one it
// rejects the document. Either way the text would no longer match the DOM, so
// rewrite the common ones as numeric references before parsing.
QString replaceHtmlNamedEntities(const QString& html) {
    static const QHash<QString, int> kEntities = {
        {"nbsp", 0xA0},   {"ensp", 0x2002},  {"emsp", 0x2003},  {"thinsp", 0x2009},
        {"zwnj", 0x200C}, {"zwj", 0x200D},   {"lrm", 0x200E},   {"rlm", 0x200F},
        {"shy", 0xAD},    {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E},
        {"laquo", 0xAB},  {"raquo", 0xBB},   {"middot", 0xB7},  {"bull", 0x2022},
        {"prime", 0x2032}, {"Prime", 0x2033}, {"deg", 0xB0},    {"plusmn", 0xB1},
        {"times", 0xD7},  {"divide", 0xF7},  {"sect", 0xA7},    {"para", 0xB6},
        {"copy", 0xA9},   {"reg", 0xAE},     {"trade", 0x2122}, {"yen", 0xA5},
        {"euro", 0x20AC}, {"cent", 0xA2},    {"pound", 0xA3},   {"iexcl", 0xA1},
        {"iquest", 0xBF},
    };
    static const QRegularExpression entity(R"(&([A-Za-z][A-Za-z0-9]*);)");

    QString out;
    out.reserve(html.size());
    qsizetype last = 0;
    auto it = entity.globalMatch(html);
    while (it.hasNext()) {
        const auto match = it.next();
        const auto code = kEntities.constFind(match.captured(1));
        if (code == kEntities.constEnd()) continue;
        out += QStringView(html).mid(last, match.capturedStart() - last);
        out += QStringLiteral("&#x%1;").arg(*code, 0, 16);
        last = match.capturedEnd();
    }
    out += QStringView(html).mid(last);
    return out;
}
}

QString EpubReader::searchableText(const QString& html) {
    QDomDocument doc;
    // Whitespace-only text nodes (e.g. the space in "<em>a</em> <em>b</em>")
    // are part of the DOM the highlight script sees, so they must be kept.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const bool parsed = static_cast<bool>(doc.setContent(
        replaceHtmlNamedEntities(html), QDomDocument::ParseOption::PreserveSpacingOnlyNodes));
#else
    const bool parsed = doc.setContent(replaceHtmlNamedEntities(html));
#endif
    if (!parsed)
        return fallbackSearchText(html);

    QDomElement body = doc.elementsByTagName("body").at(0).toElement();
    if (body.isNull())
        body = doc.documentElement();

    QString raw;
    collectSearchText(body, raw);
    return normalizeSearchText(raw);
}

QList<EpubReader::SearchResult> EpubReader::search(const QString& query,
                                                  const std::atomic<bool>* cancel) const {
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

    const QString needle = normalizeSearchText(query);
    if (needle.isEmpty()) return results;
    QRegularExpression re(QRegularExpression::escape(needle),
                          QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < m_spine.size(); ++i) {
        if (cancel && cancel->load()) break;
        const EpubChapter& ch = m_spine[i];
        QByteArray raw = fileData(ch.href);
        if (raw.isEmpty()) continue;

        const QString text = searchableText(QString::fromUtf8(raw));
        if (text.isEmpty()) continue;
        auto it = re.globalMatch(text);
        int occurrenceIndex = 0;
        while (it.hasNext()) {
            auto match = it.next();
            if (!match.hasMatch()) continue;

            const int pos    = static_cast<int>(match.capturedStart());
            const int length = static_cast<int>(match.capturedLength());
            const int start  = qMax(0, pos - 80);
            const int end    = qMin(static_cast<int>(text.size()), pos + length + 80);

            SearchResult sr;
            sr.chapterIndex    = i;
            sr.occurrenceIndex = occurrenceIndex++;
            sr.matchStart      = pos;
            sr.matchLength     = length;
            sr.href            = ch.href;
            sr.chapterTitle    = hrefToTitle.value(ch.href,
                                     QString("Chapter %1").arg(i + 1));
            sr.context = "..." + text.mid(start, end - start) + "...";
            results.append(sr);
        }
    }
    return results;
}
