#include "SpriteRenderer.h"
#include "Dx12Wrapper.h"
#include "Util.h"
#include "FontAtlas.h"
#include <vector>
#include <iostream>

SpriteRenderer::SpriteRenderer(Dx12Wrapper& dx12, std::vector<FontAtlas*> fonts): _dx12(dx12), _fonts(fonts)
{
}

SpriteRenderer::~SpriteRenderer()
{
    if (_mappedVertex != nullptr)
    {
        _vertBuff->Unmap(0, nullptr);
        _mappedVertex = nullptr;
    }
}

bool SpriteRenderer::Init()
{
    if (!CompileShaders()) return false;
    if (!CreateVertexBuffer()) return false;
    if (!CreateIndexBuffer()) return false;
    if (!CreateSrvHeap()) return false;
    if (!CreateRootSignature()) return false;
    if (!CreatePipelineState()) return false;

    return true;
}

bool SpriteRenderer::CompileShaders()
{
    ComPtr<ID3DBlob> errorBlob;

    // SpriteVertexShader の設定
    auto result = D3DCompileFromFile(
        L"SpriteVertexShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "SpriteVS", "vs_5_0",	// 関数は SpriteVS、対象シェーダーは vs_5_0
        ShaderCompileFlags,
        0,
        &_vsBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "SpriteVertexShader")) return false;

    errorBlob.Reset();

    // SpritePixelShaderの設定
    result = D3DCompileFromFile(
        L"SpritePixelShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "SpritePS", "ps_5_0",	// 関数は SpritePS、対象シェーダーは ps_5_0
        ShaderCompileFlags,
        0,
        &_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "SpritePixelShader")) return false;

    return true;
}

void SpriteRenderer::Begin()
{
    _quadCount = 0;
    _batches.clear();
}

void SpriteRenderer::FillRect(float x, float y, float w, float h,
    const DirectX::XMFLOAT4& color)
{
    // 白テクスチャ全体を貼るので UV は (0,0)-(1,1)
    PushQuad(x, y, w, h, { 0, 0 }, { 1, 1 }, color, TexWhite);
}

void SpriteRenderer::PushQuad(float x, float y, float w, float h,
    DirectX::XMFLOAT2 uv0, DirectX::XMFLOAT2 uv1,
    const DirectX::XMFLOAT4& color, UINT texSlot)
{
    if (_quadCount >= MaxQuads) return;   // 溢れたら捨てる

    // 直前と違うテクスチャなら、新しいバッチを始める
    if (_batches.empty() || _batches.back().texSlot != texSlot)
    {
        _batches.push_back({ texSlot, _quadCount, 0 });
    }

    auto* v = _mappedVertex + _dx12.FrameIndex() * MaxQuads * 4 + _quadCount * 4;

    v[0] = { { x,     y     }, { uv0.x, uv0.y }, color };   // 左上
    v[1] = { { x + w, y     }, { uv1.x, uv0.y }, color };   // 右上
    v[2] = { { x,     y + h }, { uv0.x, uv1.y }, color };   // 左下
    v[3] = { { x + w, y + h }, { uv1.x, uv1.y }, color };   // 右下

    ++_batches.back().quadCount;
    ++_quadCount;
}

void SpriteRenderer::DrawString(UINT fontId, float x, float y, const wchar_t* text, const DirectX::XMFLOAT4& color)
{
    float penX = x;
    const float baseline = y + _fonts[fontId]->Ascent();   // y は「行の上端」として扱う

    for (const wchar_t* p = text; *p != L'\0'; ++p)
    {
        const auto* g = _fonts[fontId]->Find(*p);
        if (g == nullptr)
        {
#ifdef _DEBUG
            std::cout << "not found text" << '\n';
#endif
            continue;      // charset に入れ忘れた文字
        }
        float adv = static_cast<float>(g->advance);
        float offset = 0.0f;

        // 数字だけは '0' の送り幅に揃える。桁が変わっても幅が動かない
        if (*p >= L'0' && *p <= L'9')
        {
            adv = static_cast<float>(_fonts[fontId]->DigitAdvance());
            offset = (adv - g->advance) * 0.5f;   // 字形を送り幅の真ん中へ寄せる
        }

        if (g->width > 0 && g->height > 0)   // 空白はビットマップが無い
        {
            PushQuad(penX + offset + g->bearingX, baseline - g->bearingY,
                static_cast<float>(g->width), static_cast<float>(g->height),
                { g->u0, g->v0 }, { g->u1, g->v1 }, color, FontTexSlot(fontId));
        }
        penX += adv;
    }
}

