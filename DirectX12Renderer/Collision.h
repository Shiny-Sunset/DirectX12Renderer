#pragma once
#include <DirectXMath.h>

// 当たり判定用の球
struct Sphere
{
    DirectX::XMFLOAT3 center;
    float radius;
};

// 軸に沿った直方体（Axis-Aligned Bounding Box）
// 回転しない箱なので、判定が「各軸の範囲に入っているか」だけで済む
struct AABB
{
    DirectX::XMFLOAT3 min;   // 各軸の最小値
    DirectX::XMFLOAT3 max;   // 各軸の最大値
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

// 球と箱の重なりを解消する
// @param outPush 球を押し出す量（3 軸）
// @return 重なっていて押し出しが必要なら true
bool ResolveSphereVsAABB(const Sphere& s, const AABB& b, DirectX::XMFLOAT3& outPush);