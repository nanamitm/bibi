#include "mainwindow.h"
#include "epuburlscheme.h"
#include "searchdialog.h"
#include <QtConcurrent/QtConcurrent>
#include <QEvent>
#include <QResizeEvent>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QTreeWidget>
#include <QSplitter>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QCloseEvent>
#include <QApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QUrl>
#include <QWebEngineSettings>

// ── Constructor / Destructor ──────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_reader(new EpubReader(this))
    , m_bookmarkManager(new BookmarkManager(this))
    , m_urlScheme(new EpubUrlScheme(this))
{
    setupUi();
    setupMenuBar();
    setupToolBar();

    setWindowTitle(tr("Bibi Qt Reader"));
    resize(1200, 800);

    // Restore window geometry
    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());
    restoreState(s.value("windowState").toByteArray());
    if (s.contains("splitter"))
        m_splitter->restoreState(s.value("splitter").toByteArray());
}

MainWindow::~MainWindow() = default;


void MainWindow::closeEvent(QCloseEvent* event) {
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

    m_tocTree = new QTreeWidget(m_splitter);
    m_tocTree->setHeaderLabel(tr("目次"));
    m_tocTree->setMinimumWidth(180);
    m_tocTree->setMaximumWidth(360);
    m_tocTree->setAnimated(true);

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
    // m_standbyView : バックグラウンドで次ページを読み込む
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

    m_viewA->show();
    m_viewB->show();
    m_viewA->raise();                  // A が初期アクティブ
    m_activeView  = m_viewA;  m_activePage  = m_pageA;
    m_standbyView = m_viewB;  m_standbyPage = m_pageB;

    // nav シグナル：アクティブ View からのみチャプター移動する
    for (EpubWebPage* pg : {m_pageA, m_pageB}) {
        connect(pg, &EpubWebPage::navLeft, this, [this, pg]() {
            if (m_activePage == pg) onNavLeft();
        }, Qt::QueuedConnection);
        connect(pg, &EpubWebPage::navRight, this, [this, pg]() {
            if (m_activePage == pg) onNavRight();
        }, Qt::QueuedConnection);
        // pageReady：スタンバイ View の読み込みが完了したらスワップ検査を開始
        connect(pg, &EpubWebPage::pageReady, this, [this, pg]() {
            if (m_standbyPage == pg) {
                applyZoomToViews();
                scheduleSwap();
            }
        }, Qt::QueuedConnection);
    }

    // loadFinished：どちらの View が発火しても同じ JS を注入する
    for (QWebEngineView* view : {m_viewA, m_viewB}) {
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

    m_splitter->addWidget(m_tocTree);
    m_splitter->addWidget(m_viewContainer);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({220, 980});

    setCentralWidget(m_splitter);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);

    connect(m_tocTree, &QTreeWidget::itemClicked,
            this,      &MainWindow::onTocItemClicked);

    m_swapCheckTimer = new QTimer(this);
    m_swapCheckTimer->setInterval(16); // ~1フレーム間隔でポーリング
    connect(m_swapCheckTimer, &QTimer::timeout, this, &MainWindow::trySwap);

    qApp->installEventFilter(this);
}

// ── ダブルバッファ: loadFinished 処理 ────────────────────────────────────────

