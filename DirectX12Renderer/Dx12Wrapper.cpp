#include "Dx12Wrapper.h"

#include "Util.h"

#include <DirectXTex.h>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "DirectXTex.lib")

// .cpp の中だけで使う using 宣言なので、他の翻訳単位には影響しない
using Microsoft::WRL::ComPtr;

namespace
{
	// デバッグレイヤーを有効化する
	void EnableDebugLayer()
	{
		ComPtr<ID3D12Debug> debugLayer;
		if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer))))
		{
			std::cout << "D3D12GetDebugInterface is Failed" << std::endl;
			return;
		}
		debugLayer->EnableDebugLayer();	// デバッグレイヤーを有効化する
		// ComPtr なのでスコープを抜けるときに自動で解放される
	}

	// WriteToSubresource で転送する用のヒープ設定を返す
	D3D12_HEAP_PROPERTIES TextureHeapProperties()
	{
		D3D12_HEAP_PROPERTIES texHeapProp = {};
		texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
		texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
		texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
		texHeapProp.CreationNodeMask = 0;	// 単一アダプターのため 0
		texHeapProp.VisibleNodeMask = 0;	// 単一アダプターのため 0
		return texHeapProp;
	}

	// 単色などの小さな 2D テクスチャ用のリソース設定を返す
	D3D12_RESOURCE_DESC TextureResourceDesc(UINT64 width, UINT height)
	{
		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		resDesc.Width = width;	// 幅
		resDesc.Height = height;	// 高さ
		resDesc.DepthOrArraySize = 1;	// 2D で配列でもないので 1
		resDesc.SampleDesc.Count = 1;	// 通常テクスチャなのでアンチエイリアシングしない
		resDesc.SampleDesc.Quality = 0;	// クオリティは最低
		resDesc.MipLevels = 1;	// ミップマップしないのでミップ数は 1 つ
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;	// レイアウトは決定しない
		resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		return resDesc;
	}
}

Dx12Wrapper::~Dx12Wrapper()
{
	if (_mappedScene != nullptr)
	{
		_sceneConstBuff->Unmap(0, nullptr);
		_mappedScene = nullptr;
	}
}

bool Dx12Wrapper::Init(HWND hwnd, int windowWidth, int windowHeight)
{
	_windowWidth = windowWidth;
	_windowHeight = windowHeight;

#ifdef _DEBUG
	// デバッグレイヤーを有効化
	EnableDebugLayer();
#endif // _DEBUG

	if (!InitializeDXGIDevice()) return false;
	if (!InitializeCommand()) return false;
	if (!CreateSwapChain(hwnd)) return false;
	if (!CreateFinalRenderTargets()) return false;
	if (!CreatePeraResources()) return false;
	if (!CreateShadowMap()) return false;
	if (!CreateDepthBuffer()) return false;
	if (!CreateSceneConstantBuffer()) return false;
	if (!CreateDefaultTextures()) return false;

	// ビューポートの作成
	_viewport.Width = static_cast<float>(_windowWidth);	// 出力先の幅(ピクセル数)
	_viewport.Height = static_cast<float>(_windowHeight);	// 出力先の高さ(ピクセル数)
	_viewport.TopLeftX = 0;	// 出力先の左上座標X
	_viewport.TopLeftY = 0;	// 出力先の左上座標Y
	_viewport.MaxDepth = 1.0f;	// 深度最大値
	_viewport.MinDepth = 0.0f;	// 深度最小値

	// シザー矩形
	_scissorrect.top = 0;	// 切り抜き上座標
	_scissorrect.left = 0;	// 切り抜き左座標
	_scissorrect.right = _scissorrect.left + _windowWidth;	// 切り抜き右座標
	_scissorrect.bottom = _scissorrect.top + _windowHeight;	// 切り抜き下座標

	// リソースバリアの作成
	_barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;	// 遷移
	_barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;	// 指定なし
	_barrierDesc.Transition.Subresource = 0;

	return true;
}

bool Dx12Wrapper::InitializeDXGIDevice()
{
	// -- DXGIの初期化 --
#ifdef _DEBUG
	auto result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgiFactory));
#else
	auto result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
