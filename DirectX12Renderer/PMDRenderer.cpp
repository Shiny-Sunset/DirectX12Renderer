#include "PMDRenderer.h"

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

PMDRenderer::PMDRenderer(Dx12Wrapper& dx12)
	: _dx12(dx12)
{
}

bool PMDRenderer::Init()
{
	if (!CompileShaders()) return false;
	if (!CreateRootSignature()) return false;
	if (!CreateGraphicsPipeline()) return false;

	return true;
}

bool PMDRenderer::CompileShaders()
{
	ComPtr<ID3DBlob> errorBlob;

	// BasicVertexShader の設定
	auto result = D3DCompileFromFile(
		L"BasicVertexShader.hlsl",	// シェーダー名
		nullptr,	// defineは無し
		D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
		"BasicVS", "vs_5_0",	// 関数は BasicVS、対象シェーダーは vs_5_0
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
		0,
		&_vsBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "BasicVertexShader")) return false;

	errorBlob.Reset();

	// BasicPixelShaderの設定
	result = D3DCompileFromFile(
		L"BasicPixelShader.hlsl",	// シェーダー名
		nullptr,	// defineは無し
		D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
		"BasicPS", "ps_5_0",	// 関数は BasicPS、対象シェーダーは ps_5_0
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
		0,
		&_psBlob, &errorBlob	// エラー時は errorBlob にメッセージが入る
	);
	if (!CheckShaderResult(result, errorBlob.Get(), "BasicPixelShader")) return false;

	return true;
}

bool PMDRenderer::CreateRootSignature()
{
	// ディスクリプタレンジの作成
	D3D12_DESCRIPTOR_RANGE descTblRange[3] = {};

	// 定数用レジスター 0 番(行列)
	descTblRange[0].NumDescriptors = 2;	// 定数 2 つ
	descTblRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;	// 種別は定数
	descTblRange[0].BaseShaderRegister = 0;	// 0 番スロットから
	descTblRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// 定数用レジスター 2 番(マテリアル)
	descTblRange[1].NumDescriptors = 1;	// 定数 1 つ
	descTblRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;	// 種別は定数
	descTblRange[1].BaseShaderRegister = 2;	// 1 番スロットから
	descTblRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// テクスチャ用レジスター 0 番
	descTblRange[2].NumDescriptors = 4;	// テクスチャ 4 つ(テクスチャ、.sph、.spa、トゥーン)
	descTblRange[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	// 種別はテクスチャ
	descTblRange[2].BaseShaderRegister = 0;	// 0 番スロットから
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

	// 1 番: マテリアル(b1)とテクスチャ(t0〜t3)。マテリアルごとに付け替える
	rootparam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// ディスクリプタレンジの配列の先頭アドレス
	rootparam[1].DescriptorTable.pDescriptorRanges = &descTblRange[1];
	// ディスクリプタレンジ数
	rootparam[1].DescriptorTable.NumDescriptorRanges = 2;
	// すべてのシェーダーから見える
	rootparam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// サンプラーの作成
	D3D12_STATIC_SAMPLER_DESC samplerDesc[2] = {};

	samplerDesc[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;	// 横方向の繰り返し
	samplerDesc[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;	// 縦方向の繰り返し
	samplerDesc[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;	// 奥行きの繰り返し
	samplerDesc[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;	// ボーダーは黒
	samplerDesc[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;	// 線形補間しない
	samplerDesc[0].MaxLOD = D3D12_FLOAT32_MAX;	// ミップマップ最大値
	samplerDesc[0].MinLOD = 0.0f;	// ミップマップ最小値
	samplerDesc[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;	// ピクセルシェーダーから見える
	samplerDesc[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;	// リサンプリングしない
	samplerDesc[0].ShaderRegister = 0;	// シェーダースロット番号を忘れずに

	samplerDesc[1] = samplerDesc[0];	// 変更点以外をコピー
	samplerDesc[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;	// 横方向に繰り返さない
	samplerDesc[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;	// 縦方向に繰り返さない
	samplerDesc[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;	// 奥行きに繰り返さない
	samplerDesc[1].ShaderRegister = 1;	// シェーダースロット番号を忘れずに

	// ルートシグネチャの作成
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = &rootparam[0];	// ルートパラメーターの先頭アドレス
	rootSignatureDesc.NumParameters = 2;	// ルートパラメーター数
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

bool PMDRenderer::CreateGraphicsPipeline()
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
			// ボーン番号
			"BONE_NO", 0, DXGI_FORMAT_R16G16_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			// ボーンの重み
			"WEIGHT", 0, DXGI_FORMAT_R8_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			// 輪郭線フラグ
			"EDGE_FLG", 0, DXGI_FORMAT_R8_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
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
	gpipeline.BlendState.AlphaToCoverageEnable = true;
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
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;	// 0 〜 1 に正規化された RGBA
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

void PMDRenderer::BeforeDraw()
{
	auto cmdList = _dx12.CommandList();

	// パイプラインステートの設定
	cmdList->SetPipelineState(_pipelineState.Get());

	// ルートシグネチャの設定
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	// プリミティブトポロジの設定
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
