#pragma once
#include <Windows.h>
#include <memory>
#include "GameTimer.h"
#include "Input.h"
#include "Camera.h"

class Dx12Wrapper;
class PMDRenderer;
class PMDActor;
class GltfRenderer;
class GltfActor;
class GltfModel;
class DebugUI;
class Ground;
class Pera;

// アプリケーション全体を受け持つシングルトンクラス
// ウィンドウの生成、メッセージループ、各オブジェクトの所有を行う
class Application
{
public:
	// 唯一のインスタンスを返す
	static Application& Instance();

	// 初期化(ウィンドウ生成、DirectX12 の初期化、モデルの読み込み)
	// @return 成功したら true
	bool Init();

	// メッセージループを回す
	void Run();

	// 後始末
	// 静的オブジェクトの破棄タイミングに任せると、COM の解放順が読みづらくなるため、
	// main() の中から明示的に呼んで解放を済ませる
	void Terminate();

	// ウィンドウサイズを返す
	SIZE GetWindowSize() const;

private:
	Application() = default;
	~Application() = default;

	// シングルトンなのでコピーもムーブも禁止する
	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;
	Application(Application&&) = delete;
	Application& operator=(Application&&) = delete;

	// ウィンドウの生成
	bool CreateGameWindow();

	WNDCLASSEX _windowClass = {};
	HWND _hwnd = nullptr;

	// デバッグ UI の中身を組み立てる（ImGui::Begin 〜 End）
	void BuildDebugUI();

	// プレイヤーの移動と向きを更新する
	void UpdatePlayer(float deltaTime);

	// 宣言順がそのまま構築順、破棄はその逆順になる
	// PMDActor / PMDRenderer は Dx12Wrapper を参照するので、Dx12Wrapper を先に宣言する
	std::unique_ptr<Dx12Wrapper> _dx12;
	std::unique_ptr<DebugUI> _debugUI;
	std::unique_ptr<PMDRenderer> _pmdRenderer;
	std::unique_ptr<PMDActor> _pmdActor;
	std::unique_ptr<GltfRenderer> _gltfRenderer;
	std::unique_ptr<GltfModel> _gltfModel;
	std::vector<std::unique_ptr<GltfActor>> _gltfActors;
	std::unique_ptr<Ground> _ground;
	std::unique_ptr<Pera> _pera;
	GltfActor* _player = nullptr;                        // 操作対象への参照（所有しない）
	GameTimer _timer;
	Input _input;
	Camera _camera;
};
