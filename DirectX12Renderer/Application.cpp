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
#include "Enemy.h"
#include "Player.h"
#include "King.h"
#include "Stage.h"
#include "EnemyPool.h"
#include "Spawner.h"
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

	const char* const PlayerModelPath = "Model/DangoGirl.glb";
	const char* const KingModelPath = "Model/dangomushi.glb";
	const char* const AntModelPath = "Model/Ant.glb";

	constexpr DirectX::XMFLOAT3 PlayerSpawn = { -2.0f, 0.0f, 0.0f };

	constexpr DirectX::XMFLOAT3 KingPosition = { 0.0f, 0.0f, 0.0f };
	constexpr float KingScale = 2.0f;
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

	// -- Stageの作成 --
	_stage = std::make_unique<Stage>(*_dx12, *_gltfRenderer);
	if (!_stage->Init()) return false;

	// -- マルチパスレンダリング用の板ポリの作成 --
	_pera = std::make_unique<Pera>(*_dx12);
	if (!_pera->Init()) return false;

	/*
	_pmdRenderer = std::make_unique<PMDRenderer>(*_dx12);
	if (!_pmdRenderer->Init()) return false;
	*/

	// -- モデルの読み込み（1 回だけ） --
	_playerModel = std::make_unique<GltfModel>(*_dx12);
	if (!_playerModel->Init(PlayerModelPath)) return false;

	_kingModel = std::make_unique<GltfModel>(*_dx12);
	if (!_kingModel->Init(KingModelPath)) return false;

	_antModel = std::make_unique<GltfModel>(*_dx12);
	if (!_antModel->Init(AntModelPath)) return false;

	// -- プレイヤー --
	auto playerActor = std::make_unique<GltfActor>(*_dx12, *_gltfRenderer, *_playerModel);
	if (!playerActor->Init()) return false;
	playerActor->SetPosition(PlayerSpawn);
	_gltfActors.push_back(std::move(playerActor));

	auto playerCtrl = std::make_unique<CharacterController>(*_gltfActors.back());
	playerCtrl->SetPushWeight(5.0f);
	_ownedControllers.push_back(std::move(playerCtrl));

	_player = _gltfActors[0].get();
	_playerController = _ownedControllers[0].get();
	_playerLogic = std::make_unique<Player>(*_player, *_playerController);

	// -- 王 --
	auto kingActor = std::make_unique<GltfActor>(*_dx12, *_gltfRenderer, *_kingModel);
	if (!kingActor->Init()) return false;
	kingActor->SetPosition(KingPosition);
	_gltfActors.push_back(std::move(kingActor));

	auto kingCtrl = std::make_unique<CharacterController>(*_gltfActors.back());
	_ownedControllers.push_back(std::move(kingCtrl));

	_king = std::make_unique<King>(*_gltfActors.back(), *_ownedControllers.back(), KingScale);

	// -- 敵 --
	_enemyPool = std::make_unique<EnemyPool>(*_dx12, *_gltfRenderer, *_antModel);
	if (!_enemyPool->Init(30)) return false;

	// 四隅の巣穴
	constexpr float D = 12.0f;
	_spawners.emplace_back(DirectX::XMFLOAT3{ -D, 0.0f, -D });
	_spawners.emplace_back(DirectX::XMFLOAT3{ D, 0.0f, -D });
	_spawners.emplace_back(DirectX::XMFLOAT3{ -D, 0.0f,  D });
	_spawners.emplace_back(DirectX::XMFLOAT3{ D, 0.0f,  D });

	DirectX::XMFLOAT3 eye(0.0f, 1.0f, -2.0f);	// 視点
	DirectX::XMFLOAT3 target(0.0f, 0.9f, 0.0f);	// 注視点

	_dx12->SetCamera(eye, target, 0.1f, 100.0f);

	_player->PlayAnimation("Walk");

	_player->SetOutlineEnabled(false);

	_playerController->SetPushWeight(5.0f);   // 敵 5 体ぶんの重さ

	for (auto& actor : _gltfActors) actor->Update(0.0f);

	_allControllers.clear();
	for (const auto& c : _ownedControllers)
	{
		_allControllers.push_back(c.get());
	}
	for (const auto& c : _enemyPool->Controllers())
	{
		_allControllers.push_back(c.get());
	}

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
		_gameStateTime += dt;

		// -- 入力 --
		const bool uiFree = !_debugUI->WantCaptureKeyboard();

		switch (_gameState)
		{
		case GameState::Title:
			if (uiFree && _input.IsTriggered(VK_SPACE))
			{
				ChangeGameState(GameState::Playing);
			}
			break;

		case GameState::Clear:
		case GameState::GameOver:
			// 押しっぱなしで即スキップされないよう、少し待ってから受け付ける
			if (_gameStateTime > 1.0f && uiFree && _input.IsTriggered('R'))
			{
				RestartGame();
			}
			break;

		default:
			break;
		}

		// -- 更新処理 --
		if (_gameState == GameState::Playing)
		{
			_survivedTime += dt;
			_playerLogic->Update(dt, _input, _camera.Yaw(), !_debugUI->WantCaptureKeyboard());
			_king->Update(dt);
			for (auto& s : _spawners) s.Update(dt, _survivedTime, *_enemyPool, _tuning);   // 湧く
			_enemyPool->Update(dt, _king->Position(), _king->BodySphere().radius);                            // 判断
			CheckAttackHits();	// ヒット判定
			// 床の高さを先に教えてから動かす
			for (auto& c : _allControllers)
			{
				const auto& p = c->Position();   // または既存の取得方法
				c->SetGroundHeight(_stage->GroundHeightAt(p.x, p.z, p.y + 0.05f));
			}
			for (auto& c : _allControllers) c->Update(dt);	// コリジョン更新
			for (auto& c : _allControllers) _stage->Resolve(*c);
			ResolveCollisions();	// 重なりの解消
			for (auto& c : _allControllers) _stage->Resolve(*c);
			for (auto& c : _allControllers) _stage->Unstuck(*c);
			// 敵の攻撃が王やプレイヤーに当たっているか
			for (size_t i = 0; i < _enemyPool->Size(); ++i)
			{
				if (!_enemyPool->IsActive(i)) continue;

				Enemy* e = _enemyPool->At(i);
				if (!e->IsAttackActive()) continue;

				const Sphere atk = e->AttackSphere();

				if (Intersects(atk, _king->BodySphere()))
				{
					_king->TakeDamage(1);
					continue;
				}
				if (Intersects(atk, _playerLogic->BodySphere()))
				{
					_playerLogic->TakeDamage(1);
				}
			}

			for (auto& actor : _gltfActors) actor->Update(dt);	// 実際に動かす
			_enemyPool->UpdateActors(dt);
			//_pmdActor->Update();

			_dx12->UpdateLightCamera(_player->Position());

			if (!_debugUI->WantCaptureMouse())
			{
				_camera.Update(_input, dt, _player->Position());
			}
			_dx12->SetCamera(_camera.Eye(), _camera.Focus(), 0.1f, 100.0f);

			CheckGameEnd();
		}
		else
		{
			// タイトル・結果画面では、王の周りをゆっくり回す
			constexpr float Radius = 14.0f;
			constexpr float Height = 7.0f;
			constexpr float Speed = 0.15f;   // ラジアン/秒

			const float angle = _gameStateTime * Speed;
			const DirectX::XMFLOAT3 eye = {
					sinf(angle) * Radius, Height, cosf(angle) * Radius };
			const DirectX::XMFLOAT3 target = { 0.0f, 1.5f, 0.0f };
			_dx12->UpdateLightCamera(_player->Position());

			_dx12->SetCamera(eye, target, 0.1f, 100.0f);
		}
		

		// -- デバッグ UI の組み立て --
		_debugUI->BeginFrame();
		BuildDebugUI();
		BuildGameUI();
		_debugUI->EndFrame();

		// -- 描画処理 --
		// -- 0 パス目：影 --
		_dx12->BeginShadowPass();
		for (auto& actor : _gltfActors) actor->DrawShadow();
		_enemyPool->DrawShadow();
		_stage->DrawShadow();
		_dx12->EndShadowPass();
		
		// -- 1 パス目：シーンをテクスチャへ --
		_dx12->BeginOffscreenPass();
		//_pmdRenderer->BeforeDraw();
		//_pmdActor->Draw
		_ground->Draw();
		_stage->Draw();
		_gltfRenderer->BeforeDraw();
		for (auto& actor : _gltfActors) actor->Draw();
		_enemyPool->Draw();
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
	_allControllers.clear();
	_enemyPool.reset();
	_king.reset();
	_playerLogic.reset();
	_player = nullptr;
	_playerController = nullptr;
	_gltfActors.clear();
	_ownedControllers.clear();
	_debugUI.reset();
	_pmdActor.reset();
	_pmdRenderer.reset();
	_playerModel.reset();
	_kingModel.reset();        // アクターが参照し終わってから解放
	_antModel.reset();
	_gltfRenderer.reset();
	_ground.reset();
	_stage.reset();
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
	ImGui::Text("Balance");
	ImGui::SliderFloat("Spawn Interval", &_tuning.spawnIntervalScale, 0.2f, 3.0f);   // 全体の倍率
	ImGui::SliderFloat("Ant Speed", &_tuning.antSpeedScale, 0.3f, 2.0f);
	if (ImGui::SliderInt("King HP", &_tuning.kingMaxHP, 5, 50))
	{
		_king->SetMaxHP(_tuning.kingMaxHP);
	}
	ImGui::Text("(King HP is applied on restart)");

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
	ImGui::Text("Camera");

	float distance = _camera.Distance();
	if (ImGui::SliderFloat("Distance", &distance, 0.5f, 12.0f))
	{
		_camera.SetDistance(distance);
	}

	float height = _camera.Height();
	if (ImGui::SliderFloat("Height", &height, 0.0f, 3.0f))
	{
		_camera.SetHeight(height);
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

	float walkSpeed = _playerLogic->WalkSpeed();
	if (ImGui::SliderFloat("Walk Speed", &walkSpeed, 0.5f, 6.0f))
	{
		_playerLogic->SetWalkSpeed(walkSpeed);
	}
	float runSpeed = _playerLogic->RunSpeed();
	if (ImGui::SliderFloat("Run Speed", &runSpeed, 1.0f, 12.0f))
	{
		_playerLogic->SetRunSpeed(runSpeed);
	}

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

	ImGui::Text("Collision");

	float radius = _playerController->Radius();
	if (ImGui::SliderFloat("Radius", &radius, 0.1f, 1.5f))
	{
		for (auto& c : _ownedControllers) c->SetRadius(radius);
	}

	// プレイヤーと 2 体目の距離
	if (_ownedControllers.size() > 1)
	{
		const auto pa = _ownedControllers[0]->BodySphere().center;
		const auto pb = _ownedControllers[1]->BodySphere().center;
		const float dx = pa.x - pb.x, dz = pa.z - pb.z;
		ImGui::Text("Dist to #1: %.2f (sum r = %.2f)",
			sqrtf(dx * dx + dz * dz),
			_ownedControllers[0]->Radius() + _ownedControllers[1]->Radius());
	}

	ImGui::Separator();
	ImGui::Text("Enemies");
	ImGui::Text("Alive: %d / %zu", _enemyPool->AliveCount(), _enemyPool->Size());
	ImGui::Text("Kills: %d", _killCount);
	ImGui::Text("Time:  %.1f s", _survivedTime);

	ImGui::Separator();
	ImGui::Text("King");
	ImGui::Text("HP  %d / %d", _king->HP(), _king->MaxHP());

	float kingScale = _king->Scale();
	if (ImGui::SliderFloat("King Scale", &kingScale, 0.5f, 5.0f))
	{
		_king->SetScale(kingScale);
	}
	ImGui::NewLine();

	ImGui::End();

	// ImGui で何ができるかの見本（慣れたら消す）
	ImGui::ShowDemoWindow();
}

