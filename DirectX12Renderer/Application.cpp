#include "Application.h"

#include "Dx12Wrapper.h"
#include "PMDActor.h"
#include "PMDRenderer.h"
#include "GltfActor.h"
#include "GltfRenderer.h"

#include <tchar.h>
#include <iostream>

namespace
{
	constexpr int window_width = 1920;
	constexpr int window_height = 1080;

	// 読み込むモデル
	const char* const model_path = "Model/(ファイル名).pmd";
	// 読み込むモーション
	const char* const motion_path = "motion/(ファイル名).vmd";

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
}

Application& Application::Instance()
{
	// 関数ローカル static の初期化は C++11 以降スレッドセーフ
	static Application instance;
	return instance;
}

SIZE Application::GetWindowSize() const
{
	SIZE ret;
	ret.cx = window_width;
	ret.cy = window_height;
	return ret;
}

bool Application::CreateGameWindow()
{
	_windowClass.cbSize = sizeof(WNDCLASSEX);
	_windowClass.lpfnWndProc = (WNDPROC)WindowProcedure; // コールバック関数の指定
	_windowClass.lpszClassName = _T("DX12Sample"); // アプリケーションクラス名
	_windowClass.hInstance = GetModuleHandle(nullptr); // ハンドルの取得

	RegisterClassEx(&_windowClass); // アプリケーションクラス(ウィンドウクラスの指定をOSに伝える)

	RECT wrc = { 0, 0, window_width, window_height }; // ウィンドウサイズを決める

	// 関数を使ってウィンドウサイズのサイズを補正する
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウオブジェクトの作成
	_hwnd = CreateWindow(_windowClass.lpszClassName, // クラス名指定
		_T("DX12テスト"),		// タイトルバーの文字
		WS_OVERLAPPEDWINDOW,	// タイトルバーと境界線のあるウィンドウ
		CW_USEDEFAULT,			// 表示x座標はOSにお任せ
		CW_USEDEFAULT,			// 表示y座標はOSにお任せ
		wrc.right - wrc.left,	// ウィンドウ幅
		wrc.bottom - wrc.top,	// ウィンドウ高
		nullptr,				// 親ウィンドウハンドル
		nullptr,				// メニューハンドル
		_windowClass.hInstance,	// 呼び出しアプリケーションハンドル
		nullptr);				// 追加パラメーター

	if (_hwnd == nullptr)
	{
		std::cout << "CreateWindow is Failed" << std::endl;
		return false;
	}

	return true;
}

bool Application::Init()
{
	auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(result))
	{
		std::cout << "CoInitializeEx is Failed" << std::endl;
		return false;
	}

	if (!CreateGameWindow()) return false;

	// ウィンドウ表示
	// 読み込み処理より前に出しておくことで、初期化に失敗したときに
	// 「ウィンドウすら出ない」ではなく「ウィンドウは出たが描画されない」となり、
	// どこで失敗したかの切り分けがしやすくなる
	ShowWindow(_hwnd, SW_SHOW);

	// -- DirectX12 の初期化 --
	_dx12 = std::make_unique<Dx12Wrapper>();
	if (!_dx12->Init(_hwnd, window_width, window_height)) return false;

	// -- 描画パイプラインの作成 --
	_gltfRenderer = std::make_unique<GltfRenderer>(*_dx12);
	if (!_gltfRenderer->Init()) return false;

	/*
	_pmdRenderer = std::make_unique<PMDRenderer>(*_dx12);
	if (!_pmdRenderer->Init()) return false;
	*/

	// -- モデルの読み込み --
	_gltfActor = std::make_unique<GltfActor>(*_dx12, *_gltfRenderer);
	if (!_gltfActor->Init("Model/DangoGirl.glb")) return false;
	/*
	_pmdActor = std::make_unique<PMDActor>(*_dx12);
	if (!_pmdActor->Init(model_path)) return false;
	if (!_pmdActor->LoadVMDFile(motion_path)) return false;
	*/

	//_pmdActor->PlayAnimation();


	DirectX::XMFLOAT3 eye(0.0f, 0.9f, -2.0f);	// 視点
	DirectX::XMFLOAT3 target(0.0f, 0.9f, 0.0f);	// 注視点

	_dx12->SetCamera(eye, target, 0.1f, 100.0f);

	return true;
}

void Application::Run()
{
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

		// -- 更新処理 --
		_dx12->Update();
		//_pmdActor->Update();

		// -- 描画処理 --
		_dx12->BeginDraw();
		//_pmdRenderer->BeforeDraw();
		//_pmdActor->Draw();
		_gltfRenderer->BeforeDraw();
		_gltfActor->Draw();

		_dx12->EndDraw();
		_dx12->Flip();
	}
}

void Application::Terminate()
{
	// COM / D3D12 オブジェクトの解放を main() の中で終わらせておく
	// (宣言順の逆に破棄されるが、依存関係が分かるように明示的に並べる)
	_pmdActor.reset();
	_pmdRenderer.reset();
	_gltfActor.reset();
	_gltfRenderer.reset();
	_dx12.reset();

	// 使用しないクラスの登録解除
	UnregisterClass(_windowClass.lpszClassName, _windowClass.hInstance);

	CoUninitialize();
}
