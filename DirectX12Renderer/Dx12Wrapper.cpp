#include "Dx12Wrapper.h"
#include "Util.h"
#include "GameConfig.h"
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
	D3D12_HEAP_PROPERTIES TextureheapProperties()
	{
		D3D12_HEAP_PROPERTIES texheapProp = {};
		texheapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
		texheapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
		texheapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
		texheapProp.CreationNodeMask = 0;	// 単一アダプターのため 0
		texheapProp.VisibleNodeMask = 0;	// 単一アダプターのため 0
		return texheapProp;
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
	WaitForGpuIdle();
	if (_fenceEvent != nullptr) { CloseHandle(_fenceEvent); _fenceEvent = nullptr; }
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
	if (!CreateGpuTimer()) return false;

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
	// -- コマンドアロケータをフレームの数だけ作る --
	// GPU が使い終わるまでリセットできないので、フレームごとに別々に持つ
	for (UINT i = 0; i < FrameCount; ++i)
	{
		auto r = _dev->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(_frames[i].allocator.ReleaseAndGetAddressOf())
		);
		if (!CheckResult(r, "CreateCommandAllocator")) return false;
	}

	// コマンドリストは 1 本で、Reset でアロケータを付け替える
	auto result = _dev->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		_frames[0].allocator.Get(), 
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

	// フェンスイベントの作成
	_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (_fenceEvent == nullptr) return false;

	_cmdList->Close();

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
	swapchainDesc.BufferCount = BufferCount;

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

	_dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

	return true;
}

bool Dx12Wrapper::Resize(int width, int height)
{
	if (width <= 0 || height <= 0) return true;
	if (width == _windowWidth && height == _windowHeight) return true;

	WaitForGpuIdle();   // 使用中のリソースを壊さない

	// ResizeBuffers は、バックバッファへの参照が 1 つでも残っていると失敗する
	// ComPtr を空にして参照を手放してから呼ぶ
	_backBuffers.clear();

	_windowWidth = width;
	_windowHeight = height;

	DXGI_SWAP_CHAIN_DESC desc = {};
	_swapchain->GetDesc(&desc);
	auto result = _swapchain->ResizeBuffers(BufferCount, width, height, desc.BufferDesc.Format, desc.Flags);
	if (!CheckResult(result, "ResizeBuffers")) return false;

	if (!CreateFinalRenderTargets()) return false;   // RTV ヒープごと作り直す
	if (!CreatePeraResources()) return false;        // オフスクリーンと RTV / SRV
	if (!CreateDepthBuffer()) return false;          // 深度と DSV

	_viewport.Width = static_cast<float>(width);
	_viewport.Height = static_cast<float>(height);
	_scissorrect.right = _scissorrect.left + width;
	_scissorrect.bottom = _scissorrect.top + height;

	return true;
}

