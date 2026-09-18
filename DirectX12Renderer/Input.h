#pragma once
#include <Windows.h>
#include <array>

// キーボードとマウスの状態を 1 フレーム分まとめて保持するクラス
// 「押している間」と「押した瞬間」を区別できるようにする
class Input
{
public:
    // 毎フレーム、メッセージ処理の後に 1 回だけ呼ぶ
    void Update();

    // @param key 仮想キーコード（'W' や VK_SPACE など）
    bool IsPressed(int key) const;     // 押している間ずっと true
    bool IsTriggered(int key) const;   // 押した瞬間のフレームだけ true
    bool IsReleased(int key) const;    // 離した瞬間のフレームだけ true

    // マウスの移動量（前フレームからの差。カメラの回転に使う）
    float MouseDeltaX() const { return _mouseDeltaX; }
    float MouseDeltaY() const { return _mouseDeltaY; }

private:
    static constexpr size_t KeyCount = 256;

    std::array<BYTE, KeyCount> _current = {};    // 今フレームのキー状態
    std::array<BYTE, KeyCount> _previous = {};   // 前フレームのキー状態

    POINT _prevMousePos = {};
    float _mouseDeltaX = 0.0f;
    float _mouseDeltaY = 0.0f;
    bool _hasPrevMouse = false;
};