#include "Application.h"
#include "Dx12Wrapper.h"
#include "PMDActor.h"
#include "PMDRenderer.h"
#include "GltfModel.h"
#include "GltfActor.h"
#include "GltfRenderer.h"
#include "DebugUI.h"
#include "imgui_impl_win32.h"
#include "imgui.h"
#include "Ground.h"
#include "Pera.h"
#include "CharacterController.h"
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
	const char* const model_path = "Model/DangoGirl.glb";
	// 読み込むモーション
	const char* const motion_path = "motion/motion.vmd";

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
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}
	constexpr float TurnSpeed = 12.0f;   // 向きを合わせる速さ
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

	/*
	_pmdRenderer = std::make_unique<PMDRenderer>(*_dx12);
	if (!_pmdRenderer->Init()) return false;
	*/

	// -- モデルの読み込み（1 回だけ） --
	_gltfModel = std::make_unique<GltfModel>(*_dx12);
	if (!_gltfModel->Init(model_path)) return false;

	// -- アクターを 3 体作る --
	const char* anims[] = { "Walk", "Idle", "Run" };
	for (int i = 0; i < 3; ++i)
	{
		auto actor = std::make_unique<GltfActor>(*_dx12, *_gltfRenderer, *_gltfModel);
		if (!actor->Init()) return false;
		actor->SetPosition({ i * 2.0f - 2.0f, 0.0f, 0.0f });
		actor->PlayAnimation(anims[i]);
		_gltfActors.push_back(std::move(actor));

		auto controller = std::make_unique<CharacterController>(*_gltfActors.back());
		_controllers.push_back(std::move(controller));
	}
	_player = _gltfActors[0].get();

	_playerController = _controllers[0].get();

	/*
	_pmdActor = std::make_unique<PMDActor>(*_dx12);
	if (!_pmdActor->Init(model_path)) return false;
	if (!_pmdActor->LoadVMDFile(motion_path)) return false;
	*/

	//_pmdActor->PlayAnimation();


	DirectX::XMFLOAT3 eye(0.0f, 1.0f, -2.0f);	// 視点
	DirectX::XMFLOAT3 target(0.0f, 0.9f, 0.0f);	// 注視点

	_dx12->SetCamera(eye, target, 0.1f, 100.0f);

	_player->PlayAnimation("Walk");

	_player->SetOutlineEnabled(false);

	return true;
}

void Application::Run()
{
	// -- メッセージループ --
	MSG msg = {};

	// キーが押されている間ずっと true になるので、前フレームと比較して
	// 「押された瞬間」だけを拾う
	bool prevOutlineKey = false;

	_timer.Reset();
	while (true)
	{
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

		_input.Update();
		const float dt = _timer.Tick();

		// -- 入力 --
		const bool uiHasKeyboard = _debugUI->WantCaptureKeyboard();
		if (!uiHasKeyboard)
		{
			if (_input.IsTriggered('O')) _player->SetOutlineEnabled(!_player->IsOutlineEnabled());
			if (_input.IsTriggered('1')) _player->PlayAnimation("Idle");
			if (_input.IsTriggered('2')) _player->PlayAnimation("Walk");
			if (_input.IsTriggered('3')) _player->PlayAnimation("Run");
		}

		// -- 更新処理 --
		UpdatePlayer(dt);
		for (auto& c : _controllers)
		{
			c->Update(dt);
		}
		ResolveCollisions();
		for (auto& actor : _gltfActors) actor->Update(dt);
		//_pmdActor->Update();

		_dx12->UpdateLightCamera(_player->Position());

		if (!_debugUI->WantCaptureMouse())
		{
			_camera.Update(_input, dt, _player->Position());
		}
		_dx12->SetCamera(_camera.Eye(), _camera.Focus(), 0.1f, 100.0f);

		// -- デバッグ UI の組み立て --
		_debugUI->BeginFrame();
		BuildDebugUI();
		_debugUI->EndFrame();

		// -- 描画処理 --
		// -- 0 パス目：影 --
		_dx12->BeginShadowPass();
		for (auto& actor : _gltfActors) actor->DrawShadow();
		_dx12->EndShadowPass();
		
		// -- 1 パス目：シーンをテクスチャへ --
		_dx12->BeginOffscreenPass();
		//_pmdRenderer->BeforeDraw();
		//_pmdActor->Draw
		_ground->Draw();
		_gltfRenderer->BeforeDraw();
		for (auto& actor : _gltfActors) actor->Draw();
		_dx12->EndOffscreenPass();


		// -- 2 パス目：テクスチャを画面へ --
		_dx12->BeginBackBufferPass();
		_pera->Draw();        // 画面いっぱいの板ポリ
		_debugUI->Draw();     // UI は効果の影響を受けない
		_dx12->EndDraw();
		_dx12->Flip();
	}
}

