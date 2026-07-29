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

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

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


#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif // _DEBUG
	//DebugOutputFormatString("Show window test.");
	//getchar();

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

	// -- DXGIの初期化 --
#ifdef _DEBUG
	auto result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgiFactory));
#else
	auto result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
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

	ID3D12DescriptorHeap* _rtvHeaps = nullptr;

	result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_rtvHeaps));
	if (FAILED(result))
	{
		// 失敗時の処理
		std::cout << "CreateDescriptorHeap is Failed" << std::endl;
		return -1;
	}
#ifdef _DEBUG
	if (result == S_OK)
	{
		std::cout << "CreateDescriptorHeap is OK" << std::endl;
	}
#endif // _DEBUG

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

		_dev->CreateRenderTargetView(_backBuffers[idx], nullptr, handle);
	}

	// -- リソースバリアの作成 --
	D3D12_RESOURCE_BARRIER BarrierDesc = {};
	BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;	// 遷移
	BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;	// 指定なし
	BarrierDesc.Transition.Subresource = 0;


	// 4つの頂点の作成（時計回り順）
	DirectX::XMFLOAT3 vertices[] =
	{
		{-0.4f, -0.7f, 0.0f},
		{-0.4f, 0.7f, 0.0f},
		{0.4f, -0.7f, 0.0f},
		{0.4f, 0.7f, 0.0f},
	};

	// 頂点のインデックスの作成
	unsigned short indices[] = {
		0, 1, 2,
		2, 1, 3
	};

	// -- 頂点バッファの作成 --
	// 頂点ヒープの設定
	D3D12_HEAP_PROPERTIES heapprp = {};

	heapprp.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能（マップ可能）
	heapprp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapprp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

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
		&heapprp,
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
		&heapprp,
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
	DirectX::XMFLOAT3* _vertMap = nullptr;
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

	// シェーダーオブジェクトの作成
	ID3DBlob* _vsBlob = nullptr;
	ID3DBlob* _psBlob = nullptr;
	ID3DBlob* _errorBlob = nullptr;

	// BasicVertexShaderの設定
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

	// -- 頂点レイアウトの作成 --
	// 1頂点のデータの中身を「どの部分が座標・UV・法線か」に分解し、シェーダー入力に結びつける
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{
			"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
	};

	// -- ルートシグネチャの作成 --
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
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
		float clearColor[] = { 1.0f, 1.0f, 0.0f, 1.0f };	// 黄色
		_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

		// パイプラインステートの設定
		_cmdList->SetPipelineState(_pipelinestate);

		// ルートシグネチャの設定
		_cmdList->SetGraphicsRootSignature(_rootSignature);

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

