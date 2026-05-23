#include "epuburlscheme.h"
#include "epubreader.h"
#include <QWebEngineUrlRequestJob>
#include <QBuffer>
#include <QMimeDatabase>
#include <QUrl>

EpubUrlScheme::EpubUrlScheme(QObject* parent)
    : QWebEngineUrlSchemeHandler(parent) {}

void EpubUrlScheme::setReader(EpubReader* reader) {
    m_reader = reader;
}

static void replyEmpty(QWebEngineUrlRequestJob* request) {
    auto* buf = new QBuffer;
    buf->open(QIODevice::ReadOnly);
    QObject::connect(request, &QObject::destroyed, buf, &QObject::deleteLater);
    request->reply("text/plain", buf);
}

void EpubUrlScheme::requestStarted(QWebEngineUrlRequestJob* request) {
    QString path = request->requestUrl().path();
    if (path.startsWith('/')) path = path.mid(1);

    if (!m_reader || !m_reader->isOpen()) {
        request->fail(QWebEngineUrlRequestJob::RequestFailed);
        return;
    }

    QByteArray data = m_reader->fileData(path);
    if (data.isEmpty()) {
        request->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    QString mime = m_reader->mimeTypeForPath(path);
    if (mime.isEmpty()) {
        static const QMimeDatabase db;
        mime = db.mimeTypeForData(data).name();
    }

    auto* buf = new QBuffer;
    buf->setData(data);
    buf->open(QIODevice::ReadOnly);
    QObject::connect(request, &QObject::destroyed, buf, &QObject::deleteLater);
    request->reply(mime.toUtf8(), buf);
}
