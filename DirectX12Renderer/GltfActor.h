#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>
#include <map>
#include <tuple>

class Dx12Wrapper;
class GltfRenderer;
struct cgltf_data;

// glTF(.glb) モデル 1 体分のデータと描画を受け持つクラス
// このクラスは glTF(右手系) のデータを左手系に変換して取り込む。
// 変換は GltfActor.cpp の無名名前空間にある ToLH / QuatToLH / MatrixToLH に集約している。
// 新しい種類のデータ(タンジェント、モーフターゲット、カメラ、ライト等)を
// 読み込むときも、必ずこれらを通すこと。
class GltfActor
{
public:
    explicit GltfActor(Dx12Wrapper& dx12, GltfRenderer& _renderer);
    ~GltfActor();

    GltfActor(const GltfActor&) = delete;
    GltfActor& operator=(const GltfActor&) = delete;

    // -- 配置 --
    void SetPosition(const DirectX::XMFLOAT3& pos) { _position = pos; }
    const DirectX::XMFLOAT3& Position() const { return _position; }

    // Y 軸まわりの向き（ラジアン）
    void SetRotationY(float radian) { _rotationY = radian; }
    float RotationY() const { return _rotationY; }

    void SetScale(float scale) { _scale = scale; }
    float Scale() const { return _scale; }

    void Update(float deltaTime);

    // .glb を読み込んで描画に必要なリソースを作る
    bool Init(const std::string& modelPath);

    // 描画（GltfRenderer::BeforeDraw() の後に呼ぶ）
    void Draw();

    // 名前でアニメーションを選んで再生を開始する
    // @param blendSeconds 前のアニメーションから混ぜながら移行する秒数（0 で即座に切り替え）
    // @return 見つかったら true
    bool PlayAnimation(const std::string& name, float blendSeconds = 0.2f);

    const char* CurrentAnimationName() { return _currentAnimation < 0 ? "(none)" : _animations[_currentAnimation].name.c_str(); }
    float BlendWeight() { return _prevAnimation < 0 ? 1.0f : 1.0f - _blendRemain / _blendDuration; }

    // 輪郭線の描画を切り替える
    void SetOutlineEnabled(bool enabled) { _outlineEnabled = enabled; }
    bool IsOutlineEnabled() const { return _outlineEnabled; }

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    DirectX::XMFLOAT3 _position = { 0.0f, 0.0f, 0.0f };
    float _rotationY = 0.0f;
    float _scale = 1.0f;

    void UpdateWorldMatrix();   // _position などから world を作って GPU へ書く

    // GPU に送る頂点 1 つ分
    // (GltfRenderer のインプットレイアウトと並びを合わせること)
    struct Vertex
    {
        DirectX::XMFLOAT3 pos;      // POSITION
        DirectX::XMFLOAT3 normal;         // NORMAL（シェーディング用。フラットのまま）
        DirectX::XMFLOAT3 smoothNormal;   // 輪郭線の押し出し用（同一座標で平均済み）
        DirectX::XMFLOAT2 uv;       // TEXCOORD_0
        uint8_t joints[4];          // JOINTS_0  
        DirectX::XMFLOAT4 weights;  // WEIGHTS_0 
    };      // 64 バイト

    // 輪郭線の押し出し用に、同一座標の法線を平均して smoothNormal に格納する
    void BuildSmoothNormals(std::vector<Vertex>& vertices);

    bool _outlineEnabled = true;   // 輪郭線を描くか

    // プリミティブ 1 つ分の描画範囲
    // glTF はメッシュがマテリアルごとにプリミティブへ分割されるため、
    // 全部を 1 本のバッファに詰めたうえで範囲を記録して個別に描く
    struct Primitive
    {
        UINT indexCount;           // このプリミティブのインデックス数
        UINT startIndexLocation;   // インデックスバッファ内の開始位置
        INT  baseVertexLocation;   // 頂点バッファ内の開始位置
        int  materialIndex;        // マテリアル番号
        bool isBlend;
    };

    // シェーダーに渡すマテリアルデータ
      // (GltfShaderHeader.hlsli の cbuffer Material と並びを合わせること)
    struct MaterialForHlsl
    {
        DirectX::XMFLOAT4 baseColorFactor;   // ベースカラー係数(既定は 1,1,1,1)
    };

    // マテリアル 1 つが使うディスクリプタの数
    // CBV(マテリアル) + SRV(baseColor テクスチャ)
    static constexpr UINT DescriptorsPerMaterial = 2;

    bool LoadMaterials(const cgltf_data* data);        // マテリアルとテクスチャを読む
    bool CreateMaterialBuffer();                        // 定数バッファを作る
    bool CreateMaterialAndTextureView();                // CreateDescriptorHeap を置き換え

