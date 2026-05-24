#include "mainwindow.h"
#include "epuburlscheme.h"
#include "searchdialog.h"
#include <QtConcurrent/QtConcurrent>
#include <QEvent>
#include <QResizeEvent>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QTabWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QLabel>
#include <QProgressBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QCloseEvent>
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QWebEngineSettings>
#include <QPoint>
#include <QUuid>

namespace {
constexpr const char* kZoomFactorSettingKey = "view/zoomFactor";
constexpr const char* kPreloadModeSettingKey = "view/preloadMode";

class BookmarkTreeItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem& other) const override {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 2)
            return data(col, Qt::UserRole).toInt() < other.data(col, Qt::UserRole).toInt();
        if (col == 3)
            return data(col, Qt::UserRole).toDateTime() < other.data(col, Qt::UserRole).toDateTime();
        return text(col).localeAwareCompare(other.text(col)) < 0;
    }
};
}

// ── Constructor / Destructor ──────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_reader(new EpubReader(this))
    , m_bookmarkManager(new BookmarkManager(this))
    , m_urlScheme(new EpubUrlScheme(this))
    , m_recentFiles(new RecentFiles(this))
{
    setupUi();
    setupMenuBar();
    setupToolBar();
    setupSearchBar();

    setWindowTitle(tr("Bibi Qt Reader"));
    resize(1200, 800);

    // Restore window geometry
    QSettings s;
    setZoomFactor(s.value(kZoomFactorSettingKey, 1.0).toDouble(), false);
    setPreloadMode(preloadModeFromIndex(s.value(kPreloadModeSettingKey, 2).toInt()), false);
    restoreGeometry(s.value("geometry").toByteArray());
    restoreState(s.value("windowState").toByteArray());
    if (s.contains("splitter"))
        m_splitter->restoreState(s.value("splitter").toByteArray());
}

MainWindow::~MainWindow() = default;


void MainWindow::closeEvent(QCloseEvent* event) {
    saveCurrentReadingPosition();
    m_prefetchFuture.waitForFinished();
    QSettings s;
    s.setValue("geometry",    saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("splitter",    m_splitter->saveState());
    event->accept();
}

// ── UI Setup ──────────────────────────────────────────────────────────────

void MainWindow::setupUi() {
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_sideTabs = new QTabWidget(m_splitter);
    m_sideTabs->setMinimumWidth(200);
    m_sideTabs->setMaximumWidth(380);

    m_tocTree = new QTreeWidget(m_sideTabs);
    m_tocTree->setHeaderLabel(tr("目次"));
    m_tocTree->setMinimumWidth(180);
    m_tocTree->setAnimated(true);

    m_bookmarkTree = new QTreeWidget(m_sideTabs);
    m_bookmarkTree->setHeaderLabels({tr("しおり"), tr("章"), tr("章内"), tr("作成日時")});
    m_bookmarkTree->setRootIsDecorated(false);
    m_bookmarkTree->setAlternatingRowColors(true);
    m_bookmarkTree->setSortingEnabled(true);
    m_bookmarkTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bookmarkTree->setTextElideMode(Qt::ElideRight);
    m_bookmarkTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_bookmarkTree->header()->setStretchLastSection(false);
    m_bookmarkTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_bookmarkTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_bookmarkTree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_bookmarkTree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_bookmarkTree->setColumnWidth(1, 86);
    m_bookmarkTree->setColumnWidth(2, 52);
    m_bookmarkTree->setColumnWidth(3, 118);

    m_sideTabs->addTab(m_tocTree, tr("目次"));
    m_sideTabs->addTab(m_bookmarkTree, tr("しおり"));

    auto* profile = new QWebEngineProfile("BibiQtReader", this);
    profile->installUrlSchemeHandler("epub", m_urlScheme);

    // Noto Serif 系フォントがインストール済みであればデフォルトフォントに設定する。
    // CJK JP（フルセット・vert グリフあり）を優先し、なければ JP（サブセット）を使う。
    // setFontFamily はプロファイルレベルで機能するため FOUT を起こさない。
    {
        const QStringList candidates = { "Noto Serif CJK JP", "Noto Serif JP" };
        const QStringList installed  = QFontDatabase::families();
        for (const QString& f : candidates) {
            if (installed.contains(f)) {
                auto* ws = profile->settings();
                ws->setFontFamily(QWebEngineSettings::StandardFont, f);
                ws->setFontFamily(QWebEngineSettings::SerifFont,    f);
                break;
            }
        }
    }

    // ── ダブルバッファ: View A / View B を重ねて配置 ────────────────────────
    // m_activeView  : 現在表示中（ユーザーが見ているページ）
    // m_nextBuffer / m_previousBuffer : バックグラウンドで前後ページを読み込む
    // ページが完成したら raise() で前面に出してポインタを交換（swap）する。
    // Chromium のバッファクリア（黒フラッシュ）は standby 側で起きるため
    // active 側は常に完成済みの画面を表示し続けられる。
    m_viewContainer = new QWidget(m_splitter);
    m_viewContainer->installEventFilter(this);

    m_pageA = new EpubWebPage(profile, this);
    m_viewA = new QWebEngineView(m_viewContainer);
    m_viewA->setPage(m_pageA);

    m_pageB = new EpubWebPage(profile, this);
    m_viewB = new QWebEngineView(m_viewContainer);
    m_viewB->setPage(m_pageB);

    m_pageC = new EpubWebPage(profile, this);
    m_viewC = new QWebEngineView(m_viewContainer);
    m_viewC->setPage(m_pageC);

    m_viewA->show();
    m_viewB->show();
    m_viewC->show();
    m_viewA->raise();                  // A が初期アクティブ
    m_activeView  = m_viewA;  m_activePage  = m_pageA;
    m_nextBuffer.view = m_viewB;
    m_nextBuffer.page = m_pageB;
    m_previousBuffer.view = m_viewC;
    m_previousBuffer.page = m_pageC;
    updatePageBackgroundColor();

    // nav シグナル：アクティブ View からのみチャプター移動する
    for (EpubWebPage* pg : {m_pageA, m_pageB, m_pageC}) {
        connect(pg, &EpubWebPage::navLeft, this, [this, pg]() {
            if (m_activePage == pg) onNavLeft();
        }, Qt::QueuedConnection);
        connect(pg, &EpubWebPage::navRight, this, [this, pg]() {
            if (m_activePage == pg) onNavRight();
        }, Qt::QueuedConnection);
        connect(pg, &EpubWebPage::readingPositionChanged, this, [this, pg](double pos) {
            if (m_activePage == pg) {
                m_currentScrollPosition = qBound(0.0, pos, 1.0);
                updateStatus();
            }
        }, Qt::QueuedConnection);
        // pageReady：スタンバイ View の読み込みが完了したらスワップ検査を開始
        connect(pg, &EpubWebPage::pageReady, this, [this, pg]() {
            if (BufferedView* buffer = bufferForPage(pg)) {
                if (buffer->chapter.chapterIndex < 0) return;
                const QString loadedPath = pg->url().path().mid(1);
                const QString expectedPath = m_reader->chapter(buffer->chapter.chapterIndex).href;
                if (loadedPath != expectedPath) return;

                applyZoomToViews();
                pg->runJavaScript(
                    "if (window._bibiAlignImageOnlyInitialRight) "
                    "window._bibiAlignImageOnlyInitialRight();");
                buffer->chapter.ready = true;
                if (buffer->chapter.forNavigation) {
                    m_swapBuffer = buffer;
                    scheduleSwap();
                }
            }
        }, Qt::QueuedConnection);
    }

    // loadFinished：どちらの View が発火しても同じ JS を注入する
    for (QWebEngineView* view : {m_viewA, m_viewB, m_viewC}) {
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested,
                this, [this, view](const QPoint& pos) {
                    showReaderContextMenu(view, pos);
                });
        connect(view, &QWebEngineView::loadFinished, this, [this, view](bool ok) {
            handleLoadFinished(view, ok);
        });
        // アクティブ View での本文内リンク移動でチャプター番号を追従
        connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl& url) {
            if (view != m_activeView || url.scheme() != "epub") return;
            QString path = url.path().mid(1);
            int idx = hrefToChapterIndex(path);
            if (idx >= 0 && idx != m_currentChapter) {
                m_currentChapter = idx;
                updateNavigationActions();
                updateStatus();
            }
        });
    }

    m_splitter->addWidget(m_sideTabs);
    m_splitter->addWidget(m_viewContainer);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({220, 980});

    setCentralWidget(m_splitter);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("%p%");
    m_progressBar->setFixedWidth(150);
    statusBar()->addPermanentWidget(m_progressBar);

    connect(m_tocTree, &QTreeWidget::itemClicked,
            this,      &MainWindow::onTocItemClicked);
    connect(m_bookmarkTree, &QTreeWidget::itemActivated,
            this,           &MainWindow::onBookmarkActivated);
    connect(m_bookmarkTree, &QTreeWidget::customContextMenuRequested,
            this,           &MainWindow::showBookmarkContextMenu);

    m_swapCheckTimer = new QTimer(this);
    m_swapCheckTimer->setInterval(16); // ~1フレーム間隔でポーリング
    connect(m_swapCheckTimer, &QTimer::timeout, this, &MainWindow::trySwap);

    qApp->installEventFilter(this);
}

// ── ダブルバッファ: loadFinished 処理 ────────────────────────────────────────