void SpriteRenderer::DrawStringOutlined(UINT fontId, float x, float y, const wchar_t* text,
    const DirectX::XMFLOAT4& color, const DirectX::XMFLOAT4& outlineColor, float thickness)
{
    // 縁を 4 方向へずらして描いてから、本体を上に重ねる
    // 斜め 4 方向も足すと角が埋まるが、四角の数が倍になるのでここでは上下左右だけ
    static const float dx[] = { -1.0f, 1.0f,  0.0f, 0.0f };
    static const float dy[] = { 0.0f, 0.0f, -1.0f, 1.0f };

    for (int i = 0; i < 4; ++i)
    {
        DrawString(fontId, x + dx[i] * thickness, y + dy[i] * thickness, text, outlineColor);
    }
    DrawString(fontId, x, y, text, color);
}

void SpriteRenderer::DrawStringCentered(UINT fontId, float centerX, float y,
    const wchar_t* text, const DirectX::XMFLOAT4& color, const DirectX::XMFLOAT4& outlineColor)
{
    const float w = MeasureString(fontId, text);
    // 位置を整数に丸める。半端な座標だと線形補間で文字がにじむ
    DrawStringOutlined(fontId, floorf(centerX - w * 0.5f), floorf(y), text, color, outlineColor);
}

void SpriteRenderer::Draw()
{
    if (_quadCount == 0) return;

    auto cmdList = _dx12.CommandList();

    cmdList->SetPipelineState(_pipelineState.Get());
    cmdList->SetGraphicsRootSignature(_rootSignature.Get());

    // 画面サイズをルート定数で渡す（float 2 個 = 32bit 値 2 個）
    const float screen[2] = { static_cast<float>(_dx12.WindowWidth()),
                              static_cast<float>(_dx12.WindowHeight()) };
    cmdList->SetGraphicsRoot32BitConstants(0, 2, screen, 0);

    ID3D12DescriptorHeap* heaps[] = { _srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &_vbViews[_dx12.FrameIndex()]);
    cmdList->IASetIndexBuffer(&_ibView);

    const UINT inc = _dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto base = _srvHeap->GetGPUDescriptorHandleForHeapStart();

    for (const auto& b : _batches)
    {
        auto handle = base;
        handle.ptr += static_cast<UINT64>(inc) * b.texSlot;
        cmdList->SetGraphicsRootDescriptorTable(1, handle);

        // インデックスは「何番目の四角から」で指す。頂点番号は絶対値なので BaseVertex は 0
        cmdList->DrawIndexedInstanced(b.quadCount * 6, 1, b.startQuad * 6, 0, 0);
    }
}

bool SpriteRenderer::CreateVertexBuffer()
{
    _vertexStride = MaxQuads * 4 * sizeof(SpriteVertex);   // 1 フレーム分

    // -- 頂点バッファの作成 --
    // 頂点ヒープの設定
    D3D12_HEAP_PROPERTIES heapProp = {};

    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能(マップ可能)
    heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // リソースの設定
    D3D12_RESOURCE_DESC resDesc = {};

    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = _vertexStride * Dx12Wrapper::FrameCount;	// 頂点情報が入るだけのサイズ
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto result = _dx12.Device()->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_vertBuff));
    if (!CheckResult(result, "CreateCommittedResource _vertBuff")) return false;

    // 毎フレーム書き換えるので、マップしたままにしておく(Unmap はデストラクタで行う)
    result = _vertBuff->Map(0, nullptr, reinterpret_cast<void**>(&_mappedVertex));
    if (!CheckResult(result, "_vertBuff->Map")) return false;

    // ビューはフレームごとに 1 本。違うのは先頭アドレスだけ
    for (UINT i = 0; i < Dx12Wrapper::FrameCount; ++i)
    {
        _vbViews[i].BufferLocation = _vertBuff->GetGPUVirtualAddress() + _vertexStride * i;
        _vbViews[i].SizeInBytes = _vertexStride;
        _vbViews[i].StrideInBytes = sizeof(SpriteVertex);
    }

    return true;
}

