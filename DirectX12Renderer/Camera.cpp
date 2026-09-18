#include "Camera.h"
#include "Input.h"
#include <algorithm>

namespace
{
    constexpr float MouseSensitivity = 0.005f;   // ラジアン / ピクセル
    constexpr float MaxPitch = 1.2f;             // 約 70 度
}
void Camera::Update(const Input& input, float deltaTime, const DirectX::XMFLOAT3& targetPos)
{
    // -- マウス右ボタンを押している間だけ回す --
    if (input.IsPressed(VK_RBUTTON))
    {
        _yaw += input.MouseDeltaX() * MouseSensitivity;
        _pitch += input.MouseDeltaY() * MouseSensitivity;

        // 真上・真下を越えると映像がひっくり返るので手前で止める
        _pitch = std::clamp(_pitch, -MaxPitch, MaxPitch);
    }

    // -- 注視点は対象の少し上 --
    _focus = { targetPos.x, targetPos.y + _height, targetPos.z };

    // -- 注視点から見て、カメラが向いている方向 --
    const float cp = cosf(_pitch);
    const DirectX::XMFLOAT3 forward = { cp * sinf(_yaw), -sinf(_pitch), cp * cosf(_yaw) };

    // カメラはその反対側、distance だけ離れた位置に置く
    _eye = {
            _focus.x - forward.x * _distance,
            _focus.y - forward.y * _distance,
            _focus.z - forward.z * _distance,
    };
}