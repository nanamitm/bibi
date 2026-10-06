#include "epubrequestinterceptor.h"
#include <QUrl>

// May run off the UI thread depending on the Qt version, so keep it stateless.
void EpubRequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info) {
    const QString scheme = info.requestUrl().scheme();
    if (scheme == QLatin1String("epub") ||
        scheme == QLatin1String("data") ||
        scheme == QLatin1String("blob"))
        return;
    info.block(true);
}
