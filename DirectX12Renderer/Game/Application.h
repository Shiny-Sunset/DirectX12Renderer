#pragma once

#include <Windows.h>
#include <memory>
#include <vector>
#include <optional>

#include "GameTimer.h"
#include "Input.h"
#include "Camera.h"
#include "GameConfig.h"
#include "Collision.h"

class Dx12Wrapper;
class FontAtlas;
class SpriteRenderer;
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
class King;
class EnemyPool;
class Spawner;
class GateEffect;

// アプリケーション全体を受け持つシングルトンクラス
// ウィンドウの生成、メッセージループ、各オブジェクトの所有を行う
class Application
{
public:
	enum class GameState
	{
		Title,      // タイトル画面。開始待ち
		Playing,    // プレイ中
		Paused,
		// 現仕様では到達しない。アリが無限に湧く防衛ゲームなのでクリア条件が無い
		// (当初は「敵を全滅させたらクリア」にしていたが、開始直後は敵が 0 匹なので
		//  即座にクリアになってしまい、条件ごと外した)
		Clear,
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

	enum class DisplayMode { Windowed1080, Windowed720, Fullscreen };
	void RequestDisplayMode(DisplayMode m) { _pendingMode = m; }
	void OnResize(int w, int h);

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

	void ApplyDisplayMode(DisplayMode m);
	DisplayMode _displayMode = DisplayMode::Windowed1080;
	std::optional<DisplayMode> _pendingMode;
	LONG_PTR _savedStyle = 0;
	RECT _savedRect = {};
	void SetClientSize(int w, int h);

	int AliveEnemyCount();

	int PointPerSecond = GameConfig::Score::PointPerSecond;
	int PointPerKill = GameConfig::Score::PointPerKill;
	// 生存時間 1 秒 = 10 点、討伐 1 匹 = 50 点
	int CalcScore() const
	{
		return static_cast<int>(_survivedTime * PointPerSecond) + _killCount * PointPerKill;
	}

#ifdef _DEBUG
	bool _showDebugUI = true;
#else
	bool _showDebugUI = false;
#endif
	enum : UINT { FontSmall = 0, FontLarge = 1 };
	// デバッグ UI の中身を組み立てる（ImGui::Begin 〜 End）
	void BuildDebugUI();
	void BuildProfileUI();

	void DrawGameUI();
	void DrawHud();
	void DrawTitle();
	void DrawPause();
	void DrawResult();

	void UpdateProfileAverages();

	// -- ウィンドウ --
	WNDCLASSEX _windowClass = {};
	HWND _hwnd = nullptr;

	// -- 描画（宣言順がそのまま構築順、破棄はその逆順になる） --
	// アクターは Dx12Wrapper と GltfModel を参照するので、それらを先に宣言する
	std::unique_ptr<Dx12Wrapper> _dx12;
	std::unique_ptr<FontAtlas> _fontSmall;
	std::unique_ptr<FontAtlas> _fontLarge;
	std::vector<FontAtlas*> _fonts;
	std::unique_ptr<SpriteRenderer> _ui;
	std::unique_ptr<DebugUI> _debugUI;
	std::unique_ptr<GltfRenderer> _gltfRenderer;
	std::unique_ptr<Ground> _ground;
	std::unique_ptr<Pera> _pera;
	std::unique_ptr<Stage> _stage;
	std::unique_ptr<GateEffect> _gates;

	std::unique_ptr<GltfModel> _playerModel;
	std::unique_ptr<GltfModel> _kingModel;
	std::unique_ptr<GltfModel> _antModel;

	// -- 書籍時代の PMD 描画（現在は使っていない） --
	std::unique_ptr<PMDRenderer> _pmdRenderer;
	std::unique_ptr<PMDActor> _pmdActor;

	// -- キャラクター --
	// 見た目(GltfActor) と 体(CharacterController) は、プレイヤーも敵も同じ配列で扱う
	// 描画・物理・当たり判定を、区別せずまとめてループで回せるようにするため
	std::vector<std::unique_ptr<GltfActor>> _gltfActors;
	std::vector<std::unique_ptr<CharacterController>> _ownedControllers;   // プレイヤーと王
	std::vector<CharacterController*> _allControllers;                     // 判定・更新に回す全員

	// 判断を受け持つ部分。プレイヤーは 1 体だけ
	std::unique_ptr<Player> _playerLogic;
	std::unique_ptr<King> _king;

	// 上の配列の先頭を指すだけ（所有しない）
	GltfActor* _player = nullptr;
	CharacterController* _playerController = nullptr;

	// -- その他 --
	GameTimer _timer;
	Input _input;
	Camera _camera;
	float _vfxTime = 0.0f;

	GameState _gameState = GameState::Title;
	float _gameStateTime = 0.0f;

	std::unique_ptr<EnemyPool> _enemyPool;
	std::vector<Spawner> _spawners;
	float _survivedTime = 0.0f;   // 生存時間（スコア）
	int _killCount = 0;           // 討伐数（スコア）
	int _highScore = 0;
	float _uiScale = 1.0f;
	int _fontSmallPx = 28;
	int _fontLargePx = 64;

	CollisionGrid _collisionGrid;
	std::vector<int> _candidates;

	GameConfig::Tuning _tuning; 

	// 計測値
	float _msGpuWait = 0.0f;   // ★ GPU 待ち
	float _msAI = 0.0f;   // プレイヤー・敵の判断
	float _msCollision = 0.0f;   // 当たり判定
	float _msAnim = 0.0f;   // アニメーションとボーン行列
	float _msRecord = 0.0f;   // 描画命令の記録
	float _msCollisionAvg = 0.0f;
	float _msGpuWaitAvg = 0.0f;
	float _msAIAvg = 0.0f;
	float _msAnimAvg = 0.0f;
	float _msRecordAvg = 0.0f;
	float _msGpuShadowAvg = 0.0f;   // 影パス
	float _msGpuSceneAvg = 0.0f;    // シーンパス
	float _msGpuPeraAvg = 0.0f;     // ペラ + UI
	float _msGpuTotalAvg = 0.0f;    // GPU の合計（影 + シーン + ペラ）
	float _msColGround = 0.0f;
	float _msColMove = 0.0f;
	float _msColStage1 = 0.0f;
	float _msColPairs = 0.0f;
	float _msColStage2 = 0.0f;
	float _msColUnstuck = 0.0f;
	float _msColGroundAvg = 0.0f;
	float _msColMoveAvg = 0.0f;
	float _msColStage1Avg = 0.0f;
	float _msColPairsAvg = 0.0f;
	float _msColStage2Avg = 0.0f;
	float _msColUnstuckAvg = 0.0f;
	float _msSampleAvg = 0.0f;
	float _msBoneAvg = 0.0f;
	float _msInstanceAvg = 0.0f;
	float _msColBuild = 0.0f;
	float _msColTest = 0.0f;
	float _msColBuildAvg = 0.0f;
	float _msColTestAvg = 0.0f;
	
};
