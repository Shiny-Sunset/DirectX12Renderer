# DangoRenderer

DirectX 12 を自前で書いた**静的ライブラリ版のレンダラー**。
3D アクションゲーム「DANGO DEFENSE」から切り出したもので、`Renderer/` フォルダ単位で
別のゲームへ持っていける。

**ゲーム側のヘッダを 1 つも include していない。**
インクルードパスにゲームのフォルダを入れていないので、書いても `C1083` になる。

| | |
|---|---|
| 言語 | C++20 / Windows / Visual Studio 2026（v145） |
| 出力 | `DangoRenderer.lib`（`ConfigurationType = StaticLibrary`） |
| 外部ライブラリ | DirectXTex（別途ビルド） |
| 同梱 | cgltf、Dear ImGui（`External/`。第三者コードなので修正しない） |

できること：glTF（`.glb`）のスキニングとアニメーション、3 枚カスケードのシャドウマップ、
オフスクリーン 1 枚を経由したポストエフェクト、ビットマップフォントの 2D 描画、
インスタンシング描画、Dear ImGui のデバッグ UI、GPU のパスごとの計測。

**できないこと**：シーングラフ、マテリアルの編集、アセットのパイプライン、
PBR のライティング（トゥーン寄りの簡易シェーディング）、マルチスレッドのコマンド生成。

---

## 構成

```
Core/       Dx12Wrapper  Pera  Util  ShadowConfig
Model/      GltfModel  GltfActor  GltfRenderer
2D/         SpriteRenderer  FontAtlas
Prim/       Ground  GateEffect
Platform/   Input  GameTimer
Debug/      DebugUI
Shaders/    .hlsl / .hlsli 19 本（実行時にコンパイルする）
External/   imgui  cgltf
```

| クラス | 役割 |
|---|---|
| `Dx12Wrapper` | デバイス、スワップチェーン、コマンド、フェンス、RTV/DSV、シャドウマップ、オフスクリーン、シーン用定数バッファ（`b0`）、テクスチャのキャッシュ、GPU タイマー |
| `GltfModel` | `.glb` の読み込み。頂点・マテリアル・ノード階層・スキン・アニメーション。**複数のアクターで共有する** |
| `GltfActor` | 1 体ぶんの位置・向き・ポーズ。アニメーションのブレンド |
| `GltfRenderer` | glTF 用のルートシグネチャとパイプラインステート（通常・半透明・輪郭線・影） |
| `Ground` | 地面の板ポリ |
| `Pera` | 画面いっぱいの板ポリ。オフスクリーンのテクスチャを画面へ転送する |
| `GateEffect` | XZ 平面に寝かせた円。プロシージャルな演出（インスタンシング） |
| `SpriteRenderer` | 2D のバッチ描画。矩形と文字列（縁取り・中央寄せ付き） |
| `FontAtlas` | GDI でグリフを焼いてアトラスにする。`charset` に入れた文字しか描けない |
| `Input` | キーボードとマウス。「押している間」と「押した瞬間」を区別する |
| `GameTimer` | デルタタイム、タイムスケール、過大な `dt` の切り詰め |
| `DebugUI` | Dear ImGui の初期化とフレーム管理。**何を表示するかは持たない** |

---

## 新しいゲームに組み込む

### 1. フォルダをコピーする

```
MyGame/
├── Renderer/                  ← このフォルダをそのままコピー
├── MyGame/                    ← ゲーム側（exe）
└── MyGame.slnx
```

### 2. ソリューションに追加して参照する

```xml
<!-- exe 側の .vcxproj -->
<ItemGroup>
  <ProjectReference Include="..\Renderer\DangoRenderer.vcxproj">
    <Project>{3f8a1d42-6b27-4c93-9e05-7a2d4c81b6f1}</Project>
  </ProjectReference>
</ItemGroup>
```

これで `lib` が一緒にビルドされ、リンクもされる。

### 3. exe 側のプロジェクト設定

