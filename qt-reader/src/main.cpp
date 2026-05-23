#include <QApplication>
#include <QWebEngineUrlScheme>
#include "mainwindow.h"

// Must be called BEFORE QApplication is constructed.
static void registerEpubScheme() {
    QWebEngineUrlScheme scheme("epub");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(
        QWebEngineUrlScheme::SecureScheme      |
        QWebEngineUrlScheme::LocalScheme       |
        QWebEngineUrlScheme::LocalAccessAllowed|
        QWebEngineUrlScheme::CorsEnabled       |
        QWebEngineUrlScheme::FetchApiAllowed
    );
    QWebEngineUrlScheme::registerScheme(scheme);
}

int main(int argc, char* argv[]) {
    registerEpubScheme();

    QApplication app(argc, argv);
    app.setApplicationName("BibiQtReader");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Bibi");
    app.setOrganizationDomain("bibi.epub.link");

    MainWindow window;
    window.show();

    QStringList args = app.arguments();
    if (args.size() > 1)
        window.openEpub(args.at(1));

    return app.exec();
}
