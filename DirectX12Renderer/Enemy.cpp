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
    _cooldown = std::max(0.0f, _cooldown - deltaTime);
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
            if (dist < AttackRange && _cooldown <= 0.0f)
            {
                ChangeState(State::Attack);
            }
        }
        break;

    case State::Attack:
        _controller.SetMoveVelocity(0.0f, 0.0f);
        if (_stateTime >= AttackDuration)
        {
            _cooldown = AttackCooldown;
            ChangeState(State::Chase);
        }
        break;

    case State::Damaged:
        // ノックバックの速度をだんだん弱める
        if (_stateTime >= DamagedTime)
        {
            _controller.SetMoveVelocity(0.0f, 0.0f);
            ChangeState(State::Chase);
        }
        break;

    case State::Dead:
        _controller.SetMoveVelocity(0.0f, 0.0f);
        _controller.SetPushWeight(0.0f);
        if (_stateTime > 1.0f) _actor.SetVisible(false);
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
    case State::Attack:  _actor.PlayAnimation("Curl_up1", 0.05f); break;
    case State::Damaged: _actor.PlayAnimation("Curl_up1", 0.05f); break;
    case State::Dead:    _actor.PlayAnimation("Curl_up_loop", 0.2f); break;
    }
}

const char* Enemy::StateName() const
{
    switch (_state)
    {
    case State::Idle:  return "Idle";
    case State::Chase: return "Chase";
    case State::Attack:  return "Attack";
    case State::Damaged: return "Damaged";
    case State::Dead:    return "Dead";
    }
    return "Unknown";   // switch の外に置く
}

void Enemy::FaceTowards(float dirX, float dirZ, float deltaTime)
{
    const float targetYaw = atan2f(dirX, dirZ);
    float diff = DirectX::XMScalarModAngle(targetYaw - _actor.RotationY());
    _actor.SetRotationY(_actor.RotationY() + diff * std::min(1.0f, TurnSpeed * deltaTime));
}

void Enemy::TakeDamage(int amount, unsigned int attackId, float fromX, float fromZ)
{
    if (_state == State::Dead) return;
    if (attackId == _lastHitAttackId) return;   // 同じ振りで既に当たっている
    _lastHitAttackId = attackId;

    _hp -= amount;

    // 攻撃者から離れる向きへ弾く
    const auto& p = _actor.Position();
    float dx = p.x - fromX;
    float dz = p.z - fromZ;
    const float len = sqrtf(dx * dx + dz * dz);
    if (len > 1e-4f)
    {
        _controller.SetMoveVelocity(dx / len * KnockbackSpeed, dz / len * KnockbackSpeed);
    }

    ChangeState(_hp > 0 ? State::Damaged : State::Dead);
}

Sphere Enemy::BodySphere() const
{
    return _controller.BodySphere();
}

Sphere Enemy::AttackSphere() const
{
    const float yaw = _actor.RotationY();
    const auto& p = _actor.Position();

    // 体の正面 0.7m、高さ 0.4m に半径 0.5m の球を置く
    return {
            { p.x + sinf(yaw) * 0.7f, p.y + 0.4f, p.z + cosf(yaw) * 0.7f },
            0.5f
    };
}

bool Enemy::IsAttackActive() const
{
    return _state == State::Attack && _stateTime >= AttackHitTime && _stateTime < AttackHitTime + 0.1f;
}