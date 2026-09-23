#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <string>
#include <vector>

class Dx12Wrapper;
struct cgltf_data;

// このクラスは glTF(右手系) のglTF(.glb) モデルのデータを左手系に変換して取り込む。
class GltfModel
{
public:
    explicit GltfModel(Dx12Wrapper& dx12);
    ~GltfModel() = default;

    bool Init(const std::string& modelPath);

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

    // ノード 1 つ分（アニメーションで TRS を書き換えるので自前で持つ）
    struct Node
    {
        int parent = -1;                                  // 親ノードの添字(-1 = ルート)
        DirectX::XMFLOAT3 translation = { 0, 0, 0 };
        DirectX::XMFLOAT4 rotation = { 0, 0, 0, 1 };      // クォータニオン
        DirectX::XMFLOAT3 scale = { 1, 1, 1 };
    };

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

    static constexpr size_t MaxBoneCount = 256;
    // マテリアル 1 つが使うディスクリプタの数
    // CBV(マテリアル) + SRV(baseColor テクスチャ)
    static constexpr UINT DescriptorsPerMaterial = 2;

    // -- 描画に必要な情報 --
    const D3D12_VERTEX_BUFFER_VIEW& VertexBufferView() const { return _vbView; }
    const D3D12_INDEX_BUFFER_VIEW& IndexBufferView() const { return _ibView; }
    const std::vector<Primitive>& Primitives() const { return _primitives; }

    // -- マテリアル（アクターがビューを作るのに使う） --
    size_t MaterialCount() const { return _materials.size(); }
    ID3D12Resource* MaterialBuffer() const { return _materialBuff.Get(); }
    size_t MaterialStride() const { return _materialBuffSize; }   // 256 に揃えた 1 個分のサイズ
    ID3D12Resource* MaterialTexture(size_t i) const { return _materialTextures[i].Get(); }

    // -- スキンとアニメーション --
    const std::vector<Node>& Nodes() const { return _nodes; }              // バインドポーズ
    const std::vector<int>& JointNodes() const { return _jointNodes; }
    const std::vector<DirectX::XMMATRIX>& InverseBindMatrices() const { return _inverseBindMatrices; }
    const std::vector<int>& NodeOrder() const { return _nodeOrder; }
    const std::vector<Animation>& Animations() const { return _animations; }

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

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
    bool CreateMaterialBuffer();                        // 定数バッファを作る

    bool LoadMaterials(const cgltf_data* data);        // マテリアルとテクスチャを読む

    bool LoadNodesAndSkin(const cgltf_data* data);   // ノード階層とスキンを読む

    bool LoadAnimations(const cgltf_data* data);
    // 輪郭線の押し出し用に、同一座標の法線を平均して smoothNormal に格納する
    void BuildSmoothNormals(std::vector<Vertex>& vertices);

    Dx12Wrapper& _dx12;

    ComPtr<ID3D12Resource> _vertBuff;
    ComPtr<ID3D12Resource> _idxBuff;
    D3D12_VERTEX_BUFFER_VIEW _vbView = {};
    D3D12_INDEX_BUFFER_VIEW _ibView = {};

    std::vector<Primitive> _primitives;

    std::vector<MaterialForHlsl> _materials;
    std::vector<ComPtr<ID3D12Resource>> _materialTextures;   // マテリアルごとの baseColor
    ComPtr<ID3D12Resource> _materialBuff;
    size_t _materialBuffSize = 0;

    std::vector<Node> _nodes;                         // 全ノード
    std::vector<int> _jointNodes;                     // joint 添字 -> ノード添字
    std::vector<DirectX::XMMATRIX> _inverseBindMatrices;

    // 親が必ず先に来る順序を作る（ルートから深さ優先）
    // glTF のノード配列は親子の順序を保証しないため必要
    std::vector<int> _nodeOrder;

    std::vector<Animation> _animations;
};