# BPMグリッド倍化プラグイン
グリッドの設定のBPM値を倍々に変化させることができる AviUtl / AviUtl2 プラグインです。

また、おまけ機能としてBPMグリッドの横移動・BPMの測定もできます。

## ダウンロード
https://github.com/Garech-mas/DoubleBPMGrid/releases/latest

上記のリンクにアクセスした後、`DoubleBPMGrid-v2.xx.zip` からダウンロードできます。

## 導入方法 (AviUtl 無印)
ZIPの中に入っている `DoubleBPMGrid.auf` を、AviUtlと同階層（またはPluginsフォルダ）の中にコピーしてください。
- 拡張編集 version 0.92 以外のバージョンでは動作しません。

- Visual C++ 再頒布可能パッケージ 2015-2022 (x86) をインストールしている必要があります。

  https://aka.ms/vs/17/release/vc_redist.x86.exe

## 導入方法 (AviUtl2)
ZIPの中に入っている `DoubleBPMGrid.aux2` を、AviUtl2のプレビュー画面にドラッグ＆ドロップしてください。
- 必ず最新バージョンのAviUtl2を導入してください。

  **必須バージョン以下の場合、プラグインが認識されない・使用できない場合があります。**

- Visual C++ 再頒布可能パッケージ v14 (x64) をインストールしている必要があります。

  https://aka.ms/vc14/vc_redist.x64.exe （DL直リンク）

## 注意点

**小数点以下が中途半端なBPMのままシーン切り替え・プロジェクト終了をすると、元のBPMを復旧することができません。**
