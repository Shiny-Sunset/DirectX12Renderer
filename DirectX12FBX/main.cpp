#include <Windows.h>
#include <tchar.h>
#ifdef _DEBUG
#include <iostream>
#endif // _DEBUG
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

// @brief コンソール画面にフォーマット付き文字列の表示
// @param format フォーマット
// @param 可変長引数
// @remarks この関数はデバッグ用で、デバッグ時にしか動作しない

void DebugOutputFormatString(const char* format, ...)
{
#ifdef _DEBUG
	va_list valist;
	va_start(valist, format);
	vprintf(format, valist);
	va_end(valist);
#endif // _DEBUG
}

void EnableDebugLayer()
{
	ID3D12Debug* _debugLayer = nullptr;
	auto result = D3D12GetDebugInterface(
		IID_PPV_ARGS(&_debugLayer)
	);
	_debugLayer->EnableDebugLayer();	// デバッグレイヤーを有効化する
	_debugLayer->Release();	// 有効化したらインターフェースを解放する
}

LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	// ウィンドウが破棄されたら呼ばれる
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// アライメントにそろえたサイズを返す
// @param size 元のサイズ
// @param alignment アライメントサイズ
// @return アライメントをそろえたサイズ
size_t AlignmentedSize(size_t size, size_t alignment)
{
	// alignment - 1 を足してから下位ビットを切り捨てる
	// size が既に alignment の倍数のときは、そのままのサイズが返る
	return (size + alignment - 1) & ~(alignment - 1);
}


#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif // _DEBUG
	//DebugOutputFormatString("Show window test.");
	//getchar();
	auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	// -- ウィンドウ関連 --
	WNDCLASSEX w = {};

	w.cbSize = sizeof(WNDCLASSEX);
	w.lpfnWndProc = (WNDPROC)WindowProcedure; // コールバック関数の指定
	w.lpszClassName = _T("DX12Sample"); // アプリケーションクラス名
	w.hInstance = GetModuleHandle(nullptr); // ハンドルの取得

	RegisterClassEx(&w); // アプリケーションクラス(ウィンドウクラスの指定をOSに伝える)

	int window_width = 1920;
	int window_height = 1080;

	RECT wrc = { 0, 0, window_width, window_height }; // ウィンドウサイズを決める

	// 関数を使ってウィンドウサイズのサイズを補正する
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウオブジェクトの作成
	HWND hwnd = CreateWindow(w.lpszClassName, // クラス名指定
		_T("DX12テスト"),		// タイトルバーの文字
		WS_OVERLAPPEDWINDOW,	// タイトルバーと境界線のあるウィンドウ
		CW_USEDEFAULT,			// 表示x座標はOSにお任せ
		CW_USEDEFAULT,			// 表示y座標はOSにお任せ
		wrc.right - wrc.left,	// ウィンドウ幅
		wrc.bottom - wrc.top,	// ウィンドウ高
		nullptr,				// 親ウィンドウハンドル
		nullptr,				// メニューハンドル
		w.hInstance,			// 呼び出しアプリケーションハンドル
		nullptr);				// 追加パラメーター

	// ウィンドウ表示
	ShowWindow(hwnd, SW_SHOW);

	// -- Direct3D関連 --
#ifdef _DEBUG
	// デバッグレイヤーを有効化
	EnableDebugLayer();
#endif // _DEBUG
	// -- DirectX3D デバイスの初期化 --
	// 試そうとする機能レベル（上から順に対応しているか調べる）
	D3D_FEATURE_LEVEL levels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	ID3D12Device* _dev = nullptr;
	IDXGIFactory6* _dxgiFactory = nullptr;
	IDXGISwapChain4* _swapchain = nullptr;

	D3D_FEATURE_LEVEL featureLevel;

	for (auto lv : levels)
	{
		if (D3D12CreateDevice(nullptr, lv, IID_PPV_ARGS(&_dev)) == S_OK)
		{
			featureLevel = lv;
			break; // 作成可能なバージョンが見つかったら打ち切り
		}
	}
//#ifdef _DEBUG
//	ID3D12InfoQueue* _infoQueue = nullptr;
//	if (SUCCEEDED(_dev->QueryInterface(IID_PPV_ARGS(&_infoQueue))))
//	{
//		_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
//		_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
//		_infoQueue->Release();
//	}
//#endif

	// -- DXGIの初期化 --
