#pragma once
#include <DirectXMath.h>
#include <vector>

class CharacterController;

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

// XZ 平面を等間隔のマスに区切り、近くにいるものだけを総当たりする
// セルサイズを「最大半径の 2 倍」にすることで、
// 重なっている 2 つは必ず 3x3 の範囲に収まる
class CollisionGrid
{
public:
    static constexpr float CellSize = 2.8f;   // 最大半径 1.4m の 2 倍
    static constexpr int   GridSize = 20;     // 20 × 20 マス = ±28m をカバー

    // 判定に参加するものだけを登録し直す（反復ごとに呼ぶ）
    void Build(const std::vector<CharacterController*>& controllers);

    // i 番の周囲 3x3 マスにいて、添字が i より大きいものを out に積む
    void GatherCandidates(size_t i, const DirectX::XMFLOAT3& pos, std::vector<int>& out) const;

private:
    int ToCell(float v) const;   // ワールド座標 → マス番号（範囲外はクランプ）

    std::vector<int> _cells[GridSize][GridSize];
};