void MainWindow::handleLoadFinished(QWebEngineView* view, bool ok) {
    if (!ok) return;

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

            var textEl = findTextElement();
            var vertical = isVerticalWriting(html) ||
                           isVerticalWriting(body) ||
                           isVerticalWriting(textEl);

            if (vertical && !isImageOnlyPage()) {
                body.style.boxSizing = 'border-box';
                body.style.paddingTop = '1em';
                body.style.paddingBottom = '1em';
                html.style.scrollPaddingTop = '1em';
                html.style.scrollPaddingBottom = '1em';
            } else {
                body.style.paddingTop = '';
                body.style.paddingBottom = '';
                html.style.scrollPaddingTop = '';
                html.style.scrollPaddingBottom = '';
            }
        })();
    )js");

    // ① 全画像デコード完了後にスクロール位置を確定し、epub-page-ready を通知する
    const int isRtl       = m_isRtl      ? 1 : 0;
    const int scrollToEnd = m_scrollToEnd ? 1 : 0;
    m_scrollToEnd = false;
    view->page()->runJavaScript(QString(
        "(function(){"
        "  var imgs = Array.from(document.querySelectorAll('img'));"
        "  var ps = imgs.map(function(img){"
        "    return img.decode ? img.decode().catch(function(){}) : Promise.resolve();"
        "  });"
        "  Promise.all(ps).then(function(){"
        "    requestAnimationFrame(function(){"
        "      if (%2) {"
        "        var el = document.documentElement;"
        "        if (el.scrollWidth > el.clientWidth)"
        "          window.scrollBy(%1 ? -999999 : 999999, 0);"
        "        else if (el.scrollHeight > el.clientHeight)"
        "          window.scrollBy(0, 999999);"
        "      }"
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
                window.addEventListener('wheel', function(e) {
                    if (e.ctrlKey) { e.preventDefault(); return; }  // Ctrl+Wheel は C++ イベントフィルターで処理
                    if (e.deltaY === 0) return;
                    e.preventDefault();
                    var el    = document.documentElement;
                    var canH  = el.scrollWidth  > el.clientWidth;
                    var canV  = el.scrollHeight > el.clientHeight;
                    var isRtl = SIGN < 0;
                    if (canH && canV) {
                        // 上下左右にスクロールバーあり＝ズームによる溢れ。
                        // 固定ページ（スクロールなし）と同様に即チャプター移動する。
                        if (!_busy) {
                            _busy = true;
                            console.log(SIGN * e.deltaY > 0 ? 'epub-nav:right' : 'epub-nav:left');
                            setTimeout(function(){ _busy=false; }, 1000);
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

                window.addEventListener('keydown', function(e) {
                    var left  = e.key === 'ArrowLeft';
                    var right = e.key === 'ArrowRight';
                    var up    = e.key === 'ArrowUp';
                    var down  = e.key === 'ArrowDown';
                    if (!left && !right && !up && !down) return;
                    e.preventDefault();
                    e.stopPropagation();
                    if (_busy) return;

                    var el   = document.documentElement;
                    var maxH = Math.max(0, el.scrollWidth  - el.clientWidth);
                    var maxV = Math.max(0, el.scrollHeight - el.clientHeight);
                    var canH = maxH > 2;
                    var canV = maxV > 2;

                    if (canH && canV) {
                        // 上下左右にスクロールバーあり＝ズームによる溢れ。固定ページ扱い。
                        if (left)       navLeft();
                        else if (right) navRight();
                        else if (up)    navUp();
                        else            navDown();
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

    // ④' ドラッグ＝パン：canH && canV（ズームで両スクロールバーあり）のときマウスドラッグでスクロール
    view->page()->runJavaScript(R"js(
        (function() {
            if (window._bibiDragInstalled) return;
            window._bibiDragInstalled = true;

            var drag = { active: false, x: 0, y: 0, sl: 0, st: 0 };

            function bothScrollbars() {
                var el = document.documentElement;
                return el.scrollWidth > el.clientWidth && el.scrollHeight > el.clientHeight;
            }

            document.addEventListener('mousedown', function(e) {
                if (e.button !== 0 || !bothScrollbars()) return;
                // リンク・フォーム要素は通常動作を優先
                var t = e.target;
                while (t && t !== document.body) {
                    var tn = (t.tagName || '').toUpperCase();
                    if (tn === 'A' || tn === 'INPUT' || tn === 'TEXTAREA' ||
                        tn === 'SELECT' || tn === 'BUTTON') return;
                    t = t.parentNode;
                }
                var el = document.documentElement;
                drag.active = true;
                drag.x  = e.clientX;  drag.y  = e.clientY;
                drag.sl = el.scrollLeft; drag.st = el.scrollTop;
                el.style.cursor = 'grabbing';
                el.style.userSelect = 'none';
                e.preventDefault();
            }, true);

            document.addEventListener('mousemove', function(e) {
                if (drag.active) {
                    var el = document.documentElement;
                    el.scrollLeft = drag.sl + (drag.x - e.clientX);
                    el.scrollTop  = drag.st + (drag.y - e.clientY);
                } else {
                    document.documentElement.style.cursor = bothScrollbars() ? 'grab' : '';
                }
            }, true);

            function endDrag() {
                if (!drag.active) return;
                drag.active = false;
                var el = document.documentElement;
                el.style.cursor = bothScrollbars() ? 'grab' : '';
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
    // スタンバイビューを grab() して実際に描画されているか検査する。
    // img.decode() はCPUメモリ展開のみ保証するため、GPUテクスチャ転送が
    // 完了したかどうかはピクセルを実際に読んで確認するしかない。
    QImage img = m_standbyView->grab().toImage();

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
    m_standbyView->raise();   // スタンバイを前面に
    m_standbyView->setFocus();
    std::swap(m_activeView,  m_standbyView);
    std::swap(m_activePage,  m_standbyPage);

    if (m_postSwapAction) {
        auto action = std::move(m_postSwapAction);
        m_postSwapAction = nullptr;
        QTimer::singleShot(0, this, [action]{ action(); });
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            QObject* p = obj;
            while (p) {
                if (p == m_viewA || p == m_viewB) {
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

    auto* quitAct = fileMenu->addAction(tr("終了(&Q)"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    // ── View ──────────────────────────────────────────────────────────────
    auto* viewMenu = menuBar()->addMenu(tr("表示(&V)"));

    auto* zoomInAct = viewMenu->addAction(tr("拡大"));
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, this, [this] {
        m_zoomFactor = qMin(m_zoomFactor + 0.1, 4.0);
        applyZoomToViews(); updateStatus();
    });

    auto* zoomOutAct = viewMenu->addAction(tr("縮小"));
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, this, [this] {
        m_zoomFactor = qMax(m_zoomFactor - 0.1, 0.25);
        applyZoomToViews(); updateStatus();
    });

    auto* resetZoomAct = viewMenu->addAction(tr("標準サイズ"));
    resetZoomAct->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(resetZoomAct, &QAction::triggered, this, [this] {
        m_zoomFactor = 1.0;
        applyZoomToViews(); updateStatus();
    });

    viewMenu->addSeparator();

    auto* toggleTocAct = viewMenu->addAction(tr("目次パネル(&T)"));
    toggleTocAct->setCheckable(true);
    toggleTocAct->setChecked(true);
    connect(toggleTocAct, &QAction::toggled, m_tocTree, &QWidget::setVisible);

    // ── Bookmarks ─────────────────────────────────────────────────────────
    auto* bmMenu = menuBar()->addMenu(tr("しおり(&B)"));

    auto* addBmAct = bmMenu->addAction(tr("しおりを追加(&A)"));
    addBmAct->setShortcut(Qt::CTRL | Qt::Key_D);
    connect(addBmAct, &QAction::triggered, this, &MainWindow::addBookmark);

    auto* manageBmAct = bmMenu->addAction(tr("しおり一覧(&M)…"));
    connect(manageBmAct, &QAction::triggered, this, &MainWindow::manageBookmarks);

    // ── Search ────────────────────────────────────────────────────────────
    auto* searchMenu = menuBar()->addMenu(tr("検索(&S)"));

    auto* searchAct = searchMenu->addAction(tr("全文検索(&F)…"));
    searchAct->setShortcut(Qt::CTRL | Qt::Key_F);
    connect(searchAct, &QAction::triggered, this, &MainWindow::showSearch);
}

void MainWindow::setupToolBar() {
    auto* tb = addToolBar(tr("ナビゲーション"));
    tb->setObjectName("navToolBar");
    tb->setMovable(false);

    // キーボード操作はJSが担うためショートカットは設定しない
    m_leftAct = tb->addAction(tr("◀"));
    m_leftAct->setEnabled(false);
    connect(m_leftAct, &QAction::triggered, this, &MainWindow::onLeftKey);

    m_rightAct = tb->addAction(tr("▶"));
    m_rightAct->setEnabled(false);
    connect(m_rightAct, &QAction::triggered, this, &MainWindow::onRightKey);

    tb->addSeparator();

    m_bookmarkAct = tb->addAction(tr("★ しおり"));
    m_bookmarkAct->setEnabled(false);
    connect(m_bookmarkAct, &QAction::triggered, this, &MainWindow::addBookmark);

    tb->addSeparator();

    auto* searchTbAct = tb->addAction(tr("検索"));
    connect(searchTbAct, &QAction::triggered, this, &MainWindow::showSearch);
}

// ── EPUB Loading ──────────────────────────────────────────────────────────

void MainWindow::openEpub(const QString& filePath) {
    m_prefetchFuture.waitForFinished();
    if (!m_reader->open(filePath)) {
        QMessageBox::critical(this, tr("エラー"),
            tr("EPUBファイルを開けませんでした:\n%1\n\n原因: %2")
            .arg(filePath, m_reader->lastError()));
        return;
    }

    m_urlScheme->setReader(m_reader);
    m_currentChapter = -1;
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

    updateWindowTitle();
    updateNavigationActions();

    if (m_reader->chapterCount() > 0) goToChapter(0);
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

// ── Navigation ────────────────────────────────────────────────────────────

void MainWindow::goToChapter(int index) {
    if (!m_reader->isOpen()) return;
    if (index < 0 || index >= m_reader->chapterCount()) return;

    m_currentChapter = index;
    EpubChapter ch   = m_reader->chapter(index);

    // スタンバイ View に新 URL を読み込ませる。
    // アクティブ View は完成済みの旧ページを表示し続けるためフラッシュしない。
    m_standbyView->setUrl(QUrl("epub:///" + ch.href));
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
    EpubChapter ch   = m_reader->chapter(idx);
    QUrl url("epub:///" + ch.href);
    if (!frag.isEmpty()) url.setFragment(frag);
    m_standbyView->setUrl(url);

    updateNavigationActions();
    updateStatus();
}

int MainWindow::hrefToChapterIndex(const QString& path) const {
    for (int i = 0; i < m_reader->chapterCount(); ++i) {
        if (m_reader->chapter(i).href == path)
            return i;
    }
    return -1;
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

    bool ok;
    QString label = QInputDialog::getText(
        this, tr("しおりを追加"),
        tr("しおりの名前:"), QLineEdit::Normal,
        tr("Chapter %1").arg(m_currentChapter + 1), &ok);
    if (!ok || label.trimmed().isEmpty()) return;

    m_activePage->runJavaScript(
        "(function(){"
        "  var el = document.documentElement;"
        "  var canH = el.scrollWidth > el.clientWidth;"
        "  if (canH) return Math.abs(el.scrollLeft) / Math.max(1, el.scrollWidth  - el.clientWidth);"
        "  return el.scrollTop / Math.max(1, el.scrollHeight - el.clientHeight);"
        "})()",
        [this, label](const QVariant& v) {
            Bookmark bm;
            bm.epubPath       = m_reader->filePath();
            bm.chapterIndex   = m_currentChapter;
            bm.scrollPosition = v.toDouble();
            bm.label          = label.trimmed();
            bm.createdAt      = QDateTime::currentDateTime();
            m_bookmarkManager->addBookmark(bm);
        });
}

void MainWindow::manageBookmarks() {
    if (!m_reader->isOpen()) {
        QMessageBox::information(this, tr("しおり"), tr("EPUBが開かれていません。"));
        return;
    }

    QList<Bookmark> bms = m_bookmarkManager->bookmarksForEpub(m_reader->filePath());
    if (bms.isEmpty()) {
        QMessageBox::information(this, tr("しおり"), tr("しおりがありません。"));
        return;
    }

    QStringList labels;
    for (const auto& bm : bms)
        labels << QString("%1  (チャプター %2)").arg(bm.label).arg(bm.chapterIndex + 1);

    bool ok;
    QString sel = QInputDialog::getItem(
        this, tr("しおり一覧"), tr("移動するしおりを選択:"), labels, 0, false, &ok);
    if (!ok) return;

    int idx = labels.indexOf(sel);
    if (idx < 0 || idx >= bms.size()) return;

    const Bookmark& bm = bms[idx];
    goToChapter(bm.chapterIndex);

    double pos = bm.scrollPosition;
    m_postSwapAction = [this, pos]() {
        m_activePage->runJavaScript(QString(
            "(function(){"
            "  var el = document.documentElement;"
            "  var canH = el.scrollWidth > el.clientWidth;"
            "  if (canH) el.scrollLeft = %1 * Math.max(0, el.scrollWidth  - el.clientWidth);"
            "  else      el.scrollTop  = %1 * Math.max(0, el.scrollHeight - el.clientHeight);"
            "})()").arg(pos));
    };
}

// ── Search ────────────────────────────────────────────────────────────────

void MainWindow::showSearch() {
    if (!m_reader->isOpen()) {
        QMessageBox::information(this, tr("検索"), tr("EPUBが開かれていません。"));
        return;
    }

    auto* dlg = new SearchDialog(m_reader, this);
    connect(dlg, &SearchDialog::resultSelected,
            this, [this](int chapterIndex, const QString&) {
                goToChapter(chapterIndex);
            });
    dlg->exec();
    dlg->deleteLater();
}

// ── Helpers ───────────────────────────────────────────────────────────────

void MainWindow::applyCtrlWheelZoom(int delta) {
    if (delta > 0)       // ホイール上 = 拡大
        m_zoomFactor = qMin(m_zoomFactor + 0.1, 4.0);
    else if (delta < 0)  // ホイール下 = 縮小
        m_zoomFactor = qMax(m_zoomFactor - 0.1, 0.25);
    m_zoomFactor = qRound(m_zoomFactor * 100.0) / 100.0;
    applyZoomToViews();
    updateStatus();
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
            var images = Array.from(document.images);

            function resetImageZoom() {
                body.style.width = '';
                body.style.height = '';
                body.style.minWidth = '';
                body.style.minHeight = '';
                images.forEach(function(img) {
                    if (img.dataset.bibiImageZoomOnly === '1') {
                        img.style.width = '';
                        img.style.height = '';
                        img.style.maxWidth = '';
                        img.style.maxHeight = '';
                    }
                    var node = img.parentElement;
                    while (node && node !== body) {
                        if (node.dataset && node.dataset.bibiZoomContainer === '1') {
                            node.style.overflow = '';
                            node.style.maxWidth = '';
                            node.style.maxHeight = '';
                            node.style.width = '';
                            node.style.height = '';
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
                if (!images.length) return false;
                if ((body.innerText || '').trim().length > 0) return false;

                var meaningful = Array.from(body.children).filter(function(el) {
                    return !isIgnorableElement(el);
                });
                if (!meaningful.length) return false;

                return meaningful.every(function(el) {
                    if ((el.tagName || '').toUpperCase() === 'IMG') return true;
                    return el.querySelector && el.querySelector('img');
                });
            }

            function rememberBaseSize(img) {
                if (img.dataset.bibiBaseWidth && img.dataset.bibiBaseHeight) return;

                var rect = img.getBoundingClientRect();
                img.dataset.bibiBaseWidth = String(rect.width || img.naturalWidth || img.width);
                img.dataset.bibiBaseHeight = String(rect.height || img.naturalHeight || img.height);
            }

            function applyImageZoom() {
                body.style.zoom = '';
                body.style.width = 'max-content';
                body.style.height = 'auto';
                body.style.minWidth = '100%';
                body.style.minHeight = '100%';

                images.forEach(function(img) {
                    rememberBaseSize(img);
                    img.dataset.bibiImageZoomOnly = '1';
                    img.style.maxWidth = 'none';
                    img.style.maxHeight = 'none';
                    img.style.width = ((parseFloat(img.dataset.bibiBaseWidth) || img.naturalWidth || img.width) * zoom) + 'px';
                    img.style.height = ((parseFloat(img.dataset.bibiBaseHeight) || img.naturalHeight || img.height) * zoom) + 'px';

                    var node = img.parentElement;
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
            }

            resetImageZoom();
            if (zoom === 1) {
                body.style.zoom = '';
                html.style.overflow = '';
                body.style.overflow = '';
            } else {
                html.style.overflow = 'auto';
                body.style.overflow = 'auto';
                if (isImageOnlyPage()) {
                    applyImageZoom();
                } else {
                    body.style.zoom = zoom;
                }
            }
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
    apply(m_standbyView);
}

void MainWindow::updateStatus() {
    if (!m_reader->isOpen() || m_currentChapter < 0) return;
    m_statusLabel->setText(
        tr("チャプター: %1 / %2   ズーム: %3%")
            .arg(m_currentChapter + 1)
            .arg(m_reader->chapterCount())
            .arg(qRound(m_zoomFactor * 100)));
}

void MainWindow::updateNavigationActions() {
    bool has     = m_reader->isOpen();
    bool hasPrev = has && m_currentChapter > 0;
    bool hasNext = has && m_currentChapter >= 0
                       && m_currentChapter < m_reader->chapterCount() - 1;

    // RTL: ← = 次へ、→ = 前へ
    m_leftAct->setEnabled(m_isRtl ? hasNext : hasPrev);
    m_rightAct->setEnabled(m_isRtl ? hasPrev : hasNext);
    m_bookmarkAct->setEnabled(has && m_currentChapter >= 0);
}

void MainWindow::updateWindowTitle() {
    if (m_reader->isOpen() && !m_reader->metadata().title.isEmpty())
        setWindowTitle(tr("%1 — Bibi Qt Reader").arg(m_reader->metadata().title));
    else
        setWindowTitle(tr("Bibi Qt Reader"));
}