#ifdef _DEBUG
	result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgiFactory));
#else
	result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
#endif // _DEBUG
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateDXGIFactory1 is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateDXGIFactory1 is OK" << std::endl;
	}
#endif // _DEBUG

	// アダプターの列挙用
	std::vector <IDXGIAdapter*> adapters;

	// ここに特定の名前を持つアダプターオブジェクトが入る
	IDXGIAdapter* _tmpAdapter = nullptr;

	for (int i = 0; _dxgiFactory->EnumAdapters(i, &_tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		adapters.push_back(_tmpAdapter);
	}

	for (auto adpt : adapters)
	{
		DXGI_ADAPTER_DESC adesc = {};
		adpt->GetDesc(&adesc);

		std::wstring strDesc = adesc.Description;

		// 探したいアダプターの名前を確認
		if (strDesc.find(L"NVIDIA") != std::string::npos)
		{
			_tmpAdapter = adpt;
			break;
		}
	}

	// -- コマンドリスト関連の初期化 --
	ID3D12CommandAllocator* _cmdAllocator = nullptr;
	ID3D12GraphicsCommandList* _cmdList = nullptr;

	result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_cmdAllocator));
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommandAllocator is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateCommandAllocator is OK" << std::endl;
	}
#endif // _DEBUG

	result = _dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _cmdAllocator, nullptr, IID_PPV_ARGS(&_cmdList));
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommandList is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateCommandList is OK" << std::endl;
	}
#endif // _DEBUG

	ID3D12CommandQueue* _cmdQueue = nullptr;

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
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommandQueue is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateCommandQueue is OK" << std::endl;
	}
#endif // _DEBUG

	// -- フェンスの作成 --
	ID3D12Fence* _fence = nullptr;
	UINT64 _fenceVal = 0;
	result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateFence is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateFence is OK" << std::endl;
	}
#endif // _DEBUG

	// -- スワップチェーンの作成 --
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};

	swapchainDesc.Width = window_width;
	swapchainDesc.Height = window_height;
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

	result = _dxgiFactory->CreateSwapChainForHwnd(_cmdQueue, hwnd, &swapchainDesc, nullptr, nullptr, (IDXGISwapChain1**)&_swapchain);
	// 本来はQueryInterfaceなどを用いて
	// IDXGISwapChain4* への変換チェックをするが、
	// ここではわかりやすさ重視のためにキャストで対応
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateSwapChainForHwnd is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateSwapChainForHwnd is OK" << std::endl;
	}
#endif // _DEBUG

	// -- ディスクリプタヒープの作成 --
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};

	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;		// レンダーターゲットビューなのでRTV
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;						// 表裏の2つ
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;	// 特に指定なし

	// レンダーターゲットビューの作成
	ID3D12DescriptorHeap* _rtvHeaps = nullptr;

	result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_rtvHeaps));
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateDescriptorHeap _rtvHeaps is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateDescriptorHeap _rtvHeaps is OK" << std::endl;
	}
#endif // _DEBUG

	// SRGB レンダーターゲットビューの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};

	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;	// ガンマ補正アリ (sRGB)
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// -- スワップチェーンのメモリと紐づけ --
	DXGI_SWAP_CHAIN_DESC swcDesc = {};

	result = _swapchain->GetDesc(&swcDesc);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "_swapchain->GetDesc is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "_swapchain->GetDesc is OK" << std::endl;
	}
