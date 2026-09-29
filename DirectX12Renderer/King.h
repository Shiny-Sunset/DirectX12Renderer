#pragma once
#include <DirectXMath.h>
#include "Collision.h"

class GltfActor;
class CharacterController;

// 防衛対象。動かず、攻撃されて体力が減るだけ
class King
{
public:
    King(GltfActor& actor, CharacterController& controller, float scale);

    void Update(float deltaTime);

    // -- 外からの働きかけ --
    void TakeDamage(int amount);
    void Reset();

    // -- 状態の取得 --
    int HP() const { return _hp; }
    int MaxHP() const { return MaxHitPoint; }
    bool IsDestroyed() const { return _hp <= 0; }
    void SetScale(float scale);
    float Scale() const { return _scale; }

    const DirectX::XMFLOAT3& Position() const;
    Sphere BodySphere() const;

private:
    static constexpr int MaxHitPoint = 20;
    static constexpr float DamagedTime = 0.3f;   // 被弾の表示時間
    // 拡大率 1.0 のときの当たり判定の半径
    static constexpr float BaseRadius = 0.7f;

    GltfActor& _actor;
    CharacterController& _controller;

    int _hp = MaxHitPoint;
    float _damagedTime = 0.0f;   // 0 より大きい間は被弾中
    float _scale = 1.0f;
};