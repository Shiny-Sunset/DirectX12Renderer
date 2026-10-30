#include "Input.h"

void Input::Update(HWND hwnd)
{
    _previous = _current;
    // GetKeyboardState は「メッセージキューが覚えている状態」を返すもので、
    // キー入力のメッセージを処理していないフレームでは離した状態が返ることがある
    // 1 フレームでも落ちると IsTriggered が偽の「押した瞬間」を作ってしまうため、
    // ハードウェアの現在値を返す GetAsyncKeyState を使う
    const bool active = (GetForegroundWindow() == hwnd);
    _active = (GetForegroundWindow() == hwnd);
    for (int i = 0; i < static_cast<int>(KeyCount); ++i)
    {
        _current[i] = (active && (GetAsyncKeyState(i) & 0x8000)) ? 0x80 : 0;
    }

    POINT p;
    GetCursorPos(&p);
    if (_hasPrevMouse)
    {
        _mouseDeltaX = static_cast<float>(p.x - _prevMousePos.x);
        _mouseDeltaY = static_cast<float>(p.y - _prevMousePos.y);
    }
    _prevMousePos = p;
    _hasPrevMouse = true;   // 1 フレーム目は差分を 0 にしておく
}

bool Input::IsPressed(int key) const
{
    return (_current[key] & 0x80) != 0;
}

bool Input::IsTriggered(int key) const
{
    return (_current[key] & 0x80) != 0 && (_previous[key] & 0x80) == 0;
}

bool Input::IsReleased(int key) const
{
    return (_current[key] & 0x80) == 0 && (_previous[key] & 0x80) != 0;
}