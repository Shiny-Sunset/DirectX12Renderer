# DirectX12Renderer

DirectX 12 の学習用リポジトリです。ウィンドウ生成から順に積み上げていき、最終的に Blender で作成した 3D モデルの描画を目指しています。

## 現在の実装状況

- [x] ウィンドウの生成、Direct3D 12 デバイスの初期化
- [x] コマンドリスト / コマンドキュー / スワップチェーンによる画面クリア
- [x] リソースバリアによる状態遷移
- [x] 頂点バッファ・インデックスバッファを用いた四角形の描画
- [x] ルートシグネチャ、ディスクリプタヒープ、シェーダーリソースビューによるテクスチャの貼り付け
- [x] 画像ファイルの読み込み（DirectXTex）
- [x] 定数バッファによる座標変換
- [x] 深度バッファ
- [ ] 3D モデル（glTF）の読み込みと描画

## 動作環境

| 項目 | 内容 |
|---|---|
| OS | Windows 10 / 11 |
| IDE | Visual Studio 2026（プラットフォームツールセット v145） |
| C++ | C++20 |
| プラットフォーム | x64 |

## モデル、モーションファイルの指定方法

`DirectX12Renderer/Application.cpp` の16～18行目あたりの `model_path`、`motion_path` で指定してください。

## ビルド方法

このプロジェクトは [DirectXTex](https://github.com/microsoft/DirectXTex) に依存しています。ライブラリのパスは**相対パスで指定している**ため、以下のように本リポジトリと同じ階層に配置してください。

```
任意のフォルダ/
├─ DirectX12Renderer/     ← 本リポジトリ
└─ DirectXTex/            ← DirectXTex をクローンして配置
```

1. DirectXTex をクローンする

   ```
   git clone https://github.com/microsoft/DirectXTex.git
   ```

2. `DirectXTex/DirectXTex_Desktop_2026.slnx` を開き、使用する構成（Debug / Release、x64）でビルドする
   - `DirectXTex/DirectXTex/Bin/Desktop_2026/<Platform>/<Configuration>/DirectXTex.lib` が生成されます

3. `DirectX12Renderer.slnx` を開き、DirectXTex と**同じ構成**でビルドする

> [!NOTE]
> DirectXTex 側と本プロジェクト側で構成（Debug / Release）が食い違うとリンクエラーになります。参照するライブラリのパスは `$(Platform)\$(Configuration)` で解決しているため、両者を揃えてビルドしてください。

## アセットの入手

本リポジトリには、再配布が許可されていないサードパーティのアセットを含めていません。PMD モデルを描画するには、以下を各自で用意して配置してください。

| 配置先 | 内容 | 入手元 |
|---|---|---|
| `DirectX12Renderer/Model/` | PMD モデルと付随テクスチャ | [MikuMikuDance](https://sites.google.com/view/vpvp/) 同梱の `UserFile/Model/`（あにまさ氏 制作） |
| `DirectX12Renderer/toon/` | `toon01.bmp` 〜 `toon10.bmp` | MikuMikuDance 同梱の `Data/` |
| `DirectX12Renderer/motion/` | VMD モーション | 書籍「DirectX12の魔導書」サンプルデータ（[boxerprogrammer/directx12_samples](https://github.com/boxerprogrammer/directx12_samples)） |

いずれも各配布元の利用規約に従って入手・利用してください。

`Model/DangoGirl.glb` は本リポジトリ作者が Blender で作成した自作モデルで、リポジトリに含まれています。

## サードパーティライセンス

本プロジェクトは以下のライブラリを使用しています。ライセンス全文は [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) を参照してください。

| ライブラリ | ライセンス | 著作権表示 |
|---|---|---|
| [DirectXTex](https://github.com/microsoft/DirectXTex) | MIT License | Copyright (c) Microsoft Corporation. |
| [cgltf](https://github.com/jkuhlmann/cgltf) | MIT License | Copyright (c) 2018-2021 Johannes Kuhlmann |

cgltf は `DirectX12Renderer/External/cgltf.h` として同梱しています。
