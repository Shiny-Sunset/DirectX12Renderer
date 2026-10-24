#include "Collision.h"
#include "CharacterController.h"
#include <cmath>
#include <algorithm>

int CollisionGrid::ToCell(float v) const
{
    const int c = static_cast<int>((v + GridSize * CellSize * 0.5f) / CellSize);
    return std::clamp(c, 0, GridSize - 1);   // 範囲外は端のマスへ押し込む
}

void CollisionGrid::Build(const std::vector<CharacterController*>& controllers)
{
    for (auto& row : _cells)
        for (auto& cell : row)
            cell.clear();   // 容量は保持される

    _large.clear();

    for (size_t i = 0; i < controllers.size(); ++i)
    {
        // 判定に参加しないものは最初から入れない
        if (!controllers[i]->IsCollisionEnabled()) continue;

        const int idx = static_cast<int>(i);

        // マスより大きいものは 3×3 からはみ出してしまうので、グリッドに入れない
        if (!IsSmall(controllers[i]->Radius()))
        {
            _large.push_back(idx);
            continue;
        }

        const auto& p = controllers[i]->Position();
        _cells[ToCell(p.x)][ToCell(p.z)].push_back(static_cast<int>(i));
    }
}

void CollisionGrid::GatherCandidates(size_t i, const DirectX::XMFLOAT3& pos, std::vector<int>& out) const
{
    out.clear();
    const int cx = ToCell(pos.x);
    const int cz = ToCell(pos.z);

    for (int dz = -1; dz <= 1; ++dz)
    {
        const int z = cz + dz;
        if (z < 0 || z >= GridSize) continue;

        for (int dx = -1; dx <= 1; ++dx)
        {
            const int x = cx + dx;
            if (x < 0 || x >= GridSize) continue;

            for (int j : _cells[x][z])
            {
                // 同じ組を 2 回処理しないよう、片側だけ拾う
                if (j > static_cast<int>(i)) out.push_back(j);
            }
        }
    }
}

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

bool ResolveSphereVsAABB(const Sphere& s, const AABB& b, DirectX::XMFLOAT3& outPush)
{
    // 箱の中で、球の中心に一番近い点を求める
      // 各軸を箱の範囲に収める（クランプ）だけで求まる
    const float cx = std::clamp(s.center.x, b.min.x, b.max.x);
    const float cy = std::clamp(s.center.y, b.min.y, b.max.y);
    const float cz = std::clamp(s.center.z, b.min.z, b.max.z);

    const float dx = s.center.x - cx;
    const float dy = s.center.y - cy;
    const float dz = s.center.z - cz;
    const float distSq = dx * dx + dy * dy + dz * dz;

    if (distSq > s.radius * s.radius) return false;   // 届いていない

    if (distSq > 1e-8f)
    {
        // -- 球の中心が箱の外にある（普通のケース） --
        // 最近点から球の中心へ向かう方向に、めり込んだぶんだけ押す
        const float dist = sqrtf(distSq);
        const float overlap = s.radius - dist;
        outPush = { dx / dist * overlap, dy / dist * overlap, dz / dist * overlap };
    }
    else
    {
        // -- 球の中心が箱の中に入り込んでいる（すり抜けかけ） --
        // 6 つの面のうち、一番近い面へ押し出す
        const float toMinX = s.center.x - b.min.x, toMaxX = b.max.x - s.center.x;
        const float toMinY = s.center.y - b.min.y, toMaxY = b.max.y - s.center.y;
        const float toMinZ = s.center.z - b.min.z, toMaxZ = b.max.z - s.center.z;

        float best = toMinX; outPush = { -(toMinX + s.radius), 0.0f, 0.0f };
        if (toMaxX < best) { best = toMaxX; outPush = { toMaxX + s.radius, 0.0f, 0.0f }; }
        if (toMinY < best) { best = toMinY; outPush = { 0.0f, -(toMinY + s.radius), 0.0f }; }
        if (toMaxY < best) { best = toMaxY; outPush = { 0.0f, toMaxY + s.radius, 0.0f }; }
        if (toMinZ < best) { best = toMinZ; outPush = { 0.0f, 0.0f, -(toMinZ + s.radius) }; }
        if (toMaxZ < best) { best = toMaxZ; outPush = { 0.0f, 0.0f, toMaxZ + s.radius }; }
    }
    return true;
}