#endif // _DEBUG
	if (!CheckResult(result, "CreateDXGIFactory")) return false;

	// -- DirectX3D デバイスの初期化 --
	// 試そうとする機能レベル(上から順に対応しているか調べる)
	D3D_FEATURE_LEVEL levels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	for (auto lv : levels)
	{
		// アダプターに nullptr を渡すと既定のアダプターが使われる
		if (D3D12CreateDevice(nullptr, lv, IID_PPV_ARGS(&_dev)) == S_OK)
		{
			break; // 作成可能なバージョンが見つかったら打ち切り
		}
	}
	if (_dev == nullptr)
	{
		std::cout << "D3D12CreateDevice is Failed" << std::endl;
		return false;
	}
#ifdef _DEBUG
	std::cout << "D3D12CreateDevice is OK" << std::endl;
#endif // _DEBUG

	return true;
}

bool Dx12Wrapper::InitializeCommand()
{
	// -- コマンドリスト関連の初期化 --
	auto result = _dev->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&_cmdAllocator)
	);
	if (!CheckResult(result, "CreateCommandAllocator")) return false;

	result = _dev->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		_cmdAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&_cmdList)
	);
	if (!CheckResult(result, "CreateCommandList")) return false;

	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};

	// タイムアウト無し
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	// アダプターを1つしか使わない時は0でよい
	cmdQueueDesc.NodeMask = 0;

	// プライオリティは特に指定なし
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

	// コマンドリストと合わせる
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	// キュー作成
	result = _dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&_cmdQueue));
	if (!CheckResult(result, "CreateCommandQueue")) return false;

	// -- フェンスの作成 --
	result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
	if (!CheckResult(result, "CreateFence")) return false;

	return true;
}

bool Dx12Wrapper::CreateSwapChain(HWND hwnd)
{
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};

	swapchainDesc.Width = _windowWidth;
	swapchainDesc.Height = _windowHeight;
	swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.Stereo = false;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
	swapchainDesc.BufferCount = 2;

	// バックバッファは伸び縮み可能
	swapchainDesc.Scaling = DXGI_SCALING_STRETCH;

	// フリップ後は速やかに破棄
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	// 特に指定なし
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	// ウインドウとフルスクリーンの切り替えが可能
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ComPtr<IDXGISwapChain1> swapchain1;
	auto result = _dxgiFactory->CreateSwapChainForHwnd(
		_cmdQueue.Get(),
		hwnd,
		&swapchainDesc,
		nullptr,
		nullptr,
		&swapchain1
	);
	if (!CheckResult(result, "CreateSwapChainForHwnd")) return false;

	// IDXGISwapChain4 への変換は ComPtr::As(QueryInterface)で行う
	result = swapchain1.As(&_swapchain);
	if (!CheckResult(result, "IDXGISwapChain4 QueryInterface")) return false;

	return true;
}

bool Dx12Wrapper::CreateFinalRenderTargets()
{
	// -- ディスクリプタヒープの作成 --
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};

	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;		// レンダーターゲットビューなのでRTV
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;						// 表裏の2つ
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;	// 特に指定なし

	auto result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_rtvHeaps));
	if (!CheckResult(result, "CreateDescriptorHeap _rtvHeaps")) return false;

	// SRGB レンダーターゲットビューの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};

	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;	// ガンマ補正アリ (sRGB)
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// -- スワップチェーンのメモリと紐づけ --
	DXGI_SWAP_CHAIN_DESC swcDesc = {};

	result = _swapchain->GetDesc(&swcDesc);
	if (!CheckResult(result, "_swapchain->GetDesc")) return false;

	// 表と裏の2つ分それぞれ紐づける
	_backBuffers.resize(swcDesc.BufferCount);
	for (UINT idx = 0; idx < swcDesc.BufferCount; ++idx)
	{
		result = _swapchain->GetBuffer(idx, IID_PPV_ARGS(&_backBuffers[idx]));
		if (!CheckResult(result, "_swapchain->GetBuffer")) return false;

		D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeaps->GetCPUDescriptorHandleForHeapStart();

		handle.ptr += idx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		_dev->CreateRenderTargetView(_backBuffers[idx].Get(), &rtvDesc, handle);
	}

	return true;
}

