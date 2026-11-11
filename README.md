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
| `ESC` | ポーズ / 再開 |
| `R`（結果画面） | リスタート |
| `F1` | デバッグ UI の表示 / 非表示 |

アリは王へ一直線に向かいますが、攻撃すると一定時間プレイヤーを狙ってきます。引き付けて倒すか、王の前で迎え撃つかが判断どころです。

プレイヤーと敵は同じモデルのデータを共有し、位置とアニメーションの再生状態だけを個別に持っています。

## デバッグ UI

Dear ImGui でデバッグ用の表示を 2 つ出しています。**ゲーム中の文字には ImGui を使っていません**（自作のスプライトバッチとフォントアトラスで描いています）。

| ウィンドウ | 表示 |
|---|---|
| `Debug` | `F1` で表示 / 非表示。**Debug 構成では既定で表示、Release 構成では非表示** |
| `Profile`（画面右上） | 常に表示。入力を受け取らない軽量なオーバーレイ |

ImGui の入力欄にフォーカスがある間は、ゲームのキー操作は無効になります。

### `Debug` ウィンドウ

上から順に、以下を確認・変更できます。

| 項目 | 内容 |
|---|---|
| FPS / dt | フレームレートと、1 フレームの経過時間 |
| Time Scale | ゲーム内時間の倍率（0 で一時停止、0.5 でスロー。0〜3） |
| 1920 x 1080 / 1280 x 720 / Fullscreen | 表示モードの切り替え。UI とフォントの大きさも追従する |
| Profile (ms) | GPU wait / AI / Collision / Anim / Record の内訳 |
| Enemies | 生存数と上限、描画数、カスケードごとの影の描画数、討伐数、生存時間 |
| VSync | 垂直同期の切り替え（切るとフレームレートの上限が外れる） |
| **Balance** | |
| Spawn Interval | スポーン間隔の倍率（大きいほどアリが来ない） |
| Ant Speed | アリの移動速度の倍率 |
| Enemy Limit | 同時に存在できるアリの数（上限はプールの大きさ） |
| King HP | 王の最大 HP（**リスタートから反映**） |
| **Light** | |
| Azimuth / Elevation | 光源の向き（方位角と仰角）。影の向きと長さが変わる |
| Shadow Area Scale | 影を落とす範囲の**倍率**。狭めるほど影が精細になる |
| **Camera** | |
| Distance / Height | 三人称カメラの距離と高さ |
| **Transform** | |
| Position / Rotation Y | プレイヤーの位置と向き |
| **Character** | |
| Grounded / VelocityY | 接地しているかと、垂直方向の速度 |
| Walk Speed / Run Speed | 歩く速さと走る速さ（m/秒） |
| Jump Height / Gravity | ジャンプの到達高さと重力の強さ |
| Outline | 輪郭線の表示 / 非表示 |
| Anim / Blend | 再生中のアニメーション名と、クロスフェードの進み具合（0 → 1） |
| **Collision** | |
| Radius | プレイヤーの当たり判定の球の半径 |
| Input drops | 入力の取りこぼしを検出した回数（調査用に残してあります） |
| Dist to #1 | プレイヤーと王の距離と、半径の合計 |
| **King** | |
| HP / King Scale | 王の体力と拡大率（半径は拡大率に連動） |

### `Profile` ウィンドウ

1 フレームの内訳を常に表示します。**GPU 側はタイムスタンプクエリで実測した値**です。

```
640.2 FPS (1.56 ms)
GPU wait 0.036 / CPU 1.385
GPU 1.453 = Shadow 0.610 + Scene 0.772 + Pera 0.071
AI 0.112  Col 0.138
  Ground 0.021  Move 0.034  Stage1 0.012
Anim 0.286 = Sample 0.081 + Bone 0.142 + Inst 0.063
  Pairs 0.286 (Build 0.094 + Test 0.192)  Stage2 0.008  Unstuck 0.003
Frames in flight 2  ShadowMap 2048  BufferCount 3
```

上の数値はアリ 500 体、Release 構成・VSync オフでの実測です。

## 動作環境

| 項目 | 内容 |
|---|---|
| OS | Windows 10 / 11 |
| IDE | Visual Studio 2026（プラットフォームツールセット v145） |
| C++ | C++20 |
| プラットフォーム | x64 |

## モデルファイルの指定方法

`DirectX12Renderer/Application.cpp` の先頭付近（無名名前空間）で指定しています。

| 定数 | 既定値 | 用途 |
|---|---|---|
| `PlayerModelPath` | `Model/DangoGirl.glb` | プレイヤー |
| `KingModelPath` | `Model/dangomushi.glb` | 王（防衛対象） |
| `AntModelPath` | `Model/Ant.glb` | 敵 |

PMD モデルと VMD モーションの描画は、現在は行っていません（`PMDActor` を生成していないため）。書籍で学んだ記録としてコードは残してあります。

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

Release ではランタイムを**静的リンク**（`/MT`）しています。配布した exe が Visual C++ 再頒布可能パッケージ無しで動くようにするためです。

そのため **DirectXTex も `/MT` でビルドする必要があります**。片方だけだと「ランタイム ライブラリの不一致」（`LNK2038`）になります。

DirectXTex 側のファイルは書き換えたくないので、`DirectXTex/` の直下に次の `Directory.Build.targets` を置いています。MSBuild が自動で読み込むため、**ビルド時に特別な操作は要りません**。

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

OpenMP も切っているのは、`/MT` にしただけでは DirectXTex が使う `vcomp140.dll` への依存が残ってしまうためです。Debug は対象外なので、従来どおり `/MDd` のままです。

### 注意事項

シェーダーを実行時に `D3DCompileFromFile` でコンパイルし、モデルも相対パスで読み込みます。そのため、**作業ディレクトリがプロジェクトフォルダになっていないと、どちらも見つかりません**。

Visual Studio から実行する場合は作業ディレクトリが `$(ProjectDir)` になるため、そのまま動きます。exe を直接起動する場合は、以下を exe と同じ場所に並べてください。

```
DirectX12Renderer.exe
Shaders/     ← *.hlsl / *.hlsli 一式
Model/       ← *.glb
```

とくに **`Model/` の配置**にご注意ください。以下の 3 つが読み込めないと起動に失敗します。

| ファイル | 用途 |
|---|---|
| `Model/DangoGirl.glb` | プレイヤー |
| `Model/dangomushi.glb` | 王（防衛対象） |
| `Model/Ant.glb` | 敵 |

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