bool Dx12Wrapper::CreateFinalRenderTargets()
{
	// -- ディスクリプタヒープの作成 --
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};

	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;		// レンダーターゲットビューなのでRTV
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = BufferCount;
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
	D3D12_RESOURCE_DESC depthresDesc = {};
	depthresDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;	// 2 次元のテクスチャデータ
	depthresDesc.Width = _windowWidth;	// 幅と高さはレンダーターゲットと同じ
	depthresDesc.Height = _windowHeight;	// 同上
	depthresDesc.DepthOrArraySize = 1;	// テクスチャ配列でも、3D テクスチャでもない
	depthresDesc.Format = DXGI_FORMAT_D32_FLOAT;	// 深度値書き込み用フォーマット
	depthresDesc.SampleDesc.Count = 1;	// サンプルは 1 ピクセルあたり 1 つ
	depthresDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;	// デプスステンシルとして使用

	// 深度値用ヒーププロパティ
	D3D12_HEAP_PROPERTIES depthheapProp = {};
	depthheapProp.Type = D3D12_HEAP_TYPE_DEFAULT;	// デフォルトなのであとは UNKNOWN でよい
	depthheapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	depthheapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// このクリアバリューが重要な意味を持つ
	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.DepthStencil.Depth = 1.0f;	// 深さ 1.0f (最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;	// 32 ビット float 値としてクリア

	auto result = _dev->CreateCommittedResource(
		&depthheapProp,
		D3D12_HEAP_FLAG_NONE,
		&depthresDesc,
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
	D3D12_HEAP_PROPERTIES constheapProp = {};

	constheapProp.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能(マップ可能)
	constheapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	constheapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// リソースの設定
	D3D12_RESOURCE_DESC constresDesc = {};

	constresDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	_sceneCBStride = static_cast<UINT>(AlignmentedSize(sizeof(SceneMatrix), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	constresDesc.Width = _sceneCBStride * FrameCount;
	constresDesc.Height = 1;                          // 必ず 1
	constresDesc.DepthOrArraySize = 1;                // 必ず 1
	constresDesc.MipLevels = 1;                       // 必ず 1
	constresDesc.Format = DXGI_FORMAT_UNKNOWN;        // 必ず UNKNOWN
	constresDesc.SampleDesc.Count = 1;                // 必ず 1
	constresDesc.SampleDesc.Quality = 0;              // 必ず 0
	constresDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // 必ず ROW_MAJOR
	constresDesc.Flags = D3D12_RESOURCE_FLAG_NONE;    // 用途次第

	auto result = _dev->CreateCommittedResource(
		&constheapProp,
		D3D12_HEAP_FLAG_NONE,
		&constresDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_sceneConstBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _sceneConstBuff")) return false;

	result = _sceneConstBuff->Map(0, nullptr, reinterpret_cast<void**>(&_mappedScene));	// マップ
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
	resDesc.Width = GameConfig::ShadowMapSize;
	resDesc.Height = GameConfig::ShadowMapSize;
	resDesc.DepthOrArraySize = GameConfig::CascadeCount;   // 2048 × 2048 × 3
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
	heapDesc.NumDescriptors = GameConfig::CascadeCount;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	result = _dev->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(_shadowDSVHeap.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateDescriptorHeap _shadowDSVHeap.ReleaseAndGetAddressOf()")) return false;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;

	dsvDesc.Texture2DArray.ArraySize = 1;
	dsvDesc.Texture2DArray.MipSlice = 0;

	auto dsvH = _shadowDSVHeap->GetCPUDescriptorHandleForHeapStart();
	const auto incSize = _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	for (UINT i = 0; i < GameConfig::CascadeCount; ++i)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		// DSV を作る
		_dev->CreateDepthStencilView(_shadowMap.Get(), &dsvDesc, dsvH);
		dsvH.ptr += incSize;
	}

	// SRV 用ヒープを作る
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	result = _dev->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(_shadowSRVHeap.ReleaseAndGetAddressOf())
	);
	if (!CheckResult(result, "CreateDescriptorHeap _shadowSRVHeap.ReleaseAndGetAddressOf()")) return false;

	CreateShadowMapView(_shadowSRVHeap->GetCPUDescriptorHandleForHeapStart());

	return true;
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

void Dx12Wrapper::CreateShadowMapView(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;   // TYPELESS を数値テクスチャとして読む
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = GameConfig::CascadeCount;

	_dev->CreateShaderResourceView(_shadowMap.Get(), &srvDesc, handle);
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

	auto* s = Scene();
	s->view = viewmat;
	s->viewProj = viewmat * projmat;
	s->eye = eye;
}

void Dx12Wrapper::UpdateLightCamera(const DirectX::XMFLOAT3& center)
{
	auto lightDir = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&_lightVec));
	const auto up = DirectX::XMVectorSet(0, 1, 0, 0);

	// いったん原点を光源から見た向きに変換するための行列（位置は関係ないので原点でよい）
	auto lightRot = DirectX::XMMatrixLookAtLH(
		DirectX::XMVectorZero(), lightDir, up);

	auto centerV = DirectX::XMLoadFloat3(&center);
	auto centerInLight = DirectX::XMVector3TransformCoord(centerV, lightRot);

	constexpr float LightNear = 1.0f;
	constexpr float LightFar = 300.0f;

	auto invLightRot = DirectX::XMMatrixInverse(nullptr, lightRot);

	auto* s = Scene();
	for (UINT i = 0; i < GameConfig::CascadeCount; ++i)
	{
		const float area = GameConfig::CascadeAreas[i] * _shadowAreaScale;

		// -- 注視点をテクセル境界に合わせる --
		// 光源カメラが中途半端な位置にあると、影の縁のドットが毎フレーム入れ替わってチラつく
		// 光源から見た座標系で丸めてから戻すことで、格子が常に同じ場所に来るようにする
		// テクセルスナップは「そのカスケードの」テクセル幅で行う
		// 共通にすると、粗いカスケードの影が毎フレーム踊る
		const float texelSize = area / static_cast<float>(GameConfig::ShadowMapSize);

		// 光源から見た XY（画面に相当する 2 軸）だけを丸める。Z（奥行き）はそのまま
		DirectX::XMFLOAT3 snapped;
		DirectX::XMStoreFloat3(&snapped, centerInLight);
		snapped.x = floorf(snapped.x / texelSize) * texelSize;
		snapped.y = floorf(snapped.y / texelSize) * texelSize;

		// ワールド座標へ戻す
		auto target = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&snapped), invLightRot);
		auto lightPos = DirectX::XMVectorSubtract(target,
			DirectX::XMVectorScale(lightDir, _lightDistance));      // 光の手前へ引く(20 くらい)

		auto lightView = DirectX::XMMatrixLookAtLH(lightPos, target, up);

		auto lightProj = DirectX::XMMatrixOrthographicLH(area, area, LightNear, LightFar);

		_lightCamera[i] = lightView * lightProj;
		s->lightCamera[i] = _lightCamera[i];

		(&s->cascadeNormalOffset.x)[i] = texelSize * 2.0f;
	}
	s->lightVec = _lightVec;
	s->shadowMapTexel = 1.0f / static_cast<float>(GameConfig::ShadowMapSize);
	s->lightRange = LightFar - LightNear;
	// 箱の中心と、カスケードを選ぶ基準を同じ点にする
		// ここがずれると「カスケード 0 と判定されたのに箱の外」という場所ができる
	s->shadowCenter = { center.x, center.y, center.z, 0.0f };

	s->cascadeRadius.x = GameConfig::CascadeAreas[0] * GameConfig::CascadeRadiusRatio * _shadowAreaScale;
	s->cascadeRadius.y = GameConfig::CascadeAreas[1] * GameConfig::CascadeRadiusRatio * _shadowAreaScale;
	s->cascadeRadius.z = 0.0f;
	s->cascadeRadius.w = 0.0f;
}