```
AdditionalIncludeDirectories
    $(ProjectDir)..\..\DirectXTex\DirectXTex
    $(ProjectDir)..\Renderer
    $(ProjectDir)..\Renderer\Core
    $(ProjectDir)..\Renderer\Model
    $(ProjectDir)..\Renderer\2D
    $(ProjectDir)..\Renderer\Prim
    $(ProjectDir)..\Renderer\Platform
    $(ProjectDir)..\Renderer\Debug
    $(ProjectDir)..\Renderer\External\imgui
    $(ProjectDir)..\Renderer\External\imgui\backends

AdditionalLibraryDirectories
    ...\DirectXTex\Bin\Desktop_2026\$(Platform)\$(Configuration)

LanguageStandard        stdcpp20
ConformanceMode         true
PreprocessorDefinitions NOMINMAX
```

- `d3d12.lib` `dxgi.lib` `DirectXTex.lib` は `Dx12Wrapper.cpp` の `#pragma comment(lib, ...)`
  で指定しているので、**ライブラリの置き場所だけ**教えればよい
- `NOMINMAX` が無いと `windows.h` の `min` / `max` マクロが `std::max` と衝突する
- `$(SolutionDir)` は使わない。**MSBuild が `.vcxproj` を直接ビルドすると未定義**になる
  （Visual Studio から開くと通るので気づきにくい）

### ★ ゲーム側のフォルダを Renderer のインクルードパスに入れない

ここを入れてしまうと、レンダラーからゲーム側を `#include` できるようになり、
**分けた意味が消える。** 境界を守っているのは規約ではなくこの設定。

### 4. シェーダーの探索先を教える

```cpp
// ゲームの初期化の最初。前にあるフォルダから順に探す
SetShaderDirectories({ L"Shaders/", L"../Renderer/Shaders/" });
```

`Renderer/` をどこに置いたかを知っているのは**ゲーム側だけ**なので、
パスの宣言はゲーム側の仕事にしている。レンダラーは無変更で済む。

実行時の**作業ディレクトリは exe のプロジェクトフォルダ**にすること
（モデルもシェーダーも相対パスで読む）。Visual Studio の既定（`$(ProjectDir)`）のまま。

---

## 初期化の順番

依存があるので、この順に作る。

```cpp
_dx12 = std::make_unique<Dx12Wrapper>();
_dx12->Init(hwnd, width, height);

_font = std::make_unique<FontAtlas>(*_dx12);
_font->Build(L"Meiryo", 28, charset);        // ★ SpriteRenderer より先

_ui = std::make_unique<SpriteRenderer>(*_dx12, { _font.get() });
_ui->Init();

_debugUI = std::make_unique<DebugUI>(*_dx12);
_debugUI->Init(hwnd);

_gltfRenderer = std::make_unique<GltfRenderer>(*_dx12);   // パイプライン
_gltfRenderer->Init();

_ground->Init();  _pera->Init();

_model = std::make_unique<GltfModel>(*_dx12);             // モデルは 1 回だけ読む
_model->Init("Model/Character.glb");

_actor = std::make_unique<GltfActor>(*_dx12, *_gltfRenderer, *_model);
_actor->Init();
```

### ★ `SpriteRenderer` は `FontAtlas::Build()` の後

`SpriteRenderer` の SRV はフォントのアトラステクスチャを指す。
**`Build()` は呼ぶたびに新しいリソースを作る**ので、焼き直したら
`RefreshFontViews()` で SRV を作り直すこと。忘れると解放済みのテクスチャを指したままになる。

---

## 1 フレームの流れ

描画は 3 つのパスに分かれる。

```cpp
_dx12->BeginFrame();

input.Update(hwnd);
const float dt = timer.Tick();
// ... ゲームのロジック ...
_actor->Update(dt);

_dx12->SetCamera(eye, target, 0.1f, 100.0f);
_dx12->UpdateLightCamera(playerPos);          // 影を落とす範囲の中心

_debugUI->BeginFrame();
ImGui::Begin("Debug");  /* ... */  ImGui::End();
_debugUI->EndFrame();

// -- 0 パス目：光源から見た深度をシャドウマップへ --
_dx12->BeginShadowPass();
for (UINT c = 0; c < ShadowConfig::CascadeCount; ++c)
{
    _dx12->BeginCascade(c);
    _gltfRenderer->BeforeShadowDraw();
    _actor->DrawShadow();
}
_dx12->EndShadowPass();

// -- 1 パス目：シーンをオフスクリーンのテクスチャへ --
_dx12->BeginOffscreenPass();
_ground->Draw();
_gltfRenderer->BeforeDraw();                  // ★ 自前パイプラインの後は必ず
_gltfRenderer->SetModelHeap(_actor->Model());
_actor->Draw();
_dx12->EndOffscreenPass();

// -- 2 パス目：テクスチャを画面へ。UI を重ねる --
_dx12->BeginBackBufferPass();
_pera->Draw();
_ui->Begin();  _ui->DrawString(0, 16, 16, L"SCORE", white);  _ui->Draw();
_debugUI->Draw();                             // UI はポストエフェクトの影響を受けない

_dx12->EndDraw();
_dx12->Flip();
```

