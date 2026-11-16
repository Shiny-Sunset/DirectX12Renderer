#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <vector>
#include "Dx12Wrapper.h"

class FontAtlas;

class SpriteRenderer
{
public:
    explicit SpriteRenderer(Dx12Wrapper& dx12, std::vector<FontAtlas*> fonts);
    ~SpriteRenderer();

    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    bool Init();

    void Begin();

    void FillRect(float x, float y, float w, float h, const DirectX::XMFLOAT4& color);
    void PushQuad(float x, float y, float w, float h,
        DirectX::XMFLOAT2 uv0, DirectX::XMFLOAT2 uv1,
        const DirectX::XMFLOAT4& color, UINT texSlot);

    void Draw();
    void  DrawString(UINT fontId, float x, float y, const wchar_t* text,
        const DirectX::XMFLOAT4& color);
    void  DrawStringOutlined(UINT fontId, float x, float y, const wchar_t* text,
        const DirectX::XMFLOAT4& color, const DirectX::XMFLOAT4& outlineColor,
        float thickness = 1.0f);
    void  DrawStringCentered(UINT fontId, float centerX, float y, const wchar_t* text,
        const DirectX::XMFLOAT4& color, const DirectX::XMFLOAT4& outlineColor);
    float MeasureString(UINT fontId, const wchar_t* text) const;
    int   LineHeight(UINT fontId) const;

    // フォントを焼き直したら呼ぶ。アトラスのリソースが別物になるので、
    // SRV を作り直さないと解放済みのテクスチャを指したままになる
    bool RefreshFontViews() { return CreateSrvHeap(); }

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct SpriteVertex
    {
        DirectX::XMFLOAT2 pos;     // ピクセル座標。左上が (0, 0)
        DirectX::XMFLOAT2 uv;
        DirectX::XMFLOAT4 color;   // テクスチャに掛ける色
    };

    struct Batch
    {
        UINT texSlot;
        UINT startQuad;
        UINT quadCount;
    };
    UINT FontTexSlot(UINT fontId) const { return 1 + fontId; }
    static constexpr UINT TexWhite = 0;

    static constexpr UINT MaxQuads = 4096;
    static_assert(MaxQuads * 4 <= 65536,
        "頂点が 16bit インデックスに収まらない。MaxQuads を減らすか R32_UINT にする");

    bool CompileShaders();

    bool CreateVertexBuffer();
    bool CreateIndexBuffer();
    bool CreateSrvHeap();
    bool CreateRootSignature();
    bool CreatePipelineState();

    Dx12Wrapper& _dx12;
    std::vector<FontAtlas*> _fonts;

    ComPtr<ID3DBlob> _vsBlob;
    ComPtr<ID3DBlob> _psBlob;

    ComPtr<ID3D12RootSignature> _rootSignature;
    ComPtr<ID3D12PipelineState> _pipelineState;

    ComPtr<ID3D12DescriptorHeap> _srvHeap;

    ComPtr<ID3D12Resource> _vertBuff;    // UPLOAD。MaxQuads * 4 頂点 * FrameCount
    SpriteVertex* _mappedVertex = nullptr;   // マップしたまま
    UINT _vertexStride = 0;                  // 1 フレーム分のバイト数
    D3D12_VERTEX_BUFFER_VIEW _vbViews[Dx12Wrapper::FrameCount] = {};

    ComPtr<ID3D12Resource> _indexBuff;   // 静的。最初に 1 回書いて Unmap
    UINT _quadCount = 0;
    D3D12_INDEX_BUFFER_VIEW _ibView = {};

    std::vector<Batch> _batches;
};

// UI の色はツールで見た値(sRGB)で書きたいので、リニアへ直してから渡す
// バックバッファが *_SRGB なので、そのまま渡すと明るく出る
inline DirectX::XMFLOAT4 SrgbColor(int r, int g, int b, float a = 1.0f)
{
    auto toLinear = [](int c) {
        const float s = c / 255.0f;
        return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f);
        };
    return { toLinear(r), toLinear(g), toLinear(b), a };   // アルファは変換しない
}