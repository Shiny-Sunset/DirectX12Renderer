#pragma once
#include <Windows.h>
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <optional>

#include "GameTimer.h"
#include "Input.h"

class Dx12Wrapper;
class FontAtlas;
class SpriteRenderer;
class DebugUI;
class GltfRenderer;
class GltfModel;
class GltfActor;
class Ground;
class Pera;

// アプリケーション全体を受け持つシングルトンクラス
// ウィンドウの生成、メッセージループ、各オブジェクトの所有を行う
class Application
{
public:
    static Application& Instance();

    bool Init();
    void Run();

    void Terminate();

    SIZE GetWindowSize() const;

    enum class DisplayMode { Windowed1080, Windowed720, Fullscreen };
    void RequestDisplayMode(DisplayMode m) { _pendingMode = m; }

    // ウィンドウプロシージャから呼ばれる
    void OnResize(int w, int h);

private:
    Application() = default;
    ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool CreateGameWindow();
    void ApplyDisplayMode(DisplayMode mode);
    void SetClientSize(int w, int h);

    void BuildDebugUI();

    // -- ウィンドウ --
    WNDCLASSEX _windowClass = {};
    HWND _hwnd = nullptr;
    LONG_PTR _savedStyle = 0;      // フルスクリーンに入る前のスタイル
    RECT _savedRect = {};          // 同じく位置とサイズ
    DisplayMode _displayMode = DisplayMode::Windowed1080;
    std::optional<DisplayMode> _pendingMode;   // 描画の途中で切り替えられないので持ち越す

    // -- 描画 --
    std::unique_ptr<Dx12Wrapper> _dx12;
    std::unique_ptr<GltfRenderer> _gltfRenderer;
    std::unique_ptr<Ground> _ground;
    std::unique_ptr<Pera> _pera;

    // -- UI --
    std::unique_ptr<FontAtlas> _fontSmall;
    std::unique_ptr<FontAtlas> _fontLarge;
    std::vector<FontAtlas*> _fonts;
    std::unique_ptr<SpriteRenderer> _ui;
    std::unique_ptr<DebugUI> _debugUI;

    // -- モデル（マイルストーン 0 では 1 体だけ）--
    std::unique_ptr<GltfModel> _model;
    std::unique_ptr<GltfActor> _actor;

    GameTimer _timer;
    Input _input;

    // -- 解像度に追従する UI の倍率 --
    float _uiScale = 1.0f;
    int _fontSmallPx = 0;
    int _fontLargePx = 0;

    // -- 固定カメラ --
    // ステージが画面に収まる位置。ImGui で動かして決めてから定数にする
    DirectX::XMFLOAT3 _cameraEye{ 0.0f, 28.0f, -20.0f };
    DirectX::XMFLOAT3 _cameraTarget{ 0.0f, 0.0f, 0.0f };
};