#include "Stage.h"
#include "Dx12Wrapper.h"
#include "GltfRenderer.h"
#include "CharacterController.h"
#include "Util.h"
#include <d3dcompiler.h>

Stage::Stage(Dx12Wrapper& dx12, GltfRenderer& renderer) : _dx12(dx12), _renderer(renderer)
{
}

bool Stage::Init()
{
    _boxes = {
              {{ { -6.0f, 0.0f, 2.0f }, { -2.0f, 0.5f, 6.0f } }},   // 低い段
              {{ { -2.0f, 0.0f, 2.0f }, {  2.0f, 1.0f, 6.0f } }},   // 中くらいの段
              {{ {  2.0f, 0.0f, 2.0f }, {  6.0f, 1.8f, 6.0f } }},   // 高い段
    };

    if (!CompileShaders()) return false;
    if (!CreateVertexAndIndexBuffer()) return false;
    if (!CreateRootSignature()) return false;
    if (!CreatePipelineState()) return false;

    return true;
}

bool Stage::CompileShaders()
{
    ComPtr<ID3DBlob> errorBlob;

    // StageVertexShader の設定
    auto result = D3DCompileFromFile(
        L"StageVertexShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "StageVS", "vs_5_0",	// 関数は GroundVS、対象シェーダーは vs_5_0
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
        0,
        &_vsBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "StageVertexShader")) return false;

    errorBlob.Reset();

    // StagePixelShaderの設定
    result = D3DCompileFromFile(
        L"StagePixelShader.hlsl",	// シェーダー名
        nullptr,	// defineは無し
        D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
        "StagePS", "ps_5_0",	// 関数は GroundPS、対象シェーダーは ps_5_0
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
        0,
        &_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "StagePixelShader")) return false;

    // StageShadowVertexShaderの設定
    result = D3DCompileFromFile(
        L"StageShadowVertexShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "StageShadowVS", "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
        &_shadowVsBlob, &errorBlob
    );
    if (!CheckShaderResult(result, errorBlob.Get(), "StageShadowVertexShader")) return false;

    return true;
}

void Stage::Draw()
{
    auto cmdList = _dx12.CommandList();

    cmdList->SetPipelineState(_pipelineState.Get());
    cmdList->SetGraphicsRootSignature(_rootSignature.Get());

    // 0 番：シーン共通
    cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());

    // 2 番：シャドウマップ
    auto shadowHeap = _dx12.ShadowSrvHeap();
    ID3D12DescriptorHeap* heaps[] = { shadowHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootDescriptorTable(2, shadowHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &_vbView);
    cmdList->IASetIndexBuffer(&_ibView);

    // 箱ごとに、ワールド行列だけ差し替えて同じ立方体を描く
    for (const auto& box : _boxes)
    {
        // 単位キューブを、箱の大きさと位置に合わせて変形する行列
        auto world = BoxWorldMatrix(box.bounds);
        cmdList->SetGraphicsRoot32BitConstants(1, 16, &world, 0);
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);   // 立方体は三角形 12 枚
    }
}

void Stage::DrawShadow()
{
    auto cmdList = _dx12.CommandList();

    cmdList->SetPipelineState(_shadowPipelineState.Get());
    cmdList->SetGraphicsRootSignature(_rootSignature.Get());

    // 0 番だけ設定する。2 番(シャドウマップ)は、このパスでは読まないので不要
    cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &_vbView);
    cmdList->IASetIndexBuffer(&_ibView);

    for (const auto& box : _boxes)
    {
        auto world = BoxWorldMatrix(box.bounds);
        cmdList->SetGraphicsRoot32BitConstants(1, 16, &world, 0);
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);   // 立方体は三角形 12 枚
    }
}