void MainWindow::handleLoadFinished(QWebEngineView* view, bool ok) {
    if (!ok) return;
    setViewLoaded(view, true);

    // ページ読み込み時に CSS zoom を再適用する（チャプター遷移後もズーム維持）
    applyZoomToViews();

    // ④ 「…」（U+2026）「‥」（U+2025）を縦書き内で回転させる。
    // グローバルな font-feature-settings:"vert" CSS は Chromium が "…" を
    // アップライト扱いに切り替えて回転を止めてしまうため使用しない。
    // 代わりに各文字を <span> で包み、text-orientation:sideways で個別に回転を強制する。
    // また、フォントが vert グリフを持つ場合に備え fontFeatureSettings もインラインで設定する。
    // 文字リテラルは MSVC の文字コード変換を避けるため Unicode エスケープで記述する。
    view->page()->runJavaScript(R"js(
        (function() {
            if (window._bibiEllipsisFixed) return;
            window._bibiEllipsisFixed = true;

            var body = document.body || document.documentElement;
            var walker = document.createTreeWalker(body, NodeFilter.SHOW_TEXT, null);
            var nodes = [];
            var n;
            while ((n = walker.nextNode())) {
                var t = n.textContent;
                if (t.indexOf('\u2026') >= 0 || t.indexOf('\u2025') >= 0) nodes.push(n);
            }

            var re = /[\u2025\u2026]/g;
            nodes.forEach(function(textNode) {
                var text = textNode.textContent;
                var frag = document.createDocumentFragment();
                var last = 0;
                re.lastIndex = 0;
                var m;
                while ((m = re.exec(text)) !== null) {
                    if (m.index > last)
                        frag.appendChild(document.createTextNode(text.slice(last, m.index)));
                    var span = document.createElement('span');
                    span.style.textOrientation = 'upright';
                    span.style.fontFeatureSettings = '"vert" 1';
                    span.textContent = m[0];
                    frag.appendChild(span);
                    last = m.index + m[0].length;
                }
                if (last < text.length)
                    frag.appendChild(document.createTextNode(text.slice(last)));
                textNode.parentNode.replaceChild(frag, textNode);
            });
        })();
    )js");

    // 縦書き本文は上下いっぱいに詰まると読みにくいため、約1文字分の余白を入れる。
    // 画像のみのページには適用しない。
    view->page()->runJavaScript(R"js(
        (function() {
            window._bibiApplyReadingInsets = function() {
                var html = document.documentElement;
                var body = document.body || html;
                var images = Array.from(document.images);

                function hasText(node) {
                    return !!node && (node.innerText || '').trim().length > 0;
                }

                function isIgnorableElement(el) {
                    var tag = (el.tagName || '').toUpperCase();
                    return tag === 'SCRIPT' || tag === 'STYLE' || tag === 'LINK' ||
                           tag === 'META' || tag === 'TITLE';
                }

                function isImageOnlyPage() {
                    if (!images.length || hasText(body)) return false;
                    var meaningful = Array.from(body.children).filter(function(el) {
                        return !isIgnorableElement(el);
                    });
                    return meaningful.length > 0 && meaningful.every(function(el) {
                        if ((el.tagName || '').toUpperCase() === 'IMG') return true;
                        return el.querySelector && el.querySelector('img');
                    });
                }

                function isVerticalWriting(el) {
                    if (!el) return false;
                    return (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0;
                }

                function findTextElement() {
                    var walker = document.createTreeWalker(body, NodeFilter.SHOW_TEXT, {
                        acceptNode: function(node) {
                            return node.textContent.trim()
                                ? NodeFilter.FILTER_ACCEPT
                                : NodeFilter.FILTER_REJECT;
                        }
                    });
                    var node = walker.nextNode();
                    return node ? node.parentElement : null;
                }

                function resetInsetTargets() {
                    Array.from(document.querySelectorAll('[data-bibi-reading-inset="1"]'))
                        .forEach(function(el) {
                            el.style.boxSizing = '';
                            el.style.height = '';
                            el.style.minHeight = '';
                            el.style.marginTop = '';
                            el.style.marginBottom = '';
                            el.style.paddingTop = '';
                            el.style.paddingBottom = '';
                            delete el.dataset.bibiReadingInset;
                        });
                }

                function findReadingInsetTarget(textEl) {
                    var target = null;
                    var node = textEl;
                    while (node && node !== body && node !== html) {
                        if (isVerticalWriting(node))
                            target = node;
                        node = node.parentElement;
                    }
                    return target;
                }

                function applyBodyInset() {
                    var zoom = Math.max(0.1, Number(window._bibiCssZoom || 1));
                    var viewportHeight = (100 / zoom).toFixed(4) + 'vh';
                    body.dataset.bibiReadingInset = '1';
                    body.style.boxSizing = 'border-box';
                    body.style.height = 'calc(' + viewportHeight + ' - 3em)';
                    body.style.minHeight = '';
                    body.style.paddingTop = '';
                    body.style.paddingBottom = '';
                    body.style.marginTop = '1em';
                    body.style.marginBottom = '2em';
                }

                function applyContentInsetTarget(el) {
                    if (!el || el === body) return;
                    el.dataset.bibiReadingInset = '1';
                    el.style.boxSizing = 'border-box';
                    el.style.height = '100%';
                    el.style.minHeight = '';
                    el.style.paddingTop = '';
                    el.style.paddingBottom = '';
                    el.style.marginTop = '';
                    el.style.marginBottom = '';
                }

                resetInsetTargets();

                var textEl = findTextElement();
                var vertical = isVerticalWriting(html) ||
                               isVerticalWriting(body) ||
                               isVerticalWriting(textEl);

                if (vertical && !isImageOnlyPage()) {
                    // body で余白を作り、本文ラッパーは1つだけ body 内の高さへ合わせる。
                    // 複数階層へ calc(100vh - 3em) を重ねると、EPUB によって折り返し位置が崩れる。
                    // padding だけを足すと、100vh 前提のページで余白分の縦スクロールが出てしまう。
                    applyBodyInset();
                    applyContentInsetTarget(findReadingInsetTarget(textEl));
                    html.style.scrollPaddingTop = '1em';
                    html.style.scrollPaddingBottom = '2em';
                } else {
                    html.style.scrollPaddingTop = '';
                    html.style.scrollPaddingBottom = '';
                }
            };
            window._bibiApplyReadingInsets();
        })();
    )js");

    // 読書位置はスクロール操作中に C++ 側へキャッシュしておく。
    // 終了時に同期的な JS 待ちをしないための軽い通知路。
    view->page()->runJavaScript(R"js(
        (function() {
            function readingPosition() {
                var el = document.documentElement;
                var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||
                               (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);
                var canH = el.scrollWidth  > el.clientWidth;
                var canV = el.scrollHeight > el.clientHeight;
                if ((vertical && canH) || (!canV && canH))
                    return Math.abs(el.scrollLeft) / Math.max(1, el.scrollWidth - el.clientWidth);
                return el.scrollTop / Math.max(1, el.scrollHeight - el.clientHeight);
            }

            var timer = 0;
            window._bibiReportReadingPosition = function() {
                clearTimeout(timer);
                timer = setTimeout(function() {
                    console.log('epub-position:' + readingPosition());
                }, 80);
            };

            if (window._bibiPositionReporterInstalled) {
                window._bibiReportReadingPosition();
                return;
            }
            window._bibiPositionReporterInstalled = true;

            window.addEventListener('scroll', window._bibiReportReadingPosition, true);
            window.addEventListener('wheel', window._bibiReportReadingPosition, true);
            window.addEventListener('keyup', window._bibiReportReadingPosition, true);
            window.addEventListener('mouseup', window._bibiReportReadingPosition, true);
            window.addEventListener('touchend', window._bibiReportReadingPosition, true);
            requestAnimationFrame(window._bibiReportReadingPosition);
        })();
    )js");

    // ① 全画像デコード完了後にスクロール位置を確定し、epub-page-ready を通知する
    const int isRtl       = m_isRtl      ? 1 : 0;
    const int scrollToEnd = scrollToEndForView(view) ? 1 : 0;
    m_scrollToEnd = false;
    view->page()->runJavaScript(QString(
        "(function(){"
        "  var imgs = Array.from(document.querySelectorAll('img, svg image'));"
        "  var ps = imgs.map(function(img){"
        "    return img.decode ? img.decode().catch(function(){}) : Promise.resolve();"
        "  });"
        "  function isIgnorableElement(el) {"
        "    var tag = (el.tagName || '').toUpperCase();"
        "    return tag === 'SCRIPT' || tag === 'STYLE' || tag === 'LINK' ||"
        "           tag === 'META' || tag === 'TITLE';"
        "  }"
        "  function isImageOnlyPage() {"
        "    var body = document.body || document.documentElement;"
        "    var imageLike = document.querySelector('img, svg, object, picture, svg image');"
        "    if (!imageLike || (body.innerText || '').trim().length > 0) return false;"
        "    var meaningful = Array.from(body.children).filter(function(el){"
        "      return !isIgnorableElement(el);"
        "    });"
        "    return meaningful.length > 0 && meaningful.every(function(el){"
        "      var tag = (el.tagName || '').toUpperCase();"
        "      if (tag === 'IMG' || tag === 'SVG' || tag === 'OBJECT' || tag === 'PICTURE') return true;"
        "      return el.querySelector && el.querySelector('img, svg, object, picture, svg image');"
        "    });"
        "  }"
        "  function scrollToVisualRight(el) {"
        "    el.scrollLeft = Math.max(0, el.scrollWidth - el.clientWidth);"
        "  }"
        "  window._bibiAlignImageOnlyInitialRight = function() {"
        "    var el = document.scrollingElement || document.documentElement;"
        "    if (isImageOnlyPage() && el.scrollWidth > el.clientWidth)"
        "      scrollToVisualRight(el);"
        "  };"
        "  Promise.all(ps).then(function(){"
        "    requestAnimationFrame(function(){"
        "      var el = document.documentElement;"
        "      if (%2) {"
        "        if (el.scrollWidth > el.clientWidth)"
        "          window.scrollBy(%1 ? -999999 : 999999, 0);"
        "        else if (el.scrollHeight > el.clientHeight)"
        "          window.scrollBy(0, 999999);"
        "      } else {"
        "        window._bibiAlignImageOnlyInitialRight();"
        "      }"
        "      if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();"
        "      requestAnimationFrame(function(){"
        "        console.log('epub-page-ready');"
        "      });"
        "    });"
        "  });"
        "})();").arg(isRtl).arg(scrollToEnd));

    // ② ホイール → オーバーフロー方向スクロール
        const int sign = m_isRtl ? -1 : 1;
        view->page()->runJavaScript(QString(R"js(
            (function() {
                if (window._bibiWheelInstalled) return;
                window._bibiWheelInstalled = true;
                var SIGN  = %1;   // 1=LTR, -1=RTL
                var _busy = false;
                function isVerticalPage() {
                    var html = document.documentElement;
                    var body = document.body;
                    return (getComputedStyle(html).writingMode || '').indexOf('vertical') === 0 ||
                           (body && (getComputedStyle(body).writingMode || '').indexOf('vertical') === 0);
                }
                function isZoomPanPage() {
                    return !!document.querySelector('[data-bibi-image-zoom-only="1"]');
                }
                window.addEventListener('wheel', function(e) {
                    if (e.ctrlKey) { e.preventDefault(); return; }  // Ctrl+Wheel は C++ イベントフィルターで処理
                    if (e.deltaX === 0 && e.deltaY === 0) return;
                    e.preventDefault();
                    var el    = document.documentElement;
                    var canH  = el.scrollWidth  > el.clientWidth;
                    var canV  = el.scrollHeight > el.clientHeight;
                    var isRtl = SIGN < 0;
                    if (isZoomPanPage() && (canH || canV)) {
                        var maxH = Math.max(0, el.scrollWidth  - el.clientWidth);
                        var maxV = Math.max(0, el.scrollHeight - el.clientHeight);
                        var absSl = Math.abs(el.scrollLeft);
                        var atLeft = absSl <= 2;
                        var atRight = absSl >= maxH - 2;
                        var atTop = el.scrollTop <= 2;
                        var atBottom = el.scrollTop >= maxV - 2;
                        var dx = canH ? (e.deltaX || (!canV ? SIGN * e.deltaY : 0)) : 0;
                        var dy = canV ? e.deltaY : 0;

                        if (!_busy && dy < 0 && atTop) {
                            _busy = true; console.log(SIGN > 0 ? 'epub-nav:left' : 'epub-nav:right');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else if (!_busy && dy > 0 && atBottom) {
                            _busy = true; console.log(SIGN > 0 ? 'epub-nav:right' : 'epub-nav:left');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else if (!_busy && dx < 0 && atLeft) {
                            _busy = true; console.log('epub-nav:left');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else if (!_busy && dx > 0 && atRight) {
                            _busy = true; console.log('epub-nav:right');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else {
                            el.scrollLeft += dx;
                            el.scrollTop  += dy;
                        }
                    } else if (canH && canV) {
                        if (isVerticalPage()) {
                            var maxH = Math.max(0, el.scrollWidth - el.clientWidth);
                            var absSl = Math.abs(el.scrollLeft);
                            var dx = SIGN * e.deltaY;
                            var atLeft = isRtl ? absSl >= maxH - 2 : absSl <= 2;
                            var atRight = isRtl ? absSl <= 2 : absSl >= maxH - 2;
                            if (!_busy && dx < 0 && atLeft) {
                                _busy = true;
                                console.log('epub-nav:left');
                                setTimeout(function(){ _busy=false; }, 1000);
                            } else if (!_busy && dx > 0 && atRight) {
                                _busy = true;
                                console.log('epub-nav:right');
                                setTimeout(function(){ _busy=false; }, 1000);
                            } else {
                                window.scrollBy(dx, 0);
                            }
                        } else {
                            window.scrollBy(e.deltaX || 0, e.deltaY);
                        }
                    } else if (canH) {
                        var sl    = el.scrollLeft;
                        var max   = Math.max(0, el.scrollWidth - el.clientWidth);
                        var absSl = Math.abs(sl);
                        var dx    = SIGN * e.deltaY;
                        // LTR: 左端=absSl≈0、右端=absSl≈max
                        // RTL: 左端=absSl≈max（読み終わり）、右端=absSl≈0（読み始め）
                        var atLeft  = isRtl ? absSl >= max - 2 : absSl <= 2;
                        var atRight = isRtl ? absSl <= 2        : absSl >= max - 2;
                        if (!_busy && dx < 0 && atLeft) {
                            _busy = true;
                            console.log('epub-nav:left');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else if (!_busy && dx > 0 && atRight) {
                            _busy = true;
                            console.log('epub-nav:right');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else {
                            window.scrollBy(dx, 0);
                        }
                    } else if (canV) {
                        var st    = el.scrollTop;
                        var maxV  = Math.max(0, el.scrollHeight - el.clientHeight);
                        var atTop    = st <= 2;
                        var atBottom = st >= maxV - 2;
                        if (!_busy && e.deltaY < 0 && atTop) {
                            _busy = true;
                            console.log(SIGN > 0 ? 'epub-nav:left' : 'epub-nav:right');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else if (!_busy && e.deltaY > 0 && atBottom) {
                            _busy = true;
                            console.log(SIGN > 0 ? 'epub-nav:right' : 'epub-nav:left');
                            setTimeout(function(){ _busy=false; }, 1000);
                        } else {
                            window.scrollBy(0, e.deltaY);
                        }
                    } else {
                        if (!_busy) {
                            _busy = true;
                            console.log(SIGN * e.deltaY > 0 ? 'epub-nav:right' : 'epub-nav:left');
                            setTimeout(function(){ _busy=false; }, 1000);
                        }
                    }
                }, { passive: false });
            })();
        )js").arg(sign));

        // ③ 矢印キー4方向対応キーハンドラー
        const int rtl = m_isRtl ? 1 : 0;
        view->page()->runJavaScript(QString(R"js(
            (function() {
                if (window._bibiKeyInstalled) return;
                window._bibiKeyInstalled = true;
                var STEP  = Math.round(window.innerWidth  * 0.5);
                var VSTEP = Math.round(window.innerHeight * 0.5);
                var isRtl = !!%1;
                var SIGN  = isRtl ? -1 : 1;
                var _busy = false;

                // ← → キーは常に epub-nav:left/right を送りC++側でRTL変換する。
                // ↑ ↓ キーは縦方向＝読む方向が常に上→下なので SIGN で変換する。
                function navLeft() {
                    _busy = true; console.log('epub-nav:left');
                    setTimeout(function(){ _busy = false; }, 1000);
                }
                function navRight() {
                    _busy = true; console.log('epub-nav:right');
                    setTimeout(function(){ _busy = false; }, 1000);
                }
                function navUp() {   // ↑ = 常に前チャプター（読む方向に関係なく）
                    _busy = true; console.log(SIGN > 0 ? 'epub-nav:left' : 'epub-nav:right');
                    setTimeout(function(){ _busy = false; }, 1000);
                }
                function navDown() { // ↓ = 常に次チャプター
                    _busy = true; console.log(SIGN > 0 ? 'epub-nav:right' : 'epub-nav:left');
                    setTimeout(function(){ _busy = false; }, 1000);
                }
                function isVerticalPage() {
                    var html = document.documentElement;
                    var body = document.body;
                    return (getComputedStyle(html).writingMode || '').indexOf('vertical') === 0 ||
                           (body && (getComputedStyle(body).writingMode || '').indexOf('vertical') === 0);
                }
                function isZoomPanPage() {
                    return !!document.querySelector('[data-bibi-image-zoom-only="1"]');
                }

                window.addEventListener('keydown', function(e) {
                    var left  = e.key === 'ArrowLeft';
                    var right = e.key === 'ArrowRight';
                    var up    = e.key === 'ArrowUp';
                    var down  = e.key === 'ArrowDown';
                    var pageUp = e.key === 'PageUp';
                    var pageDown = e.key === 'PageDown';
                    var space = e.key === ' ';
                    if (!left && !right && !up && !down && !pageUp && !pageDown && !space) return;
                    e.preventDefault();
                    e.stopPropagation();
                    if (_busy) return;

                    var el   = document.documentElement;
                    var maxH = Math.max(0, el.scrollWidth  - el.clientWidth);
                    var maxV = Math.max(0, el.scrollHeight - el.clientHeight);
                    var canH = maxH > 2;
                    var canV = maxV > 2;

                    if ((canH || canV) && isZoomPanPage()) {
                        var absSl = Math.abs(el.scrollLeft);
                        var atLeft = absSl <= 2;
                        var atRight = absSl >= maxH - 2;
                        var atTop = el.scrollTop <= 2;
                        var atBot = el.scrollTop >= maxV - 2;
                        if (left) {
                            if (atLeft) navLeft(); else el.scrollLeft -= STEP;
                        } else if (right) {
                            if (atRight) navRight(); else el.scrollLeft += STEP;
                        } else if (up || pageUp || (space && e.shiftKey)) {
                            if (atTop) navUp(); else el.scrollTop -= VSTEP;
                        } else if (down || pageDown || (space && !e.shiftKey)) {
                            if (atBot) navDown(); else el.scrollTop += VSTEP;
                        }
                        if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
                        return;
                    }

                    if (!isVerticalPage()) {
                        var st = el.scrollTop;
                        var atTop = st <= 2;
                        var atBot = st >= maxV - 2;
                        var backward = up || pageUp || (space && e.shiftKey);
                        var forward = down || pageDown || (space && !e.shiftKey);

                        if (left) {
                            navLeft();
                        } else if (right) {
                            navRight();
                        } else if (canV && backward) {
                            if (atTop) navLeft();
                            else window.scrollBy(0, -VSTEP);
                        } else if (canV && forward) {
                            if (atBot) navRight();
                            else window.scrollBy(0, VSTEP);
                        } else if (backward) {
                            navLeft();
                        } else if (forward) {
                            navRight();
                        }

                        if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
                        return;
                    }

                    if (canH && canV) {
                        var dx = right || up ? STEP : left || down ? -STEP : 0;
                        if (dx) window.scrollBy(dx, 0);
                    } else if (canH) {
                        // ↑ → は右スクロール、↓ ← は左スクロール（+ 端でチャプター移動）
                        var goRight = right || up;
                        var goLeft  = left  || down;
                        var absSl   = Math.abs(el.scrollLeft);
                        var atLeft  = isRtl ? absSl >= maxH - 2 : absSl <= 2;
                        var atRight = isRtl ? absSl <= 2        : absSl >= maxH - 2;
                        if (goLeft)  { if (atLeft)  navLeft();  else window.scrollBy(-STEP, 0); }
                        else         { if (atRight) navRight(); else window.scrollBy( STEP, 0); }
                    } else if (canV) {
                        if (up || down) {
                            var st    = el.scrollTop;
                            var atTop = st <= 2;
                            var atBot = st >= maxV - 2;
                            if (up)  { if (atTop) navUp();   else window.scrollBy(0, -VSTEP); }
                            else     { if (atBot) navDown(); else window.scrollBy(0,  VSTEP); }
                        } else {
                            // 縦ページで → : ↑ と同じ（上スクロール＋上端で前チャプター）
                            // 縦ページで ← : ↓ と同じ（下スクロール＋下端で次チャプター）
                            var st    = el.scrollTop;
                            var atTop = st <= 2;
                            var atBot = st >= maxV - 2;
                            if (right) { if (atTop) navUp();   else window.scrollBy(0, -VSTEP); }
                            else       { if (atBot) navDown(); else window.scrollBy(0,  VSTEP); }
                        }
                    } else {
                        // 固定レイアウト: 全方向で即チャプター移動
                        if (left)  navLeft();
                        else if (right) navRight();
                        else if (up)    navUp();
                        else            navDown();
                    }
                }, true);
            })();
        )js").arg(rtl));

    // ④' ドラッグ＝パン：画像ズームページ、または両スクロールバーありのときマウスドラッグでスクロール
    view->page()->runJavaScript(R"js(
        (function() {
            if (window._bibiDragInstalled) return;
            window._bibiDragInstalled = true;

            var drag = { active: false, x: 0, y: 0, sl: 0, st: 0 };

            function scrollElement() {
                return document.scrollingElement || document.documentElement;
            }

            function isZoomPanPage() {
                return !!document.querySelector('[data-bibi-image-zoom-only="1"]');
            }

            function canDragPan() {
                var el = scrollElement();
                var canH = el.scrollWidth > el.clientWidth;
                var canV = el.scrollHeight > el.clientHeight;
                return isZoomPanPage() ? (canH || canV) : (canH && canV);
            }

            document.addEventListener('mousedown', function(e) {
                if (e.button !== 0 || !canDragPan()) return;
                // リンク・フォーム要素は通常動作を優先
                var t = e.target;
                while (t && t !== document.body) {
                    var tn = (t.tagName || '').toUpperCase();
                    if (tn === 'A' || tn === 'INPUT' || tn === 'TEXTAREA' ||
                        tn === 'SELECT' || tn === 'BUTTON') return;
                    t = t.parentNode;
                }
                var el = scrollElement();
                drag.active = true;
                drag.x  = e.clientX;  drag.y  = e.clientY;
                drag.sl = el.scrollLeft; drag.st = el.scrollTop;
                el.style.cursor = 'grabbing';
                el.style.userSelect = 'none';
                e.preventDefault();
            }, true);

            document.addEventListener('mousemove', function(e) {
                if (drag.active) {
                    var el = scrollElement();
                    el.scrollLeft = drag.sl + (drag.x - e.clientX);
                    el.scrollTop  = drag.st + (drag.y - e.clientY);
                } else {
                    document.documentElement.style.cursor = canDragPan() ? 'grab' : '';
                }
            }, true);

            function endDrag() {
                if (!drag.active) return;
                drag.active = false;
                var el = scrollElement();
                el.style.cursor = canDragPan() ? 'grab' : '';
                el.style.userSelect = '';
            }
            document.addEventListener('mouseup',    endDrag, true);
            document.addEventListener('mouseleave', endDrag, true);
        })();
    )js");

    // 前後チャプターをバックグラウンドで先読みする
    const int cur      = m_currentChapter;
    EpubReader* reader = m_reader;
    m_prefetchFuture = QtConcurrent::run([reader, cur]() {
        reader->prefetchChapter(cur + 1);
        reader->prefetchChapter(cur - 1);
    });
}

// ── ダブルバッファ: swap & リサイズ ──────────────────────────────────────────

void MainWindow::scheduleSwap() {
    m_swapCheckCount = 0;
    m_swapCheckTimer->start();
}

void MainWindow::trySwap() {
    if (!m_swapBuffer || !m_swapBuffer->view) {
        m_swapCheckTimer->stop();
        return;
    }

    // スタンバイビューを grab() して実際に描画されているか検査する。
    // img.decode() はCPUメモリ展開のみ保証するため、GPUテクスチャ転送が
    // 完了したかどうかはピクセルを実際に読んで確認するしかない。
    QImage img = m_swapBuffer->view->grab().toImage();

    bool hasContent = false;
    if (!img.isNull() && img.width() > 0 && img.height() > 0) {
        // 左上隅を基準色とし、グリッド状にサンプリングして差異を探す
        const QRgb ref = img.pixel(0, 0);
        const int  xStep = qMax(1, img.width()  / 12);
        const int  yStep = qMax(1, img.height() / 12);
        for (int y = 0; y < img.height() && !hasContent; y += yStep)
            for (int x = 0; x < img.width() && !hasContent; x += xStep)
                if (img.pixel(x, y) != ref) hasContent = true;
    }

    if (hasContent || ++m_swapCheckCount >= kMaxSwapChecks) {
        m_swapCheckTimer->stop();
        performSwap();
    }
}

void MainWindow::performSwap() {
    if (!m_swapBuffer || !m_swapBuffer->view || !m_swapBuffer->page) return;

    promoteBuffer(*m_swapBuffer);
    m_swapBuffer = nullptr;
    m_swapFromChapter = -1;
    m_swapToChapter = -1;

    if (m_postSwapAction) {
        auto action = std::move(m_postSwapAction);
        m_postSwapAction = nullptr;
        QTimer::singleShot(0, this, [action]{ action(); });
    }

    QTimer::singleShot(0, this, &MainWindow::refreshPreloads);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == qApp &&
        (event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::PaletteChange)) {
        updatePageBackgroundColor();
    }

    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            QObject* p = obj;
            while (p) {
                if (p == m_viewA || p == m_viewB || p == m_viewC) {
                    applyCtrlWheelZoom(we->angleDelta().y());
                    return true;
                }
                p = p->parent();
            }
        }
    }
    // コンテナのリサイズに合わせて両 View を同じサイズにする
    if (obj == m_viewContainer && event->type() == QEvent::Resize) {
        const QSize s = m_viewContainer->size();
        m_viewA->setGeometry(0, 0, s.width(), s.height());
        m_viewB->setGeometry(0, 0, s.width(), s.height());
        m_viewC->setGeometry(0, 0, s.width(), s.height());
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setupMenuBar() {
    // ── File ──────────────────────────────────────────────────────────────
    auto* fileMenu = menuBar()->addMenu(tr("ファイル(&F)"));

    auto* openAct = fileMenu->addAction(tr("EPUBを開く(&O)…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(
            this, tr("EPUBを開く"), {},
            tr("EPUBファイル (*.epub);;すべてのファイル (*)"));
        if (!path.isEmpty()) openEpub(path);
    });

    fileMenu->addSeparator();

    m_recentFiles->attachToMenu(fileMenu);
    connect(m_recentFiles, &RecentFiles::openRequested,
            this, &MainWindow::openEpub);
    connect(m_recentFiles, &RecentFiles::missingFile, this, [this](const QString& path) {
        QMessageBox::warning(this, tr("最近開いたファイル"),
            tr("ファイルが見つかりません:\n%1\n\n履歴から削除します。").arg(path));
    });

    fileMenu->addSeparator();

    auto* quitAct = fileMenu->addAction(tr("終了(&Q)"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    // ── View ──────────────────────────────────────────────────────────────
    auto* viewMenu = menuBar()->addMenu(tr("表示(&V)"));

    auto* zoomInAct = viewMenu->addAction(tr("拡大"));
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, this, [this] {
        setZoomFactor(m_zoomFactor + 0.1);
    });

    auto* zoomOutAct = viewMenu->addAction(tr("縮小"));
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, this, [this] {
        setZoomFactor(m_zoomFactor - 0.1);
    });

    auto* resetZoomAct = viewMenu->addAction(tr("標準サイズ"));
    resetZoomAct->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(resetZoomAct, &QAction::triggered, this, [this] {
        setZoomFactor(1.0);
    });

    viewMenu->addSeparator();

    auto* toggleTocAct = viewMenu->addAction(tr("目次パネル(&T)"));
    toggleTocAct->setCheckable(true);
    toggleTocAct->setChecked(true);
    connect(toggleTocAct, &QAction::toggled, m_sideTabs, &QWidget::setVisible);

    viewMenu->addSeparator();

    auto* settingsAct = viewMenu->addAction(tr("設定(&P)…"));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::showSettings);

    // ── Bookmarks ─────────────────────────────────────────────────────────
    auto* bmMenu = menuBar()->addMenu(tr("しおり(&B)"));

    auto* addBmAct = bmMenu->addAction(tr("しおりを追加(&A)"));
    addBmAct->setShortcut(Qt::CTRL | Qt::Key_D);
    connect(addBmAct, &QAction::triggered, this, &MainWindow::addBookmark);

    auto* manageBmAct = bmMenu->addAction(tr("しおり一覧(&M)…"));
    connect(manageBmAct, &QAction::triggered, this, &MainWindow::manageBookmarks);

    // ── Search ────────────────────────────────────────────────────────────
    auto* searchMenu = menuBar()->addMenu(tr("検索(&S)"));

    m_searchMenuAct = searchMenu->addAction(tr("検索(&F)…"));
    m_searchMenuAct->setShortcut(Qt::CTRL | Qt::Key_F);
    connect(m_searchMenuAct, &QAction::triggered, this, &MainWindow::showSearch);
}

void MainWindow::setupToolBar() {
    auto* tb = addToolBar(tr("ナビゲーション"));
    tb->setObjectName("navToolBar");
    tb->setMovable(false);

    m_coverAct = tb->addAction(tr("|< 表紙"));
    m_coverAct->setToolTip(tr("表紙へ移動（Ctrl+Home）"));
    m_coverAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    m_coverAct->setEnabled(false);
    connect(m_coverAct, &QAction::triggered, this, &MainWindow::goToCover);

    // 左右キーはJSが担うため、前後移動ボタンにはショートカットを設定しない。
    m_leftAct = tb->addAction(tr("◀"));
    m_leftAct->setEnabled(false);
    connect(m_leftAct, &QAction::triggered, this, &MainWindow::onLeftKey);

    m_rightAct = tb->addAction(tr("▶"));
    m_rightAct->setEnabled(false);
    connect(m_rightAct, &QAction::triggered, this, &MainWindow::onRightKey);

    m_endAct = tb->addAction(tr("末尾 >|"));
    m_endAct->setToolTip(tr("末尾へ移動（Ctrl+End）"));
    m_endAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_End));
    m_endAct->setEnabled(false);
    connect(m_endAct, &QAction::triggered, this, &MainWindow::goToBookEnd);

    tb->addSeparator();

    m_bookmarkAct = tb->addAction(tr("★ しおり"));
    m_bookmarkAct->setEnabled(false);
    connect(m_bookmarkAct, &QAction::triggered, this, &MainWindow::addBookmark);

    tb->addSeparator();

    auto* searchTbAct = tb->addAction(tr("検索"));
    connect(searchTbAct, &QAction::triggered, this, &MainWindow::showSearch);
}

void MainWindow::setupSearchBar() {
    m_searchBar = addToolBar(tr("検索"));
    m_searchBar->setObjectName("searchToolBar");
    m_searchBar->setMovable(false);
    m_searchBar->setVisible(false);

    m_searchEdit = new QLineEdit(m_searchBar);
    m_searchEdit->setPlaceholderText(tr("全文検索"));
    m_searchEdit->setMinimumWidth(260);
    m_searchBar->addWidget(m_searchEdit);

    m_searchPrevAct = m_searchBar->addAction(tr("前へ"));
    m_searchNextAct = m_searchBar->addAction(tr("次へ"));
    m_searchCountLabel = new QLabel(tr("0 / 0"), m_searchBar);
    m_searchBar->addWidget(m_searchCountLabel);
    m_searchListAct = m_searchBar->addAction(tr("一覧"));
    auto* closeAct = m_searchBar->addAction(tr("閉じる"));

    m_searchPrevAct->setEnabled(false);
    m_searchNextAct->setEnabled(false);
    m_searchListAct->setEnabled(false);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
            searchPrevious();
        else
            searchNext();
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] {
        m_searchResults.clear();
        m_searchIndex = -1;
        m_searchQuery.clear();
        m_searchCountLabel->setText(tr("0 / 0"));
        m_searchPrevAct->setEnabled(false);
        m_searchNextAct->setEnabled(false);
        m_searchListAct->setEnabled(false);
        if (m_searchEdit->text().trimmed().isEmpty())
            clearSearchHighlights();
    });
    connect(m_searchPrevAct, &QAction::triggered, this, &MainWindow::searchPrevious);
    connect(m_searchNextAct, &QAction::triggered, this, &MainWindow::searchNext);
    connect(m_searchListAct, &QAction::triggered, this, &MainWindow::openSearchResults);
    connect(closeAct, &QAction::triggered, this, &MainWindow::closeSearchBar);

    auto* findNextAct = new QAction(this);
    findNextAct->setShortcut(Qt::Key_F3);
    addAction(findNextAct);
    connect(findNextAct, &QAction::triggered, this, &MainWindow::searchNext);

    auto* findPrevAct = new QAction(this);
    findPrevAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    addAction(findPrevAct);
    connect(findPrevAct, &QAction::triggered, this, &MainWindow::searchPrevious);
}

// ── EPUB Loading ──────────────────────────────────────────────────────────

void MainWindow::openEpub(const QString& filePath) {
    saveCurrentReadingPosition();
    m_prefetchFuture.waitForFinished();
    if (!m_reader->open(filePath)) {
        QMessageBox::critical(this, tr("エラー"),
            tr("EPUBファイルを開けませんでした:\n%1\n\n原因: %2")
            .arg(filePath, m_reader->lastError()));
        return;
    }
    m_recentFiles->addFile(filePath);

    m_urlScheme->setReader(m_reader);
    m_currentChapter = -1;
    m_currentScrollPosition = 0.0;
    invalidatePreloads();
    m_isRtl = (m_reader->metadata().pageProgressionDirection == "rtl");

    // ボタンラベルをページ進行方向に合わせる
    if (m_isRtl) {
        m_leftAct->setText(tr("◀ 次へ"));
        m_leftAct->setToolTip(tr("次のページ（←）"));
        m_rightAct->setText(tr("前へ ▶"));
        m_rightAct->setToolTip(tr("前のページ（→）"));
    } else {
        m_leftAct->setText(tr("◀ 前へ"));
        m_leftAct->setToolTip(tr("前のページ（←）"));
        m_rightAct->setText(tr("次へ ▶"));
        m_rightAct->setToolTip(tr("次のページ（→）"));
    }

    m_tocTree->clear();
    populateToc(m_reader->toc());
    m_tocTree->expandAll();
    refreshBookmarkList();

    updateWindowTitle();
    updateNavigationActions();

    ReadingPosition pos;
    if (m_reader->chapterCount() > 0 &&
        m_bookmarkManager->readingPositionForEpub(m_reader->filePath(), &pos) &&
        pos.chapterIndex >= 0 && pos.chapterIndex < m_reader->chapterCount()) {
        jumpToReadingPosition(pos);
        statusBar()->showMessage(tr("前回の位置から再開しました"), 1500);
    } else if (m_reader->chapterCount() > 0) {
        goToChapter(0);
    }
}

void MainWindow::populateToc(const QList<NavPoint>& pts, QTreeWidgetItem* parent) {
    for (const auto& np : pts) {
        QTreeWidgetItem* item = parent
            ? new QTreeWidgetItem(parent)
            : new QTreeWidgetItem(m_tocTree);
        item->setText(0, np.label);
        item->setData(0, Qt::UserRole, np.src);
        populateToc(np.children, item);
    }
}

void MainWindow::onTocItemClicked(QTreeWidgetItem* item, int /*column*/) {
    QString src = item->data(0, Qt::UserRole).toString();
    if (!src.isEmpty()) goToHref(src);
}

void MainWindow::refreshBookmarkList() {
    const bool sorting = m_bookmarkTree->isSortingEnabled();
    m_bookmarkTree->setSortingEnabled(false);
    m_bookmarkTree->clear();
    if (!m_reader->isOpen()) {
        m_bookmarkTree->setSortingEnabled(sorting);
        return;
    }

    const QList<Bookmark> bms = m_bookmarkManager->bookmarksForEpub(m_reader->filePath());
    for (const Bookmark& bm : bms) {
        const QString chapter = chapterLabel(bm.chapterIndex);
        const QString created = bm.createdAt.toString("yyyy/MM/dd HH:mm");
        const int progress = qRound(bm.scrollPosition * 100.0);
        auto* item = new BookmarkTreeItem(m_bookmarkTree);
        item->setText(0, bm.label);
        item->setText(1, chapter);
        item->setText(2, QString("%1%").arg(progress));
        item->setText(3, created);
        item->setData(0, Qt::UserRole, bm.id);
        item->setData(2, Qt::UserRole, progress);
        item->setData(3, Qt::UserRole, bm.createdAt);
        item->setToolTip(0, bm.label);
        item->setToolTip(1, chapter);
        item->setToolTip(2, tr("位置: %1%").arg(progress));
        item->setToolTip(3, created);
    }
    m_bookmarkTree->setSortingEnabled(sorting);
}

void MainWindow::onBookmarkActivated(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    const QString id = item->data(0, Qt::UserRole).toString();
    const QList<Bookmark> bms = m_bookmarkManager->bookmarksForEpub(m_reader->filePath());
    for (const Bookmark& bm : bms) {
        if (bm.id == id) {
            jumpToBookmark(bm);
            return;
        }
    }
}

void MainWindow::showBookmarkContextMenu(const QPoint& pos) {
    auto* item = m_bookmarkTree->itemAt(pos);
    if (!item || !m_reader->isOpen()) return;

    const QString id = item->data(0, Qt::UserRole).toString();
    const QList<Bookmark> bms = m_bookmarkManager->bookmarksForEpub(m_reader->filePath());
    Bookmark bm;
    bool found = false;
    for (const Bookmark& candidate : bms) {
        if (candidate.id == id) {
            bm = candidate;
            found = true;
            break;
        }
    }
    if (!found) return;

    QMenu menu(this);
    QAction* jumpAct = menu.addAction(tr("移動"));
    QAction* renameAct = menu.addAction(tr("名前変更..."));
    QAction* deleteAct = menu.addAction(tr("削除"));
    QAction* selected = menu.exec(m_bookmarkTree->viewport()->mapToGlobal(pos));

    if (selected == jumpAct) {
        jumpToBookmark(bm);
    } else if (selected == renameAct) {
        bool ok = false;
        QString label = QInputDialog::getText(
            this, tr("しおりの名前変更"), tr("しおりの名前:"),
            QLineEdit::Normal, bm.label, &ok);
        if (ok && m_bookmarkManager->renameBookmark(bm.id, label))
            refreshBookmarkList();
    } else if (selected == deleteAct) {
        const auto answer = QMessageBox::question(
            this, tr("しおりを削除"),
            tr("「%1」を削除しますか？").arg(bm.label),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer == QMessageBox::Yes &&
            m_bookmarkManager->removeBookmark(bm.id)) {
            refreshBookmarkList();
            statusBar()->showMessage(tr("しおりを削除しました"), 1500);
        }
    }
}

void MainWindow::showReaderContextMenu(QWebEngineView* view, const QPoint& pos) {
    if (view != m_activeView || !m_reader->isOpen()) return;

    QMenu menu(this);

    QAction* copyAct = nullptr;
    if (!view->page()->selectedText().isEmpty()) {
        copyAct = menu.addAction(tr("コピー"));
        menu.addSeparator();
    }

    QAction* bookmarkAct = menu.addAction(tr("しおりを追加"));
    menu.addSeparator();

    QAction* coverAct = menu.addAction(tr("表紙へ"));
    QAction* prevAct = menu.addAction(m_isRtl ? tr("前へ") : tr("前へ"));
    QAction* nextAct = menu.addAction(m_isRtl ? tr("次へ") : tr("次へ"));
    QAction* endAct = menu.addAction(tr("末尾へ"));
    coverAct->setEnabled(m_coverAct->isEnabled());
    prevAct->setEnabled(m_isRtl ? m_rightAct->isEnabled() : m_leftAct->isEnabled());
    nextAct->setEnabled(m_isRtl ? m_leftAct->isEnabled() : m_rightAct->isEnabled());
    endAct->setEnabled(m_endAct->isEnabled());

    menu.addSeparator();
    QAction* zoomInAct = menu.addAction(tr("拡大"));
    QAction* zoomOutAct = menu.addAction(tr("縮小"));
    QAction* resetZoomAct = menu.addAction(tr("標準サイズ"));

    menu.addSeparator();
    QAction* searchAct = menu.addAction(tr("検索"));

    QAction* selected = menu.exec(view->mapToGlobal(pos));
    if (!selected) return;

    if (selected == copyAct) {
        view->page()->triggerAction(QWebEnginePage::Copy);
    } else if (selected == bookmarkAct) {
        addBookmark();
    } else if (selected == coverAct) {
        goToCover();
    } else if (selected == prevAct) {
        prevChapter();
    } else if (selected == nextAct) {
        nextChapter();
    } else if (selected == endAct) {
        goToBookEnd();
    } else if (selected == zoomInAct) {
        setZoomFactor(m_zoomFactor + 0.1);
    } else if (selected == zoomOutAct) {
        setZoomFactor(m_zoomFactor - 0.1);
    } else if (selected == resetZoomAct) {
        setZoomFactor(1.0);
    } else if (selected == searchAct) {
        showSearch();
    }
}

void MainWindow::jumpToBookmark(const Bookmark& bm) {
    goToChapter(bm.chapterIndex);

    const double pos = bm.scrollPosition;
    m_currentScrollPosition = pos;
    m_postSwapAction = [this, pos]() {
        m_activePage->runJavaScript(QString(
            "(function(){"
            "  var el = document.documentElement;"
            "  var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||"
            "                 (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);"
            "  var canH = el.scrollWidth  > el.clientWidth;"
            "  var canV = el.scrollHeight > el.clientHeight;"
            "  if ((vertical && canH) || (!canV && canH))"
            "    el.scrollLeft = %1 * Math.max(0, el.scrollWidth  - el.clientWidth);"
            "  else"
            "    el.scrollTop  = %1 * Math.max(0, el.scrollHeight - el.clientHeight);"
            "})()").arg(pos));
    };
}

void MainWindow::jumpToReadingPosition(const ReadingPosition& pos) {
    goToChapter(pos.chapterIndex);

    const double scrollPosition = pos.scrollPosition;
    m_currentScrollPosition = scrollPosition;
    m_postSwapAction = [this, scrollPosition]() {
        m_activePage->runJavaScript(QString(
            "(function(){"
            "  var el = document.documentElement;"
            "  var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||"
            "                 (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);"
            "  var canH = el.scrollWidth  > el.clientWidth;"
            "  var canV = el.scrollHeight > el.clientHeight;"
            "  if ((vertical && canH) || (!canV && canH))"
            "    el.scrollLeft = %1 * Math.max(0, el.scrollWidth  - el.clientWidth);"
            "  else"
            "    el.scrollTop  = %1 * Math.max(0, el.scrollHeight - el.clientHeight);"
            "})()").arg(scrollPosition));
    };
}

void MainWindow::jumpToSearchResult(int chapterIndex, const QString& query, int occurrenceIndex) {
    if (query.trimmed().isEmpty()) {
        goToChapter(chapterIndex);
        return;
    }

    const QString queryJson = QString::fromUtf8(
        QJsonDocument(QJsonArray{query}).toJson(QJsonDocument::Compact));
    const QString needleExpression = queryJson.mid(1, queryJson.size() - 2);

    int activeChapter = -1;
    if (m_activePage) {
        const QString activePath = m_activePage->url().path().mid(1);
        activeChapter = hrefToChapterIndex(activePath);
    }
    const bool targetIsDisplayed = chapterIndex == activeChapter;
    const bool sameHighlightedChapter =
        targetIsDisplayed &&
        chapterIndex == m_highlightedSearchChapter &&
        query == m_highlightedSearchQuery;
    const bool shouldScrollToMatch = !sameHighlightedChapter;

    auto applyHighlight = [this, needleExpression, occurrenceIndex, shouldScrollToMatch]() {
        m_activePage->runJavaScript(QString(R"js(
            (function() {
                var needle = %1;
                var occurrenceIndex = %2;
                var scrollToMatch = %3;
                if (!needle) return false;

                var root = document.body || document.documentElement;
                var lower = needle.toLocaleLowerCase();

                Array.from(document.querySelectorAll('mark[data-bibi-search-highlight="1"]'))
                    .forEach(function(mark) {
                        var parent = mark.parentNode;
                        if (!parent) return;
                        while (mark.firstChild)
                            parent.insertBefore(mark.firstChild, mark);
                        parent.removeChild(mark);
                        parent.normalize();
                    });

                var style = document.getElementById('bibi-search-highlight-style');
                if (!style) {
                    style = document.createElement('style');
                    document.head.appendChild(style);
                }
                style.id = 'bibi-search-highlight-style';
                style.textContent =
                    'mark[data-bibi-search-highlight="1"] {' +
                    '  background: rgba(255, 230, 109, 0.42);' +
                    '  color: inherit;' +
                    '  padding: 0 0.08em;' +
                    '  border-radius: 0.12em;' +
                    '}' +
                    'mark[data-bibi-search-current="1"] {' +
                    '  background: #ffb000;' +
                    '  box-shadow: 0 0 0 1px rgba(80, 52, 0, 0.28);' +
                    '}';

                var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
                    acceptNode: function(node) {
                        if (node.parentElement &&
                            node.parentElement.closest('script, style, mark[data-bibi-search-highlight="1"]'))
                            return NodeFilter.FILTER_REJECT;
                        return node.textContent.trim()
                            ? NodeFilter.FILTER_ACCEPT
                            : NodeFilter.FILTER_REJECT;
                    }
                });

                var node;
                var seen = 0;
                var matches = [];
                while ((node = walker.nextNode())) {
                    var text = node.textContent;
                    var lowerText = text.toLocaleLowerCase();
                    var index = lowerText.indexOf(lower);
                    var positions = [];
                    while (index >= 0) {
                        positions.push({
                            start: index,
                            end: index + needle.length,
                            occurrence: seen++
                        });
                        index = lowerText.indexOf(lower, index + needle.length);
                    }
                    if (positions.length)
                        matches.push({ node: node, text: text, positions: positions });
                }

                var currentMark = null;
                var firstMark = null;
                matches.forEach(function(entry) {
                    var frag = document.createDocumentFragment();
                    var last = 0;
                    entry.positions.forEach(function(match) {
                        if (match.start > last)
                            frag.appendChild(document.createTextNode(entry.text.slice(last, match.start)));

                        var mark = document.createElement('mark');
                        mark.dataset.bibiSearchHighlight = '1';
                        mark.textContent = entry.text.slice(match.start, match.end);
                        if (match.occurrence === occurrenceIndex) {
                            mark.dataset.bibiSearchCurrent = '1';
                            currentMark = mark;
                        }
                        if (!firstMark)
                            firstMark = mark;
                        frag.appendChild(mark);
                        last = match.end;
                    });
                    if (last < entry.text.length)
                        frag.appendChild(document.createTextNode(entry.text.slice(last)));
                    if (entry.node.parentNode)
                        entry.node.parentNode.replaceChild(frag, entry.node);
                });

                var mark = currentMark || firstMark;
                if (!mark) return false;

                var selection = window.getSelection();
                selection.removeAllRanges();

                function scrollMatchIntoViewIfNeeded() {
                    requestAnimationFrame(function() {
                        var el = document.scrollingElement || document.documentElement;
                        var rect = mark.getBoundingClientRect();
                        if (!rect || (!rect.width && !rect.height)) {
                            if (scrollToMatch)
                                mark.scrollIntoView({ block: 'center', inline: 'center' });
                            return;
                        }

                        var margin = Math.max(24, Math.round(Math.min(el.clientWidth, el.clientHeight) * 0.08));
                        var dx = 0;
                        var dy = 0;

                        if (scrollToMatch) {
                            dx = rect.left + rect.width / 2 - el.clientWidth / 2;
                            dy = rect.top + rect.height / 2 - el.clientHeight / 2;
                        } else {
                            if (rect.left < margin)
                                dx = rect.left - margin;
                            else if (rect.right > el.clientWidth - margin)
                                dx = rect.right - (el.clientWidth - margin);

                            if (rect.top < margin)
                                dy = rect.top - margin;
                            else if (rect.bottom > el.clientHeight - margin)
                                dy = rect.bottom - (el.clientHeight - margin);
                        }

                        // RTL/vertical-rl では scrollLeft の絶対値モデルがブラウザ依存になるため、
                        // 絶対座標代入ではなく、表示位置との差分だけをスクロールする。
                        if (dx || dy)
                            el.scrollBy(dx, dy);

                        requestAnimationFrame(function() {
                            var after = mark.getBoundingClientRect();
                            if (scrollToMatch && after && (after.width || after.height)) {
                                var adjustX = after.left + after.width / 2 - el.clientWidth / 2;
                                var adjustY = after.top + after.height / 2 - el.clientHeight / 2;
                                if (Math.abs(adjustX) > 4 || Math.abs(adjustY) > 4)
                                    el.scrollBy(adjustX, adjustY);
                            }
                            if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
                        });
                    });
                }
                scrollMatchIntoViewIfNeeded();
                return true;
            })();
        )js").arg(needleExpression).arg(occurrenceIndex).arg(shouldScrollToMatch ? QStringLiteral("true") : QStringLiteral("false")));
    };

    m_highlightedSearchQuery = query;
    m_highlightedSearchChapter = chapterIndex;

    if (targetIsDisplayed) {
        applyHighlight();
        return;
    }

    if (m_swapBuffer && m_swapToChapter == chapterIndex) {
        m_postSwapAction = applyHighlight;
        return;
    }

    goToChapter(chapterIndex);
    m_postSwapAction = applyHighlight;
}

void MainWindow::saveCurrentReadingPosition() {
    if (!m_reader->isOpen() || m_currentChapter < 0) return;

    ReadingPosition pos;
    pos.epubPath       = m_reader->filePath();
    pos.chapterIndex   = m_currentChapter;
    pos.scrollPosition = m_currentScrollPosition;
    pos.updatedAt      = QDateTime::currentDateTime();
    m_bookmarkManager->saveReadingPosition(pos);
}

QString MainWindow::chapterLabel(int chapterIndex) const {
    if (m_reader->isOpen() && chapterIndex >= 0 && chapterIndex < m_reader->chapterCount()) {
        const QString href = m_reader->chapter(chapterIndex).href;
        QString title;

        std::function<bool(const QList<NavPoint>&)> findTitle =
            [&](const QList<NavPoint>& pts) -> bool {
                for (const NavPoint& np : pts) {
                    const QString path = np.src.section('#', 0, 0);
                    if (path == href && !np.label.trimmed().isEmpty()) {
                        title = np.label.trimmed();
                        return true;
                    }
                    if (findTitle(np.children)) return true;
                }
                return false;
            };

        if (findTitle(m_reader->toc()))
            return title;
    }
    return tr("Chapter %1").arg(chapterIndex + 1);
}

// ── Navigation ────────────────────────────────────────────────────────────

void MainWindow::goToChapter(int index) {
    if (!m_reader->isOpen()) return;
    if (index < 0 || index >= m_reader->chapterCount()) return;

    const int fromChapter = m_currentChapter;
    m_currentChapter = index;
    const bool scrollToEnd = m_scrollToEnd;
    m_currentScrollPosition = scrollToEnd ? 1.0 : 0.0;
    m_scrollToEnd = false;

    BufferedView* matched = nullptr;
    if (bufferedChapterMatches(m_nextBuffer, index, scrollToEnd))
        matched = &m_nextBuffer;
    else if (bufferedChapterMatches(m_previousBuffer, index, scrollToEnd))
        matched = &m_previousBuffer;

    m_swapFromChapter = fromChapter;
    m_swapToChapter = index;

    if (matched) {
        matched->chapter.forNavigation = true;
        m_swapBuffer = matched;
        if (matched->chapter.ready)
            scheduleSwap();
    } else {
        invalidatePreloads();
        loadBufferedChapter(m_nextBuffer, index, scrollToEnd, true);
        m_swapBuffer = &m_nextBuffer;
    }
    updateNavigationActions();
    updateStatus();
}

void MainWindow::goToHref(const QString& href) {
    // href is epub-root-relative, may include #fragment
    QString path = href.section('#', 0, 0);
    QString frag = href.section('#', 1);

    int idx = hrefToChapterIndex(path);
    if (idx < 0) return;

    m_currentChapter = idx;
    m_currentScrollPosition = 0.0;
    EpubChapter ch   = m_reader->chapter(idx);
    QUrl url("epub:///" + ch.href);
    if (!frag.isEmpty()) url.setFragment(frag);
    invalidatePreloads();
    m_swapBuffer = &m_nextBuffer;
    m_swapFromChapter = -1;
    m_swapToChapter = idx;
    m_scrollToEnd = false;
    m_nextBuffer.chapter.chapterIndex = idx;
    m_nextBuffer.chapter.ready = false;
    m_nextBuffer.chapter.forNavigation = true;
    m_nextBuffer.chapter.scrollToEnd = false;
    setViewLoaded(m_nextBuffer.view, false);
    m_nextBuffer.view->setUrl(url);

    updateNavigationActions();
    updateStatus();
}

void MainWindow::loadBufferedChapter(BufferedView& buffer, int index, bool scrollToEnd, bool forNavigation) {
    if (!m_reader->isOpen()) return;
    if (index < 0 || index >= m_reader->chapterCount()) return;

    m_swapCheckTimer->stop();
    buffer.chapter.chapterIndex = index;
    buffer.chapter.ready = false;
    buffer.chapter.forNavigation = forNavigation;
    buffer.chapter.scrollToEnd = scrollToEnd;
    m_scrollToEnd = scrollToEnd;

    EpubChapter ch = m_reader->chapter(index);
    setViewLoaded(buffer.view, false);
    buffer.view->setUrl(QUrl("epub:///" + ch.href));
}

int MainWindow::nextReadingChapterIndex() const {
    if (!m_reader->isOpen() || m_currentChapter < 0) return -1;
    const int next = m_currentChapter + 1;
    return (next >= 0 && next < m_reader->chapterCount()) ? next : -1;
}

int MainWindow::previousReadingChapterIndex() const {
    if (!m_reader->isOpen() || m_currentChapter < 0) return -1;
    const int previous = m_currentChapter - 1;
    return (previous >= 0 && previous < m_reader->chapterCount()) ? previous : -1;
}

void MainWindow::refreshPreloads() {
    if (m_preloadMode == PreloadMode::None) return;
    if (!m_reader->isOpen() || m_postSwapAction) return;
    if (m_swapCheckTimer && m_swapCheckTimer->isActive()) return;

    const int next = nextReadingChapterIndex();
    if (next >= 0 &&
        !(m_nextBuffer.chapter.chapterIndex == next && !m_nextBuffer.chapter.forNavigation)) {
        loadBufferedChapter(m_nextBuffer, next, false, false);
    }

    if (m_preloadMode != PreloadMode::NextAndPrevious) return;

    const int previous = previousReadingChapterIndex();
    if (previous >= 0 &&
        !(m_previousBuffer.chapter.chapterIndex == previous && !m_previousBuffer.chapter.forNavigation)) {
        loadBufferedChapter(m_previousBuffer, previous, true, false);
    }
}

void MainWindow::invalidatePreloads() {
    if (m_swapCheckTimer)
        m_swapCheckTimer->stop();
    m_nextBuffer.chapter = {};
    m_previousBuffer.chapter = {};
    m_swapBuffer = nullptr;
}

bool MainWindow::bufferedChapterMatches(const BufferedView& buffer, int index, bool scrollToEnd) const {
    return buffer.chapter.chapterIndex == index &&
           buffer.chapter.scrollToEnd == scrollToEnd;
}

MainWindow::BufferedView* MainWindow::bufferForPage(EpubWebPage* page) {
    if (m_nextBuffer.page == page) return &m_nextBuffer;
    if (m_previousBuffer.page == page) return &m_previousBuffer;
    return nullptr;
}

const MainWindow::BufferedView* MainWindow::bufferForPage(EpubWebPage* page) const {
    if (m_nextBuffer.page == page) return &m_nextBuffer;
    if (m_previousBuffer.page == page) return &m_previousBuffer;
    return nullptr;
}

bool MainWindow::scrollToEndForView(QWebEngineView* view) const {
    if (view && view->page() == m_nextBuffer.page)
        return m_nextBuffer.chapter.scrollToEnd;
    if (view && view->page() == m_previousBuffer.page)
        return m_previousBuffer.chapter.scrollToEnd;
    return m_scrollToEnd;
}

void MainWindow::promoteBuffer(BufferedView& buffer) {
    QWebEngineView* oldActiveView = m_activeView;
    EpubWebPage* oldActivePage = m_activePage;
    const int oldChapter = m_swapFromChapter;
    const int targetChapter = m_swapToChapter;

    buffer.view->raise();
    buffer.view->setFocus();
    m_activeView = buffer.view;
    m_activePage = buffer.page;

    const bool movedNext = oldChapter >= 0 && targetChapter == oldChapter + 1;
    const bool movedPrevious = oldChapter >= 0 && targetChapter == oldChapter - 1;

    if (&buffer == &m_nextBuffer) {
        QWebEngineView* spareView = m_previousBuffer.view;
        EpubWebPage* sparePage = m_previousBuffer.page;
        if (movedNext) {
            m_previousBuffer.view = oldActiveView;
            m_previousBuffer.page = oldActivePage;
            m_previousBuffer.chapter = {oldChapter, true, false, true};
            m_nextBuffer.view = spareView;
            m_nextBuffer.page = sparePage;
            m_nextBuffer.chapter = {};
        } else {
            m_nextBuffer.view = oldActiveView;
            m_nextBuffer.page = oldActivePage;
            m_nextBuffer.chapter = {};
            m_previousBuffer.view = spareView;
            m_previousBuffer.page = sparePage;
            m_previousBuffer.chapter = {};
        }
    } else {
        QWebEngineView* spareView = m_nextBuffer.view;
        EpubWebPage* sparePage = m_nextBuffer.page;
        if (movedPrevious) {
            m_nextBuffer.view = oldActiveView;
            m_nextBuffer.page = oldActivePage;
            m_nextBuffer.chapter = {oldChapter, true, false, false};
            m_previousBuffer.view = spareView;
            m_previousBuffer.page = sparePage;
            m_previousBuffer.chapter = {};
        } else {
            m_previousBuffer.view = oldActiveView;
            m_previousBuffer.page = oldActivePage;
            m_previousBuffer.chapter = {};
            m_nextBuffer.view = spareView;
            m_nextBuffer.page = sparePage;
            m_nextBuffer.chapter = {};
        }
    }
}

int MainWindow::hrefToChapterIndex(const QString& path) const {
    for (int i = 0; i < m_reader->chapterCount(); ++i) {
        if (m_reader->chapter(i).href == path)
            return i;
    }
    return -1;
}

void MainWindow::goToCover() {
    if (!m_reader->isOpen() || m_reader->chapterCount() <= 0) return;
    m_scrollToEnd = false;
    goToChapter(0);
}

void MainWindow::goToBookEnd() {
    if (!m_reader->isOpen() || m_reader->chapterCount() <= 0) return;
    m_scrollToEnd = true;
    goToChapter(m_reader->chapterCount() - 1);
}

void MainWindow::nextChapter(bool scrollToEnd) {
    if (m_currentChapter + 1 < m_reader->chapterCount()) {
        m_scrollToEnd = scrollToEnd;
        goToChapter(m_currentChapter + 1);
    }
}

void MainWindow::prevChapter(bool scrollToEnd) {
    if (m_currentChapter > 0) {
        m_scrollToEnd = scrollToEnd;
        goToChapter(m_currentChapter - 1);
    }
}

// ツールバーボタン：scrollToEnd なし（先頭から表示）
void MainWindow::onLeftKey()  { if (m_isRtl) nextChapter(); else prevChapter(); }
void MainWindow::onRightKey() { if (m_isRtl) prevChapter(); else nextChapter(); }

// JSページ端通知：前のチャプターへ移動するときは末尾へスクロール
void MainWindow::onNavLeft() {
    if (m_isRtl) nextChapter(false);  // RTL: ← = 次チャプターの先頭へ
    else         prevChapter(true);   // LTR: ← = 前チャプターの末尾へ
}
void MainWindow::onNavRight() {
    if (m_isRtl) prevChapter(true);   // RTL: → = 前チャプターの末尾へ
    else         nextChapter(false);  // LTR: → = 次チャプターの先頭へ
}

// ── Bookmarks ─────────────────────────────────────────────────────────────

void MainWindow::addBookmark() {
    if (!m_reader->isOpen() || m_currentChapter < 0) return;

    m_activePage->runJavaScript(
        "(function(){"
        "  var el = document.documentElement;"
        "  var vertical = (getComputedStyle(el).writingMode || '').indexOf('vertical') === 0 ||"
        "                 (document.body && (getComputedStyle(document.body).writingMode || '').indexOf('vertical') === 0);"
        "  var canH = el.scrollWidth  > el.clientWidth;"
        "  var canV = el.scrollHeight > el.clientHeight;"
        "  if ((vertical && canH) || (!canV && canH))"
        "    return Math.abs(el.scrollLeft) / Math.max(1, el.scrollWidth - el.clientWidth);"
        "  return el.scrollTop / Math.max(1, el.scrollHeight - el.clientHeight);"
        "})()",
        [this](const QVariant& v) {
            const double pos = v.toDouble();
            Bookmark bm;
            bm.id             = QUuid::createUuid().toString(QUuid::WithoutBraces);
            bm.epubPath       = m_reader->filePath();
            bm.chapterIndex   = m_currentChapter;
            bm.scrollPosition = pos;
            bm.label          = tr("%1  %2%")
                                    .arg(chapterLabel(m_currentChapter))
                                    .arg(qRound(pos * 100.0));
            bm.createdAt      = QDateTime::currentDateTime();
            m_bookmarkManager->addBookmark(bm);
            refreshBookmarkList();
            m_sideTabs->setCurrentWidget(m_bookmarkTree);
            statusBar()->showMessage(tr("しおりを追加しました"), 1500);
        });
}

void MainWindow::manageBookmarks() {
    if (!m_reader->isOpen()) {
        QMessageBox::information(this, tr("しおり"), tr("EPUBが開かれていません。"));
        return;
    }

    refreshBookmarkList();
    m_sideTabs->setVisible(true);
    m_sideTabs->setCurrentWidget(m_bookmarkTree);
    if (m_bookmarkTree->topLevelItemCount() == 0)
        QMessageBox::information(this, tr("しおり"), tr("しおりがありません。"));
}

// ── Search ────────────────────────────────────────────────────────────────

void MainWindow::showSearch() {
    if (!m_searchBar) return;
    m_searchBar->setVisible(true);
    const QString selected = m_activeView ? m_activeView->selectedText().trimmed() : QString();
    if (!selected.isEmpty() && !selected.contains('\n'))
        m_searchEdit->setText(selected);
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void MainWindow::runSearch() {
    if (!m_reader->isOpen()) {
        QMessageBox::information(this, tr("検索"), tr("EPUBが開かれていません。"));
        return;
    }

    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    m_searchQuery = query;
    m_searchResults = m_reader->search(query);
    m_searchIndex = m_searchResults.isEmpty() ? -1 : firstSearchResultAtOrAfterCurrentChapter();

    const bool hasResults = !m_searchResults.isEmpty();
    m_searchPrevAct->setEnabled(hasResults);
    m_searchNextAct->setEnabled(hasResults);
    m_searchListAct->setEnabled(hasResults);
    m_searchCountLabel->setText(hasResults
        ? tr("%1 / %2").arg(m_searchIndex + 1).arg(m_searchResults.size())
        : tr("見つかりません"));
}

void MainWindow::searchNext() {
    if (!m_searchBar->isVisible())
        showSearch();
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;
    const bool freshSearch = query != m_searchQuery || m_searchResults.isEmpty();
    if (freshSearch)
        runSearch();
    if (m_searchResults.isEmpty()) return;

    if (freshSearch || m_searchIndex < 0)
        m_searchIndex = firstSearchResultAtOrAfterCurrentChapter();
    else
        m_searchIndex = (m_searchIndex + 1) % m_searchResults.size();

    const auto& result = m_searchResults[m_searchIndex];
    m_searchCountLabel->setText(tr("%1 / %2").arg(m_searchIndex + 1).arg(m_searchResults.size()));
    jumpToSearchResult(result.chapterIndex, m_searchQuery, result.occurrenceIndex);
}

void MainWindow::searchPrevious() {
    if (!m_searchBar->isVisible())
        showSearch();
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;
    const bool freshSearch = query != m_searchQuery || m_searchResults.isEmpty();
    if (freshSearch)
        runSearch();
    if (m_searchResults.isEmpty()) return;

    if (freshSearch || m_searchIndex < 0)
        m_searchIndex = lastSearchResultAtOrBeforeCurrentChapter();
    else
        m_searchIndex = (m_searchIndex - 1 + m_searchResults.size()) % m_searchResults.size();

    const auto& result = m_searchResults[m_searchIndex];
    m_searchCountLabel->setText(tr("%1 / %2").arg(m_searchIndex + 1).arg(m_searchResults.size()));
    jumpToSearchResult(result.chapterIndex, m_searchQuery, result.occurrenceIndex);
}

void MainWindow::openSearchResults() {
    if (m_searchResults.isEmpty())
        runSearch();
    if (m_searchResults.isEmpty()) return;

    auto* dlg = new SearchDialog(m_reader, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (m_searchMenuAct)
        m_searchMenuAct->setEnabled(false);
    connect(dlg, &QObject::destroyed, this, [this] {
        if (m_searchMenuAct)
            m_searchMenuAct->setEnabled(true);
    });
    dlg->setResults(m_searchQuery, m_searchResults, m_searchIndex);
    connect(dlg, &SearchDialog::resultSelected,
            this, [this](int resultIndex, int chapterIndex, const QString&,
                         const QString& query, int occurrenceIndex) {
                m_searchIndex = resultIndex;
                m_searchQuery = query;
                if (m_searchCountLabel)
                    m_searchCountLabel->setText(tr("%1 / %2").arg(m_searchIndex + 1).arg(m_searchResults.size()));
                jumpToSearchResult(chapterIndex, query, occurrenceIndex);
            });
    dlg->show();
}

void MainWindow::closeSearchBar() {
    clearSearchHighlights();
    if (m_searchBar)
        m_searchBar->setVisible(false);
}

void MainWindow::clearSearchHighlights() {
    m_highlightedSearchQuery.clear();
    m_highlightedSearchChapter = -1;

    if (!m_activePage) return;
    m_activePage->runJavaScript(R"js(
        (function() {
            Array.from(document.querySelectorAll('mark[data-bibi-search-highlight="1"]'))
                .forEach(function(mark) {
                    var parent = mark.parentNode;
                    if (!parent) return;
                    while (mark.firstChild)
                        parent.insertBefore(mark.firstChild, mark);
                    parent.removeChild(mark);
                    parent.normalize();
                });
        })();
    )js");
}

int MainWindow::firstSearchResultAtOrAfterCurrentChapter() const {
    if (m_searchResults.isEmpty()) return -1;
    for (int i = 0; i < m_searchResults.size(); ++i) {
        if (m_searchResults[i].chapterIndex >= m_currentChapter)
            return i;
    }
    return 0;
}

int MainWindow::lastSearchResultAtOrBeforeCurrentChapter() const {
    if (m_searchResults.isEmpty()) return -1;
    for (int i = m_searchResults.size() - 1; i >= 0; --i) {
        if (m_searchResults[i].chapterIndex <= m_currentChapter)
            return i;
    }
    return m_searchResults.size() - 1;
}

void MainWindow::showSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("設定"));

    auto* layout = new QFormLayout(&dlg);

    auto* zoomSpin = new QSpinBox(&dlg);
    zoomSpin->setRange(25, 400);
    zoomSpin->setSingleStep(10);
    zoomSpin->setSuffix("%");
    zoomSpin->setValue(qRound(m_zoomFactor * 100.0));
    layout->addRow(tr("ズーム率"), zoomSpin);

    auto* preloadCombo = new QComboBox(&dlg);
    preloadCombo->addItem(tr("なし（省メモリ）"));
    preloadCombo->addItem(tr("次ページのみ（標準）"));
    preloadCombo->addItem(tr("前後ページ（高速）"));
    preloadCombo->setCurrentIndex(preloadModeToIndex(m_preloadMode));
    layout->addRow(tr("先読み"), preloadCombo);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    setZoomFactor(zoomSpin->value() / 100.0);
    setPreloadMode(preloadModeFromIndex(preloadCombo->currentIndex()));
}

// ── Helpers ───────────────────────────────────────────────────────────────

void MainWindow::setZoomFactor(double zoomFactor, bool saveSetting) {
    m_zoomFactor = qBound(0.25, zoomFactor, 4.0);
    m_zoomFactor = qRound(m_zoomFactor * 100.0) / 100.0;
    if (saveSetting) {
        QSettings s;
        s.setValue(kZoomFactorSettingKey, m_zoomFactor);
    }
    applyZoomToViews();
    updateStatus();
}

void MainWindow::setPreloadMode(PreloadMode mode, bool saveSetting) {
    if (m_preloadMode == mode) {
        if (saveSetting) {
            QSettings s;
            s.setValue(kPreloadModeSettingKey, preloadModeToIndex(mode));
        }
        return;
    }

    m_preloadMode = mode;
    if (saveSetting) {
        QSettings s;
        s.setValue(kPreloadModeSettingKey, preloadModeToIndex(mode));
    }

    invalidatePreloads();
    QTimer::singleShot(0, this, &MainWindow::refreshPreloads);
}

int MainWindow::preloadModeToIndex(PreloadMode mode) const {
    switch (mode) {
    case PreloadMode::None: return 0;
    case PreloadMode::NextOnly: return 1;
    case PreloadMode::NextAndPrevious: return 2;
    }
    return 2;
}

MainWindow::PreloadMode MainWindow::preloadModeFromIndex(int index) const {
    switch (index) {
    case 0: return PreloadMode::None;
    case 1: return PreloadMode::NextOnly;
    case 2: return PreloadMode::NextAndPrevious;
    default: return PreloadMode::NextAndPrevious;
    }
}

void MainWindow::applyCtrlWheelZoom(int delta) {
    if (delta > 0)       // ホイール上 = 拡大
        setZoomFactor(m_zoomFactor + 0.1);
    else if (delta < 0)  // ホイール下 = 縮小
        setZoomFactor(m_zoomFactor - 0.1);
}

void MainWindow::applyZoomToViews() {
    // ビューポートサイズを変えない CSS zoom で両 View をスケールする。
    // setZoomFactor は CSS ビューポートを縮小するため、100vw/100vh 指定の
    // EPUB コンテンツがリサイズされてズームが相殺される問題を回避する。
    // ズーム時は overflow を auto に強制してスクロールバーを出す
    // （EPUB が overflow:hidden を持つ場合でも拡大後にパンできるようにする）。
    const QString js = QString(R"js(
        (function() {
            var zoom = %1;
            var html = document.documentElement;
            var body = document.body || html;
            window._bibiCssZoom = zoom;
            var images = Array.from(document.images);
            var svgElements = Array.from(document.querySelectorAll('svg'));
            var svgImages = Array.from(document.querySelectorAll('svg image'));
            var imageLikeElements = images.concat(svgElements);
            if (!imageLikeElements.length)
                imageLikeElements = Array.from(document.querySelectorAll('object'));
            var scrollEl = document.scrollingElement || html;
            var prevCanH = scrollEl.scrollWidth > scrollEl.clientWidth;
            var prevMaxH = Math.max(1, scrollEl.scrollWidth - scrollEl.clientWidth);
            var prevScrollRatio = Math.abs(scrollEl.scrollLeft) / prevMaxH;

            function resetImageZoom() {
                body.style.width = '';
                body.style.height = '';
                body.style.minWidth = '';
                body.style.minHeight = '';
                imageLikeElements.forEach(function(el) {
                    if (el.dataset && el.dataset.bibiImageZoomOnly === '1') {
                        el.style.width = '';
                        el.style.height = '';
                        el.style.maxWidth = '';
                        el.style.maxHeight = '';
                        el.style.transform = '';
                        el.style.transformOrigin = '';
                        el.style.display = '';
                        el.style.willChange = '';
                        el.style.overflow = '';
                    }
                    var node = el.parentElement;
                    while (node && node !== body) {
                        if (node.dataset && node.dataset.bibiZoomContainer === '1') {
                            node.style.overflow = '';
                            node.style.maxWidth = '';
                            node.style.maxHeight = '';
                            node.style.width = '';
                            node.style.height = '';
                            node.style.minWidth = '';
                            node.style.minHeight = '';
                        }
                        node = node.parentElement;
                    }
                });
            }

            function isIgnorableElement(el) {
                var tag = (el.tagName || '').toUpperCase();
                return tag === 'SCRIPT' || tag === 'STYLE' || tag === 'LINK' ||
                       tag === 'META' || tag === 'TITLE';
            }

            function isImageOnlyPage() {
                if (!imageLikeElements.length && !svgImages.length) return false;
                if ((body.innerText || '').trim().length > 0) return false;

                var meaningful = Array.from(body.children).filter(function(el) {
                    return !isIgnorableElement(el);
                });
                if (!meaningful.length) return false;

                return meaningful.every(function(el) {
                    var tag = (el.tagName || '').toUpperCase();
                    if (tag === 'IMG' || tag === 'SVG' || tag === 'OBJECT' || tag === 'PICTURE') return true;
                    return el.querySelector && el.querySelector('img, svg, object, picture, svg image');
                });
            }

            function numericAttr(el, name) {
                var value = el.getAttribute && el.getAttribute(name);
                if (!value) return 0;
                var n = parseFloat(String(value).replace(/px$/i, ''));
                return isFinite(n) ? n : 0;
            }

            function viewBoxSize(el) {
                var box = el.viewBox && el.viewBox.baseVal;
                if (box && box.width > 0 && box.height > 0)
                    return { width: box.width, height: box.height };
                var attr = el.getAttribute && el.getAttribute('viewBox');
                if (!attr) return null;
                var parts = attr.trim().split(/[\s,]+/).map(parseFloat);
                if (parts.length === 4 && parts[2] > 0 && parts[3] > 0)
                    return { width: parts[2], height: parts[3] };
                return null;
            }

            function measuredBaseSize(el) {
                var rect = el.getBoundingClientRect();
                var attrWidth = numericAttr(el, 'width');
                var attrHeight = numericAttr(el, 'height');
                var box = viewBoxSize(el);
                var width = 0;
                var height = 0;

                if (el.naturalWidth > 0 && el.naturalHeight > 0) {
                    width = el.naturalWidth;
                    height = el.naturalHeight;
                } else if (box) {
                    width = box.width;
                    height = box.height;
                } else if (attrWidth > 0 && attrHeight > 0) {
                    width = attrWidth;
                    height = attrHeight;
                } else if (rect.width > 1 && rect.height > 1) {
                    width = rect.width;
                    height = rect.height;
                }

                if (width > 0 && height <= 0 && rect.width > 1 && rect.height > 1)
                    height = width * rect.height / rect.width;
                if (height > 0 && width <= 0 && rect.width > 1 && rect.height > 1)
                    width = height * rect.width / rect.height;

                if (!isFinite(width) || !isFinite(height) || width <= 1 || height <= 1)
                    return null;
                return { width: width, height: height };
            }

            function useTransformZoom(el) {
                return false;
            }

            function rememberBaseSize(el) {
                if (!el.dataset) return false;
                if (parseFloat(el.dataset.bibiBaseWidth) > 1 &&
                    parseFloat(el.dataset.bibiBaseHeight) > 1)
                    return true;

                var size = measuredBaseSize(el);
                if (!size) return false;
                el.dataset.bibiBaseWidth = String(size.width);
                el.dataset.bibiBaseHeight = String(size.height);
                return true;
            }

            function applyImageZoom() {
                body.style.zoom = '';
                body.style.width = 'max-content';
                body.style.height = 'auto';
                body.style.minWidth = '100%';
                body.style.minHeight = '100%';

                var allReady = true;
                imageLikeElements.forEach(function(el) {
                    if (!el.dataset) return;
                    if (!rememberBaseSize(el)) {
                        allReady = false;
                        return;
                    }
                    var baseWidth = parseFloat(el.dataset.bibiBaseWidth) || el.naturalWidth || 1;
                    var baseHeight = parseFloat(el.dataset.bibiBaseHeight) || el.naturalHeight || 1;
                    var scaledWidth = baseWidth * zoom;
                    var scaledHeight = baseHeight * zoom;

                    el.dataset.bibiImageZoomOnly = '1';
                    el.style.maxWidth = 'none';
                    el.style.maxHeight = 'none';
                    el.style.width = scaledWidth + 'px';
                    el.style.height = scaledHeight + 'px';
                    el.style.display = 'block';
                    el.style.overflow = 'visible';
                    if (useTransformZoom(el)) {
                        el.style.transformOrigin = '0 0';
                        el.style.transform = 'scale(' + zoom + ')';
                        el.style.willChange = 'transform';
                    } else {
                        el.style.width = scaledWidth + 'px';
                        el.style.height = scaledHeight + 'px';
                        el.style.transform = '';
                        el.style.transformOrigin = '';
                        el.style.willChange = '';
                    }

                    var immediateParent = el.parentElement;
                    if (!immediateParent || immediateParent === body) {
                        body.style.width = Math.max(body.scrollWidth, scaledWidth) + 'px';
                        body.style.height = Math.max(body.scrollHeight, scaledHeight) + 'px';
                    } else {
                        immediateParent.dataset.bibiZoomContainer = '1';
                        immediateParent.style.overflow = 'visible';
                        immediateParent.style.maxWidth = 'none';
                        immediateParent.style.maxHeight = 'none';
                        immediateParent.style.width = scaledWidth + 'px';
                        immediateParent.style.height = scaledHeight + 'px';
                        immediateParent.style.minWidth = scaledWidth + 'px';
                        immediateParent.style.minHeight = scaledHeight + 'px';
                    }

                    var node = immediateParent ? immediateParent.parentElement : null;
                    while (node && node !== body) {
                        node.dataset.bibiZoomContainer = '1';
                        node.style.overflow = 'visible';
                        node.style.maxWidth = 'none';
                        node.style.maxHeight = 'none';
                        node.style.width = 'max-content';
                        node.style.height = 'auto';
                        node = node.parentElement;
                    }
                });
                return allReady;
            }

            var imageOnly = isImageOnlyPage();
            resetImageZoom();
            if (zoom === 1) {
                body.style.zoom = '';
                html.style.overflow = '';
                body.style.overflow = '';
            } else {
                html.style.overflow = 'auto';
                body.style.overflow = 'auto';
                if (imageOnly) {
                    var ready = applyImageZoom();
                    if (!ready) {
                        requestAnimationFrame(function() {
                            applyImageZoom();
                            if (window._bibiAlignImageOnlyInitialRight)
                                window._bibiAlignImageOnlyInitialRight();
                            if (window._bibiReportReadingPosition)
                                window._bibiReportReadingPosition();
                        });
                    }
                } else {
                    body.style.zoom = zoom;
                }
            }
            if (window._bibiApplyReadingInsets) window._bibiApplyReadingInsets();
            requestAnimationFrame(function() {
                if (!imageOnly) return;
                var el = document.scrollingElement || document.documentElement;
                if (el.scrollWidth <= el.clientWidth) return;
                var maxH = Math.max(0, el.scrollWidth - el.clientWidth);
                if (prevCanH)
                    el.scrollLeft = prevScrollRatio * maxH;
                else
                    el.scrollLeft = maxH;
                if (window._bibiReportReadingPosition) window._bibiReportReadingPosition();
            });
        })();
    )js").arg(m_zoomFactor);

    // Chromium の Ctrl+Wheel 内部ズームが setZoomFactor を変更した場合にリセットする。
    // setZoomFactor はビューポートサイズを変更するため、EPUB の vw/vh 指定画像の
    // 表示サイズが変わり CSS zoom と打ち消し合う。CSS zoom のみを使うので常に 1.0 に保つ。
    if (m_viewA && qAbs(m_viewA->zoomFactor() - 1.0) > 1e-6) m_viewA->setZoomFactor(1.0);
    if (m_viewB && qAbs(m_viewB->zoomFactor() - 1.0) > 1e-6) m_viewB->setZoomFactor(1.0);
    auto apply = [&js](QWebEngineView* view) {
        if (view && view->page()) view->page()->runJavaScript(js);
    };
    apply(m_activeView);
    apply(m_nextBuffer.view);
    apply(m_previousBuffer.view);
}

void MainWindow::updateStatus() {
    if (!m_reader->isOpen() || m_currentChapter < 0) {
        m_statusLabel->clear();
        m_progressBar->setValue(0);
        return;
    }

    const int chapterCount = qMax(1, m_reader->chapterCount());
    const int chapterProgress = qRound(m_currentScrollPosition * 100.0);
    const double overall = (m_currentChapter + m_currentScrollPosition) / chapterCount;
    const QString chapterTitle = chapterLabel(m_currentChapter);
    m_statusLabel->setText(
        tr("章 %1/%2 ・ %3 ・ 章内 %4% ・ ズーム %5%")
            .arg(m_currentChapter + 1)
            .arg(m_reader->chapterCount())
            .arg(chapterTitle)
            .arg(chapterProgress)
            .arg(qRound(m_zoomFactor * 100)));
    m_statusLabel->setToolTip(chapterTitle);
    m_progressBar->setValue(qBound(0, qRound(overall * 1000.0), 1000));
}

void MainWindow::updatePageBackgroundColor() {
    const QColor color = palette().color(QPalette::Window);
    for (QWebEngineView* view : {m_viewA, m_viewB, m_viewC}) {
        if (!view || !view->page()) continue;
        view->page()->setBackgroundColor(m_loadedViews.contains(view) ? Qt::white : color);
    }
    if (m_viewContainer)
        m_viewContainer->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name()));
}

