#include "CharacterController.h"
#include "GltfActor.h"

CharacterController::CharacterController(GltfActor& actor):_actor(actor)
{ }

void CharacterController::SetMoveVelocity(float vx, float vz)
{
    _velocity.x = vx;
    _velocity.z = vz;
}

void CharacterController::Update(float deltaTime)
{
    // -- 重力で下向きの速度を増やす --
    _velocity.y += _gravity * deltaTime;

    // -- 速度に従って位置を進める --
    DirectX::XMFLOAT3 pos = _actor.Position();
    pos.x += _velocity.x * deltaTime;
    pos.y += _velocity.y * deltaTime;
    pos.z += _velocity.z * deltaTime;

    // -- 地面より下へ行ったら押し戻す --
    if (pos.y <= GroundHeight)
    {
        pos.y = GroundHeight;
        _velocity.y = 0.0f;
        _grounded = true;
    }
    else
    {
        _grounded = false;
    }

    _actor.SetPosition(pos);
}

void CharacterController::Jump()
{
    if (!_grounded) return;   // 空中では跳べない

    // 「高さ h に到達する初速」を物理の式から求める
    //   v = √(2 × g × h)
    _velocity.y = sqrtf(2.0f * fabsf(_gravity) * _jumpHeight);
    _grounded = false;
}

Sphere CharacterController::BodySphere() const
{
    const auto& p = _actor.Position();
    return { { p.x, p.y + _radius, p.z }, _radius };
}

void CharacterController::PushXZ(float dx, float dz)
{
    auto p = _actor.Position();
    p.x += dx;
    p.z += dz;
    _actor.SetPosition(p);
}