bool SpriteRenderer::CreateIndexBuffer()
{
    // 四角 i の頂点は i*4 + 0..3。並びは 0=左上 1=右上 2=左下 3=右下
    std::vector<uint16_t> indices(MaxQuads * 6);
    for (UINT i = 0; i < MaxQuads; ++i)
    {
        const auto base = static_cast<uint16_t>(i * 4);
        uint16_t* dst = &indices[i * 6];

        dst[0] = base + 0; dst[1] = base + 1; dst[2] = base + 2;   // 左上 右上 左下
        dst[3] = base + 2; dst[4] = base + 1; dst[5] = base + 3;   // 左下 右上 右下
    }

    const UINT size = static_cast<UINT>(indices.size() * sizeof(uint16_t));

    D3D12_HEAP_PROPERTIES heapProp = {};

    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;     // CPUからアクセス可能(マップ可能)
    heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // リソースの設定
    D3D12_RESOURCE_DESC resDesc = {};

    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = size;	// 頂点情報が入るだけのサイズ
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto result = _dx12.Device()->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_indexBuff));
    if (!CheckResult(result, "CreateCommittedResource _indexBuff")) return false;

    uint16_t* map = nullptr;
    result = _indexBuff->Map(0, nullptr, reinterpret_cast<void**>(&map));
    if (!CheckResult(result, "_indexBuff->Map")) return false;

    std::copy(indices.begin(), indices.end(), map);
    _indexBuff->Unmap(0, nullptr);   // 中身は二度と変わらないので、マップしたままにしない

    _ibView.BufferLocation = _indexBuff->GetGPUVirtualAddress();
    _ibView.Format = DXGI_FORMAT_R16_UINT;   // 頂点は 8192 本なので 16bit で足りる
    _ibView.SizeInBytes = size;

    return true;
}

bool SpriteRenderer::CreateSrvHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 1 + static_cast<UINT>(_fonts.size());
    heapDesc.NodeMask = 0;

    auto result = _dx12.Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap));
    if (!CheckResult(result, "CreateDescriptorHeap _srvHeap")) return false;

    const UINT inc = _dx12.Device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = _srvHeap->GetCPUDescriptorHandleForHeapStart();

    // 0 番: 白テクスチャ。そのまま読む
    {
        auto* tex = _dx12.WhiteTexture();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        srvDesc.Format = tex->GetDesc().Format;   // リソースと食い違うと実行時エラー
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        _dx12.Device()->CreateShaderResourceView(tex, &srvDesc, handle);
    }

    // 1 番以降: フォントアトラス。R しか無いので、RGB は 1 に固定して A へ R を持ってくる
    for (UINT i = 0; i < _fonts.size(); ++i)
    {
        handle.ptr += inc;

        auto* tex = _fonts[i]->Texture();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        srvDesc.Format = tex->GetDesc().Format;   // DXGI_FORMAT_R8_UNORM
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
            0);
        srvDesc.Texture2D.MipLevels = 1;

        _dx12.Device()->CreateShaderResourceView(tex, &srvDesc, handle);
    }

    return true;
}

bool SpriteRenderer::CreateRootSignature()
{
    // ディスクリプタレンジの作成
    D3D12_DESCRIPTOR_RANGE descTblRange = {};

    // t0: テクスチャ
    descTblRange.NumDescriptors = 1;
    descTblRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descTblRange.BaseShaderRegister = 0;
    descTblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ルートパラメーター(ディスクリプタテーブル)の作成
    D3D12_ROOT_PARAMETER rootParam[2] = {};

    // 0 番: 画面サイズ(b0)
    rootParam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam[0].Constants.ShaderRegister = 0;
    rootParam[0].Constants.Num32BitValues = 2;
    rootParam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // 1 番: テクスチャ(t0)
    rootParam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam[1].DescriptorTable.pDescriptorRanges = &descTblRange;
    rootParam[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // サンプラーの作成
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};

    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;        // 線形補間する
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.ShaderRegister = 0;

    // ルートシグネチャの作成
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = &rootParam[0];	// ルートパラメーターの先頭アドレス
    rootSignatureDesc.NumParameters = 2;	// ルートパラメーター数
    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.NumStaticSamplers = 1;

    // バイナリコードの作成
    ComPtr<ID3DBlob> rootSigBlob;
    ComPtr<ID3DBlob> errorBlob;
    auto result = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1_0,
        &rootSigBlob,
        &errorBlob
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "D3D12SerializeRootSignature")) return false;


    // ルートシグネチャオブジェクトの作成
    result = _dx12.Device()->CreateRootSignature(
        0,
        rootSigBlob->GetBufferPointer(),
        rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&_rootSignature)
    );
    if (!CheckResult(result, "CreateRootSignature")) return false;

    return true;
}