void Application::BuildGameUI()
{
	const auto& io = ImGui::GetIO();

	// -- プレイ中の情報表示（左上） --
	if (_gameState == GameState::Playing)
	{
		ImGui::SetNextWindowPos(ImVec2(20, 20));
		ImGui::Begin("HUD", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);

		ImGui::Text("KING  %d / %d", _king->HP(), _king->MaxHP());
		ImGui::Text("HP    %d", _playerLogic->HP());
		ImGui::Text("TIME  %.1f", _survivedTime);
		ImGui::Text("KILL  %d", _killCount);

		ImGui::End();
		return;
	}
	// -- 中央のメッセージ --
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
		ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::Begin("Message", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoBackground);
	if (_gameState == GameState::Title)
	{
		ImGui::SetWindowFontScale(3.0f);
		ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "DANGO DEFENSE");

		ImGui::SetWindowFontScale(1.2f);
		ImGui::Text(" ");
		ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "Protect the King from the ants!");
		ImGui::Text(" ");
		ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "WASD    Move        Shift  Run");
		ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "Space   Jump        Click  Attack");
		ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "Mouse Right Drag    Camera");
		ImGui::Text(" ");

		ImGui::SetWindowFontScale(1.5f);
		ImGui::Text("Press SPACE to start");
	}
	else   // Clear / GameOver
	{
		ImGui::SetWindowFontScale(3.0f);
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "GAME OVER");

		ImGui::SetWindowFontScale(1.5f);
		ImGui::Text(" ");
		ImGui::Text("TIME     %6.1f s   x%d", _survivedTime, PointPerSecond);
		ImGui::Text("KILLS    %6d      x%d", _killCount, PointPerKill);
		ImGui::Separator();

		ImGui::SetWindowFontScale(2.0f);
		ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "SCORE    %6d", CalcScore());

		ImGui::SetWindowFontScale(1.2f);
		if (CalcScore() >= _highScore && _highScore > 0)
		{
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "NEW RECORD!");
		}
		else
		{
			ImGui::Text("BEST     %6d", _highScore);
		}

		ImGui::Text(" ");
		ImGui::Text("Press R to retry");
	}

	ImGui::End();
}

