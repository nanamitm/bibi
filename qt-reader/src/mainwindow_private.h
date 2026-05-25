#pragma once
#include <QFile>
#include <QString>
#include <QDebug>

constexpr const char* kZoomFactorSettingKey        = "view/zoomFactor";
constexpr const char* kPreloadModeSettingKey       = "view/preloadMode";
constexpr const char* kPixelSwapDetectionSettingKey = "view/pixelSwapDetection";
constexpr const char* kFolderRootSettingKey        = "library/rootFolder";

inline QString loadScript(const char* path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("loadScript: cannot open \"%s\"", path);
        return {};
    }
    return QString::fromUtf8(f.readAll());
}
