#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include "Collision.h"
#include <vector>

class Dx12Wrapper;
class GltfRenderer;
class CharacterController;

class Stage
{
public:
    Stage(Dx12Wrapper& dx12, GltfRenderer& renderer);

    bool Init();
    void Draw();
    void DrawShadow();

    // キャラクターをステージと衝突させて押し戻す
    void Resolve(CharacterController& controller) const;

    // 指定した位置の真下にある足場の高さを返す
    // @param x, z 調べる水平位置（キャラクターの足元）
    // @param maxY この高さ以下の足場だけを対象にする（頭上の足場を拾わないため）
    // @return 一番高い足場の上面。何も無ければ 0（地面）
    float GroundHeightAt(float x, float z, float maxY) const;

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CompileShaders();
    bool CreateVertexAndIndexBuffer();
    bool CreateRootSignature();
    bool CreatePipelineState();

    DirectX::XMMATRIX BoxWorldMatrix(const AABB& b) const;

    struct Box
    {
        AABB bounds;
    };

    struct Vertex
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 normal;
    };

    Dx12Wrapper& _dx12;
    GltfRenderer& _renderer;

    std::vector<Box> _boxes;   // 足場。描画にも当たり判定にもこの 1 つを使う

    ComPtr<ID3DBlob> _vsBlob;
    ComPtr<ID3DBlob> _psBlob;
    ComPtr<ID3DBlob> _shadowVsBlob;

    ComPtr<ID3D12Resource> _vertBuff;
    D3D12_VERTEX_BUFFER_VIEW _vbView = {};
    ComPtr<ID3D12Resource> _idxBuff;
    D3D12_INDEX_BUFFER_VIEW _ibView = {};
    ComPtr<ID3D12RootSignature> _rootSignature;
    ComPtr<ID3D12PipelineState> _pipelineState;
    ComPtr<ID3D12PipelineState> _shadowPipelineState;
};