void Dx12Wrapper::BeginCascade(UINT c)
{
	_currentCascade = c;
	auto dsvH = _shadowDSVHeap->GetCPUDescriptorHandleForHeapStart();
	dsvH.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV) * _currentCascade;

	_cmdList->OMSetRenderTargets(0, nullptr, false, &dsvH);
	_cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void Dx12Wrapper::BeginShadowPass()
{
	StampGpuTimer(0);
	// テクスチャとして読める状態 → 描き込める状態へ
	_barrierDesc.Transition.pResource = _shadowMap.Get();
	_barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	_cmdList->ResourceBarrier(1, &_barrierDesc);

	// ビューポートはどのカスケードでも同じなので、ここで 1 回
	D3D12_VIEWPORT vp = {};
	vp.Width = vp.Height = static_cast<float>(GameConfig::ShadowMapSize);
	vp.MaxDepth = 1.0f;
	D3D12_RECT rect = { 0, 0, GameConfig::ShadowMapSize, GameConfig::ShadowMapSize };
	_cmdList->RSSetViewports(1, &vp);
	_cmdList->RSSetScissorRects(1, &rect);
}

void Dx12Wrapper::EndShadowPass()
{
	// 描き込める状態 → テクスチャとして読める状態へ
	_barrierDesc.Transition.pResource = _shadowMap.Get();
	_barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	_cmdList->ResourceBarrier(1, &_barrierDesc);
	StampGpuTimer(1);
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
	StampGpuTimer(2);
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

void Dx12Wrapper::BeginFrame()
{
	assert(_frameIndex < FrameCount);
	auto& f = _frames[_frameIndex];

	// このフレームのリソースを GPU が使い終わるまで待つ
	if (_fence->GetCompletedValue() < f.fenceValue)
	{
		_fence->SetEventOnCompletion(f.fenceValue, _fenceEvent);
		WaitForSingleObject(_fenceEvent, INFINITE);
	}
	ReadGpuTimer();

	f.allocator->Reset();
	_cmdList->Reset(f.allocator.Get(), nullptr);
}

void Dx12Wrapper::EndDraw()
{
	// リソースバリアの指定(描画完了 → 表示できる状態に戻す)
	_barrierDesc.Transition.pResource = _backBuffers[_currentBackBufferIdx].Get();
	_barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 直前はレンダーターゲット状態
	_barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;	// 今からPRESENT状態
	_cmdList->ResourceBarrier(1, &_barrierDesc);	// バリア指定実行

	StampGpuTimer(3);
	ResolveGpuTimer();
	// 命令のクローズ
	_cmdList->Close();

	// -- コマンドリストの実行 --
	// ComPtr の operator& は中身を Release してしまうため、
	// 配列を受け取る API には必ず Get() で取り出した生ポインタを渡す
	ID3D12CommandList* cmdlists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(1, cmdlists);

	// 「このフレームはここまで」という印をキューに積む（待たない）
	const UINT64 value = ++_fenceVal;
	_cmdQueue->Signal(_fence.Get(), value);
	_frames[_frameIndex].fenceValue = value;
}

void Dx12Wrapper::Flip()
{
	// 第1引数(SyncInterval)は「何回の垂直同期を待ってから表示するか」
	// 1 = モニターのリフレッシュレートに同期する（60Hz なら 60 FPS が上限）
	// 0 = 待たずに即座に表示する（上限なし）
	_swapchain->Present(_vsyncEnabled ? 1 : 0, 0);
	_frameIndex = (_frameIndex + 1) % FrameCount;
}

void Dx12Wrapper::WaitForGpuIdle()
{
	const UINT64 value = ++_fenceVal;
	_cmdQueue->Signal(_fence.Get(), value);

	if (_fence->GetCompletedValue() < value)
	{
		_fence->SetEventOnCompletion(value, _fenceEvent);
		WaitForSingleObject(_fenceEvent, INFINITE);
	}
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
	auto texheapProp = TextureheapProperties();

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
		&texheapProp,
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
	auto texheapProp = TextureheapProperties();

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
		&texheapProp,
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
	auto texheapProp = TextureheapProperties();
	auto resDesc = TextureResourceDesc(4, 4);

	ComPtr<ID3D12Resource> whiteBuff;
	auto result = _dev->CreateCommittedResource(
		&texheapProp,
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
	auto texheapProp = TextureheapProperties();
	auto resDesc = TextureResourceDesc(4, 4);

	ComPtr<ID3D12Resource> blackBuff;
	auto result = _dev->CreateCommittedResource(
		&texheapProp,
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
	auto texheapProp = TextureheapProperties();
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
		&texheapProp,
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

bool Dx12Wrapper::CreateGpuTimer()
{
	D3D12_QUERY_HEAP_DESC qd = {};
	qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	qd.Count = MarkerCount * FrameCount;
	auto r = _dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&_queryHeap));
	if (!CheckResult(r, "CreateQueryHeap")) return false;

	// READBACK ヒープ：GPU が書き、CPU が読む。UPLOAD のちょうど逆向き
	D3D12_HEAP_PROPERTIES hp = {};
	hp.Type = D3D12_HEAP_TYPE_READBACK;
	hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC rd = {};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = sizeof(UINT64) * MarkerCount * FrameCount;
	rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
	rd.Format = DXGI_FORMAT_UNKNOWN;
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// READBACK ヒープのリソースは COPY_DEST 以外の初期状態を受け付けない
	// 以後この状態から動かさないので、バリアも不要
	r = _dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_queryReadback));
	if (!CheckResult(r, "CreateCommittedResource _queryReadback")) return false;

	// カウンタの進む速さ。キューごとに決まっていて、実行中は変わらない
	r = _cmdQueue->GetTimestampFrequency(&_timestampFreq);
	if (!CheckResult(r, "GetTimestampFrequency")) return false;

	return true;
}