    std::vector<MaterialForHlsl> _materials;
    std::vector<ComPtr<ID3D12Resource>> _materialTextures;   // マテリアルごとの baseColor
    ComPtr<ID3D12Resource> _materialBuff;
    size_t _materialBuffSize = 0;

    // シェーダー側の bones[] の要素数(GltfShaderHeader.hlsli と合わせる)
    static constexpr size_t MaxBoneCount = 256;

    // シェーダーに渡すモデル固有の行列
    // (GltfShaderHeader.hlsli の cbuffer Transform と並びを合わせること)
    struct TransformBufferData
    {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX bones[MaxBoneCount];
    };

    // ノード 1 つ分（アニメーションで TRS を書き換えるので自前で持つ）
    struct Node
    {
        int parent = -1;                                  // 親ノードの添字(-1 = ルート)
        DirectX::XMFLOAT3 translation = { 0, 0, 0 };
        DirectX::XMFLOAT4 rotation = { 0, 0, 0, 1 };      // クォータニオン
        DirectX::XMFLOAT3 scale = { 1, 1, 1 };
    };

    bool LoadNodesAndSkin(const cgltf_data* data);   // ノード階層とスキンを読む
    bool CreateTransformBuffer();                     // ボーン行列用の定数バッファ(b1)
    void UpdateBoneMatrices();                        // ノードから _boneMatrices を作り直す

    std::vector<Node> _nodes;                         // 全ノード
    std::vector<int> _jointNodes;                     // joint 添字 -> ノード添字
    std::vector<DirectX::XMMATRIX> _inverseBindMatrices;
    std::vector<DirectX::XMMATRIX> _boneMatrices;          // GPU に送る

    // 親が必ず先に来る順序を作る（ルートから深さ優先）
      // glTF のノード配列は親子の順序を保証しないため必要
    std::vector<int> _nodeOrder;

    ComPtr<ID3D12Resource> _transformBuff;
    TransformBufferData* _mappedTransform = nullptr;    // マップしたまま保持

    // アニメーションのチャンネル 1 本
      // 「あるノードの translation / rotation / scale を時刻で動かす」1 系統
    struct AnimChannel
    {
        int targetNode = -1;                    // 対象ノードの添字
        int path = 0;                           // 0:translation 1:rotation 2:scale
        bool isStep = false;                    // true なら補間せず手前のキーの値を使う
        std::vector<float> times;               // キーの時刻(秒、昇順)
        std::vector<DirectX::XMFLOAT4> values;  // 値(VEC3 は w を 0 として格納)
    };

    // アニメーション 1 本
    struct Animation
    {
        std::string name;
        float duration = 0.0f;                  // 最後のキーの時刻
        std::vector<AnimChannel> channels;
    };

    // アニメーション 1 本を指定時刻でサンプリングして、ノードの配列に書き出す
    // @param animIndex アニメーション番号
    // @param timeSec 再生位置（秒）
    // @param out 書き出し先（バインドポーズで初期化してから上書きする）
    void SampleAnimation(int animIndex, float timeSec, std::vector<Node>& out) const;

    // 2 つのポーズを混ぜる
    // @param weight 0 で a、1 で b
    void BlendNodes(const std::vector<Node>& a, const std::vector<Node>& b,
        float weight, std::vector<Node>& out) const;

    std::vector<Animation> _animations;
    int _currentAnimation = -1;                 // 再生中のアニメーション(-1 = 停止)
    float _animTime = 0.0f;

    // -- ブレンド（移行元として残っている、1 つ前のアニメーション） --
    int _prevAnimation = -1;                    // -1 ならブレンド中ではない
    float _prevAnimTime = 0.0f;
    float _blendRemain = 0.0f;                  // 残りの移行時間（秒）
    float _blendDuration = 0.0f;                // 移行にかける時間（割合の計算に使う）

    std::vector<Node> _animNodes;               // 最終的なポーズ
    std::vector<Node> _blendNodes;              // 移行元のポーズ（作業用）

    bool LoadAnimations(const cgltf_data* data);
    void ApplyAnimation(float timeSec);         // _animNodes を書き換える

    // -- 初期化のサブルーチン --
    bool LoadGltfFile(
        const std::string& modelPath,
        std::vector<Vertex>& vertices,
        std::vector<uint16_t>& indices
    );
    bool CreateVertexAndIndexBuffer(
        const std::vector<Vertex>& vertices,
        const std::vector<uint16_t>& indices
    );

    Dx12Wrapper& _dx12;
    GltfRenderer& _renderer;

    ComPtr<ID3D12Resource> _vertBuff;
    ComPtr<ID3D12Resource> _idxBuff;
    D3D12_VERTEX_BUFFER_VIEW _vbView = {};
    D3D12_INDEX_BUFFER_VIEW _ibView = {};

    std::vector<Primitive> _primitives;

    ComPtr<ID3D12DescriptorHeap> _descHeap;
};