bool Dx12Wrapper::CreateDepthBuffer()
{
	// 深度バッファの作成
	D3D12_RESOURCE_DESC depthResDesc = {};
	depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;	// 2 次元のテクスチャデータ
	depthResDesc.Width = _windowWidth;	// 幅と高さはレンダーターゲットと同じ
	depthResDesc.Height = _windowHeight;	// 同上
	depthResDesc.DepthOrArraySize = 1;	// テクスチャ配列でも、3D テクスチャでもない
	depthResDesc.Format = DXGI_FORMAT_D32_FLOAT;	// 深度値書き込み用フォーマット
	depthResDesc.SampleDesc.Count = 1;	// サンプルは 1 ピクセルあたり 1 つ
	depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;	// デプスステンシルとして使用

	// 深度値用ヒーププロパティ
	D3D12_HEAP_PROPERTIES depthHeapProp = {};
	depthHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;	// デフォルトなのであとは UNKNOWN でよい
	depthHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	depthHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// このクリアバリューが重要な意味を持つ
	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.DepthStencil.Depth = 1.0f;	// 深さ 1.0f (最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;	// 32 ビット float 値としてクリア

	auto result = _dev->CreateCommittedResource(
		&depthHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&depthResDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&_depthBuffer)
	);
	if (!CheckResult(result, "CreateCommittedResource _depthBuffer")) return false;

	// 深度のためのディスクリプタヒープを作成
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	result = _dev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&_dsvHeap));
	if (!CheckResult(result, "CreateDescriptorHeap _dsvHeap")) return false;

	// 深度ビューの作成
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;	// 深度値に 32 ビット使用
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;	// 2D テクスチャ
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;	// フラグは特になし

	_dev->CreateDepthStencilView(
		_depthBuffer.Get(),
		&dsvDesc,
		_dsvHeap->GetCPUDescriptorHandleForHeapStart()
	);

	return true;
}