void Application::Terminate()
{
	// COM / D3D12 オブジェクトの解放を main() の中で終わらせておく
	// (宣言順の逆に破棄されるが、依存関係が分かるように明示的に並べる)
	_debugUI.reset();
	_pmdActor.reset();
	_pmdRenderer.reset();
	_playerController = nullptr;
	_controllers.clear();
	_player = nullptr;         // 所有しないポインタを先に無効化
	_gltfActors.clear();       // 中の unique_ptr をすべて解放
	_gltfModel.reset();        // アクターが参照し終わってから解放
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
	ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	// -- 性能 --
	const auto& io = ImGui::GetIO();
	ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

	ImGui::Separator();

	// -- 時間 --
	ImGui::Text("dt: %.2f ms", _timer.RawDeltaTime() * 1000.0f);

	float timeScale = _timer.TimeScale();
	if (ImGui::SliderFloat("Time Scale", &timeScale, 0.0f, 3.0f))
	{
		_timer.SetTimeScale(timeScale);
	}

	ImGui::Separator();

	bool vsync = _dx12->IsVSyncEnabled();
	if (ImGui::Checkbox("VSync", &vsync))
	{
		_dx12->SetVSyncEnabled(vsync);
	}

	ImGui::Separator();
	ImGui::Text("Light");

	static float azimuthDeg = 45.0f;     // 水平方向の向き
	static float elevationDeg = 45.0f;   // 高さ方向の角度

	bool lightChanged = false;
	lightChanged |= ImGui::SliderFloat("Azimuth", &azimuthDeg, -180.0f, 180.0f);
	lightChanged |= ImGui::SliderFloat("Elevation", &elevationDeg, 5.0f, 89.0f);

	if (lightChanged)
	{
		const float az = DirectX::XMConvertToRadians(azimuthDeg);
		const float el = DirectX::XMConvertToRadians(elevationDeg);

		// 光が「進む」向きなので、下向き(-Y)を基本にする
		DirectX::XMFLOAT3 dir;
		dir.x = cosf(el) * sinf(az);
		dir.y = -sinf(el);
		dir.z = cosf(el) * cosf(az);

		_dx12->SetLightVec(dir);
	}

	float shadowArea = _dx12->ShadowArea();
	if (ImGui::SliderFloat("Shadow Area", &shadowArea, 4.0f, 40.0f))
	{
		_dx12->SetShadowArea(shadowArea);
	}

	ImGui::Separator();

	ImGui::Text("Transform");

	DirectX::XMFLOAT3 pos = _player->Position();
	if (ImGui::DragFloat3("Position", &pos.x, 0.01f))
	{
		_player->SetPosition(pos);
	}

	float yawDeg = DirectX::XMConvertToDegrees(_player->RotationY());
	if (ImGui::SliderFloat("Rotation Y", &yawDeg, -180.0f, 180.0f))
	{
		_player->SetRotationY(DirectX::XMConvertToRadians(yawDeg));
	}

	ImGui::Separator();
	ImGui::Text("Character");
	ImGui::Text("Grounded: %s", _playerController->IsGrounded() ? "yes" : "no");
	ImGui::Text("VelocityY: %.2f", _playerController->VelocityY());

	ImGui::SliderFloat("Walk Speed", &_walkSpeed, 0.5f, 6.0f);
	ImGui::SliderFloat("Run Speed", &_runSpeed, 1.0f, 12.0f);

	float jumpHeight = _playerController->JumpHeight();
	if (ImGui::SliderFloat("Jump Height", &jumpHeight, 0.3f, 3.0f))
	{
		_playerController->SetJumpHeight(jumpHeight);
	}

	float gravity = _playerController->Gravity();
	if (ImGui::SliderFloat("Gravity", &gravity, -50.0f, -5.0f))
	{
		_playerController->SetGravity(gravity);
	}

	ImGui::Separator();

	// -- 表示 --
	bool outline = _player->IsOutlineEnabled();
	if (ImGui::Checkbox("Outline", &outline))
	{
		_player->SetOutlineEnabled(outline);
	}

	ImGui::Separator();

	// -- アニメーション --
	ImGui::Text("Anim: %s", _player->CurrentAnimationName());
	ImGui::Text("Blend: %.2f", _player->BlendWeight());
	ImGui::Separator();

	ImGui::Text("Animation");
	for (const char* name : { "Idle", "Walk", "Run", "Eat_loop", "Curl_up_loop" })
	{
		if (ImGui::Button(name))
		{
			_player->PlayAnimation(name);
		}
		ImGui::SameLine();
	}

	ImGui::Separator();
	ImGui::Text("Collision");

	float radius = _playerController->Radius();
	if (ImGui::SliderFloat("Radius", &radius, 0.1f, 1.5f))
	{
		for (auto& c : _controllers) c->SetRadius(radius);
	}

	// プレイヤーと 2 体目の距離
	if (_controllers.size() > 1)
	{
		const auto pa = _controllers[0]->BodySphere().center;
		const auto pb = _controllers[1]->BodySphere().center;
		const float dx = pa.x - pb.x, dz = pa.z - pb.z;
		ImGui::Text("Dist to #1: %.2f (sum r = %.2f)",
			sqrtf(dx * dx + dz * dz),
			_controllers[0]->Radius() + _controllers[1]->Radius());
	}
	ImGui::NewLine();

	ImGui::End();

	// ImGui で何ができるかの見本（慣れたら消す）
	ImGui::ShowDemoWindow();
}

