#include "Spawner.h"
#include "EnemyPool.h"
#include <cstdlib>

Spawner::Spawner(const DirectX::XMFLOAT3& position): _position(position)
{ }

float Spawner::CurrentInterval(float elapsed) const
{
    using namespace GameConfig::Spawn;
    // 経過時間 0 → StartInterval、RampTime 以降 → MinInterval
    const float t = std::min(1.0f, elapsed / RampTime);
    return StartInterval + (MinInterval - StartInterval) * t;
}

void Spawner::Update(float deltaTime, float elapsed, EnemyPool& pool, const GameConfig::Tuning& tuning)
{
    _timer += deltaTime;

    const float interval = CurrentInterval(elapsed) * tuning.spawnIntervalScale;
    if (_timer < interval) return;

    _timer = 0.0f;

    // 巣穴の位置から少しばらけさせて出す（重なって湧かないように）
    DirectX::XMFLOAT3 pos = _position;
    pos.x += (rand() % 100 / 100.0f - 0.5f) * 1.0f;
    pos.z += (rand() % 100 / 100.0f - 0.5f) * 1.0f;

    pool.Spawn(pos, tuning.antSpeedScale);   // 空きが無ければ何も起きない（戻り値は無視でよい）
}

void Spawner::Reset()
{
    _timer = 0.0f;
}