#endif // _DEBUG

	// 表と裏の2つ分それぞれ紐づける
	std::vector<ID3D12Resource*> _backBuffers(swcDesc.BufferCount);
	for (int idx = 0; idx < swcDesc.BufferCount; ++idx)
	{
		result = _swapchain->GetBuffer(idx, IID_PPV_ARGS(&_backBuffers[idx]));
		if (FAILED(result))
		{
			// 失敗時の処理
			std::cout << "_swapchain->GetBuffer: _backBuffers[" << idx << "] is Failed" << std::endl;
			return -1;
		}
#ifdef _DEBUG
		if (result == S_OK)
		{
			std::cout << "_swapchain->GetBuffer: _backBuffers[" << idx << "] is OK" << std::endl;
		}
#endif // _DEBUG

		D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeaps->GetCPUDescriptorHandleForHeapStart();

		handle.ptr += idx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		_dev->CreateRenderTargetView(_backBuffers[idx], &rtvDesc, handle);
	}

	// -- リソースバリアの作成 --
	D3D12_RESOURCE_BARRIER BarrierDesc = {};
	BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;	// 遷移
	BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;	// 指定なし
	BarrierDesc.Transition.Subresource = 0;

	// 頂点データ構造体
	struct Vertex
	{
		DirectX::XMFLOAT3 pos;	// xyz 座標
		DirectX::XMFLOAT2 uv;	// uv 座標
	};

	// 4つの頂点の作成（時計回り順）
	Vertex vertices[] =
	{
		{{-0.4f, -0.7f, 0.0f}, {0.0f, 1.0f}},	// 左下
		{{-0.4f,  0.7f, 0.0f}, {0.0f, 0.0f}},	// 左上
		{{ 0.4f, -0.7f, 0.0f}, {1.0f, 1.0f}},	// 右下
		{{ 0.4f,  0.7f, 0.0f}, {1.0f, 0.0f}}	// 右上
	};

	// 頂点のインデックスの作成
	unsigned short indices[] = {
		0, 1, 2,
		2, 1, 3
	};

	// -- 頂点バッファの作成 --
	// 頂点ヒープの設定
	D3D12_HEAP_PROPERTIES heapprop = {};

	heapprop.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能（マップ可能）
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

	ID3D12Resource* _vertBuff = nullptr;

	result = _dev->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_vertBuff)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommittedResource _vertBuff is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateCommittedResource _vertBuff is OK" << std::endl;
	}
#endif // _DEBUG

	// インデックスバッファの作成
	ID3D12Resource* _idxBuff = nullptr;
	// 設定は、バッファのサイズ以外、頂点バッファの設定を使いまわす
	resdesc.Width = sizeof(indices);

	result = _dev->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_idxBuff)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommittedResource _idxBuff is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateCommittedResource _idxBuff is OK" << std::endl;
	}
#endif // _DEBUG

	// 頂点情報のコピー（マップ）
	Vertex* _vertMap = nullptr;
	_vertBuff->Map(0, nullptr, (void**)&_vertMap);
	std::copy(std::begin(vertices), std::end(vertices), _vertMap);
	_vertBuff->Unmap(0, nullptr);	// マップの解除

	// インデックスデータをコピー（マップ）
	unsigned short* _mappedIdx = nullptr;
	_idxBuff->Map(0, nullptr, (void**)&_mappedIdx);
	std::copy(std::begin(indices), std::end(indices), _mappedIdx);
	_idxBuff->Unmap(0, nullptr);

	// 頂点バッファビューの作成
	// バッファ全体を「何バイトごとの頂点の列」として解釈する
	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress();	// バッファの仮想アドレス
	vbView.SizeInBytes = sizeof(vertices);	// 全体のバイト数
	vbView.StrideInBytes = sizeof(vertices[0]);	// 1頂点あたりのバイト数

	// インデックスバッファビューの作成
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	ibView.BufferLocation = _idxBuff->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;	// 今回はunsigned short（16ビット）を使用しているため
	ibView.SizeInBytes = sizeof(indices);

	// テクスチャデータの作成
	struct TexRGBA
	{
		unsigned char R, G, B, A;
	};

	std::vector<TexRGBA> texturedata(256 * 256);
	for (auto& rgba : texturedata)
	{
		rgba.R = rand() % 256;
		rgba.G = rand() % 256;
		rgba.B = rand() % 256;
		rgba.A = 255;	// αは1にする
	}

	// WIC テクスチャのロード
	DirectX::TexMetadata metadata = {};
	DirectX::ScratchImage scratchImg = {};

	result = DirectX::LoadFromWICFile(
		L"img/Electric.png",
		DirectX::WIC_FLAGS_NONE,
		&metadata,
		scratchImg
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "LoadFromWICFile img/Electric.png is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "LoadFromWICFile img/Electric.png is OK" << std::endl;
	}
