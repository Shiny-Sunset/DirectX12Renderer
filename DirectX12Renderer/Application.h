#pragma once

#include <Windows.h>
#include <memory>
#include <vector>

#include "GameTimer.h"
#include "Input.h"
#include "Camera.h"

class Dx12Wrapper;
class PMDRenderer;
class PMDActor;
class GltfRenderer;
class GltfModel;
class GltfActor;
class CharacterController;
class Player;
class Enemy;
class Ground;
class Pera;
class Stage;
class DebugUI;

// アプリケーション全体を受け持つシングルトンクラス
// ウィンドウの生成、メッセージループ、各オブジェクトの所有を行う
class Application
{
public:
	enum class GameState
	{
		Title,      // タイトル画面。開始待ち
		Playing,    // プレイ中
		Clear,      // 敵を全滅させた
		GameOver,   // プレイヤーの HP が 0 になった
	};

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

	// -- 初期化 --
	bool CreateGameWindow();

	// -- 毎フレームの処理 --
	// キャラクター同士の重なりを解消する
	void ResolveCollisions();

	// プレイヤーの攻撃が敵に当たっているかを調べる
	void CheckAttackHits();

	void CheckGameEnd();

	void ChangeGameState(GameState next);

	void RestartGame();

	int AliveEnemyCount();

	// デバッグ UI の中身を組み立てる（ImGui::Begin 〜 End）
	void BuildDebugUI();

	void BuildGameUI();

	// -- ウィンドウ --
	WNDCLASSEX _windowClass = {};
	HWND _hwnd = nullptr;

	// -- 描画（宣言順がそのまま構築順、破棄はその逆順になる） --
	// アクターは Dx12Wrapper と GltfModel を参照するので、それらを先に宣言する
	std::unique_ptr<Dx12Wrapper> _dx12;
	std::unique_ptr<DebugUI> _debugUI;
	std::unique_ptr<GltfRenderer> _gltfRenderer;
	std::unique_ptr<GltfModel> _gltfModel;
	std::unique_ptr<Ground> _ground;
	std::unique_ptr<Pera> _pera;
	std::unique_ptr<Stage> _stage;

	// -- 書籍時代の PMD 描画（現在は使っていない） --
	std::unique_ptr<PMDRenderer> _pmdRenderer;
	std::unique_ptr<PMDActor> _pmdActor;

	// -- キャラクター --
	// 見た目(GltfActor) と 体(CharacterController) は、プレイヤーも敵も同じ配列で扱う。
	// 描画・物理・当たり判定を、区別せずまとめてループで回せるようにするため
	std::vector<std::unique_ptr<GltfActor>> _gltfActors;
	std::vector<std::unique_ptr<CharacterController>> _controllers;

	// 判断を受け持つ部分。プレイヤーは 1 体だけ
	std::unique_ptr<Player> _playerLogic;
	std::vector<std::unique_ptr<Enemy>> _enemies;

	// 上の配列の先頭を指すだけ（所有しない）
	GltfActor* _player = nullptr;
	CharacterController* _playerController = nullptr;

	// -- その他 --
	GameTimer _timer;
	Input _input;
	Camera _camera;

	GameState _gameState = GameState::Title;
	float _gameStateTime = 0.0f;
};
