#include "Enemy.h"
#include "GltfActor.h"
#include "CharacterController.h"
#include <algorithm>
#include <cmath>

Enemy::Enemy(GltfActor& actor, CharacterController& controller):_actor(actor), _controller(controller)
{ 
    _actor.PlayAnimation("Idle", 0.0f);
}

void Enemy::Update(float deltaTime, const DirectX::XMFLOAT3& playerPos)
{
    _stateTime += deltaTime;

    // -- プレイヤーまでの距離（水平方向のみ） --
    const auto& myPos = _actor.Position();
    const float dx = playerPos.x - myPos.x;
    const float dz = playerPos.z - myPos.z;
    const float dist = sqrtf(dx * dx + dz * dz);

    switch (_state)
    {
    case State::Idle:
        _controller.SetMoveVelocity(0.0f, 0.0f);
        if (dist < DetectRange)
        {
            ChangeState(State::Chase);
        }
        break;

    case State::Chase:
        if (dist > LoseRange)
        {
            ChangeState(State::Idle);
            break;
        }

        // -- 距離に関係なく、常にプレイヤーの方を向く --
        // dist が 0 に近いと方向が定まらないので、そのときは向きを変えない
        if (dist > 1e-4f)
        {
            FaceTowards(dx / dist, dz / dist, deltaTime);
        }

        // -- 近すぎなければ前進する --
        if (dist > StopDistance)
        {
            const float nx = dx / dist;
            const float nz = dz / dist;
            _controller.SetMoveVelocity(nx * ChaseSpeed, nz * ChaseSpeed);
        }
        else
        {
            _controller.SetMoveVelocity(0.0f, 0.0f);
        }
        break;
    }
}

void Enemy::ChangeState(State next)
{
    if (_state == next) return;   // 同じ状態なら何もしない

    _state = next;
    _stateTime = 0.0f;

    // 状態に応じたアニメーションは、ここだけで切り替える
    switch (_state)
    {
    case State::Idle:  _actor.PlayAnimation("Idle"); break;
    case State::Chase: _actor.PlayAnimation("Walk"); break;
    }
}

const char* Enemy::StateName() const
{
    switch (_state)
    {
    case State::Idle:  return "Idle";
    case State::Chase: return "Chase";
    }
    return "Unknown";   // switch の外に置く
}

void Enemy::FaceTowards(float dirX, float dirZ, float deltaTime)
{
    const float targetYaw = atan2f(dirX, dirZ);
    float diff = DirectX::XMScalarModAngle(targetYaw - _actor.RotationY());
    _actor.SetRotationY(_actor.RotationY() + diff * std::min(1.0f, TurnSpeed * deltaTime));
}