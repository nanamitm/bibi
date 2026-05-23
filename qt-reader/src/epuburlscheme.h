#pragma once
#include <QWebEngineUrlSchemeHandler>

class EpubReader;

class EpubUrlScheme : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit EpubUrlScheme(QObject* parent = nullptr);

    void setReader(EpubReader* reader);
    void requestStarted(QWebEngineUrlRequestJob* request) override;

private:
    EpubReader* m_reader = nullptr;
};
