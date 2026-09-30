# DirectX12Renderer

DirectX 12 でレンダラーを自作し、その上で 3D アクションゲーム「DANGO DEFENSE」を制作したリポジトリです。ウィンドウ生成から順に積み上げ、モデルの読み込み・スキニング・影・マルチパスレンダリングまでを実装したうえで、Blender で自作したモデルを使ったゲームを載せています。

ゲームエンジンは使用していません。描画・当たり判定・ゲームの仕組みは、DirectXTex / cgltf / Dear ImGui 以外すべて自作です。

## ゲームの概要

中央にいる「ダンゴムシの王」を、四隅の巣穴から湧いてくるアリの群れから守るゲームです。倒したアリの数と、守り切った時間がスコアになります。

## 現在の実装状況

- [x] ウィンドウの生成、Direct3D 12 デバイスの初期化
- [x] コマンドリスト / コマンドキュー / スワップチェーンによる画面クリア
- [x] リソースバリアによる状態遷移
- [x] 頂点バッファ・インデックスバッファを用いた四角形の描画
- [x] ルートシグネチャ、ディスクリプタヒープ、シェーダーリソースビューによるテクスチャの貼り付け
- [x] 画像ファイルの読み込み（DirectXTex）
- [x] 定数バッファによる座標変換
- [x] 深度バッファ
- [x] PMD モデルの読み込みと描画（マテリアル、スフィアマップ、トゥーンテクスチャ）
- [x] VMD モーションの再生（キーフレーム補間、ベジェ曲線による補間）
- [x] クラス分割（Application / Dx12Wrapper / PMDRenderer / PMDActor）
- [x] glTF(.glb) モデルの読み込みと描画（cgltf、埋め込みテクスチャ、アルファブレンド）
- [x] glTF のスキニング（ノード階層、逆バインド行列、4 ボーンブレンド）
- [x] glTF のアニメーション（線形補間、ループ再生）
- [x] トゥーンシェーダー（ハーフランバートの階調量子化）
- [x] 輪郭線の描画（法線方向への押し出しによる背面法、O キーで切り替え）
- [x] Dear ImGui によるデバッグ UI
- [x] デルタタイムによるフレームレート非依存の更新（GameTimer、タイムスケール、VSync の切り替え）
- [x] ワールド行列のモデルごとの管理（位置・向き・拡大率をアクターが持つ）
- [x] 入力の管理（「押している間」と「押した瞬間」の区別、マウスの移動量）
- [x] キャラクターの移動（カメラ基準の移動方向、進行方向への旋回、速度に応じたアニメーションの切り替え）
- [x] 三人称カメラ（対象への追従、マウスでの回り込み）
- [x] 地面の描画（ルートディスクリプタによる定数バッファの受け渡し、ピクセルシェーダーでの格子模様）
- [x] アニメーションのブレンド（2 本を同時にサンプリングしてクロスフェード、回転は球面線形補間）
- [x] マルチパスレンダリング（オフスクリーンのテクスチャへ描画し、画面いっぱいの板ポリで転送）
- [x] シャドウマップによる影（光源から見た深度テクスチャ、比較サンプラーによる PCF、プレイヤーへの追従）
- [x] モデルとアクターの分離（頂点・テクスチャ・アニメーションのデータを共有し、位置と再生状態は 1 体ごとに保持）
- [x] 重力とジャンプ（速度による移動、接地判定、到達したい高さから初速を求める）
- [x] 当たり判定（球同士の交差判定、水平方向への押し戻し、押されにくさの重み付け）
- [x] 敵の追跡 AI（状態遷移による待機 / 追跡の切り替え、ヒステリシスによる検知範囲）
- [x] 攻撃と体力（アクティブフレームのある攻撃判定、多段ヒットの防止、ノックバック、無敵時間）
- [x] ゲームの流れ（タイトル → プレイ → クリア / ゲームオーバー、リスタート）
- [x] ステージの足場（ルート定数で箱ごとの行列を渡して描画、影の投下、AABB との当たり判定）
- [x] 接地判定と押し戻しの分離（足元の真下から床の高さを求め、横方向は円と長方形で押し戻す）
- [x] 影と陰影の計算の共通化（法線による遮蔽の判定、バイアスをメートル指定して正射影の深度へ換算）
- [x] キャラクターが影を受ける（アクターごとのヒープにシャドウマップのビューを作成、ノーマルオフセットによるアクネ対策）
- [x] シャドウマップの品質向上（3×3 PCF、光源のテクセル単位スナップによるちらつき防止）
- [x] 敵のオブジェクトプール（実行中にリソースを確保しない使い回し）と、時間経過で間隔が縮むスポーン
- [x] 防衛対象（王）と、ヘイトによる標的の切り替え（攻撃されると一定時間プレイヤーを狙う）
- [x] 障害物の回避（進めていないことを検知して横へ逸れる）と、めり込みからの強制脱出
- [x] スコアの集計と結果画面、ゲームバランスの調整値の一元管理

