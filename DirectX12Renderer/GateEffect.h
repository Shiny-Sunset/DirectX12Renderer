#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <vector>

class Dx12Wrapper;

class GateEffect
{
public:
    explicit GateEffect(Dx12Wrapper& dx12);
    bool Init(const std::vector<DirectX::XMFLOAT3>& positions);
    void Draw();

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Vertex
    {
        DirectX::XMFLOAT2 pos;   // -1〜1。XZ 平面に寝かせるので 2 成分でいい
    };

    struct Gate { DirectX::XMFLOAT4 posRadius; };   // xyz = 位置, w = 半径

    // 四角 1 枚ぶん。XZ 平面に寝かせる
    bool CreateVertexAndIndexBuffer();
    bool CreateInstanceBuffer(const std::vector<DirectX::XMFLOAT3>& positions);
    bool CompileShaders();
    bool CreateRootSignature();   // b0 = シーン, t0 = インスタンス（ルート SRV）
    bool CreatePipelineState();

    Dx12Wrapper& _dx12;

    std::vector<Gate> _gates;

    ComPtr<ID3DBlob> _vsBlob;
    ComPtr<ID3DBlob> _psBlob;

    ComPtr<ID3D12Resource> _vertBuff;
    D3D12_VERTEX_BUFFER_VIEW _vbView = {};
    ComPtr<ID3D12Resource> _idxBuff;
    D3D12_INDEX_BUFFER_VIEW _ibView = {};
    ComPtr<ID3D12Resource> _instanceBuff;
    ComPtr<ID3D12RootSignature> _rootSignature;
    ComPtr<ID3D12PipelineState> _pipelineState;
};