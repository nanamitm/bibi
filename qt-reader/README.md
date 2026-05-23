# Bibi Qt Reader

Qt6/C++ クロスプラットフォーム EPUB リーダー。[Bibi](../README.md) ウェブリーダーの
デスクトップ補完として、Windows / macOS / Linux 上でネイティブ動作します。

## 機能

| 機能 | 詳細 |
|------|------|
| **EPUB 2/3 対応** | OPF・NCX・NAV ドキュメントを解析 |
| **縦書き / RTL** | Chromium ベースの QWebEngineView が `writing-mode` / `direction` CSS を完全サポート |
| **しおり** | チャプター番号・スクロール位置を JSON で永続保存 |
| **全文検索** | 全チャプターのテキストを横断検索、コンテキスト表示 |
| **目次パネル** | ネストされた TOC ツリーから直接ジャンプ |
| **ズーム** | Ctrl+/- / メニューで自由に調整 |
| **ウィンドウ状態保存** | 起動時に前回のレイアウトを復元 |

## 必要環境

| 項目 | バージョン |
|------|-----------|
| CMake | 3.19 以上 |
| Qt | 6.4 以上（Core, Gui, Widgets, WebEngineWidgets, WebEngineCore, Xml） |
| C++ コンパイラ | C++17 対応（MSVC 2019+, GCC 10+, Clang 12+） |
| Git | FetchContent による miniz 自動取得に使用 |
| インターネット接続 | 初回ビルド時のみ（miniz のダウンロード） |

> **注意**: `QtWebEngine` は Qt のインストール時に明示的に選択する必要があります。
> また、LGPL 版ではなく GPL / 商用ライセンスが必要な場合があります。

## ビルド方法

```bash
# リポジトリ直下の qt-reader/ ディレクトリで実行
cd qt-reader

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows (Visual Studio)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2019_64"
cmake --build build --config Release
```

### macOS

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## 使い方

```bash
# GUI で起動
./build/BibiQtReader

# コマンドラインで直接開く
./build/BibiQtReader path/to/book.epub
```

## アーキテクチャ

```
qt-reader/
├── CMakeLists.txt
└── src/
    ├── main.cpp              カスタム URL スキーム登録 + QApplication
    ├── mainwindow.h/.cpp     メインウィンドウ（ツールバー・メニュー・ナビゲーション）
    ├── epubreader.h/.cpp     EPUB 解析（ZIP 展開・OPF/NCX/NAV パース・全文検索）
    ├── epuburlscheme.h/.cpp  epub:// カスタム URL スキームハンドラ
    ├── bookmarkmanager.h/.cpp しおり管理（JSON 永続化）
    └── searchdialog.h/.cpp   全文検索ダイアログ
```

### epub:// URL スキーム

EPUB コンテンツはディスクに展開せず、ZIP から直接メモリに読み込んで
`QWebEngineUrlSchemeHandler` でサーブします。

```
epub:///OEBPS/chapter1.html   → 対応する ZIP エントリを読み込んで返す
epub:///OEBPS/images/fig1.png → 画像も同様
```

### スレッドセーフティ

`QWebEngineUrlSchemeHandler::requestStarted()` は Qt の I/O スレッドから呼ばれます。
`EpubReader::fileData()` は miniz の ZIP アーカイブアクセスを `QMutex` で保護しています。

## ライセンス

MIT（本プロジェクトと同じ）。依存ライブラリ:

- **miniz** (MIT) — ZIP/deflate 展開
- **Qt6** — LGPL / GPL / 商用（選択したエディションによる）