#endif // _DEBUG

	auto img = scratchImg.GetImage(0, 0, 0);

	// テクスチャバッファーの作成

	/*
	// WriteToSubresource で転送するためのヒープ設定
	D3D12_HEAP_PROPERTIES heapProp = {};

	// 特殊な設定なので DEFFAULT でも UPLOAD でもない
	heapProp.Type = D3D12_HEAP_TYPE_CUSTOM;

	// ライトバック
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;

	// 転送は L0 、つまり CPU 側から直接行う
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	// 単一アダプターのため 0
	heapProp.CreationNodeMask = 0;
	heapProp.VisibleNodeMask = 0;
	*/

	// アップロード用リソースの作成
	// 中間バッファとしてのアップロードヒープ設定
	D3D12_HEAP_PROPERTIES uploadHeapProp = {};

	// マップ可能にするため、UPLOAD にする
	uploadHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;

	// アップロード用に使用すること前提なので UNKNOWN でよい
	uploadHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	uploadHeapProp.CreationNodeMask = 0;	// 単一アダプターのため 0
	uploadHeapProp.VisibleNodeMask = 0;	// 単一アダプターのため 0

	// アップロード用リソースの設定
	D3D12_RESOURCE_DESC resDesc = {};

	resDesc.Format = DXGI_FORMAT_UNKNOWN;	// 単なるデータの塊なので UNKNOWN
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;	// 単なるバッファとして指定
	resDesc.Width = AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * img->height;	// データサイズ
	resDesc.Height = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;	// 連続したデータ
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;	// 特にフラグ無し
	resDesc.SampleDesc.Count = 1;	// 通常テクスチャなのでアンチエイリアシングしない
	resDesc.SampleDesc.Quality = 0;	// クオリティは最低

	// 中間バッファ作成
	ID3D12Resource* _uploadbuff = nullptr;

	result = _dev->CreateCommittedResource(
		&uploadHeapProp,
		D3D12_HEAP_FLAG_NONE,	// 特に指定なし
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_uploadbuff)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommittedResource _uploadbuff is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateCommittedResource _uploadbuff is OK" << std::endl;
	}
#endif // _DEBUG

	// テクスチャのためのヒープ設定
	D3D12_HEAP_PROPERTIES texHeapProp = {};

	texHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;	// テクスチャ用
	texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	texHeapProp.CreationNodeMask = 0;	// 単一アダプターのため 0
	texHeapProp.VisibleNodeMask = 0;	// 単一アダプターのため 0

	// コピー用リソースの設定
	resDesc.Format = metadata.format;
	resDesc.Width = metadata.width;	// 幅
	resDesc.Height = metadata.height;	// 高さ
	resDesc.DepthOrArraySize = metadata.arraySize;	// 2D で配列でもないので 1
	resDesc.MipLevels = metadata.mipLevels;	// ミップマップしないのでミップ数は 1 つ
	resDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;	// レイアウトは決定しない

	ID3D12Resource* _texBuff = nullptr;

	result = _dev->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,	// コピー先
		nullptr,
		IID_PPV_ARGS(&_texBuff)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateCommittedResource _texBuff is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateCommittedResource _texBuff is OK" << std::endl;
	}