bool Dx12Wrapper::CreateSceneConstantBuffer()
{
	// ヒープの設定
	D3D12_HEAP_PROPERTIES constHeapProp = {};

	constHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能(マップ可能)
	constHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	constHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// リソースの設定
	D3D12_RESOURCE_DESC constResDesc = {};

	constResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	constResDesc.Width = AlignmentedSize(sizeof(SceneMatrix), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	constResDesc.Height = 1;                          // 必ず 1
	constResDesc.DepthOrArraySize = 1;                // 必ず 1
	constResDesc.MipLevels = 1;                       // 必ず 1
	constResDesc.Format = DXGI_FORMAT_UNKNOWN;        // 必ず UNKNOWN
	constResDesc.SampleDesc.Count = 1;                // 必ず 1
	constResDesc.SampleDesc.Quality = 0;              // 必ず 0
	constResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // 必ず ROW_MAJOR
	constResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;    // 用途次第

	auto result = _dev->CreateCommittedResource(
		&constHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&constResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_sceneConstBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _sceneConstBuff")) return false;

	result = _sceneConstBuff->Map(0, nullptr, (void**)&_mappedScene);	// マップ
	if (!CheckResult(result, "_sceneConstBuff->Map")) return false;

	return true;
}

bool Dx12Wrapper::CreatePeraResources()
{
	// 作成済みのヒープ情報を使ってもう 1 枚作る
	auto heapDesc = _rtvHeaps->GetDesc();

	// 使っているバックバッファーの情報を利用する
	auto& bbuff = _backBuffers[0];
	auto resDesc = bbuff->GetDesc();

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// レンダリング時のクリア値と同じ値
	float clsClr[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	clearValue.Color[0] = clsClr[0];
	clearValue.Color[1] = clsClr[1];
	clearValue.Color[2] = clsClr[2];
	clearValue.Color[3] = clsClr[3];

	auto result = _dev->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(_peraResource.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateCommittedResource _peraResource.ReleaseAndGetAddressOf()")) return false;

	// RTV 用ヒープを作る
	heapDesc.NumDescriptors = 1;
	result = _dev->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(_peraRTVHeap.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateDescriptorHeap _peraRTVHeap.ReleaseAndGetAddressOf()")) return false;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// RTV を作る
	_dev->CreateRenderTargetView(
		_peraResource.Get(),
		&rtvDesc,
		_peraRTVHeap->GetCPUDescriptorHandleForHeapStart()
	);

	// SRV 用ヒープを作る
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	result = _dev->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(_peraSRVHeap.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateDescriptorHeap _peraSRVHeap.ReleaseAndGetAddressOf()")) return false;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = rtvDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// SRV を作る
	_dev->CreateShaderResourceView(
		_peraResource.Get(),
		&srvDesc,
		_peraSRVHeap->GetCPUDescriptorHandleForHeapStart()
	);

	return true;
}

bool Dx12Wrapper::CreateShadowMap()
{
	auto heapDesc = _rtvHeaps->GetDesc();

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Width = ShadowMapSize;      // 1024 で十分
	resDesc.Height = ShadowMapSize;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	resDesc.Format = DXGI_FORMAT_R32_TYPELESS;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.DepthStencil.Depth = 1.0f;	// 深さ 1.0f (最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;	// 32 ビット float 値としてクリア

	auto result = _dev->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&depthClearValue,
		IID_PPV_ARGS(_shadowMap.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateCommittedResource _shadowMap.ReleaseAndGetAddressOf()")) return false;

	// DSV 用ヒープを作る
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	result = _dev->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(_shadowDSVHeap.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateDescriptorHeap _shadowDSVHeap.ReleaseAndGetAddressOf()")) return false;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;

	// DSV を作る
	_dev->CreateDepthStencilView(
		_shadowMap.Get(),
		&dsvDesc,
		_shadowDSVHeap->GetCPUDescriptorHandleForHeapStart()
	);

	// SRV 用ヒープを作る
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	result = _dev->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(_shadowSRVHeap.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateDescriptorHeap _shadowSRVHeap.ReleaseAndGetAddressOf()")) return false;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// SRV を作る
	_dev->CreateShaderResourceView(
		_shadowMap.Get(),
		&srvDesc,
		_shadowSRVHeap->GetCPUDescriptorHandleForHeapStart()
	);

	return true;
}

void Dx12Wrapper::SetCamera(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& target, float nearZ, float farZ)
{
	DirectX::XMFLOAT3 up(0, 1, 0);	// 上ベクトル

	// ビュー行列
	// カメラの座標と向きに合わせて頂点座標の変換
	auto viewmat = DirectX::XMMatrixLookAtLH(
		DirectX::XMLoadFloat3(&eye),
		DirectX::XMLoadFloat3(&target),
		DirectX::XMLoadFloat3(&up)
	);

	// プロジェクション行列
	// 近くにあるものを大きく、遠くにあるものを小さくする
	auto projmat = DirectX::XMMatrixPerspectiveFovLH(
		DirectX::XM_PIDIV2,	// 画角は 90°
		static_cast<float>(_windowWidth) / static_cast<float>(_windowHeight),	// アスペクト比
		nearZ,	// 近いほう
		farZ	// 遠いほう
	);
	_mappedScene->view = viewmat;
	_mappedScene->proj = projmat;
	_mappedScene->eye = eye;
}

void Dx12Wrapper::UpdateLightCamera(const DirectX::XMFLOAT3& center)
{
	auto lightDir = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&_lightVec));
	auto target = DirectX::XMLoadFloat3(&center); 
	auto lightPos = DirectX::XMVectorSubtract(target,
		DirectX::XMVectorScale(lightDir, _lightDistance));      // 光の手前へ引く(20 くらい)

	auto lightView = DirectX::XMMatrixLookAtLH(lightPos, target, DirectX::XMVectorSet(0, 1, 0, 0));
	auto lightProj = DirectX::XMMatrixOrthographicLH(
		_shadowArea, _shadowArea, 1.0f, 100.0f);                 // ShadowArea = 30 くらい

	_mappedScene->lightCamera = lightView * lightProj;
	_mappedScene->lightVec = _lightVec;
}

bool Dx12Wrapper::CreateDefaultTextures()
{
	_whiteTex = CreateWhiteTexture();
	_blackTex = CreateBlackTexture();
	_gradTex = CreateGrayGradationTexture();

	return _whiteTex != nullptr && _blackTex != nullptr && _gradTex != nullptr;
}

void Dx12Wrapper::CreateSceneConstantBufferView(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	D3D12_CONSTANT_BUFFER_VIEW_DESC matrixCBVDesc = {};

	matrixCBVDesc.BufferLocation = _sceneConstBuff->GetGPUVirtualAddress();
	matrixCBVDesc.SizeInBytes = static_cast<UINT>(_sceneConstBuff->GetDesc().Width);	// 256 バイト境界に揃ったサイズ

	_dev->CreateConstantBufferView(&matrixCBVDesc, handle);
}

void Dx12Wrapper::BeginShadowPass()
{
	// テクスチャとして読める状態 → 描き込める状態へ
	_barrierDesc.Transition.pResource = _shadowMap.Get();
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	_cmdList->ResourceBarrier(1, &_barrierDesc);

	auto dsvH = _shadowDSVHeap->GetCPUDescriptorHandleForHeapStart();
	_cmdList->OMSetRenderTargets(0, nullptr, false, &dsvH);
	_cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.Width = static_cast<float>(ShadowMapSize);
	vp.Height = static_cast<float>(ShadowMapSize);
	vp.MaxDepth = 1.0f;

	D3D12_RECT rect = {};
	rect.right = ShadowMapSize;
	rect.bottom = ShadowMapSize;

	_cmdList->RSSetViewports(1, &vp);
	_cmdList->RSSetScissorRects(1, &rect);
}

void Dx12Wrapper::EndShadowPass()
{
	// 描き込める状態 → テクスチャとして読める状態へ
	_barrierDesc.Transition.pResource = _shadowMap.Get();
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	_cmdList->ResourceBarrier(1, &_barrierDesc);
}

void Dx12Wrapper::BeginOffscreenPass()
{
	// テクスチャとして読める状態 → 描き込める状態へ
	_barrierDesc.Transition.pResource = _peraResource.Get();
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	_cmdList->ResourceBarrier(1, &_barrierDesc);

	auto rtvH = _peraRTVHeap->GetCPUDescriptorHandleForHeapStart();
	auto dsvH = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
	_cmdList->OMSetRenderTargets(1, &rtvH, true, &dsvH);   // 深度バッファは使い回す

	float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
	_cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	_cmdList->RSSetViewports(1, &_viewport);
	_cmdList->RSSetScissorRects(1, &_scissorrect);
}

void Dx12Wrapper::EndOffscreenPass()
{
	// 描き込める状態 → テクスチャとして読める状態へ
	_barrierDesc.Transition.pResource = _peraResource.Get();
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	_cmdList->ResourceBarrier(1, &_barrierDesc);
}

void Dx12Wrapper::BeginBackBufferPass()
{
	_currentBackBufferIdx = _swapchain->GetCurrentBackBufferIndex();

	auto rtvH = _rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	rtvH.ptr += _currentBackBufferIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	_barrierDesc.Transition.pResource = _backBuffers[_currentBackBufferIdx].Get();
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	_cmdList->ResourceBarrier(1, &_barrierDesc);

	// 深度バッファは指定しない（画面いっぱいの板を 1 枚描くだけなので不要）
	_cmdList->OMSetRenderTargets(1, &rtvH, false, nullptr);

	float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
}

void Dx12Wrapper::EndDraw()
{
	// リソースバリアの指定(描画完了 → 表示できる状態に戻す)
	_barrierDesc.Transition.pResource = _backBuffers[_currentBackBufferIdx].Get();
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 直前はレンダーターゲット状態
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;	// 今からPRESENT状態
	_cmdList->ResourceBarrier(1, &_barrierDesc);	// バリア指定実行

	// 命令のクローズ
	_cmdList->Close();

	// -- コマンドリストの実行 --
	// ComPtr の operator& は中身を Release してしまうため、
	// 配列を受け取る API には必ず Get() で取り出した生ポインタを渡す
	ID3D12CommandList* cmdlists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(1, cmdlists);

	// -- GPUの処理を待つ(フェンスによる同期) --
	WaitForCommandQueue();

	// GPUの処理が終わったので、ここで初めてResetできる
	_cmdAllocator->Reset();						// キューをクリア
	_cmdList->Reset(_cmdAllocator.Get(), nullptr);	// 再びコマンドリストを貯める準備
}

void Dx12Wrapper::WaitForCommandQueue()
{
	_cmdQueue->Signal(_fence.Get(), ++_fenceVal);
	if (_fence->GetCompletedValue() != _fenceVal)
	{
		// イベントを作ってGPUの完了を待つ
		// イベントハンドルの取得
		auto event = CreateEvent(nullptr, false, false, nullptr);
		_fence->SetEventOnCompletion(_fenceVal, event);
		// イベントが発生するまで待ち続ける
		WaitForSingleObject(event, INFINITE);
		// イベントハンドルを閉じる
		CloseHandle(event);
	}
}

void Dx12Wrapper::Flip()
{
	// 第1引数(SyncInterval)は「何回の垂直同期を待ってから表示するか」
	//   1 = モニターのリフレッシュレートに同期する（60Hz なら 60 FPS が上限）
	//   0 = 待たずに即座に表示する（上限なし）
	_swapchain->Present(_vsyncEnabled ? 1 : 0, 0);
}

ComPtr<ID3D12Resource> Dx12Wrapper::CreateTextureFromMemory(const uint8_t* data, size_t size)
{
	// -- テクスチャのロード --
	DirectX::TexMetadata metadata = {};
	DirectX::ScratchImage scratchImg = {};

	auto result = DirectX::LoadFromWICMemory(
		data, size, DirectX::WIC_FLAGS_NONE, &metadata, scratchImg
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		// 戻り値がポインタなので return -1 ではなく nullptr を返す
		std::cout << "CreateTextureFromMemory " << " is Failed" << std::endl;
		return nullptr;
	}
#ifdef _DEBUG
	std::cout << "CreateTextureFromMemory " << " is OK" << std::endl;
#endif // _DEBUG

	auto img = scratchImg.GetImage(0, 0, 0);	// 生データ抽出

	// WriteToSubresource で転送する用のヒープ設定
	auto texHeapProp = TextureHeapProperties();

	metadata.format = DirectX::MakeSRGB(metadata.format);

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Format = metadata.format;
	resDesc.Width = metadata.width;	// 幅
	resDesc.Height = static_cast<UINT>(metadata.height);	// 高さ
	resDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);	// 2D で配列でもないので 1
	resDesc.SampleDesc.Count = 1;	// 通常テクスチャなのでアンチエイリアシングしない
	resDesc.SampleDesc.Quality = 0;	// クオリティは最低
	resDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);	// ミップマップしないのでミップ数は 1 つ
	resDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;	// レイアウトは決定しない
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// バッファ作成
	ComPtr<ID3D12Resource> texbuff;
	result = _dev->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&texbuff)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommittedResource texbuff is Failed" << std::endl;
		return nullptr;
	}

	result = texbuff->WriteToSubresource(
		0,
		nullptr,
		img->pixels,
		static_cast<UINT>(img->rowPitch),
		static_cast<UINT>(img->slicePitch)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "texbuff->WriteToSubresource is Failed" << std::endl;
		return nullptr;
	}

	return texbuff.Get();
}

ID3D12Resource* Dx12Wrapper::GetTextureByPath(const std::string& texPath)
{
	// 拡張子ごとの読み込み関数テーブル
	// キャプチャ無しラムダは関数ポインタに暗黙変換できるため、std::function の型消去は不要
	using LoadLambda_t = HRESULT(*)(const std::wstring& path, DirectX::TexMetadata*, DirectX::ScratchImage&);

	// WIC が扱える形式は 1 つの関数を共有する
	constexpr LoadLambda_t loadWIC =
		[](const std::wstring& path, DirectX::TexMetadata* meta, DirectX::ScratchImage& img)
		-> HRESULT
		{
			return LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, meta, img);
		};

	// static const にすることで、呼び出しのたびにテーブルを作り直すのを避ける
	// (関数ローカル static の初期化は C++11 以降スレッドセーフ)
	static const std::unordered_map<std::string, LoadLambda_t> loadLambdaTable =
	{
		{ "sph", loadWIC },
		{ "spa", loadWIC },
		{ "bmp", loadWIC },
		{ "png", loadWIC },
		{ "jpg", loadWIC },
		{
			"tga",
			[](const std::wstring& path, DirectX::TexMetadata* meta, DirectX::ScratchImage& img)
			-> HRESULT
			{
				return LoadFromTGAFile(path.c_str(), meta, img);
			}
		},
		{
			"dds",
			[](const std::wstring& path, DirectX::TexMetadata* meta, DirectX::ScratchImage& img)
			-> HRESULT
			{
				return LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, meta, img);
			}
		},
	};

	auto it1 = _resourceTable.find(texPath);
	if (it1 != _resourceTable.end())
	{
		return it1->second.Get();
	}

	// -- テクスチャのロード --
	DirectX::TexMetadata metadata = {};
	DirectX::ScratchImage scratchImg = {};

	auto wtexPath = GetWideStringFromString(texPath);	// wchar_t 版のパスに変換

	auto ext = GetExtension(texPath);

	// operator[] だと未登録の拡張子のときに空の要素が挿入され、
	// そのまま呼び出して落ちてしまうため、find で存在を確認する
	auto it2 = loadLambdaTable.find(ext);
	if (it2 == loadLambdaTable.end())
	{
		// 失敗時の処理
		std::cout << "GetTextureByPath " << texPath << " is Failed (unsupported extension: " << ext << ")" << std::endl;
		return nullptr;
	}

	auto result = it2->second(
		wtexPath,
		&metadata,
		scratchImg
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		// 戻り値がポインタなので return -1 ではなく nullptr を返す
		std::cout << "GetTextureByPath " << texPath << " is Failed" << std::endl;
		return nullptr;
	}
#ifdef _DEBUG
	std::cout << "GetTextureByPath " << texPath << " is OK" << std::endl;
#endif // _DEBUG

	auto img = scratchImg.GetImage(0, 0, 0);	// 生データ抽出

	// WriteToSubresource で転送する用のヒープ設定
	auto texHeapProp = TextureHeapProperties();

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Format = metadata.format;
	resDesc.Width = metadata.width;	// 幅
	resDesc.Height = static_cast<UINT>(metadata.height);	// 高さ
	resDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);	// 2D で配列でもないので 1
	resDesc.SampleDesc.Count = 1;	// 通常テクスチャなのでアンチエイリアシングしない
	resDesc.SampleDesc.Quality = 0;	// クオリティは最低
	resDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);	// ミップマップしないのでミップ数は 1 つ
	resDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;	// レイアウトは決定しない
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// バッファ作成
	ComPtr<ID3D12Resource> texbuff;
	result = _dev->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&texbuff)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommittedResource texbuff is Failed" << std::endl;
		return nullptr;
	}

	result = texbuff->WriteToSubresource(
		0,
		nullptr,
		img->pixels,
		static_cast<UINT>(img->rowPitch),
		static_cast<UINT>(img->slicePitch)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "texbuff->WriteToSubresource is Failed" << std::endl;
		return nullptr;
	}

	_resourceTable[texPath] = texbuff;
	return texbuff.Get();
}

