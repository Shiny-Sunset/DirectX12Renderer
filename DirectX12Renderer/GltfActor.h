#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

class Dx12Wrapper;
class GltfRenderer;
struct cgltf_data;

// glTF(.glb) モデル 1 体分のデータと描画を受け持つクラス
class GltfActor
{
public:
    explicit GltfActor(Dx12Wrapper& dx12, GltfRenderer& _renderer);
    ~GltfActor();

    GltfActor(const GltfActor&) = delete;
    GltfActor& operator=(const GltfActor&) = delete;

    // .glb を読み込んで描画に必要なリソースを作る
    bool Init(const std::string& modelPath);

    // 描画（GltfRenderer::BeforeDraw() の後に呼ぶ）
    void Draw();
private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // GPU に送る頂点 1 つ分
    // (GltfRenderer のインプットレイアウトと並びを合わせること)
    struct Vertex
    {
        DirectX::XMFLOAT3 pos;      // POSITION
        DirectX::XMFLOAT3 normal;   // NORMAL
        DirectX::XMFLOAT2 uv;       // TEXCOORD_0
        uint8_t joints[4];          // JOINTS_0  
        DirectX::XMFLOAT4 weights;  // WEIGHTS_0 
    };      // 52 バイト

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
    DirectX::XMMATRIX* _mappedTransform = nullptr;    // マップしたまま保持

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