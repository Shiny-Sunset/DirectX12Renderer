#include "Application.h"
#include "Dx12Wrapper.h"
#include "FontAtlas.h"
#include "SpriteRenderer.h"
#include "GltfModel.h"
#include "GltfActor.h"
#include "GltfRenderer.h"
#include "DebugUI.h"
#include "imgui_impl_win32.h"
#include "imgui.h"
#include "Ground.h"
#include "Pera.h"
#include "Util.h"
#include "ShadowConfig.h"
#include <tchar.h>
#include <iostream>
#include <algorithm>

// imgui_impl_win32.h では意図的にコメントアウトされているため、自分で宣言する
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	constexpr int window_width = 1920;
	constexpr int window_height = 1080;

	// 読み込むモデル
	const char* const model_path = "../DirectX12Renderer/Model/DangoGirl.glb";

	LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// ImGui が処理したメッセージはここで終わる
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
		{
			return true;
		}

		// ウィンドウが破棄されたら呼ばれる
		if (msg == WM_DESTROY)
		{
			PostQuitMessage(0);
			return 0;
		}

		if (msg == WM_SIZE && wparam != SIZE_MINIMIZED)
		{
			Application::Instance().OnResize(LOWORD(lparam), HIWORD(lparam));
			return 0;
		}
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}

	enum : UINT { FontSmall = 0, FontLarge = 1 };
}

