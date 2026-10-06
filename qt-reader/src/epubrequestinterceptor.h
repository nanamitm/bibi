#pragma once
#include <QWebEngineUrlRequestInterceptor>

// EPUB は信頼できないコンテンツとして扱い、本の中身（epub: スキーム）と
// それが生成する data: / blob: 以外へのリクエストをすべて遮断する。
// 悪意のある EPUB が file: のローカルファイルを読んだり、http(s) で外部へ
// 情報を送ったりできないようにするため。
// 外部リンクのクリックは EpubWebPage::acceptNavigationRequest() 側で扱う。
class EpubRequestInterceptor : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;
};