void MainWindow::setViewLoaded(QWebEngineView* view, bool loaded) {
    if (!view) return;
    if (loaded)
        m_loadedViews.insert(view);
    else
        m_loadedViews.remove(view);
    updatePageBackgroundColor();
}

void MainWindow::updateNavigationActions() {
    bool has     = m_reader->isOpen();
    bool hasPrev = has && m_currentChapter > 0;
    bool hasNext = has && m_currentChapter >= 0
                       && m_currentChapter < m_reader->chapterCount() - 1;

    // RTL: ← = 次へ、→ = 前へ
    m_coverAct->setEnabled(has && m_currentChapter > 0);
    m_leftAct->setEnabled(m_isRtl ? hasNext : hasPrev);
    m_rightAct->setEnabled(m_isRtl ? hasPrev : hasNext);
    m_endAct->setEnabled(has && m_currentChapter >= 0
                             && m_currentChapter < m_reader->chapterCount() - 1);
    m_bookmarkAct->setEnabled(has && m_currentChapter >= 0);
}

void MainWindow::updateWindowTitle() {
    if (m_reader->isOpen() && !m_reader->metadata().title.isEmpty())
        setWindowTitle(tr("%1 — Bibi Qt Reader").arg(m_reader->metadata().title));
    else
        setWindowTitle(tr("Bibi Qt Reader"));
}