## 遊び方

中央の王が倒されるとゲームオーバーです。生存時間と討伐数がスコアになります。ステージには壁と足場があり、ジャンプで登れます。

| 操作 | 動作 |
|---|---|
| `Space`（タイトル） | ゲーム開始 |
| `W` / `A` / `S` / `D` | カメラから見た方向へ移動 |
| `Shift`（押しながら） | 走る |
| `Space` | ジャンプ |
| マウス左クリック | 攻撃（2 回当てるとアリを倒せる） |
| マウス右ドラッグ | カメラを回り込ませる |
| `R`（結果画面） | リスタート |

アリは王へ一直線に向かいますが、攻撃すると一定時間プレイヤーを狙ってきます。引き付けて倒すか、王の前で迎え撃つかが判断どころです。
| `O` | 輪郭線の表示 / 非表示を切り替え |
| `1` / `2` / `3` | アニメーションを Idle / Walk / Run に切り替え |

プレイヤーと敵は同じモデルのデータを共有し、位置とアニメーションの再生状態だけを個別に持っています。

画面上の「Debug」ウィンドウから、以下を確認・変更できます。ImGui の入力欄にフォーカスがある間は、上記のキー操作は無効になります。

| 項目 | 内容 |
|---|---|
| FPS / dt | フレームレートと、1 フレームの経過時間 |
| Time Scale | ゲーム内時間の倍率（0 で一時停止、0.5 でスロー） |
| VSync | 垂直同期の切り替え（切るとフレームレートの上限が外れる） |
| Position / Rotation Y | モデルの位置と向き |
| Outline | 輪郭線の表示 / 非表示 |
| Anim / Blend | 再生中のアニメーション名と、クロスフェードの進み具合（0 → 1） |
| Animation | 再生するアニメーションの選択 |
| Azimuth / Elevation | 光源の向き（方位角と仰角）。影の向きと長さが変わる |
| Shadow Area | シャドウマップがカバーする範囲。狭めるほど影が精細になる |
| Grounded / VelocityY | 接地しているかと、垂直方向の速度 |
| Jump Height / Gravity | ジャンプの到達高さと重力の強さ |
| Walk Speed / Run Speed | 歩く速さと走る速さ（m/秒） |
| Radius | 当たり判定の球の半径 |
| Enemies | 敵ごとの現在の状態（Idle / Chase） |

## 動作環境

| 項目 | 内容 |
|---|---|
| OS | Windows 10 / 11 |
| IDE | Visual Studio 2026（プラットフォームツールセット v145） |
| C++ | C++20 |
| プラットフォーム | x64 |

## モデル、モーションファイルの指定方法

`DirectX12Renderer/Application.cpp` で指定しています。

- glTF モデル：`Application::Init` 内の `_gltfActor->Init("Model/DangoGirl.glb")`
- PMD モデル / VMD モーション：ファイル先頭付近の `model_path`、`motion_path`（現在は glTF の描画に切り替えているため、PMD の処理はコメントアウトしています）

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

### Release 構成について

配布した exe が実行環境を選ばないよう、Release ではランタイムを**静的リンク**（`/MT`）しています。Visual C++ 再頒布可能パッケージが無くても動きます。

そのため、**DirectXTex も `/MT` でビルドする必要があります**。片方だけだと「ランタイム ライブラリの不一致」（`LNK2038`）になります。

DirectXTex 側のファイルは書き換えたくないので、`DirectXTex/` の直下に以下の `Directory.Build.targets` を置いています。MSBuild はプロジェクトのあるフォルダから上へ辿って最初に見つけたこのファイルを自動で読み込むため、**ビルド時に特別な操作は要りません**。読み込まれるのはプロジェクト本体の設定より後なので、`ItemDefinitionGroup` を上書きできます。