static std::wstring BuildCharset()
{
	std::wstring s;

	// ASCII の印字可能文字。数字と記号は swprintf_s でいつでも出てくるので全部入れる
	for (wchar_t c = L' '; c <= L'~'; ++c) s += c;

	// 重複を消す。アトラスを無駄に埋めないため
	std::sort(s.begin(), s.end());
	s.erase(std::unique(s.begin(), s.end()), s.end());

	return s;
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
	_windowClass.lpszClassName = _T("AntsCommander"); // アプリケーションクラス名
	_windowClass.hInstance = GetModuleHandle(nullptr); // ハンドルの取得

	RegisterClassEx(&_windowClass); // アプリケーションクラス(ウィンドウクラスの指定をOSに伝える)

	RECT wrc = { 0, 0, window_width, window_height }; // ウィンドウサイズを決める

	// 関数を使ってウィンドウサイズのサイズを補正する
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウオブジェクトの作成
	_hwnd = CreateWindow(_windowClass.lpszClassName, // クラス名指定
		_T("Ants Commander"),	// タイトルバーの文字
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

	_savedStyle = GetWindowLongPtr(_hwnd, GWL_STYLE);

	SetClientSize(window_width, window_height);

	return true;
}

void Application::ApplyDisplayMode(DisplayMode mode)
{
	const bool wasFullscreen = (_displayMode == DisplayMode::Fullscreen);

	if (mode == DisplayMode::Fullscreen)
	{
		if (!wasFullscreen)
		{
			_savedStyle = GetWindowLongPtr(_hwnd, GWL_STYLE);
			GetWindowRect(_hwnd, &_savedRect);
		}

		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST), &mi);

		// rcWork ではなく rcMonitor。rcWork はタスクバーを除いた範囲
		SetWindowLongPtr(_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(_hwnd, HWND_TOP,
			mi.rcMonitor.left, mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
	}
	else
	{
		const int w = (mode == DisplayMode::Windowed1080) ? 1920 : 1280;
		const int h = (mode == DisplayMode::Windowed1080) ? 1080 : 720;

		const LONG_PTR style = _savedStyle | WS_VISIBLE;
		SetWindowLongPtr(_hwnd, GWL_STYLE, style);

		RECT r = { 0, 0, w, h };
		AdjustWindowRectExForDpi(&r, static_cast<DWORD>(_savedStyle), FALSE, 0,
			GetDpiForWindow(_hwnd));

		// フルスクリーンから戻るときだけ元の位置へ
		// ウィンドウ同士の切り替えでは動かさない
		const UINT flags = SWP_NOZORDER | SWP_FRAMECHANGED
			| (wasFullscreen ? 0 : SWP_NOMOVE);
		SetWindowPos(_hwnd, nullptr, _savedRect.left, _savedRect.top,
			r.right - r.left, r.bottom - r.top, flags);
	}
	_displayMode = mode;
}

void Application::SetClientSize(int w, int h)
{
	const DWORD style = static_cast<DWORD>(GetWindowLongPtr(_hwnd, GWL_STYLE));
	RECT r = { 0, 0, w, h };

	// DPI 対応を入れたので AdjustWindowRect ではなく ForDpi
	// 前者はシステム DPI 基準なので、拡大率の違うモニタで枠のぶんがずれる
	AdjustWindowRectExForDpi(&r, style, FALSE, 0, GetDpiForWindow(_hwnd));

	SetWindowPos(_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
		SWP_NOMOVE | SWP_NOZORDER);
}

void Application::OnResize(int w, int h)
{
	if (_dx12 == nullptr || _ui == nullptr || _debugUI == nullptr) return;
	_dx12->Resize(w, h);

	_uiScale = h / 1080.0f;
	ImGui::GetStyle().FontScaleMain = 1.5f * _uiScale;

	const int s = static_cast<int>(28 * _uiScale);
	const int l = static_cast<int>(64 * _uiScale);
	if (s != _fontSmallPx)      // 同じサイズなら焼き直さない
	{
		_fontSmallPx = s;
		_fontLargePx = l;
		if (_fontSmall->Build(L"Meiryo", s, BuildCharset())
			&& _fontLarge->Build(L"Meiryo", l, BuildCharset(), 2048))
		{
			_ui->RefreshFontViews();   // 両方焼けてから SRV を作り直す
		}
	}
}

bool Application::Init()
{
	// シェーダーを探す順番。ゲーム側 → レンダラー側
	// レンダラーは Renderer/ フォルダごと別のゲームへ持っていけるように、
	// 自分のシェーダーを自分のフォルダに置いている
	SetShaderDirectories({ L"Shaders/", L"../Renderer/Shaders/" });

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

	// -- フォント --
	_fontSmall = std::make_unique<FontAtlas>(*_dx12);
	if (!_fontSmall->Build(L"Meiryo", 28, BuildCharset())) return false;
	_fonts.push_back(_fontSmall.get());

	_fontLarge = std::make_unique<FontAtlas>(*_dx12);
	if (!_fontLarge->Build(L"Meiryo", 64, BuildCharset(), 2048)) return false;
	_fonts.push_back(_fontLarge.get());

	// -- UI --（SRV がフォントのテクスチャを指すので、Build のあとで作る）
	_ui = std::make_unique<SpriteRenderer>(*_dx12, _fonts);
	if (!_ui->Init()) return false;

	// -- デバッグ UI --
	_debugUI = std::make_unique<DebugUI>(*_dx12);
	if (!_debugUI->Init(_hwnd)) return false;

	// -- 描画パイプラインの作成 --
	_gltfRenderer = std::make_unique<GltfRenderer>(*_dx12);
	if (!_gltfRenderer->Init()) return false;

	// -- 地面の作成 --
	_ground = std::make_unique<Ground>(*_dx12);
	if (!_ground->Init()) return false;

	// -- マルチパスレンダリング用の板ポリの作成 --
	_pera = std::make_unique<Pera>(*_dx12);
	if (!_pera->Init()) return false;

	// -- モデルの読み込み --
	_model = std::make_unique<GltfModel>(*_dx12);
	if (!_model->Init(model_path)) return false;

	_actor = std::make_unique<GltfActor>(*_dx12, *_gltfRenderer, *_model);
	if (!_actor->Init()) return false;

	_actor->PlayAnimation("Idle");

	return true;
}

void Application::Run()
{
	// -- メッセージループ --
	MSG msg = {};

	_timer.Reset();
	while (true)
	{
		_dx12->BeginFrame();

		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// アプリケーションが終わるときにmessageがWM_QUITになる
		if (msg.message == WM_QUIT)
		{
			break;
		}

		_input.Update(_hwnd);
		const float dt = _timer.Tick();

		if (_input.IsTriggered(VK_ESCAPE)) PostQuitMessage(0);

		// -- ゲームの更新（Phase 1 でここに中身が入る）--
		_actor->Update(dt);

		// ★ 固定カメラでも毎フレーム呼ぶこと（理由は下）
		_dx12->SetCamera(_cameraEye, _cameraTarget, 0.1f, 100.0f);
		_dx12->UpdateLightCamera(DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f });

		_debugUI->BeginFrame();
		BuildDebugUI();
		_debugUI->EndFrame();

		// -- 0 パス目：影 --
		_dx12->BeginShadowPass();
		for (UINT c = 0; c < ShadowConfig::CascadeCount; ++c)
		{
			_dx12->BeginCascade(c);
			_gltfRenderer->BeforeShadowDraw();
			_actor->DrawShadow();
		}
		_dx12->EndShadowPass();

		// -- 1 パス目：シーンをオフスクリーンへ --
		_dx12->BeginOffscreenPass();
		_ground->Draw();
		_gltfRenderer->BeforeDraw();
		_gltfRenderer->SetModelHeap(_actor->Model());
		_actor->Draw();
		_dx12->EndOffscreenPass();

		// -- 2 パス目：画面へ --
		_dx12->BeginBackBufferPass();
		_pera->Draw();

		_ui->Begin();
		// ここに文字描画
		_ui->DrawStringOutlined(FontSmall, 16 * _uiScale, 16 * _uiScale,
			L"Ants Commander", { 1, 1, 1, 1 }, { 0, 0, 0, 1 });
		_ui->Draw();

		_debugUI->Draw();
		_dx12->EndDraw();
		_dx12->Flip();

		// ★ 表示モードの変更は描画が終わってから
		if (_pendingMode)
		{
			ApplyDisplayMode(*_pendingMode);
			_pendingMode.reset();
		}
	}
}

void Application::Terminate()
{
	_dx12->WaitForGpuIdle();
	// COM / D3D12 オブジェクトの解放を main() の中で終わらせておく
	// (宣言順の逆に破棄されるが、依存関係が分かるように明示的に並べる)
	_actor.reset();
	_model.reset();      // アクターが参照し終わってから解放
	_debugUI.reset();
	_ui.reset();
	_fontSmall.reset();
	_fontLarge.reset();
	_gltfRenderer.reset();
	_ground.reset();
	_pera.reset();
	_dx12.reset();

	// 使用しないクラスの登録解除
	UnregisterClass(_windowClass.lpszClassName, _windowClass.hInstance);

	CoUninitialize();
}

void Application::BuildDebugUI()
{
	ImGui::Begin("Debug");
	ImGui::Text("%.1f fps (%.2f ms)", 1.0f / _timer.RawDeltaTime(), _timer.RawDeltaTime() * 1000);
	ImGui::DragFloat3("camera eye", &_cameraEye.x, 0.1f);
	ImGui::DragFloat3("camera target", &_cameraTarget.x, 0.1f);
	if (ImGui::Button("1920x1080")) RequestDisplayMode(DisplayMode::Windowed1080);
	if (ImGui::Button("1280x720"))  RequestDisplayMode(DisplayMode::Windowed720);
	ImGui::End();
}