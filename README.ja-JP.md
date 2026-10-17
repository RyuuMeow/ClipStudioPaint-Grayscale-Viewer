# CSP Grayscale Viewer

[English](README.md) | [繁體中文](README.zh-TW.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja-JP.md)

Clip Studio Paint などの描画アプリで明暗を確認するための、Windows ネイティブのトレイツールです。マウスやペン入力はプレビューを通過します。作品の変更や描画アプリへのコード注入は行いません。

## 使い方

1. [Releases](../../releases) からインストーラーかポータブル版を入手するか、下記の手順でビルドします。
2. `CSP-Grayscale-Viewer.exe` を実行すると、Windows の通知領域にアイコンが表示されます。
3. Clip Studio Paint に切り替え、**F9** でプレビューを切り替えます。アイコンの左クリックでも切り替えられます。
4. 右クリックメニューから対象アプリ、ショートカット、更新上限、レンダラー、自動起動を設定でき、診断ログの表示や終了も行えます。

対象アプリとショートカットは、必要時だけ作成されるネイティブのダイアログで編集します。初期設定では対象アプリにフォーカスがある間だけショートカットを登録します。新しいキーが競合した場合、以前の割り当てを維持します。Win 修飾キーに対応し、Windows 予約キーの F12 は使用できません。

## 描画

新しいプロファイルは **Direct3D**、上限 60 fps を使用します。キャプチャ、グレースケール変換、表示は専用スレッドで処理します。フレームごとの UI メッセージを蓄積せず、新しい画面がない場合やカーソルだけの更新では不要な再描画を省きます。非表示時は描画を停止し、5 秒後に GPU リソースを解放します。フォーカス追跡は定期ポーリングではなく Windows イベントを使用します。

**Magnification** は手動選択できる互換モードとして残しています。Direct3D のキャプチャ失敗、画面をまたぐウィンドウ、回転したディスプレイでは自動的に互換モードへ切り替えます。対象アプリに再度切り替えると一時的な D3D 障害を再試行できます。現在のモードはメニューに表示されます。

- **Automatic** は対象ディスプレイのリフレッシュレートに追従します。手動指定もそのレートを上限とします。
- Direct3D は最大 144 fps、Magnification は Windows タイマーの制約で最大 100 fps です。指定値は達成を保証するものではありません。
- Direct3D の[自身をキャプチャから除外する機能](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)には Windows 10 2004 以降が必要です。旧版では互換モードを使用します。
- HDR、保護されたコンテンツ、リモート接続、複数 GPU、実際のペン遅延は対応する環境での検証が必要です。特定の遅延改善値は主張していません。

## 設定の移行

設定を検証し、原子的なファイル置換で `%APPDATA%\CSP-Grayscale-Viewer\native-settings.ini` に保存します。初回起動時は同じフォルダーの旧 `settings.ini` を読み込み、元ファイルとレンダラーの選択を維持します。従来の設定ウィンドウと最小化起動オプションに代わり、起動時からトレイに常駐します。

`Open diagnostics` は件数制限付きのメモリ内ログを設定ファイルの隣へ書き出します。常駐ログウィンドウや継続的なディスク書き込みは不要です。

独立したプロファイルを使う場合：

```powershell
.\CSP-Grayscale-Viewer.exe --settings .\test-settings.ini --configure
```

## ビルドとテスト

Windows 10/11 x64、Visual Studio 2022 の C++ デスクトップ開発環境と Windows SDK、CMake 3.20 以降が必要です。Qt や MSVC ランタイムの別途インストールは不要です。

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke
```

出力先は `build/native/Release` です。GUI の実行ファイルは単体で使用できます。任意の `_Console.exe` は既定の設定で動作し、コンソールにログを出力します。Inno Setup 6 はインストーラー作成時だけ必要です。

Debug、デスクトップ描画テスト、手動確認については[テストと構成の説明](docs/testing.md)を参照してください。Windows CI は Debug と Release の回帰テストおよびトレイ起動テストを実行します。

## デモとライセンス

![グレースケールプレビュー](demo/demo.gif)

デモ内の設定画面は、ネイティブトレイへの移行前のものです。

[MIT License](LICENSE)。