void Application::UpdatePlayer(float deltaTime)
{
	// -- 1. 入力を「前後」「左右」の量に変換する --
	float inputX = 0.0f;   // 右が +
	float inputZ = 0.0f;   // 前が +
	if (!_debugUI->WantCaptureKeyboard())
	{
		if (_input.IsPressed('W')) inputZ += 1.0f;
		if (_input.IsPressed('S')) inputZ -= 1.0f;
		if (_input.IsPressed('D')) inputX += 1.0f;
		if (_input.IsPressed('A')) inputX -= 1.0f;
	}

	const bool onGround = _playerController->IsGrounded();

	// -- 2. ジャンプ --
	if (!_debugUI->WantCaptureKeyboard() && _input.IsTriggered(VK_SPACE))
	{
		_playerController->Jump();
	}

	// -- 3. 移動 --
	if (inputX == 0.0f && inputZ == 0.0f)
	{
		_playerController->SetMoveVelocity(0.0f, 0.0f);   //速度を止めるのを忘れずに
		if (onGround) _player->PlayAnimation("Idle");
	}
	else
	{
		// -- カメラの向きを基準に、ワールドでの進行方向を作る --
		const float camYaw = _camera.Yaw();
		const DirectX::XMFLOAT3 forward = { sinf(camYaw), 0.0f, cosf(camYaw) };
		const DirectX::XMFLOAT3 right = { forward.z, 0.0f, -forward.x };

		DirectX::XMVECTOR dir = DirectX::XMVectorAdd(
			DirectX::XMVectorScale(DirectX::XMLoadFloat3(&forward), inputZ),
			DirectX::XMVectorScale(DirectX::XMLoadFloat3(&right), inputX));
		dir = DirectX::XMVector3Normalize(dir);

		const bool isRunning = _input.IsPressed(VK_SHIFT);
		const float speed = isRunning ? _runSpeed : _walkSpeed;

		DirectX::XMFLOAT3 d;
		DirectX::XMStoreFloat3(&d, dir);
		_playerController->SetMoveVelocity(d.x * speed, d.z * speed);

		// 向きの補間（位置は動かさない）
		const float targetYaw = atan2f(d.x, d.z);
		float diff = DirectX::XMScalarModAngle(targetYaw - _player->RotationY());
		const float t = std::min(1.0f, TurnSpeed * deltaTime);
		_player->SetRotationY(_player->RotationY() + diff * t);

		if (onGround) _player->PlayAnimation(isRunning ? "Run" : "Walk");
	}

	// -- 4. 空中のアニメーション --
	if (!onGround) _player->PlayAnimation("Curl_up_loop", 0.1f);
}

void Application::ResolveCollisions()
{
	// 総当たりで調べる
	for (size_t i = 0; i < _controllers.size(); ++i)
	{
		for (size_t j = i + 1; j < _controllers.size(); ++j)
		{
			auto& a = *_controllers[i];
			auto& b = *_controllers[j];

			float pushX = 0.0f, pushZ = 0.0f;
			if (!ResolveXZ(a.BodySphere(), b.BodySphere(), pushX, pushZ)) continue;

			// 両方動けるなら半分ずつ、片方だけなら動ける側が全部負担する
			if (a.IsMovable() && b.IsMovable())
			{
				a.PushXZ(pushX * 0.5f, pushZ * 0.5f);
				b.PushXZ(-pushX * 0.5f, -pushZ * 0.5f);
			}
			else
			{
				a.PushXZ(pushX, pushZ);
				b.PushXZ(-pushX, -pushZ);   // 動けない側は PushXZ の中で無視される
			}
		}
	}
}