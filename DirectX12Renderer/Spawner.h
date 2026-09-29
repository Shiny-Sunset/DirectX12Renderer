#pragma once
#include <DirectXMath.h>

class EnemyPool;

// 巣穴 1 つ。一定間隔でアリを出す
class Spawner
{
public:
    Spawner(const DirectX::XMFLOAT3& position);

    // @param elapsed ゲーム開始からの経過時間（難易度を上げるのに使う）
    void Update(float deltaTime, float elapsed, EnemyPool& pool);

    void Reset();

    const DirectX::XMFLOAT3& Position() const { return _position; }

private:
    // 出現間隔（秒）。時間が経つほど短くなる
    float CurrentInterval(float elapsed) const;

    static constexpr float StartInterval = 4.0f;   // 開始時の間隔
    static constexpr float MinInterval = 0.8f;     // これ以上は短くしない
    static constexpr float RampTime = 120.0f;      // この秒数かけて最短まで詰める

    DirectX::XMFLOAT3 _position;
    float _timer = 0.0f;
};