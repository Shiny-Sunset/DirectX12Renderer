#include "GltfRenderer.h"
#include "Dx12Wrapper.h"
#include "Util.h"
#include "GltfModel.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

GltfRenderer::GltfRenderer(Dx12Wrapper& dx12)
	: _dx12(dx12)
{
}

bool GltfRenderer::Init()
{
	if (!CompileShaders()) return false;
	if (!CreateRootSignature()) return false;
	if (!CreateShadowRootSignature()) return false;
	if (!CreateIdentityVisible()) return false;
	if (!CreateGraphicsPipeline()) return false;

	return true;
}

void GltfRenderer::SetModelHeap(const GltfModel& model)
{
	ID3D12DescriptorHeap* heaps[] = { model.DescriptorHeap() };
	_dx12.CommandList()->SetDescriptorHeaps(1, heaps);
}

bool GltfRenderer::CompileShaders()
{
	ComPtr<ID3DBlob> errorBlob;

	// GltfVertexShader の設定
	auto result = D3DCompileFromFile(
		L"GltfVertexShader.hlsl",	// シェーダー名
		nullptr,	// defineは無し
		D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
		"GltfVS", "vs_5_0",	// 関数は GltfVS、対象シェーダーは vs_5_0
		ShaderCompileFlags,
		0,
		&_vsBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "GltfVertexShader")) return false;

	errorBlob.Reset();

	// GltfPixelShaderの設定
	result = D3DCompileFromFile(
		L"GltfPixelShader.hlsl",	// シェーダー名
		nullptr,	// defineは無し
		D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
		"GltfPS", "ps_5_0",	// 関数は GltfPS、対象シェーダーは ps_5_0
		ShaderCompileFlags,
		0,
		&_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "GltfPixelShader")) return false;

	errorBlob.Reset();

	// GltfOutlineVertexShaderの設定
	result = D3DCompileFromFile(
		L"GltfOutlineVertexShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"GltfOutlineVS", "vs_5_0",
		ShaderCompileFlags, 0,
		&_outlineVsBlob, &errorBlob
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "GltfOutlineVertexShader")) return false;

	errorBlob.Reset();

	// GltfOutlinePixelShaderの設定
	result = D3DCompileFromFile(
		L"GltfOutlinePixelShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"GltfOutlinePS", "ps_5_0",
		ShaderCompileFlags, 0,
		&_outlinePsBlob, &errorBlob
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "GltfOutlinePixelShader")) return false;

	errorBlob.Reset();

	// GltfShadowVertexShaderの設定
	result = D3DCompileFromFile(
		L"GltfShadowVertexShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"ShadowVS", "vs_5_0",
		ShaderCompileFlags, 0,
		&_shadowVsBlob, &errorBlob
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "GltfShadowVertexShader")) return false;

	return true;
}