#endif // _DEBUG

	// アップロードリソースへのマップ
	uint8_t* mapforImg = nullptr;	// image->pixels と同じ型にする
	result = _uploadbuff->Map(0, nullptr, (void**)&mapforImg);	// マップ

	auto srcAddress = img->pixels;

	auto rowPitch = AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	for (size_t y = 0; y < img->height; ++y)
	{
		std::copy_n(srcAddress, img->rowPitch, mapforImg);	// 読むのは元データの実サイズぶんだけ

		// 1 行ごとのつじつまを合わせる
		srcAddress += img->rowPitch;	// 元データは詰まっているので実サイズぶん進む
		mapforImg += rowPitch;	// 転送先は行末に余白があるのでアライメント済みサイズぶん進む
	}
	_uploadbuff->Unmap(0, nullptr);	// アンマップ

	// テクスチャのデータを渡す
	/*
	result = _texBuff->WriteToSubresource(
		0,
		nullptr,	// 全領域へコピー
		img->pixels,	// 元データのアドレス
		img->rowPitch,	// 1 ラインサイズ
		img->slicePitch// 1 枚サイズ
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "WriteToSubresource is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "WriteToSubresource is OK" << std::endl;
	}
#endif // _DEBUG
	*/

	// テクスチャのデータをアップロードリソースからコピー
	D3D12_TEXTURE_COPY_LOCATION src = {};

	// コピー元（アップロード側）の設定
	src.pResource = _uploadbuff;	// 中間バッファ
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;	// フットプリント設定
	src.PlacedFootprint.Offset = 0;
	src.PlacedFootprint.Footprint.Width = metadata.width;
	src.PlacedFootprint.Footprint.Height = metadata.height;
	src.PlacedFootprint.Footprint.Depth = metadata.depth;
	src.PlacedFootprint.Footprint.RowPitch = AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	src.PlacedFootprint.Footprint.Format = img->format;

	D3D12_TEXTURE_COPY_LOCATION dst = {};

	// コピー先設定
	dst.pResource = _texBuff;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	// コピー先テクスチャを COPY_DEST → PIXEL_SHADER_RESOURCE へ遷移
	D3D12_RESOURCE_BARRIER texBarrierDesc = {};
	texBarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	texBarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	texBarrierDesc.Transition.pResource = _texBuff;
	texBarrierDesc.Transition.Subresource = 0;
	texBarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	texBarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	_cmdList->ResourceBarrier(1, &texBarrierDesc);

	// 命令のクローズ
	_cmdList->Close();

	// コマンドリストの実行
	ID3D12CommandList* _texCmdLists[] = { _cmdList };
	_cmdQueue->ExecuteCommandLists(1, _texCmdLists);

	// 転送完了を待つ（フェンスによる同期）
	_cmdQueue->Signal(_fence, ++_fenceVal);
		if (_fence->GetCompletedValue() != _fenceVal)
		{
			auto event = CreateEvent(nullptr, false, false, nullptr);
			_fence->SetEventOnCompletion(_fenceVal, event);
			WaitForSingleObject(event, INFINITE);
			CloseHandle(event);
		}

	// 描画ループで使えるように、コマンドリストを記録可能な状態へ戻す
	_cmdAllocator->Reset();
	_cmdList->Reset(_cmdAllocator, nullptr);

	// シェーダーリソースビュー用のディスクリプタヒープの作成
	ID3D12DescriptorHeap* _texDescHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};

	// シェーダーから見えるように
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	// マスクは 0 
	descHeapDesc.NodeMask = 0;

	// ビューは今のところ 1 つだけ
	descHeapDesc.NumDescriptors = 1;

	// シェーダーリソースビュー用
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	result = _dev->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&_texDescHeap));
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateDescriptorHeap _texDescHeap is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateDescriptorHeap _texDescHeap is OK" << std::endl;
	}
#endif // _DEBUG

	// シェーダーリソースビューの作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;	// 2D テクスチャ
	srvDesc.Texture2D.MipLevels = 1;	// ミップマップは使用しないので 1

	_dev->CreateShaderResourceView(
		_texBuff,	// ビューと関連付けるバッファー
		&srvDesc,	// 先ほど設定したテクスチャの設定
		_texDescHeap->GetCPUDescriptorHandleForHeapStart()	// ヒープのどこに割り当てるか
	);

	// -- シェーダーオブジェクトの作成 --
	ID3DBlob* _vsBlob = nullptr;
	ID3DBlob* _psBlob = nullptr;
	ID3DBlob* _errorBlob = nullptr;

	// BasicVertexShader の設定
	result = D3DCompileFromFile(
		L"BasicVertexShader.hlsl",	// シェーダー名
		nullptr,	// defineは無し
		D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
		"BasicVS", "vs_5_0",	// 関数は BasicVS、対象シェーダーは vs_5_0
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
		0,
		&_vsBlob, &_errorBlob	// エラー時は _errorBlob にメッセージが入る
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			::OutputDebugStringA("ファイルが見つかりません");
		}
		else
		{
			std::string errstr;
			errstr.resize(_errorBlob->GetBufferSize());

			std::copy_n(
				(char*)_errorBlob->GetBufferPointer(),
				_errorBlob->GetBufferSize(),
				errstr.begin()
			);
			errstr += "\n";

			::OutputDebugStringA(errstr.c_str());
		}
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "BasicVertexShader is OK" << std::endl;
	}