```
任意のフォルダ/
├─ DirectX12Renderer/
└─ DirectXTex/
   └─ Directory.Build.targets   ← これを作る
```

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
      <OpenMPSupport>false</OpenMPSupport>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
```

Debug は対象外なので、従来どおり `/MDd` のままです。

あわせて OpenMP も切っています。`/MT` にしただけでは DirectXTex が使う `vcomp140.dll` への依存が残り、結局は再頒布可能パッケージが必要になってしまうためです。OpenMP は BC6H / BC7 圧縮の並列化にしか使われておらず、本プロジェクトはテクスチャの読み込みしか行わないので影響しません。

なお Release では全体最適化（`/GL`）を切っています。`/MT` と組み合わせるとリンク時のコード生成で `imgui_impl_win32.cpp` が内部コンパイラエラー（`C1001`）を起こすためです。

### 配布する場合

シェーダーを実行時に `D3DCompileFromFile` でコンパイルし、モデルも相対パスで読むため、**exe 単体では動きません**。同じフォルダに以下を並べてください。

```
DirectX12Renderer.exe
*.hlsl / *.hlsli      ← プロジェクト直下のシェーダー一式
Model/*.glb           ← モデル
```

## アセットの入手

本リポジトリには、再配布が許可されていないサードパーティのアセットを含めていません。PMD モデルを描画するには、以下を各自で用意して配置してください。

| 配置先 | 内容 | 入手元 |
|---|---|---|
| `DirectX12Renderer/Model/` | PMD モデルと付随テクスチャ | [MikuMikuDance](https://sites.google.com/view/vpvp/) 同梱の `UserFile/Model/` |
| `DirectX12Renderer/toon/` | `toon01.bmp` 〜 `toon10.bmp` | MikuMikuDance 同梱の `Data/` |
| `DirectX12Renderer/motion/` | VMD モーション | 書籍「DirectX12の魔導書」サンプルデータ（[boxerprogrammer/directx12_samples](https://github.com/boxerprogrammer/directx12_samples)） |

いずれも各配布元の利用規約に従って入手・利用してください。

`Model/DangoGirl.glb` は本リポジトリ作者が Blender / Substance Painter で制作した自作モデルで、リポジトリに含まれています。利用条件は「[ライセンス](#ライセンス)」を参照してください。

### モデルのクレジット

動作確認に使用している「初音ミク」モデルのクレジットは以下のとおりです。

```
PolyMo用モデルデータ：初音ミク ver.1.3
(物理演算対応モデル)

モデリング  ：あにまさ氏
データ変換  ：京 秋人氏
Copyright   ：CRYPTON FUTURE MEDIA, INC
```

キャラクター「初音ミク」はクリプトン・フューチャー・メディア株式会社の著作物であり、
[ピアプロ・キャラクター・ライセンス](https://piapro.jp/license/pcl/summary)に基づく非営利の利用となります。

## ライセンス

本リポジトリは、**ソースコード**と**同梱の 3D モデル**で条件が異なります。

### ソースコード

MIT License です。詳細は [LICENSE](LICENSE) を参照してください。

```
Copyright (c) 2026 Shiny-Sunset
```

### `Model/DangoGirl.glb`（自作モデル）

本リポジトリ作者が Blender / Substance Painter で制作した著作物です。**MIT License の対象外**です。

動作確認用に同梱しているものであり、**転載・再配布・改変はご遠慮ください**。

## サードパーティライセンス

本プロジェクトは以下のライブラリを使用しています。ライセンス全文は [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) を参照してください。

| ライブラリ | ライセンス | 著作権表示 |
|---|---|---|
| [DirectXTex](https://github.com/microsoft/DirectXTex) | MIT License | Copyright (c) Microsoft Corporation. |
| [cgltf](https://github.com/jkuhlmann/cgltf) | MIT License | Copyright (c) 2018-2021 Johannes Kuhlmann |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT License | Copyright (c) 2014-2026 Omar Cornut |

cgltf は `DirectX12Renderer/External/cgltf.h`、Dear ImGui は `DirectX12Renderer/External/imgui/` として同梱しています。