bool SpriteRenderer::CreatePipelineState()
{
    // -- 頂点レイアウト(インプットレイアウト)の作成 --
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
            // 座標情報
            "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            // uv
            "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
    };

    // -- グラフィックスパイプラインステートの作成 --
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
    // ルートシグネチャの設定
    gpipeline.pRootSignature = _rootSignature.Get();
    // シェーダーの設定
    gpipeline.VS.pShaderBytecode = _vsBlob->GetBufferPointer();
    gpipeline.VS.BytecodeLength = _vsBlob->GetBufferSize();
    gpipeline.PS.pShaderBytecode = _psBlob->GetBufferPointer();
    gpipeline.PS.BytecodeLength = _psBlob->GetBufferSize();

    // 深度バッファ関連の設定
    gpipeline.DepthStencilState.DepthEnable = false;

    gpipeline.DSVFormat = DXGI_FORMAT_UNKNOWN;

    // サンプルマスクの設定
    gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;	// デフォルトのサンプルマスクを表す定数
    // ラスタライザーステートの設定
    gpipeline.RasterizerState.MultisampleEnable = false;	// アンチエイリアシングは使わない
    gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;	// カリングしない
    gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;	// 中身を塗りつぶす
    gpipeline.RasterizerState.DepthClipEnable = true;	// 深度方向のクリッピングは有効に
    // ブレンドステートの設定
    gpipeline.BlendState.AlphaToCoverageEnable = false;
    gpipeline.BlendState.IndependentBlendEnable = false;

    D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
    renderTargetBlendDesc.BlendEnable = true;
    renderTargetBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    renderTargetBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    renderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    renderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    renderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    renderTargetBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    // = {} で 0 になったままだと「どの成分も書かない」になり、何も出なくなる
    renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;
    // 入力レイアウトの設定
    gpipeline.InputLayout.pInputElementDescs = inputLayout;	// レイアウトの先頭アドレス
    gpipeline.InputLayout.NumElements = _countof(inputLayout);	// レイアウトの配列の要素数

    gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;	// カット無し
    //三角形で構成
    gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // レンダーターゲットの設定
    gpipeline.NumRenderTargets = 1;	// 今回は1つ
    gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;	// 0 〜 1 に正規化された RGBA
    // アンチエイリアシングのためのサンプル数設定
    gpipeline.SampleDesc.Count = 1;	// サンプリングは1ピクセルにつき1
    gpipeline.SampleDesc.Quality = 0;	// クオリティは最低

    // グラフィックスパイプラインステートオブジェクトの作成
    auto result = _dx12.Device()->CreateGraphicsPipelineState(
        &gpipeline, IID_PPV_ARGS(&_pipelineState)
    );
    if (!CheckResult(result, "CreateGraphicsPipelineState")) return false;

    return true;
}

float SpriteRenderer::MeasureString(UINT fontId, const wchar_t* text) const
{
    float w = 0.0f;
    for (const wchar_t* p = text; *p != L'\0'; ++p)
    {
        const auto* g = _fonts[fontId]->Find(*p);
        if (g == nullptr) continue;

        w += (*p >= L'0' && *p <= L'9')
            ? static_cast<float>(_fonts[fontId]->DigitAdvance())
            : static_cast<float>(g->advance);
    }
    return w;
}

int SpriteRenderer::LineHeight(UINT fontId) const
{
    return _fonts[fontId]->LineHeight();
}
