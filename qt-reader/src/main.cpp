#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QWebEngineUrlScheme>
#include "mainwindow.h"

// Must be called BEFORE QApplication is constructed.
// EPUB content is untrusted: the scheme is deliberately NOT Local/LocalAccessAllowed,
// which would give book scripts the same access to file: URLs as a local file.
static void registerEpubScheme() {
    QWebEngineUrlScheme scheme("epub");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(
        QWebEngineUrlScheme::SecureScheme      |
        QWebEngineUrlScheme::CorsEnabled       |
        QWebEngineUrlScheme::FetchApiAllowed
    );
    QWebEngineUrlScheme::registerScheme(scheme);
}

#ifdef Q_OS_LINUX
// The AppImage bundles Japanese fonts for systems that have none. Point
// fontconfig, used by both Qt and QtWebEngine's Chromium, at a config that adds
// them to the system fonts. Must run before anything looks up a font.
static void useBundledFonts() {
    if (qEnvironmentVariableIsSet("FONTCONFIG_FILE")) return;
    const QString exe = QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget();
    const QString conf = QDir::cleanPath(QFileInfo(exe).absolutePath() +
                                         QStringLiteral("/../share/BibiQtReader/fonts/fonts.conf"));
    if (QFileInfo::exists(conf))
        qputenv("FONTCONFIG_FILE", QFile::encodeName(conf));
}
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_LINUX
    useBundledFonts();
#endif
    registerEpubScheme();

    QApplication app(argc, argv);
    app.setApplicationName("BibiQtReader");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Bibi");
    app.setOrganizationDomain("bibi.epub.link");
    app.setWindowIcon(QIcon(":/icons/app.ico"));

    MainWindow window;
    window.show();

    QStringList args = app.arguments();
    if (args.size() > 1)
        window.openEpub(args.at(1));

    return app.exec();
}
