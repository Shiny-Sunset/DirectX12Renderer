#include "King.h"
#include "GltfActor.h"
#include "CharacterController.h"

King::King(GltfActor& actor, CharacterController& controller, float scale)
    : _actor(actor), _controller(controller)
{
    _controller.SetPushWeight(CharacterController::Immovable);   // 押されない
    SetScale(scale);
}

void King::Update(float deltaTime)
{
    _damagedTime = std::max(0.0f, _damagedTime - deltaTime);

    // 被弾中は点滅させる（0.05 秒ごとに表示 / 非表示）
    _actor.SetVisible(_damagedTime <= 0.0f || fmodf(_damagedTime, 0.1f) > 0.05f);
}

void King::TakeDamage(int amount)
{
    if (IsDestroyed()) return;

    _hp -= amount;
    _damagedTime = DamagedTime;

    if (IsDestroyed())
    {
        _actor.PlayAnimation("Defense", 0.2f);   // 丸まって耐える
    }

}

void King::Reset()
{
    _hp = _maxHP;
    _damagedTime = 0.0f;
    _actor.SetVisible(true);
    _actor.StopAnimation();
}

const DirectX::XMFLOAT3& King::Position() const
{
    return _actor.Position();
}

Sphere King::BodySphere() const
{
    return _controller.BodySphere();
}

void King::SetScale(float scale)
{
    _scale = scale;
    _actor.SetScale(scale);
    _controller.SetRadius(BaseRadius * scale);   // 見た目に合わせて判定も変わる
}