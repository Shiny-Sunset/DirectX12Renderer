#pragma once
#include <DirectXMath.h>

// 当たり判定用の球
struct Sphere
{
    DirectX::XMFLOAT3 center;
    float radius;
};

// 2 つの球が交差しているか
bool Intersects(const Sphere& a, const Sphere& b);

// 水平方向（XZ 平面）で見て重なっているぶんだけ、a を b から押し出す
// 上下方向は動かさない（乗り上げてしまうため）
// @param a 押し出される側の球
// @param b 押し出す側の球
// @param outPushX 押し出す量（X）
// @param outPushZ 押し出す量（Z）
// @return 重なっていて押し出しが必要なら true
bool ResolveXZ(const Sphere& a, const Sphere& b, float& outPushX, float& outPushZ);