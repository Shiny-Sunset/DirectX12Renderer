#include "GltfRenderer.h"

#include "Dx12Wrapper.h"
#include "Util.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace
{
	// シェーダーのコンパイル結果を検査し、失敗ならエラー内容を出力する
	// @param result D3DCompileFromFile の戻り値
	// @param errorBlob エラーメッセージが入る blob(null のこともある)
	// @param what 処理名
	// @return 成功したら true
	bool CheckShaderResult(HRESULT result, ID3DBlob* errorBlob, const char* what)
	{
		if (SUCCEEDED(result))
		{
#ifdef _DEBUG
			std::cout << what << " is OK" << std::endl;
#endif // _DEBUG
			return true;
		}

		if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			std::cout << what << ": ファイルが見つかりません" << std::endl;
			::OutputDebugStringA("ファイルが見つかりません");
			return false;
		}

		std::cout << what << " is Failed" << std::endl;

		// エラー内容が取れないこともあるので、必ず null チェックしてから読む
		if (errorBlob != nullptr)
		{
			std::string errstr;
			errstr.resize(errorBlob->GetBufferSize());

			std::copy_n(
				(char*)errorBlob->GetBufferPointer(),
				errorBlob->GetBufferSize(),
				errstr.begin()
			);
			errstr += "\n";

			std::cout << errstr;
			::OutputDebugStringA(errstr.c_str());
		}
		return false;
	}
}

GltfRenderer::GltfRenderer(Dx12Wrapper& dx12)
	: _dx12(dx12)
{
}

bool GltfRenderer::Init()
{
	if (!CompileShaders()) return false;
	if (!CreateRootSignature()) return false;
	if (!CreateGraphicsPipeline()) return false;

	return true;
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
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
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
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
		0,
		&_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "GltfPixelShader")) return false;

	return true;
}

bool GltfRenderer::CreateRootSignature()
{
	// ディスクリプタレンジの作成
	D3D12_DESCRIPTOR_RANGE descTblRange[3] = {};

	// b0: シーン行列
	// b1: ボーン行列
	descTblRange[0].NumDescriptors = 2;
	descTblRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descTblRange[0].BaseShaderRegister = 0;
	descTblRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// b2: マテリアル
	descTblRange[1].NumDescriptors = 1;
	descTblRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descTblRange[1].BaseShaderRegister = 2;
	descTblRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// t0: baseColor テクスチャ
	descTblRange[2].NumDescriptors = 1;
	descTblRange[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descTblRange[2].BaseShaderRegister = 0;
	descTblRange[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメーター(ディスクリプタテーブル)の作成
	D3D12_ROOT_PARAMETER rootparam[2] = {};

	// 0 番: 行列(b0)。描画中は変わらないので 1 回だけ設定する
	rootparam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// ディスクリプタレンジの配列の先頭アドレス
	rootparam[0].DescriptorTable.pDescriptorRanges = &descTblRange[0];
	// ディスクリプタレンジ数
	rootparam[0].DescriptorTable.NumDescriptorRanges = 1;
	// すべてのシェーダーから見える
	rootparam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	
	// 1 番: マテリアル(b2)とテクスチャ(t0)。マテリアルごとに付け替える
	rootparam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// ディスクリプタレンジの配列の先頭アドレス
	rootparam[1].DescriptorTable.pDescriptorRanges = &descTblRange[1];
	// ディスクリプタレンジ数
	rootparam[1].DescriptorTable.NumDescriptorRanges = 2;
	// すべてのシェーダーから見える
	rootparam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	

	// サンプラーの作成
	D3D12_STATIC_SAMPLER_DESC samplerDesc[1] = {};

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

	// ルートシグネチャの作成
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = &rootparam[0];	// ルートパラメーターの先頭アドレス
	rootSignatureDesc.NumParameters = 2;	// ルートパラメーター数
	rootSignatureDesc.pStaticSamplers = samplerDesc;
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
