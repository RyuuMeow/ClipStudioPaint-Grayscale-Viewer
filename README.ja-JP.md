# CSP Grayscale Viewer

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows-blue">
  <img src="https://img.shields.io/badge/language-C++20-blue">
  <img src="https://img.shields.io/badge/framework-Qt%206.9-green">
  <img src="https://img.shields.io/github/license/RyuuMeow/ClipStudioPaint-Grayscale-Viewer">
</p>

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh-TW.md">繁體中文</a> |
  <a href="README.zh-CN.md">简体中文</a> |
  <a href="README.ja-JP.md">日本語</a>
</p>

<p align="center">
  <img src="demo/demo.gif" alt="CSP Grayscale Viewer デモ">
</p>

CSP Grayscale Viewer は、Clip Studio Paint やその他の Windows 向け描画アプリのための軽量なグレースケールプレビュー補助ツールです。対象アプリの上にクリック透過のグレースケール overlay を重ねることで、作品そのものを変更せずに明暗のバランスをすばやく確認できます。

## この方式を採用する理由

- **DLL 注入、hook、メモリパッチなし**: このアプリは Clip Studio Paint にコードを注入せず、レンダリングパイプラインを hook せず、プロセスメモリも変更しません。
- **非侵襲的な設計**: CSP は通常どおり描画を続け、グレースケールプレビューは独立した overlay ウィンドウが表示します。
- **描画アプリのクラッシュリスクを低減**: ツールは別プロセスで動作し、CSP の内部に触れないため、renderer 側で問題が起きても CSP を巻き込みにくい構成です。
- **制作フローを妨げない**: overlay は入力を透過するため、ペンストローク、マウス入力、ショートカット、キャンバス操作はそのまま CSP に届きます。
- **描きながらライブプレビュー**: 書き出しやレイヤー複製をせず、通常の作業中に明暗を確認できます。
- **効率的なネイティブ実装**: C++20、Win32、Qt、Windows Magnification、実験的な D3D11 backend で構築されています。

## 機能

- **対象アプリ連動 overlay**: `CLIPStudioPaint.exe` など、設定した対象アプリにフォーカスがあるときだけグレースケールプレビューを有効にします。
- **グローバルホットキー**: カスタマイズ可能なショートカットでグレースケール表示を切り替えます。既定は `F9` です。
- **Focus-only ホットキーモード**: 他のアプリで作業しているときにショートカットを捕捉しないようにできます。
- **2 種類の renderer backend**:
  - **Magnification**: Windows Magnification API を利用した安定性重視の互換モードです。
  - **Experimental Direct3D**: より滑らかなプレビューと将来のエフェクト拡張に向けた GPU ベースの renderer です。
- **クリック透過 overlay**: マウスとペン入力は下にある描画アプリへ通過します。
- **モダンな Qt トレイアプリ**: 小さな設定ウィンドウを備え、システムトレイで静かに動作します。
- **ランタイムログページ**: アプリ内で focus、hotkey、renderer の診断情報を確認できます。

## 仕組み

このアプリは前景ウィンドウを追跡し、設定された対象アプリに overlay を合わせます。Magnification モードでは、Windows Magnification API とグレースケール用のカラーマトリクスを使用します。実験的な Direct3D モードでは、デスクトップ出力をキャプチャし、対象ウィンドウの範囲を切り出し、pixel shader でグレースケール化して、DirectComposition overlay として表示します。

## インストール

1. [Releases](../../releases) から最新のインストーラーをダウンロードします。
2. `CSP-Grayscale-Viewer_Setup.exe` を実行します。
3. CSP Grayscale Viewer を起動し、対象アプリとホットキーを設定します。

## 使い方

1. Clip Studio Paint、または設定済みの別の描画アプリを開きます。
2. 設定したホットキー、既定では `F9`、を押してグレースケールプレビューを切り替えます。
3. トレイアプリから設定ウィンドウを開き、更新頻度、対象アプリ、ホットキー、スタートアップ動作、renderer backend を調整します。
4. renderer やフォーカス動作を診断するときは Log ページを使用します。

## Renderer メモ

最大限の互換性を重視する場合は **Magnification** モードを使用してください。より滑らかな表示を試したい場合や新しい backend のテストに協力できる場合は **Experimental Direct3D** モードを使用してください。

Direct3D backend はまだ実験的です。将来的には、コントラストチェック、gamma プレビュー、LUT、色覚シミュレーションなどの GPU エフェクトを追加するための基盤になる予定です。

## ビルド

### 要件

- Windows 10 / 11
- Visual Studio 2022 with MSVC
- CMake 3.20 以降
- Qt 6.9.0 以降
- Inno Setup 6、インストーラーパッケージ作成時のみ必要

### コマンド

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/path/to/Qt/6.9.0/msvc2022_64"
cmake --build build --config Release --parallel

New-Item -ItemType Directory -Force deploy
Copy-Item build/Release/CSP-Grayscale-Viewer.exe deploy/
Copy-Item build/Release/CSP-Grayscale-Viewer_Console.exe deploy/
windeployqt --release --no-translations --no-opengl-sw deploy/CSP-Grayscale-Viewer.exe
```

## ライセンス

このプロジェクトは [MIT License](LICENSE) のもとで公開されています。

Qt は動的リンクで使用されており、LGPL ライセンスに従います。必要に応じて、インストールディレクトリ内の Qt runtime DLL を置き換えることができます。