void Application::ResolveCollisions()
{
	constexpr int Iterations = 3;
	for (int iter = 0; iter < Iterations; ++iter)
	{
		// 総当たりで調べる
		for (size_t i = 0; i < _allControllers.size(); ++i)
		{
			for (size_t j = i + 1; j < _allControllers.size(); ++j)
			{
				auto& a = *_allControllers[i];
				auto& b = *_allControllers[j];

				// 死んだキャラクターなどは判定しない
				if (!a.IsCollisionEnabled() || !b.IsCollisionEnabled()) continue;

				float pushX = 0.0f, pushZ = 0.0f;
				if (!ResolveXZ(a.BodySphere(), b.BodySphere(), pushX, pushZ)) continue;

				const float total = a.PushWeight() + b.PushWeight();
				if (total <= 0.0f) continue;   // 念のため（通常は起きない）

				// 重い方が動かない。相手が重いほど、自分がたくさん動く
				a.PushXZ(pushX * (b.PushWeight() / total), pushZ * (b.PushWeight() / total));
				b.PushXZ(-pushX * (a.PushWeight() / total), -pushZ * (a.PushWeight() / total));
			}
		}
	}
}

void Application::CheckAttackHits()
{
	if (!_playerLogic->IsAttackActive()) return;

	const Sphere atk = _playerLogic->AttackSphere();
	const auto& pp = _player->Position();

	for (size_t i = 0; i < _enemyPool->Size(); ++i)
	{
		if (!_enemyPool->IsActive(i)) continue;

		Enemy* e = _enemyPool->At(i);
		if (e->IsDead()) continue;
		if (!Intersects(atk, e->BodySphere())) continue;

		const int hpBefore = e->HP();
		e->TakeDamage(1, _playerLogic->AttackId(), pp.x, pp.z);

		if (hpBefore > 0 && e->IsDead())
		{
			++_killCount;
		}
	}
}

void Application::CheckGameEnd()
{
	// プレイヤーが死んだ または 王が死んだ時
	if (_playerLogic->CurrentState() == Player::State::Dead || _king->IsDestroyed())
	{
		ChangeGameState(GameState::GameOver);
		_highScore = std::max(_highScore, CalcScore());
	}
}

void Application::ChangeGameState(GameState next)
{
	if (_gameState == next) return;
	_gameState = next;
	_gameStateTime = 0.0f;
}

void Application::RestartGame()
{
	// 初期位置は Init と同じ式を使う（2 か所に散らばらないよう定数化しておくとなお良い）
	_playerLogic->Reset(PlayerSpawn);
	_king->Reset();

	_enemyPool->ResetAll(); 
	for (auto& s : _spawners) s.Reset();

	_survivedTime = 0.0f;
	_killCount = 0;
	for (auto& actor : _gltfActors) actor->Update(0.0f);

	ChangeGameState(GameState::Playing);
}

int Application::AliveEnemyCount()
{
	return _enemyPool->AliveCount();
}