#include "GateEffect.h"
#include "Dx12Wrapper.h"
#include "Util.h"
#include <d3dcompiler.h>

GateEffect::GateEffect(Dx12Wrapper& dx12): _dx12(dx12)
{ }

bool GateEffect::Init(const std::vector<DirectX::XMFLOAT3>& positions)
{
    if (!CompileShaders()) return false;
    if (!CreateVertexAndIndexBuffer()) return false;
    if (!CreateInstanceBuffer(positions)) return false;
    if (!CreateRootSignature()) return false;
    if (!CreatePipelineState()) return false;

    return true;
}

bool GateEffect::CompileShaders()
{
    ComPtr<ID3DBlob> errorBlob;

    // GateVertexShader の設定
    auto result = D3DCompileFromFile(
        L"GateVertexShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "GateVS", "vs_5_0",	// 関数は GateVS、対象シェーダーは vs_5_0
        ShaderCompileFlags,
        0,
        &_vsBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "GateVertexShader")) return false;

    errorBlob.Reset();

    // GatePixelShaderの設定
    result = D3DCompileFromFile(
        L"GatePixelShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "GatePS", "ps_5_0",	// 関数は GatePS、対象シェーダーは ps_5_0
        ShaderCompileFlags,
        0,
        &_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "GatePixelShader")) return false;

    return true;
}

void GateEffect::Draw()
{
    auto cmdList = _dx12.CommandList();

    cmdList->SetPipelineState(_pipelineState.Get());
    cmdList->SetGraphicsRootSignature(_rootSignature.Get());

    // 0 番：シーン共通
    cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());

    cmdList->SetGraphicsRootShaderResourceView(1, _instanceBuff->GetGPUVirtualAddress());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &_vbView);
    cmdList->IASetIndexBuffer(&_ibView);

    cmdList->DrawIndexedInstanced(6, static_cast<UINT>(_gates.size()), 0, 0, 0);
}

bool GateEffect::CreateVertexAndIndexBuffer()
{
    const Vertex vertices[] = {
        {{-1.0f, -1.0f}}, {{-1.0f, 1.0f}}, {{ 1.0f, 1.0f}}, {{ 1.0f, -1.0f}},
    };
    // 上から見て時計回り
    const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
    auto device = _dx12.Device();

    // -- 頂点バッファの作成 --
    // 頂点ヒープの設定
    D3D12_HEAP_PROPERTIES heapProp = {};

    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能(マップ可能)
    heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // リソースの設定
    D3D12_RESOURCE_DESC resDesc = {};

    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = sizeof(vertices);	// 頂点情報が入るだけのサイズ
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto result = device->CreateCommittedResource(
        &heapProp,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
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


    // インデックスバッファの作成
    resDesc.Width = sizeof(indices);

    result = device->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&_idxBuff));
    if (!CheckResult(result, "CreateCommittedResource _idxBuff")) return false;

    // インデックスデータをコピー(マップ)
    uint16_t* mappedIdx = nullptr;
    result = _idxBuff->Map(0, nullptr, (void**)&mappedIdx);
    if (!CheckResult(result, "_idxBuff->Map")) return false;

    std::copy(std::begin(indices), std::end(indices), mappedIdx);
    _idxBuff->Unmap(0, nullptr);

    // インデックスバッファビューの作成
    _ibView.BufferLocation = _idxBuff->GetGPUVirtualAddress();
    _ibView.Format = DXGI_FORMAT_R16_UINT;   // uint16_t なのでこれ
    _ibView.SizeInBytes = sizeof(indices);

    return true;
}

bool GateEffect::CreateInstanceBuffer(const std::vector<DirectX::XMFLOAT3>& positions)
{

    constexpr float Radius = 2.2f;
    _gates.clear();
    for (const auto& p : positions)
    {
        _gates.push_back({ { p.x, p.y, p.z, Radius } });
    }
    if (_gates.empty()) return false;

    D3D12_HEAP_PROPERTIES heapProp = {};
    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = sizeof(Gate) * _gates.size();
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    auto result = _dx12.Device()->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&_instanceBuff));
    if (!CheckResult(result, "CreateCommittedResource _instanceBuff")) return false;

    Gate* mapped = nullptr;
    result = _instanceBuff->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (!CheckResult(result, "_instanceBuff->Map")) return false;

    std::copy(_gates.begin(), _gates.end(), mapped);
    _instanceBuff->Unmap(0, nullptr);

    return true;
}

bool GateEffect::CreateRootSignature()
{
    // ルートパラメーター(ディスクリプタテーブル)の作成
    D3D12_ROOT_PARAMETER rootParam[2] = {};

    // 0 番: シーンの定数バッファ(b0)。viewProj と time
    rootParam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam[0].Descriptor.ShaderRegister = 0;
    rootParam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // 1 番: ゲートごとの位置と半径(t0)。ルート SRV なのでヒープが要らない
    rootParam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParam[1].Descriptor.ShaderRegister = 0;
    rootParam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // ルートシグネチャの作成
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParam;	// ルートパラメーターの先頭アドレス
    rootSignatureDesc.NumParameters = 2;	// ルートパラメーター数
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

bool GateEffect::CreatePipelineState()
{
    // -- 頂点レイアウト(インプットレイアウト)の作成 --
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
            // 座標情報
            "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
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
    // 深度は読むが書かない
    gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    // サンプルマスクの設定
    gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;	// デフォルトのサンプルマスクを表す定数
    // ラスタライザーステートの設定
    gpipeline.RasterizerState.MultisampleEnable = false;	// アンチエイリアシングは使わない
    gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;	// 中身を塗りつぶす
    gpipeline.RasterizerState.DepthClipEnable = true;	// 深度方向のクリッピングは有効に
    // ブレンドステートの設定
    gpipeline.BlendState.AlphaToCoverageEnable = false;
    gpipeline.BlendState.IndependentBlendEnable = false;

    D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
    renderTargetBlendDesc.BlendEnable = true;
    renderTargetBlendDesc.LogicOpEnable = false;

    // 加算合成。result = 元の色 × a + 背景。光っているものはこれ
    renderTargetBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    renderTargetBlendDesc.DestBlend = D3D12_BLEND_ONE;
    renderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;

    // アルファ側は書き捨て。オフスクリーンの A を誰も読まないので何でもよい
    renderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    renderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_ONE;
    renderTargetBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;

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