ComPtr<ID3D12Resource> Dx12Wrapper::CreateWhiteTexture()
{
	auto texHeapProp = TextureHeapProperties();
	auto resDesc = TextureResourceDesc(4, 4);

	ComPtr<ID3D12Resource> whiteBuff;
	auto result = _dev->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&whiteBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _whiteTex")) return nullptr;

	std::vector<unsigned char> data(4 * 4 * 4);
	std::fill(data.begin(), data.end(), 0xff);	// 全部 255 で埋める

	// データ転送
	result = whiteBuff->WriteToSubresource(
		0,
		nullptr,
		data.data(),
		4 * 4,
		static_cast<UINT>(data.size())
	);
	if (!CheckResult(result, "_whiteTex->WriteToSubresource")) return nullptr;

	return whiteBuff;
}

ComPtr<ID3D12Resource> Dx12Wrapper::CreateBlackTexture()
{
	auto texHeapProp = TextureHeapProperties();
	auto resDesc = TextureResourceDesc(4, 4);

	ComPtr<ID3D12Resource> blackBuff;
	auto result = _dev->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&blackBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _blackTex")) return nullptr;

	std::vector<unsigned char> data(4 * 4 * 4);
	std::fill(data.begin(), data.end(), 0x00);	// 全部 0 で埋める

	// データ転送
	result = blackBuff->WriteToSubresource(
		0,
		nullptr,
		data.data(),
		4 * 4,
		static_cast<UINT>(data.size())
	);
	if (!CheckResult(result, "_blackTex->WriteToSubresource")) return nullptr;

	return blackBuff;
}

ComPtr<ID3D12Resource> Dx12Wrapper::CreateGrayGradationTexture()
{
	auto texHeapProp = TextureHeapProperties();
	auto resDesc = TextureResourceDesc(4, 256);

	// 上が白くて下が黒いテクスチャデータを作成
	std::vector<unsigned int> data(4 * 256);
	auto it = data.begin();
	unsigned int c = 0xff;
	for (; it != data.end(); it += 4)
	{
		// RGBA が逆並びのため RGB マクロと 0xff<<24 を用いて表す
		auto col = (0xff << 24) | RGB(c, c, c);
		std::fill(it, it + 4, col);
		--c;
	}

	ComPtr<ID3D12Resource> gradBuff;
	auto result = _dev->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&gradBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _gradTex")) return nullptr;

	// データ転送
	result = gradBuff->WriteToSubresource(
		0,
		nullptr,
		data.data(),
		4 * sizeof(unsigned int),
		static_cast<UINT>(sizeof(unsigned int) * data.size())
	);
	if (!CheckResult(result, "_gradTex->WriteToSubresource")) return nullptr;

	return gradBuff;
}