各 `Begin〜` が**リソースバリアの遷移・描画先の設定・クリア・ビューポートとシザー矩形**まで
面倒を見る。呼ぶ順番だけ守れば、状態遷移は意識しなくていい。

ウィンドウサイズが変わったら `Dx12Wrapper::Resize(w, h)` を呼ぶ
（スワップチェーンと深度バッファ、オフスクリーンを作り直す）。

---

## ルートシグネチャの規約

**ここが一番の資産。** 既存のシェーダーを真似して書くときの参照表。

### シーン共通（`b0`）

`Dx12Wrapper` が 1 つだけ持ち、毎フレーム書き換える。
`GltfShaderHeader.hlsli` の `SceneBuffer` と並びを合わせること。

```
view  viewProj  lightCamera[3]  eye  lightVec  shadowMapTexel
cascadeRadius  cascadeNormalOffset  shadowCenter  lightRange  time
```

`float3` の後ろにパディングが要る。**HLSL のベクトルは 16 バイト境界をまたげない。**

### パイプラインごとの割り当て

| | `GltfRenderer`（通常・輪郭） | `GltfRenderer`（影） |
|---|---|---|
| 0 | `b0` シーン（CBV） | `b0` シーン（CBV） |
| 1 | `t2` インスタンス（ルート SRV） | `t2` インスタンス |
| 2 | テーブル：`b2` マテリアル + `t0` baseColor | `t3` ボーン行列 |
| 3 | テーブル：`t1` シャドウマップ | `b1` カスケード番号（32bit 定数） |
| 4 | `t3` ボーン行列（ルート SRV） | `t4` 可視リスト |
| サンプラー | `s0` wrap linear / `s1` 比較（影） | なし |

| | `Ground` | `Pera` | `SpriteRenderer` | `GateEffect` |
|---|---|---|---|---|
| 0 | `b0` シーン | テーブル：`t0` | `b0` 画面サイズ（32bit 定数） | `b0` シーン |
| 1 | テーブル：`t0` 影 | — | テーブル：`t0` | `t0` ゲート配列（ルート SRV） |
| サンプラー | `s0` 比較 | `s0` linear | `s0` linear | なし |

**`b0` の意味はパイプラインごとに違う。** `SpriteRenderer` の `b0` は画面サイズの
32bit 定数で、シーンの定数バッファではない。2D は 3D の行列を一切使わない。

### ★ ボーンは定数バッファではなく `StructuredBuffer`

インスタンシングを入れたときに、`b1`（ワールド行列 + `bones[256]`）を廃止して
`t2` / `t3` に移した。

```hlsl
StructuredBuffer<InstanceData> g_instances : register(t2);   // world + boneOffset
StructuredBuffer<matrix>       g_bones     : register(t3);   // 全個体ぶんを連結
StructuredBuffer<uint>         g_visible   : register(t4);   // 影パスの絞り込み
```

1 個体ずつ定数バッファを差し替える形では、`DrawIndexedInstanced` が使えない。
`boneOffset` で自分のボーンの先頭を指す形にしたので、**何体いても描画呼び出しは 1 回**。

> リポジトリ直下の `CLAUDE.md` には「`b1` = world, bones[256]」と書いてあるが、
> **これはインスタンシングを入れる前の記述**。実体は上の表のとおり。

### ルート SRV とディスクリプタテーブルの使い分け

ルート SRV（`SetGraphicsRootShaderResourceView`）は**ディスクリプタヒープが要らない**。
バッファ 1 本をそのまま渡せる場合はこちらにしている。影パスは
テーブルを 1 つも使わないので、**ヒープの切り替えが発生しない。**

