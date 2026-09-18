#include "Input.h"

void Input::Update()
{
    _previous = _current;                  // 今の状態を「前フレーム」へ送る
    GetKeyboardState(_current.data());     // 256 キー分を 1 回で取得

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