#endif // _DEBUG

	// BasicPixelShaderの設定
	result = D3DCompileFromFile(
		L"BasicPixelShader.hlsl",	// シェーダー名
		nullptr,	// defineは無し
		D3D_COMPILE_STANDARD_FILE_INCLUDE,	// インクルードはデフォルト
		"BasicPS", "ps_5_0",	// 関数は BasicPS、対象シェーダーは ps_5_0
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,	// デバッグ用及び最適化なし
		0,
		&_psBlob, &_errorBlob	// エラー時は _errorBlob にメッセージが入る
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			::OutputDebugStringA("ファイルが見つかりません");
		}
		else
		{
			std::string errstr;
			errstr.resize(_errorBlob->GetBufferSize());

			std::copy_n(
				(char*)_errorBlob->GetBufferPointer(),
				_errorBlob->GetBufferSize(),
				errstr.begin()
			);
			errstr += "\n";

			::OutputDebugStringA(errstr.c_str());
		}
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "BasicPixelShader is OK" << std::endl;
	}
#endif // _DEBUG

	// -- 頂点レイアウト（インプットレイアウト）の作成 --
	// 1頂点のデータの中身を「どの部分が座標・UV・法線か」に分解し、シェーダー入力に結びつける
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

	// -- ルートシグネチャ関連 --

	// ディスクリプタレンジの作成
	D3D12_DESCRIPTOR_RANGE descTblRange = {};

	descTblRange.NumDescriptors = 1;	// テクスチャ 1 つ
	descTblRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	// 種別はテクスチャ
	descTblRange.BaseShaderRegister = 0;	// 0 番スロットから
	descTblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメーター（ディスクリプタテーブル）の作成
	D3D12_ROOT_PARAMETER rootparam = {};

	rootparam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

	// ピクセルシェーダーから見える
	rootparam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// ディスクリプタレンジのアドレス
	rootparam.DescriptorTable.pDescriptorRanges = &descTblRange;

	// ディスクリプタレンジ数
	rootparam.DescriptorTable.NumDescriptorRanges = 1;

	// サンプラーの作成
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};

	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;	// 横方向の繰り返し
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;	// 縦方向の繰り返し
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;	// 奥行きの繰り返し
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;	// ボーダーは黒
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;	// 線形補間
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;	// ミップマップ最大値
	samplerDesc.MinLOD = 0.0f;	// ミップマップ最小値
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;	// ピクセルシェーダーから見える
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;	// リサンプリングしない

	// ルートシグネチャの作成
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = &rootparam;	// ルートパラメーターの先頭アドレス
	rootSignatureDesc.NumParameters = 1;	// ルートパラメーター数
	rootSignatureDesc.pStaticSamplers = &samplerDesc;
	rootSignatureDesc.NumStaticSamplers = 1;

	// バイナリコードの作成
	ID3DBlob* _rootSigBlob = nullptr;
	result = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_0,
		&_rootSigBlob,
		&_errorBlob
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "D3D12SerializeRootSignature is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "D3D12SerializeRootSignature is OK" << std::endl;
	}
#endif // _DEBUG
	// ルートシグネチャオブジェクトの作成
	ID3D12RootSignature* _rootSignature = nullptr;
	result = _dev->CreateRootSignature(
		0,
		_rootSigBlob->GetBufferPointer(),
		_rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignature)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateRootSignature is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateRootSignature is OK" << std::endl;
	}
#endif // _DEBUG
	_rootSigBlob->Release();

	// -- グラフィックスパイプラインステートの作成 --
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	// ルートシグネチャの設定
	gpipeline.pRootSignature = _rootSignature;
	// シェーダーの設定
	gpipeline.VS.pShaderBytecode = _vsBlob->GetBufferPointer();
	gpipeline.VS.BytecodeLength = _vsBlob->GetBufferSize();
	gpipeline.PS.pShaderBytecode = _psBlob->GetBufferPointer();
	gpipeline.PS.BytecodeLength = _psBlob->GetBufferSize();
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
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;	// 0 ～ 1 に正規化された RGBA
	// アンチエイリアシングのためのサンプル数設定
	gpipeline.SampleDesc.Count = 1;	// サンプリングは1ピクセルにつき1
	gpipeline.SampleDesc.Quality = 0;	// クオリティは最低

	// グラフィックスパイプラインステートオブジェクトの作成
	ID3D12PipelineState* _pipelinestate = nullptr;
	result = _dev->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_pipelinestate)
	);
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateGraphicsPipelineState is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateGraphicsPipelineState is OK" << std::endl;
	}