---

## 踏みやすいところ

- **CBV_SRV_UAV のディスクリプタヒープは同時に 1 本しかバインドできない。**
  そのため各モデルと ImGui がそれぞれヒープを持ち、描画のたびに `SetDescriptorHeaps` を
  呼び直している。シーンの `b0` を各ヒープの先頭に同居させるための
  `CreateSceneConstantBufferView()` / `CreateShadowMapView()` がある
- RTV は `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`、DSV は `D32_FLOAT`。
  **パイプラインステートの `RTVFormats` / `DSVFormat` と食い違うと実行時に落ちる**
- 自前のパイプラインを設定するクラス（`Ground` など）を描画した後は、
  次の glTF 描画の前に **`GltfRenderer::BeforeDraw()`** で設定し直す
- 配列を要求する API に `&comptr` を渡さない。`operator&` が中身を Release する。`comptr.Get()` を使う
- 定数バッファは 256 バイト境界に揃える（`Util.h` の `AlignmentedSize`）
- **UPLOAD ヒープは読み出しが極端に遅い。** CPU 側で光源行列を使うので、
  `Dx12Wrapper` は `_lightCamera` の控えを別に持っている
- CPU 側は DirectXMath の**行ベクトル**（合成は `子 × 親`、`S * R * T`）。
  HLSL 側は列優先で、行列は転置せずそのまま送って `mul(行列, ベクトル)` と書く
- glTF は右手系。`GltfActor.cpp` の `ToLH` / `QuatToLH` / `MatrixToLH` を**必ず通す**。
  頂点だけ変換してノードやアニメーションを忘れると、**バインドポーズでは気づけない**

### シェーダー

- 実行時に `D3DCompileFromFile` でコンパイルする。`.cso` は作らない
- `D3DCompileFromFile` の呼び出しは **`Util.cpp` の 1 箇所だけ**。
  追加するときは `CompileShader(L"Foo.hlsl", "FooVS", "vs_5_0", blob)` と書く
- `#include` は自前の `ID3DInclude`（`Util.cpp` の `ShaderInclude`）が解決する。
  **まず「その `.hlsl` / `.hlsli` があるフォルダ」、次に `SetShaderDirectories` の探索パス**。
  C++ の `"..."` と同じ順なので、同じフォルダに置いた `.hlsli` はこれまでどおり書ける。
  見つからないときは、探したパスを全部出す
- `line` は予約語。変数名に使えない
- 頂点シェーダーの出力構造体は、`SV_POSITION` を含めてピクセルシェーダー側と並びを合わせる
- 日本語コメントは書いてよい（実行時コンパイルなので BOM は不要）

---

## 持っていくときに残っている事情

| | |
|---|---|
| **`b0` が影の前提で作られている** | `lightCamera[3]` `cascadeRadius` `shadowCenter` が入っている。影を使わないゲームでも付いてくる（288 バイト程度） |
| **カスケードは 3 枚固定** | `ShadowConfig::CascadeCount`。変えるときは `.hlsli` 側の配列も合わせる |
| **フォントは起動時に焼き切り** | `charset` に入れていない文字は描けない。実行中に増やす仕組みは無い |
| **`Camera` は入っていない** | 三人称カメラはゲーム固有なのでゲーム側に置いた。汎用の orbit が欲しければ `Platform/` に入れて派生させる形になる |
| **PMD（書籍時代のコード）は入っていない** | `.pmd` モデルが再配布不可なので、持っていっても動かない。ゲーム側の `Legacy/` に置いてある |
| **1 フレームの組み立てはゲーム側** | 「どのパスをどの順で回すか」はレンダラーに上げていない。ゲームが 2 本できて、本当に毎回同じだと分かってから上げる |

---

## ライセンス

ソースコードは MIT ライセンス（Copyright (c) 2026 Shiny-Sunset）。

- `External/imgui`、`External/cgltf.h` はそれぞれのライセンスに従う（どちらも MIT）
- DirectXTex は含まれていない。各自で取得してビルドすること
- **モデルやテクスチャは含まれていない。** `.glb` などのアセットは
  リポジトリ直下の `DirectX12Renderer/README.md` の記載に従うこと
