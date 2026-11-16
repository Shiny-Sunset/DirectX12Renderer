#pragma once
#include <DirectXMath.h>

class Input;

// 追従対象の周りを回る三人称カメラ
// 対象の少し上を見下ろす位置に、毎フレーム配置しなおす
class Camera
{
public:
    // @param input マウスの移動量を取るため
    // @param targetPos 追従する対象（プレイヤー）の足元の座標
    void Update(const Input& input, float deltaTime, const DirectX::XMFLOAT3& targetPos);

    const DirectX::XMFLOAT3& Eye() const { return _eye; }      // カメラの位置
    const DirectX::XMFLOAT3& Focus() const { return _focus; }  // 注視点

    // 水平方向の向き（ラジアン）。移動方向の基準に使う
    float Yaw() const { return _yaw; }

    // -- 調整用 --
    void SetDistance(float d) { _distance = d; }
    float Distance() const { return _distance; }
    void SetHeight(float h) { _height = h; }
    float Height() const { return _height; }

private:
    float _yaw = 0.0f;         // 水平の回転
    float _pitch = 0.25f;      // 上下の回転（+ で見下ろす）
    float _distance = 4.0f;    // 対象からの距離
    float _height = 1.0f;      // 対象の足元から見る高さ

    DirectX::XMFLOAT3 _eye = { 0.0f, 1.0f, -4.0f };
    DirectX::XMFLOAT3 _focus = { 0.0f, 1.0f, 0.0f };
};