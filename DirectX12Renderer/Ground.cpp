#include "Ground.h"
#include "Util.h"
#include "Dx12Wrapper.h"
#include <d3dcompiler.h>

Ground::Ground(Dx12Wrapper& dx12)
    : _dx12(dx12)
{
}

bool Ground::Init()
{
    if (!CompileShaders()) return false;
    if (!CreateVertexBuffer()) return false;
    if (!CreateRootSignature()) return false;
    if (!CreatePipelineState()) return false;

    return true;
}

bool Ground::CompileShaders()
{
    ComPtr<ID3DBlob> errorBlob;

    // GroundVertexShader の設定
    auto result = D3DCompileFromFile(
        L"GroundVertexShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "GroundVS", "vs_5_0",	// 関数は GltfVS、対象シェーダーは vs_5_0
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
        0,
        &_vsBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "GroundVertexShader")) return false;

    errorBlob.Reset();

    // GroundPixelShaderの設定
    result = D3DCompileFromFile(
        L"GroundPixelShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "GroundPS", "ps_5_0",	// 関数は GltfPS、対象シェーダーは ps_5_0
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
        0,
        &_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "GroundPixelShader")) return false;

    return true;
}

void Ground::Draw()
{
    auto cmdList = _dx12.CommandList();

    cmdList->SetPipelineState(_pipelineState.Get());
    cmdList->SetGraphicsRootSignature(_rootSignature.Get());

    // ヒープを介さず、定数バッファのアドレスを直接渡す
    cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->IASetVertexBuffers(0, 1, &_vbView);
    cmdList->DrawInstanced(4, 1, 0, 0);
}

bool Ground::CreateVertexBuffer()
{
    constexpr float half = 50.0f * 0.5f;   // GroundSize = 50.0f
    const Vertex vertices[] = {
            {{ -half, 0.0f, -half }},
            {{ -half, 0.0f,  half }},
            {{  half, 0.0f, -half }},
            {{  half, 0.0f,  half }},
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
    resdesc.Width = sizeof(vertices);	// 頂点情報が入るだけのサイズ
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
    Vertex* vertMap = nullptr;
    result = _vertBuff->Map(0, nullptr, (void**)&vertMap);
    if (!CheckResult(result, "_vertBuff->Map")) return false;

    std::copy(std::begin(vertices), std::end(vertices), vertMap);
    _vertBuff->Unmap(0, nullptr);	// マップの解除

    // 頂点バッファビューの作成
    // バッファ全体を「何バイトごとの頂点の列」として解釈する
    _vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress();	// バッファの仮想アドレス
    _vbView.SizeInBytes = sizeof(vertices);	// 全体のバイト数
    _vbView.StrideInBytes = sizeof(Vertex);	// 1頂点あたりのバイト数

    return true;
}

bool Ground::CreateRootSignature()
{
    // ルートパラメーター(ディスクリプタテーブル)の作成
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   // ディスクリプタテーブルではない
    rootParam.Descriptor.ShaderRegister = 0;                   // b0
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // ルートシグネチャの作成
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = &rootParam;	// ルートパラメーターの先頭アドレス
    rootSignatureDesc.NumParameters = 1;	// ルートパラメーター数
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.NumStaticSamplers = 0;

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

bool Ground::CreatePipelineState()
{
    // -- 頂点レイアウト(インプットレイアウト)の作成 --
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
            // 座標情報
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
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
    gpipeline.DepthStencilState.DepthEnable = true;	// 深度バッファを使う
    gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;	// 書き込む
    gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;	// 小さい方を使う

    gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;

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
