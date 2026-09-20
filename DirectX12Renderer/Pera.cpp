#include "Pera.h"
#include "Util.h"
#include "Dx12Wrapper.h"
#include <d3dcompiler.h>

Pera::Pera(Dx12Wrapper& dx12)
    : _dx12(dx12)
{
}

bool Pera::Init()
{
    if (!CompileShaders()) return false;
    if (!CreateVertexBuffer()) return false;
    if (!CreateRootSignature()) return false;
    if (!CreatePipelineState()) return false;

    return true;
}

bool Pera::CompileShaders()
{
    ComPtr<ID3DBlob> errorBlob;

    // PeraVertexShader の設定
    auto result = D3DCompileFromFile(
        L"PeraVertexShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "PeraVS", "vs_5_0",	// 関数は PeraVS、対象シェーダーは vs_5_0
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
        0,
        &_vsBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "PeraVertexShader")) return false;

    errorBlob.Reset();

    // PeraPixelShaderの設定
    result = D3DCompileFromFile(
        L"PeraPixelShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "PeraPS", "ps_5_0",	// 関数は PeraPS、対象シェーダーは ps_5_0
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
        0,
        &_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "PeraPixelShader")) return false;

    return true;
}

void Pera::Draw()
{
    auto cmdList = _dx12.CommandList();

    cmdList->SetPipelineState(_pipelineState.Get());
    cmdList->SetGraphicsRootSignature(_rootSignature.Get());

    auto peraHeap = _dx12.PeraSrvHeap();
    ID3D12DescriptorHeap* heaps[] = { peraHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    // ルートパラメーター 0 番に、ヒープの先頭(= 1 パス目の描画結果)を割り当てる
    cmdList->SetGraphicsRootDescriptorTable(0, peraHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->IASetVertexBuffers(0, 1, &_vbView);
    cmdList->DrawInstanced(4, 1, 0, 0);
}

bool Pera::CreateVertexBuffer()
{
    PeraVertex pv[4] = {
        {{-1, -1, 0.1}, {0, 1}},
        {{-1,  1, 0.1}, {0, 0}},
        {{ 1, -1, 0.1}, {1, 1}},
        {{ 1,  1, 0.1}, {1, 0}}
    };

    auto device = _dx12.Device();

    // -- 頂点バッファの作成 --
    // 頂点ヒープの設定
    D3D12_HEAP_PROPERTIES heapprop = {};

    heapprop.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能(マップ可能)
    heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // リソースの設定
    D3D12_RESOURCE_DESC resdesc = {};

    resdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resdesc.Width = sizeof(pv);	// 頂点情報が入るだけのサイズ
    resdesc.Height = 1;
    resdesc.DepthOrArraySize = 1;
    resdesc.MipLevels = 1;
    resdesc.Format = DXGI_FORMAT_UNKNOWN;
    resdesc.SampleDesc.Count = 1;
    resdesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto result = device->CreateCommittedResource(
        &heapprop,
        D3D12_HEAP_FLAG_NONE,
        &resdesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&_vertBuff)
    );
    if (!CheckResult(result, "CreateCommittedResource _vertBuff")) return false;

    // 頂点情報のコピー(マップ)
    PeraVertex* vertMap = nullptr;
    result = _vertBuff->Map(0, nullptr, (void**)&vertMap);
    if (!CheckResult(result, "_vertBuff->Map")) return false;

    std::copy(std::begin(pv), std::end(pv), vertMap);
    _vertBuff->Unmap(0, nullptr);	// マップの解除

    // 頂点バッファビューの作成
    // バッファ全体を「何バイトごとの頂点の列」として解釈する
    _vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress();	// バッファの仮想アドレス
    _vbView.SizeInBytes = sizeof(pv);	// 全体のバイト数
    _vbView.StrideInBytes = sizeof(PeraVertex);	// 1頂点あたりのバイト数

    return true;
}

bool Pera::CreateRootSignature()
{
    // ディスクリプタレンジの作成
    D3D12_DESCRIPTOR_RANGE descTblRange = {};

    // t0: テクスチャ
    descTblRange.NumDescriptors = 1;
    descTblRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descTblRange.BaseShaderRegister = 0;
    descTblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ルートパラメーター(ディスクリプタテーブル)の作成
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam.DescriptorTable.pDescriptorRanges = &descTblRange;
    rootParam.DescriptorTable.NumDescriptorRanges = 1;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
    rootSignatureDesc.pParameters = &rootParam;	// ルートパラメーターの先頭アドレス
    rootSignatureDesc.NumParameters = 1;	// ルートパラメーター数
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

bool Pera::CreatePipelineState()
{
    // -- 頂点レイアウト(インプットレイアウト)の作成 --
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
            // 座標情報
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            // uv
            "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
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
    gpipeline.DepthStencilState.DepthEnable = false;	// 深度バッファを使う

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
    renderTargetBlendDesc.BlendEnable = false;
    renderTargetBlendDesc.LogicOpEnable = false;
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