#endif // _DEBUG

	// ビューポートの作成
	D3D12_VIEWPORT viewport = {};
	
	viewport.Width = window_width;	// 出力先の幅（ピクセル数）
	viewport.Height = window_height;	// 出力先の高さ（ピクセル数）
	viewport.TopLeftX = 0;	// 出力先の左上座標X
	viewport.TopLeftY = 0;	// 出力先の左上座標Y
	viewport.MaxDepth = 1.0f;	// 深度最大値
	viewport.MinDepth = 0.0f;	// 深度最小値

	// シザー矩形
	D3D12_RECT scissorrect = {};
	scissorrect.top = 0;	// 切り抜き上座標
	scissorrect.left = 0;	// 切り抜き左座標
	scissorrect.right = scissorrect.left + window_width;	// 切り抜き右座標
	scissorrect.bottom = scissorrect.top + window_height;	// 切り抜き下座標

	// -- メッセージループ --
	MSG msg = {};

	while (true)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// アプリケーションが終わるときにmessageがWM_QUITになる
		if (msg.message == WM_QUIT)
		{
			break;
		}

		// -- 描画処理 --
		// 現在のバックバッファ（描画対象）の番号を取得
		auto bbIdx = _swapchain->GetCurrentBackBufferIndex();

		// 描画対象のRTVのハンドルを求める
		auto rtvH = _rtvHeaps->GetCPUDescriptorHandleForHeapStart();
		rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// リソースバリアの指定
		BarrierDesc.Transition.pResource = _backBuffers[bbIdx];	// バックバッファリソース
		BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;	// 直前はPRESENT状態
		BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 今からレンダーターゲット状態
		_cmdList->ResourceBarrier(1, &BarrierDesc);	// バリア指定実行


		// -- 命令の記述開始 --
		// レンダーターゲットを指定
		_cmdList->OMSetRenderTargets(1, &rtvH, true, nullptr);

		// 画面クリア
		float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };	// 灰色
		_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

		// パイプラインステートの設定
		_cmdList->SetPipelineState(_pipelinestate);

		// ルートシグネチャの設定
		_cmdList->SetGraphicsRootSignature(_rootSignature);

		// ディスクリプタヒープの指定
		_cmdList->SetDescriptorHeaps(1, &_texDescHeap);

		// ルートパラメーターとディスクリプタヒープの関連付け
		_cmdList->SetGraphicsRootDescriptorTable(
			0,	// ルートパラメーターインデックス
			_texDescHeap->GetGPUDescriptorHandleForHeapStart()	// ヒープアドレス
		);

		// ビューポートの設定
		_cmdList->RSSetViewports(1, &viewport);

		// シザー矩形の設定
		_cmdList->RSSetScissorRects(1, &scissorrect);

		// プリミティブトポロジの設定
		_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 頂点バッファの設定
		_cmdList->IASetVertexBuffers(0, 1, &vbView);

		// インデックスバッファの設定
		_cmdList->IASetIndexBuffer(&ibView);

		// 描画命令
		_cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);

		// -- 命令の記述終了 --
		
		// リソースバリアの指定（描画完了 → 表示できる状態に戻す）
		BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 直前はレンダーターゲット状態
		BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;	// 今からPRESENT状態
		_cmdList->ResourceBarrier(1, &BarrierDesc);	// バリア指定実行

		// 命令のクローズ
		_cmdList->Close();

		// -- コマンドリストの実行 --
		ID3D12CommandList* _cmdlists[] = { _cmdList };
		_cmdQueue->ExecuteCommandLists(1, _cmdlists);

		// -- GPUの処理を待つ（フェンスによる同期） --
		_cmdQueue->Signal(_fence, ++_fenceVal);
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

		// GPUの処理が終わったので、ここで初めてResetできる
		_cmdAllocator->Reset();						// キューをクリア
		_cmdList->Reset(_cmdAllocator, nullptr);	// 再びコマンドリストを貯める準備

		// 画面のスワップ（フリップ）
		_swapchain->Present(1, 0);
	}

	// 使用しないクラスの登録解除
	UnregisterClass(w.lpszClassName, w.hInstance);

	return 0;
}

