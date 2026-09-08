# VOID PIT: BORROWED CROWN

<img width="707" height="538" alt="image" src="https://github.com/user-attachments/assets/a4f39718-3a5c-42a0-9421-919769d89b9b" />


[Astra-1.md](Astra-1.md)に基づくWindows Terminal用コマンド戦闘ゲーム。C++17で実装し、ローカルはGCC／MinGW-w64、本番のGitHub ActionsはClang／MSVC環境でCMakeビルドします。外部ライブラリ・画像・追加パッケージは不要です。

操作方法は[日本語説明書](MANUAL-ja.md)を参照してください。

## download and play

https://nightly.link/DaisukeDaisuke/Arena-cli/workflows/windows-clang/main/void-pit-windows-x64.zip

## CMake / MinGWビルド

CMake 3.21以上と64bit MinGW-w64の`g++.exe`・`mingw32-make.exe`をPATHに設定します。プリセットは既存のツールを使い、コンパイラーのインストールや自動切替は行いません。

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
.\build\mingw-release\bin\void-pit-windows-x64.exe
```

このワークスペースのWinLibsを明示する場合:

```powershell
$mingwBin = 'C:\Users\owner\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin'
$env:PATH = "$mingwBin;$env:PATH"
cmake --preset mingw-release "-DCMAKE_CXX_COMPILER=$mingwBin/g++.exe"
cmake --build --preset mingw-release
```

Debug版はプリセット名を`mingw-debug`へ変えてビルドできます。IDEではCMakeプロジェクトとして開き、Toolchainに同じMinGWを指定してください。CMake Presetsを使わず、次の構成も可能です。

```powershell
cmake -S . -B build/custom -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++.exe
cmake --build build/custom --parallel 4
```

GCC版はC++ランタイムを静的リンクします。ReleaseはLTO・関数の同一化を無効にし、デバッグ情報を付けません。ソースからWindows API・シェル・ブラウザーを呼び出しません。

## 表示

```powershell
.\build\mingw-release\bin\void-pit-windows-x64.exe --plain
.\build\mingw-release\bin\void-pit-windows-x64.exe --redraw=clear
.\build\mingw-release\bin\void-pit-windows-x64.exe --color=osc --progress
```

既定はRGB色・UTF-8ピクセル絵・8改行区切りの追記表示です。枠内は76列。`--plain`はASCIIアートと日本語説明を使い、表示制御文字を出しません。`--plain --redraw=clear`は起動エラーです。

OSCモードは指定したパレット16〜41のうち使用する番号だけを設定・リセットします。端末によって過去の表示色も変わるため、記録用途には既定のRGBを使ってください。OSC104は端末の既定色への復帰です。強制終了時の復元や、起動前の他アプリの一時設定の復元は行えません。

## 再現と検証

```powershell
.\build\mingw-release\bin\void-pit-windows-x64.exe --seed 0000000000000001 --plain
.\build\mingw-release\bin\void-pit-windows-x64.exe --self-test --plain
```

seed指定はPRACTICE、未指定はRANDOM RUNです。未指定時は`std::random_device`を2回使用し、取得に失敗したら起動を中止します。C++規格だけではデバイス乱数の実装方法や暗号学的強度を保証しません。

中断は版・seed・大会種別・確定済み操作列を保存し、再開時は通常の入力検証を通して再生します。改変防止用の認証はありません。`--self-test`はLCG既知値、戦闘・履歴・装備の境界条件、中断再生、描画制御を検証する起動オプションです。

CTestの`ordinary-routes`はseed 0〜999と最大値の全8経路、および初期技27構成の個別fixtureを検査します。正規装備技・通常購入だけで優勝する経路を有限幅の探索で探し、借り技は使いません。この有限seed検査を、全2^64シードでの勝利保証や人間のプレイテスト結果とは扱いません。

## GitHub Actionsでのexeビルド

[Windows Clang workflow](.github/workflows/windows-clang.yml)は、`main`へのpush・Pull Request・手動実行・`v*`タグで起動します。`windows-latest`で指定の`ilammy/msvc-dev-cmd`を使ってx64のMSVC環境を設定し、既存の`clang-cl`とNinjaでCMakeビルドします。`windows-clang-release`プリセットで構成・ビルド・CTestを実行し、`void-pit-windows-x64` Artifactにexe・日本語説明書・SHA256・コンパイラー情報を保存します。

Actionは現在の設定（checkout/upload v7、download v8、github-script v9）を使います。`msvc-dev-cmd`は指定のcommit SHAへ固定しています。追加ソフトウェアはインストールしません。

`v*`タグではビルド成功後に同じファイルを添付したdraft Releaseを作ります。既存Releaseの上書きや自動公開は行いません。タグ以外ではArtifactのみです。

設計上の所有確認・履歴監査の不整合は[Astra-1.md](Astra-1.md)の仕様です。実バイナリの逆解析結果や、未実施の実端末・プレイテストを検証済みとはしていません。WebAssembly版はこのWindows実装に含みません。
