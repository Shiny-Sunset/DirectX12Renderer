#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>

class Dx12Wrapper;

class Pera
{
public:
    explicit Pera(Dx12Wrapper& dx12);
    ~Pera() = default;

    Pera(const Pera&) = delete;
    Pera& operator=(const Pera&) = delete;

    bool Init();
    void Draw();   // Dx12Wrapper::BeginBackBufferPass() と EndDraw の間で呼ぶ

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // シェーダーのコンパイル
    bool CompileShaders();

    struct PeraVertex
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT2 uv;
    };

    bool CreateVertexBuffer();
    bool CreateRootSignature();
    bool CreatePipelineState();

    Dx12Wrapper& _dx12;

    ComPtr<ID3DBlob> _vsBlob;
    ComPtr<ID3DBlob> _psBlob;

    ComPtr<ID3D12Resource> _vertBuff;
    D3D12_VERTEX_BUFFER_VIEW _vbView = {};
    ComPtr<ID3D12RootSignature> _rootSignature;
    ComPtr<ID3D12PipelineState> _pipelineState;
};