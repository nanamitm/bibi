# Bibi HTML5 Reader

Qt Readerの主要な読書機能をブラウザー向けに実装した、独立したHTML5アプリです。
Qt/C++のバイナリではなく、epub.jsでEPUBを表示します。

公開URL: https://nanamitm.github.io/bibi/

## 機能

- ローカルEPUB 2/3の選択、ドラッグ＆ドロップ
- 縦書き・RTL、前後のページ移動（左右矢印キーは書籍の方向に対応）
- EPUB NAV / NCXの階層目次
- 本文の全文検索、結果への移動とハイライト（最大500件）
- しおりの追加・移動・削除、読書位置の自動復元
- 文字サイズ、白・セピア・暗色の配色、モバイル画面
- 縦書きの内蔵サンプル

EPUBは端末内で展開します。書籍をサーバーにアップロードしません。
読書位置・しおり・表示設定はlocalStorageに保存します。書籍自体は保存せず、
再訪時には同じファイルを開いてください。ブラウザーのデータ削除で保存内容も消えます。
書籍の識別にはファイル内容のSHA-256を使います。

EPUBのスクリプト・フォーム・埋め込みフレームを無効化し、書籍中の外部リソースは
CSPでブロックします。DRM付きEPUB、外部リソースが必須の書籍、書籍内の
JavaScriptを必要とするインタラクティブコンテンツには対応しません。
Qt版のフォルダー閲覧、ネイティブウィンドウ設定、三画面バッファーなどは移植対象外です。
全文検索はepub.jsのテキストノード単位の検索を使用するため、タグを跨ぐ語句は一致しません。

## 開発・検証

Node.js 22.17以上、または24。

```sh
cd html5-reader
npm ci
npm run dev
# Production build and browser integration tests
npx playwright install chromium
npm test
npm run build
npm run preview
```

既存のBibi（リポジトリ直下）やQt版のビルド依存関係とは独立しています。
出力は `dist/`、ベースURLは相対パスなのでGitHub Pagesの `/bibi/` でも動作します。
ブラウザーテストは `/bibi/` 配下で実行します。

## GitHub Pages

`.github/workflows/pages.yml` が master 上のHTML5版変更をテスト・ビルド・公開します。
PRではテストとビルドのみ実行します。
リポジトリの Settings → Pages → Source を **GitHub Actions** に設定してください。
手動実行は Actions → HTML5 Reader / GitHub Pages → Run workflow。

## ライセンス

本リポジトリと同じMIT。epub.jsはBSD-2-Clause、JSZipはMITまたはGPL-3.0。
アプリアイコンはQt Readerの既存アセットを再利用しています。