bool GltfRenderer::CreateRootSignature()
{
	// ディスクリプタレンジの作成
	D3D12_DESCRIPTOR_RANGE descTblRange[3] = {};

	// b2: マテリアル
	descTblRange[0].NumDescriptors = 1;
	descTblRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descTblRange[0].BaseShaderRegister = 2;	// b2
	descTblRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// t0: baseColor テクスチャ
	descTblRange[1].NumDescriptors = 1;
	descTblRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descTblRange[1].BaseShaderRegister = 0;	// t0
	descTblRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// t1: シャドウマップ
	descTblRange[2].NumDescriptors = 1;
	descTblRange[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descTblRange[2].BaseShaderRegister = 1;   // t1
	descTblRange[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメーター(ディスクリプタテーブル)の作成
	D3D12_ROOT_PARAMETER rootParam[5] = {};

	// 0 番: シーン定数(b0)
	rootParam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParam[0].Descriptor.ShaderRegister = 0;
	rootParam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// 1 番: インスタンスデータ(b1)
	rootParam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParam[1].Descriptor.ShaderRegister = 2;
	rootParam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// 2 番: マテリアル(b2)とテクスチャ(t0)。マテリアルごとに付け替える
	rootParam[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// ディスクリプタレンジの配列の先頭アドレス
	rootParam[2].DescriptorTable.pDescriptorRanges = &descTblRange[0];
	// ディスクリプタレンジ数
	rootParam[2].DescriptorTable.NumDescriptorRanges = 2;
	// すべてのシェーダーから見える
	rootParam[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 3 番: シャドウマップ(t1)
	rootParam[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParam[3].DescriptorTable.pDescriptorRanges = &descTblRange[2];
	rootParam[3].DescriptorTable.NumDescriptorRanges = 1;
	rootParam[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 4 番: ボーン行列(t3)
	rootParam[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParam[4].Descriptor.ShaderRegister = 3;
	rootParam[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	

	// サンプラーの作成
	D3D12_STATIC_SAMPLER_DESC samplerDesc[2] = {};

	samplerDesc[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;      // 横方向の繰り返し
	samplerDesc[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;      // 縦方向の繰り返し
	samplerDesc[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;      // 奥行きの繰り返し
	samplerDesc[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplerDesc[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;        // 線形補間する
	samplerDesc[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc[0].MinLOD = 0.0f;
	samplerDesc[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	samplerDesc[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samplerDesc[0].ShaderRegister = 0;

	samplerDesc[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	samplerDesc[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	samplerDesc[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	samplerDesc[1].MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	samplerDesc[1].ShaderRegister = 1;

	// ルートシグネチャの作成
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = &rootParam[0];	// ルートパラメーターの先頭アドレス
	rootSignatureDesc.NumParameters = 5;	// ルートパラメーター数
	rootSignatureDesc.pStaticSamplers = samplerDesc;
	rootSignatureDesc.NumStaticSamplers = 2;

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

bool GltfRenderer::CreateShadowRootSignature()
{
	// 影パスは深度しか書かない
	// ピクセルシェーダーが無いので、マテリアルもテクスチャもサンプラーも要らない
	// ディスクリプタテーブルが 1 つも無いため、ヒープの切り替えも不要になる
	D3D12_ROOT_PARAMETER rootParam[5] = {};

	// 0 番: シーン定数(b0)。lightCamera を読む
	rootParam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParam[0].Descriptor.ShaderRegister = 0;
	rootParam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// 1 番: インスタンスデータ(t2)
	rootParam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParam[1].Descriptor.ShaderRegister = 2;
	rootParam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// 2 番: ボーン行列(t3)
	rootParam[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParam[2].Descriptor.ShaderRegister = 3;
	rootParam[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// 3 番: カスケードの番号(b1)
	rootParam[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParam[3].Constants.ShaderRegister = 1;
	rootParam[3].Constants.RegisterSpace = 0;
	rootParam[3].Constants.Num32BitValues = 1;
	rootParam[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// 4 番: 可視リスト(t4)
	rootParam[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParam[4].Descriptor.ShaderRegister = 4;
	rootParam[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	// 頂点レイアウトは使う。それ以外のステージからのアクセスは明示的に拒否する
	rootSignatureDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	rootSignatureDesc.pParameters = rootParam;
	rootSignatureDesc.NumParameters = 5;
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
	if (!CheckShaderResult(result, errorBlob.Get(), "D3D12SerializeRootSignature (shadow)")) return false;

	// ルートシグネチャオブジェクトの作成
	result = _dx12.Device()->CreateRootSignature(
		0,
		rootSigBlob->GetBufferPointer(),
		rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&_shadowRootSignature)
	);
	if (!CheckResult(result, "CreateRootSignature (shadow)")) return false;

	return true;
}

bool GltfRenderer::CreateGraphicsPipeline()
{
	// -- 頂点レイアウト(インプットレイアウト)の作成 --
	// 1頂点のデータの中身を「どの部分が座標・UV・法線か」に分解し、シェーダー入力に結びつける
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{
			// 座標情報
			"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			// 法線
			"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			// 輪郭線の押し出し用の法線
			"SMOOTH_NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			// uv
			"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			// ジョイント
			"JOINTS", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			// ボーンの重み
			"WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
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

	D3D12_RENDER_TARGET_BLEND_DESC blendDesc = {};
	blendDesc.BlendEnable = true;
	blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;               // 描く色 × α
	blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;   // 背景 × (1-α)
	blendDesc.BlendOp = D3D12_BLEND_OP_ADD;                  // 足し合わせる
	blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.LogicOpEnable = false;
	blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	gpipeline.BlendState.RenderTarget[0] = blendDesc;

	// 半透明は深度を書き込まない(後ろのものが描けなくなるのを防ぐ)
	gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	result = _dx12.Device()->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_blendPipelineState)
	);
	if (!CheckResult(result, "CreateGraphicsPipelineState")) return false;

	// -- 輪郭線用 PSO --
	// シェーダーを差し替え、前面カリングで裏面だけを描く
	gpipeline.VS.pShaderBytecode = _outlineVsBlob->GetBufferPointer();
	gpipeline.VS.BytecodeLength = _outlineVsBlob->GetBufferSize();
	gpipeline.PS.pShaderBytecode = _outlinePsBlob->GetBufferPointer();
	gpipeline.PS.BytecodeLength = _outlinePsBlob->GetBufferSize();

	// 半透明用に変更した設定を戻す
	gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;      // BlendEnable = false のもの
	gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	// 押し出したメッシュの「裏面」だけを描く
	gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;

	result = _dx12.Device()->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_outlinePipelineState)
	);
	if (!CheckResult(result, "CreateGraphicsPipelineState (outline)")) return false;

	// -- 影用 --
	gpipeline.pRootSignature = _shadowRootSignature.Get();

	gpipeline.VS.pShaderBytecode = _shadowVsBlob->GetBufferPointer();
	gpipeline.VS.BytecodeLength = _shadowVsBlob->GetBufferSize();

	// 色を出さないのでピクセルシェーダーは不要
	gpipeline.PS.pShaderBytecode = nullptr;
	gpipeline.PS.BytecodeLength = 0;

	gpipeline.NumRenderTargets = 0;
	gpipeline.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	result = _dx12.Device()->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_shadowPipelineState)
	);
	if (!CheckResult(result, "CreateGraphicsPipelineState (shadow)")) return false;

	return true;
}

bool GltfRenderer::CreateIdentityVisible()
{
	D3D12_HEAP_PROPERTIES heapProp = {};

	heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC resDesc = {};

	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.Width = sizeof(UINT);        // 1 要素だけ
	resDesc.Height = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.Format = DXGI_FORMAT_UNKNOWN;
	resDesc.SampleDesc.Count = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	auto result = _dx12.Device()->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_identityVisible)
	);
	if (!CheckResult(result, "CreateCommittedResource _identityVisible")) return false;

	UINT* mapped = nullptr;

	result = _identityVisible->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	if (!CheckResult(result, "_identityVisible->Map")) return false;

	mapped[0] = 0;                       // 0 番のインスタンスを指す
	_identityVisible->Unmap(0, nullptr); // 以後書き換えないので、マップしたままにしない

	return true;
}

void GltfRenderer::BeforeDraw()
{
	auto cmdList = _dx12.CommandList();

	// ルートシグネチャの設定
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	// プリミティブトポロジの設定
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void GltfRenderer::BeforeShadowDraw()
{
	auto cmdList = _dx12.CommandList();

	// 影パスは全アクターで PSO もルートシグネチャも同じなので、1 回だけ設定する
	cmdList->SetGraphicsRootSignature(_shadowRootSignature.Get());
	cmdList->SetPipelineState(_shadowPipelineState.Get());
	cmdList->SetGraphicsRoot32BitConstant(3, _dx12.CurrentCascade(), 0);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}