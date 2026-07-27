#include <Windows.h>
#include <tchar.h>
#ifdef _DEBUG
#include <iostream>
#endif // _DEBUG
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

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
	ID3D12Debug* debugLayer = nullptr;
	auto result = D3D12GetDebugInterface(
		IID_PPV_ARGS(&debugLayer)
	);
	debugLayer->EnableDebugLayer();	// デバッグレイヤーを有効化する
	debugLayer->Release();	// 有効化したらインターフェースを解放する
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

	// ウィンドウオブジェクトの生成
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
			break; // 生成可能なバージョンが見つかったら打ち切り
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
	IDXGIAdapter* tmpAdapter = nullptr;

	for (int i = 0; _dxgiFactory->EnumAdapters(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		adapters.push_back(tmpAdapter);
	}

	for (auto adpt : adapters)
	{
		DXGI_ADAPTER_DESC adesc = {};
		adpt->GetDesc(&adesc);

		std::wstring strDesc = adesc.Description;

		// 探したいアダプターの名前を確認
		if (strDesc.find(L"NVIDIA") != std::string::npos)
		{
			tmpAdapter = adpt;
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

	// キュー生成
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

	// -- フェンスの生成 --
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

	// -- スワップチェーンの生成 --
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};

	swapchainDesc.Width = window_width;
	swapchainDesc.Height = window_height;
	swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.Stereo = false;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
	swapchainDesc.BufferCount = 2;

	// バックバッファな伸び縮み可能
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

	ID3D12DescriptorHeap* rtvHeaps = nullptr;

	result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));
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

		D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();

		handle.ptr += idx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		_dev->CreateRenderTargetView(_backBuffers[idx], nullptr, handle);
	}

	// リソースバリアの生成
	D3D12_RESOURCE_BARRIER BarrierDesc = {};
	BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;	// 遷移
	BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;	// 指定なし
	BarrierDesc.Transition.Subresource = 0;

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
		auto rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
		rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// リソースバリアの指定
		BarrierDesc.Transition.pResource = _backBuffers[bbIdx];	// バックバッファーリソース
		BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;	// 直前はPRESENT状態
		BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 今からレンダーターゲット状態
		_cmdList->ResourceBarrier(1, &BarrierDesc);	// バリア指定実行

		// レンダーターゲットを指定
		_cmdList->OMSetRenderTargets(1, &rtvH, true, nullptr);

		// 画面クリア
		float clearColor[] = { 1.0f, 1.0f, 0.0f, 1.0f };	// 黄色
		_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

		// リソースバリアの指定（描画完了 → 表示できる状態に戻す）
		BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;	// 直前はレンダーターゲット状態
		BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;	// 今からPRESENT状態
		_cmdList->ResourceBarrier(1, &BarrierDesc);	// バリア指定実行

		// 命令のクローズ
		_cmdList->Close();

		// -- コマンドリストの実行 --
		ID3D12CommandList* cmdlists[] = { _cmdList };
		_cmdQueue->ExecuteCommandLists(1, cmdlists);

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