void Dx12Wrapper::StampGpuTimer(UINT marker)
{
	// タイムスタンプは「区間」ではなく「点」なので、BeginQuery は使わない
	// EndQuery だけを呼ぶのが正しい（BeginQuery を呼ぶとデバッグレイヤーが警告を出す）
	_cmdList->EndQuery(_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
		_frameIndex * MarkerCount + marker);
}

void Dx12Wrapper::ResolveGpuTimer()
{
	const UINT base = _frameIndex * MarkerCount;
	// コマンドリストに積む命令なので、必ず Close の前に置く
	_cmdList->ResolveQueryData(_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
		base, MarkerCount,
		_queryReadback.Get(), base * sizeof(UINT64));   // オフセットは 8 の倍数であること
}

void Dx12Wrapper::ReadGpuTimer()
{
	if (_timestampFreq == 0) return;

	// 作ったばかりの READBACK バッファの中身は未定義
	// 引き算が巻き戻って数百 ms になるので、一巡するまでは捨てる
	if (_gpuTimerWarmup < FrameCount + 1) { ++_gpuTimerWarmup; return; }

	const UINT base = _frameIndex * MarkerCount;
	const SIZE_T head = base * sizeof(UINT64);

	// 読む範囲を正確に伝える。ここを {0,0} にすると「読まない」という宣言になり、
	// 環境によっては古いキャッシュが返る。UPLOAD で Unmap に {0,0} を渡すのと対の話
	D3D12_RANGE readRange{ head, head + MarkerCount * sizeof(UINT64) };
	UINT64* mapped = nullptr;
	if (FAILED(_queryReadback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) return;

	const UINT64* t = mapped + base;
	const double toMs = 1000.0 / static_cast<double>(_timestampFreq);

	_gpuPassMs[0] = static_cast<float>((t[1] - t[0]) * toMs);   // 影
	_gpuPassMs[1] = static_cast<float>((t[2] - t[1]) * toMs);   // シーン
	_gpuPassMs[2] = static_cast<float>((t[3] - t[2]) * toMs);   // ペラ + UI
	_gpuPassMs[3] = static_cast<float>((t[3] - t[0]) * toMs);   // 合計

	// CPU からは 1 バイトも書いていないので、書き込み範囲は空
	D3D12_RANGE noWrite{ 0, 0 };
	_queryReadback->Unmap(0, &noWrite);
}

void Dx12Wrapper::SetTime(float time)
{
	auto* s = Scene();
	s->time = time;
}
