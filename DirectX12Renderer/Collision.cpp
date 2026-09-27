#include "Collision.h"
#include <cmath>

bool Intersects(const Sphere& a, const Sphere& b)
{
    const float dx = a.center.x - b.center.x;
    const float dy = a.center.y - b.center.y;
    const float dz = a.center.z - b.center.z;
    const float sum = a.radius + b.radius;

    // 平方根を取らずに、2 乗同士で比べる（sqrt は重いので避ける定石）
    return (dx * dx + dy * dy + dz * dz) < (sum * sum);
}

bool ResolveXZ(const Sphere& a, const Sphere& b, float& outPushX, float& outPushZ)
{
    outPushX = 0.0f;
    outPushZ = 0.0f;
    float dx = a.center.x - b.center.x;
    float dz = a.center.z - b.center.z;
    const float sum = a.radius + b.radius;

    const float distSq = dx * dx + dz * dz;
    if (distSq >= sum * sum) return false;   // 重なっていない

    float dist = sqrtf(distSq);

    // 中心がぴったり重なると方向が決まらないので、適当な向きへ逃がす
    if (dist < 1e-4f)
    {
        dx = 1.0f;
        dz = 0.0f;
        dist = 1.0f;
    }

    // めり込んだぶんだけ、b から離れる向きへ押す
    const float overlap = sum - dist;
    outPushX = (dx / dist) * overlap;
    outPushZ = (dz / dist) * overlap;
    return true;
}