bool Stage::CreateVertexAndIndexBuffer()
{
    constexpr float h = 0.5f;   // 1 辺 1 なので半分は 0.5

    const Vertex vertices[] = {
        // +Y（上面）
        {{-h, h,-h},{0,1,0}}, {{-h, h, h},{0,1,0}}, {{ h, h, h},{0,1,0}}, {{ h, h,-h},{0,1,0}},
        // -Y（底面）
        {{-h,-h, h},{0,-1,0}}, {{-h,-h,-h},{0,-1,0}}, {{ h,-h,-h},{0,-1,0}}, {{ h,-h, h},{0,-1,0}},
        // -Z（手前）
        {{-h,-h,-h},{0,0,-1}}, {{-h, h,-h},{0,0,-1}}, {{ h, h,-h},{0,0,-1}}, {{ h,-h,-h},{0,0,-1}},
        // +Z（奥）
        {{ h,-h, h},{0,0,1}}, {{ h, h, h},{0,0,1}}, {{-h, h, h},{0,0,1}}, {{-h,-h, h},{0,0,1}},
        // -X（左）
        {{-h,-h, h},{-1,0,0}}, {{-h, h, h},{-1,0,0}}, {{-h, h,-h},{-1,0,0}}, {{-h,-h,-h},{-1,0,0}},
        // +X（右）
        {{ h,-h,-h},{1,0,0}}, {{ h, h,-h},{1,0,0}}, {{ h, h, h},{1,0,0}}, {{ h,-h, h},{1,0,0}},
    };

    // 各面を 2 枚の三角形に分ける。時計回りが表（D3D の既定）
    uint16_t indices[36] = {};
    for (uint16_t f = 0; f < 6; ++f)
    {
        const uint16_t b = f * 4;
        const uint16_t tri[6] = { b, uint16_t(b + 1), uint16_t(b + 2),
                                  b, uint16_t(b + 2), uint16_t(b + 3) };
        memcpy(&indices[f * 6], tri, sizeof(tri));
    }

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


    // インデックスバッファの作成
    resdesc.Width = sizeof(indices);

    result = device->CreateCommittedResource(
        &heapprop, D3D12_HEAP_FLAG_NONE, &resdesc,
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

bool Stage::CreateRootSignature()
{
    auto cmdList = _dx12.CommandList();
    // ディスクリプタレンジの作成
    D3D12_DESCRIPTOR_RANGE descTblRange = {};

    // t0: シャドウマップ用テクスチャ
    descTblRange.NumDescriptors = 1;
    descTblRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descTblRange.BaseShaderRegister = 0;
    descTblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ルートパラメーター(ディスクリプタテーブル)の作成
    D3D12_ROOT_PARAMETER rootParam[3] = {};

    // 0 番: シーンの定数バッファ(b0)。view / proj / lightCamera
    rootParam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam[0].Descriptor.ShaderRegister = 0;
    rootParam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // 1 番: 箱ごとのワールド行列(b1)。定数バッファを作らず値を直接渡す
    rootParam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam[1].Constants.ShaderRegister = 1;
    rootParam[1].Constants.Num32BitValues = 16;
    rootParam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // 2 番: シャドウマップ(t0)
    rootParam[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam[2].DescriptorTable.pDescriptorRanges = &descTblRange;
    rootParam[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParam[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // サンプラーの作成
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};

    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplerDesc.ShaderRegister = 0;

    // ルートシグネチャの作成
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParam;	// ルートパラメーターの先頭アドレス
    rootSignatureDesc.NumParameters = 3;	// ルートパラメーター数
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

bool Stage::CreatePipelineState()
{
    // -- 頂点レイアウト(インプットレイアウト)の作成 --
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
            // 座標情報
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            // 法線（面の向き。陰影と、上面かどうかの判定に使う）
            "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
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
    gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
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

    // -- 影用 PSO --
      // ルートシグネチャは通常描画と同じものを使い回す
    gpipeline.VS.pShaderBytecode = _shadowVsBlob->GetBufferPointer();
    gpipeline.VS.BytecodeLength = _shadowVsBlob->GetBufferSize();

    // 色を書かないのでピクセルシェーダーは不要
    gpipeline.PS.pShaderBytecode = nullptr;
    gpipeline.PS.BytecodeLength = 0;

    gpipeline.NumRenderTargets = 0;
    gpipeline.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;   // 0 本なので UNKNOWN に戻す
    gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

    result = _dx12.Device()->CreateGraphicsPipelineState(
        &gpipeline, IID_PPV_ARGS(&_shadowPipelineState));
    if (!CheckResult(result, "CreateGraphicsPipelineState (stage shadow)")) return false;

    return true;
}


void Stage::Resolve(CharacterController& controller) const
{
    for (const auto& box : _boxes)
    {
        DirectX::XMFLOAT3 push;
        if (!ResolveSphereVsAABB(controller.BodySphere(), box.bounds, push)) continue;

        // 床に乗る処理は GroundHeightAt が担当するので、ここでは横だけ押し戻す
        controller.PushXZ(push.x, push.z);
    }
}

float Stage::GroundHeightAt(float x, float z, float maxY) const
{
    float best = 0.0f;   // 地面の高さ

    for (const auto& box : _boxes)
    {
        const auto& b = box.bounds;

        // 水平方向で箱の範囲に入っているか
        if (x < b.min.x || x > b.max.x) continue;
        if (z < b.min.z || z > b.max.z) continue;

        // 足元より上にある面は床にならない（頭上の足場を無視する）
        if (b.max.y > maxY) continue;

        best = std::max(best, b.max.y);   // 重なっていれば高い方を採用
    }
    return best;
}

DirectX::XMMATRIX Stage::BoxWorldMatrix(const AABB& b) const
{
    auto cmdList = _dx12.CommandList();
    // 単位キューブを、箱の大きさと位置に合わせて変形する行列
    auto world = DirectX::XMMatrixScaling(b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z)
        * DirectX::XMMatrixTranslation(
            (b.min.x + b.max.x) * 0.5f,
            (b.min.y + b.max.y) * 0.5f,
            (b.min.z + b.max.z) * 0.5f);
    return world;
}